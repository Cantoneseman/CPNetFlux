#include "cpnetflux/core/io/persistent_directory.h"
#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include "cpnetflux/checksum/crc32c.h"
#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/core/io/work_executor.h"
#include "cpnetflux/core/tree/tree_scan.h"
namespace cpnetflux::core::io {
namespace {
using common::Status;
using protocol::FrameType;
using protocol::FrameStatusCode;
Status write(FramedDataSocket* socket, protocol::FrameHeader h, const std::vector<std::uint8_t>& payload,
             std::uint64_t* wire) {
    h.payloadSize = static_cast<std::uint32_t>(payload.size());
    auto valid = protocol::validateFrameHeader(h, 4132);
    if (!valid.isOk()) return valid;
    const auto header = protocol::encodeFrameHeader(h);
    auto result = socket->writeFrame(header.data(), header.size(), payload.data(), payload.size());
    if (result.isOk() && wire) *wire += header.size() + payload.size();
    return result;
}
std::uint32_t metadataCrc(const protocol::FrameHeader& h, const std::uint8_t* data, std::size_t size) {
    const auto header = protocol::encodeFrameHeader(h);
    auto state = checksum::crc32cUpdate(checksum::crc32cInitialState(),header.data(),header.size());
    return checksum::crc32cFinalize(checksum::crc32cUpdate(state,data,size));
}
WorkItem directoryWork(const tree::TreeDirectoryRecord& d, const protocol::FrameHeader& h) {
    // Identity storage is shared with the V3 executor, with an explicit Directory kind.
    // No file transaction, payload, FILE_BEGIN or file completion is manufactured.
    PersistentFileIdentity identity;
    identity.fileId = h.streamId; identity.generation = 1; identity.chunkSize = 1;
    identity.relativePath = d.relativePath; identity.transferId = d.transferId; identity.attempt = h.attempt;
    return {identity, d.transferId, false, WorkKind::Directory};
}
Status event(const PersistentDirectoryTransfer& transfer, const tree::TreeDirectoryRecord& d, bool complete) {
    return transfer.callback ? transfer.callback(d, complete) : Status::ok();
}
Status validRecord(const tree::TreeDirectoryRecord& d) {
    auto valid = tree::validateTreeRelativePath(d.relativePath);
    if (!valid.isOk()) return valid;
    if (d.relativePath.size() > tree::kMaxTreeDirectoryPathBytes || d.relativePath.back() == '/' ||
        d.relativePath.find("//") != std::string::npos || d.transferId.size() != 32 ||
        !checkpoint::isValidTransferId(d.transferId)) return Status::invalidArgument("invalid directory identity");
    return Status::ok();
}
struct LeaseGuard {
    WorkExecutor& executor; WorkLease lease; bool completed = false;
    ~LeaseGuard() { if (!completed) (void)executor.reject(lease); }
};
}
Status sendPersistentDirectories(FramedDataSocket* socket, const PersistentDirectoryTransfer& transfer,
    std::uint32_t channel, std::uint32_t channels, std::uint64_t attempt, std::uint64_t* wire) {
    if (!socket || !socket->valid() || !channels || channels > 64 || channel >= channels || transfer.entries.size() > tree::kMaxTreeDirectories)
        return Status::invalidArgument("directory send budget/channel invalid");
    DynamicFileQueue queue(std::vector<WorkItem>{}, 1, 1, 1);
    auto worker = queue.worker(0); if (!worker.isOk()) return worker.status();
    WorkExecutor executor(queue, worker.value());
    std::size_t bytes = 0; std::uint64_t count = 0;
    std::unordered_set<std::string> paths;
    for (std::size_t i = 0; channel == 0 && i < transfer.entries.size(); ++i) {
        const auto& d = transfer.entries[i];
        auto valid = validRecord(d); if (!valid.isOk()) return valid;
        if (d.relativePath.size() > tree::kMaxTreeDirectoryBytes - bytes || !paths.insert(d.relativePath).second)
            return Status::invalidArgument("directory metadata budget or duplicate path");
        bytes += d.relativePath.size();
        for (auto parent = std::filesystem::path(d.relativePath).parent_path(); !parent.empty(); parent = parent.parent_path())
            if (!paths.contains(parent.generic_string())) return Status::invalidArgument("directory parent metadata missing or out of order");
        protocol::FrameHeader h; h.type = FrameType::DirectoryEntry;
        h.streamId = static_cast<std::uint32_t>(i + 1); h.chunkId = 1; h.attempt = attempt;
        auto claimed = executor.claimIncoming(directoryWork(d,h)); if (!claimed.isOk()) return claimed.status();
        LeaseGuard guard{executor, claimed.value()};
        auto status = executor.begin(guard.lease); if (!status.isOk()) return status;
        status = event(transfer,d,false); if (!status.isOk()) return status;
        std::vector<std::uint8_t> payload(d.transferId.begin(), d.transferId.end());
        payload.insert(payload.end(), d.relativePath.begin(), d.relativePath.end());
        h.payloadSize = static_cast<std::uint32_t>(payload.size()+4);
        auto crc = metadataCrc(h,payload.data(),payload.size());
        for (int shift = 24; shift >= 0; shift -= 8) payload.push_back(static_cast<std::uint8_t>(crc >> shift));
        status = write(socket,h,payload,wire); if (!status.isOk()) return status;
        status = executor.awaitResult(guard.lease); if (!status.isOk()) return status;
        auto reply = PersistentDataSession::readNext(socket,0); if (!reply.isOk()) return reply.status();
        if (wire) *wire += protocol::kFrameHeaderSize;
        const auto& rh = reply.value().header;
        if (rh.type != FrameType::DirectoryResult || rh.streamId != h.streamId || rh.chunkId != h.chunkId || rh.attempt != h.attempt)
            return Status::invalidArgument("directory result identity mismatch");
        if (rh.statusCode != FrameStatusCode::Ok) return Status::invalidArgument("peer rejected directory: " + d.relativePath);
        status = executor.commit(guard.lease); if (!status.isOk()) return status;
        guard.completed = true;
        status = event(transfer,d,true); if (!status.isOk()) return status;
        ++count;
    }
    protocol::FrameHeader end; end.type = FrameType::DirectoryMetadataEnd; end.totalSize = count;
    return write(socket,end,{},wire);
}
Status receivePersistentDirectories(FramedDataSocket* socket, const std::string& root,
    const PersistentDirectoryTransfer& transfer, std::uint32_t channel, std::uint32_t channels, std::uint64_t* wire) {
    if (!socket || !socket->valid() || !channels || channels > 64 || channel >= channels) return Status::invalidArgument("directory receive socket/channel invalid");
    DynamicFileQueue queue(std::vector<WorkItem>{},1,1,1);
    auto worker = queue.worker(0); if (!worker.isOk()) return worker.status();
    WorkExecutor executor(queue,worker.value());
    std::unordered_set<std::string> paths;
    std::unordered_set<std::uint32_t> ids;
    std::size_t bytes = 0; std::uint64_t count = 0;
    for (;;) {
        auto frame = PersistentDataSession::readNext(socket,4132); if (!frame.isOk()) return frame.status();
        const auto& h = frame.value().header; const auto& payload = frame.value().payload;
        if (wire) *wire += protocol::kFrameHeaderSize + payload.size();
        if (h.type == FrameType::DirectoryMetadataEnd) {
            if (h.totalSize != count) return Status::invalidArgument("directory metadata count mismatch");
            return Status::ok();
        }
        if (h.type != FrameType::DirectoryEntry || h.streamId > tree::kMaxTreeDirectories || channel != 0)
            return Status::invalidArgument("unexpected directory metadata identity/channel");
        auto dataSize = payload.size() - 4;
        std::uint32_t expected = 0;
        for (auto i = dataSize; i < payload.size(); ++i) expected = (expected << 8) | payload[i];
        if (metadataCrc(h,payload.data(),dataSize) != expected) return Status::invalidArgument("directory metadata CRC mismatch");
        tree::TreeDirectoryRecord d{std::string(payload.begin()+32,payload.end()-4), std::string(payload.begin(),payload.begin()+32)};
        auto valid = validRecord(d); if (!valid.isOk()) return valid;
        if (count >= tree::kMaxTreeDirectories || d.relativePath.size() > tree::kMaxTreeDirectoryBytes - bytes ||
            !ids.insert(h.streamId).second || !paths.insert(d.relativePath).second)
            return Status::invalidArgument("directory metadata duplicate or budget exceeded");
        for (auto parent = std::filesystem::path(d.relativePath).parent_path(); !parent.empty(); parent = parent.parent_path())
            if (!paths.contains(parent.generic_string())) return Status::invalidArgument("directory parent metadata missing or out of order");
        bytes += d.relativePath.size(); ++count;
        auto claimed = executor.claimIncoming(directoryWork(d,h)); if (!claimed.isOk()) return claimed.status();
        LeaseGuard guard{executor,claimed.value()};
        auto status = executor.begin(guard.lease); if (!status.isOk()) return status;
        status = event(transfer,d,false); if (!status.isOk()) return status;
        auto created = tree::ensureTreeDirectory(root,d.relativePath);
        auto reply = h; reply.type = FrameType::DirectoryResult;
        reply.statusCode = created.isOk() ? FrameStatusCode::Ok : FrameStatusCode::WriteFailed;
        auto written = write(socket,reply,{},wire);
        if (!created.isOk()) return created;
        if (!written.isOk()) return written;
        status = executor.finalize(guard.lease,Status::ok()); if (!status.isOk()) return status;
        guard.completed = true;
        status = event(transfer,d,true); if (!status.isOk()) return status;
    }
}
} // namespace cpnetflux::core::io
