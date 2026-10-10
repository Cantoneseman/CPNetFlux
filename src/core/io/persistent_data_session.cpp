#include "cpnetflux/core/io/persistent_data_session.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <netinet/in.h>

#include "cpnetflux/core/protocol/frame.h"
#include "cpnetflux/checkpoint/transfer_manifest.h"

namespace cpnetflux::core::io {

bool PersistentDataSession::safeRelativePath(const std::string& path) noexcept {
    if (path.empty() || path.size() > 4096 || path.front() == '/' ||
        path.find(char(0)) != std::string::npos || path.find('\\') != std::string::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return true;
}

common::Status PersistentDataSession::begin(const protocol::FrameHeader& header,
                                             const std::string& relativePath) {
    if (active_) {
        return common::Status::runtimeError("persistent file already active");
    }
    const common::Status valid = protocol::validateFrameHeader(header, kMaxPayload);
    if (!valid.isOk() || header.type != protocol::FrameType::FileBegin ||
        header.streamId == 0 || header.chunkId == 0 ||
        (unordered_ ? (seenFiles_.contains(header.streamId) || seenGenerations_.contains(header.chunkId)) :
            (header.streamId <= lastFileId_ || header.chunkId <= lastGeneration_)) ||
        header.offset != 0 || !safeRelativePath(relativePath)) {
        return common::Status::invalidArgument("invalid FILE_BEGIN identity or path");
    }
    current_ = PersistentFileIdentity{header.streamId, header.chunkId, header.totalSize,
                                      relativePath};
    receivedBytes_ = 0;
    active_ = true;
    lastFileId_ = header.streamId;
    lastGeneration_ = header.chunkId;
    if (unordered_) { seenFiles_.insert(header.streamId); seenGenerations_.insert(header.chunkId); }
    return common::Status::ok();
}

common::Status PersistentDataSession::match(const protocol::FrameHeader& header) const {
    if (!active_ || header.streamId != current_.fileId || header.chunkId != current_.generation ||
        header.totalSize != current_.totalSize) {
        return common::Status::invalidArgument("persistent file identity mismatch");
    }
    return common::Status::ok();
}

common::Status PersistentDataSession::data(const protocol::FrameHeader& header,
                                            std::size_t logicalBytes) {
    const common::Status identity = match(header);
    if (!identity.isOk()) {
        return identity;
    }
    if (!protocol::validateFrameHeader(header, kMaxPayload).isOk() ||
        header.type != protocol::FrameType::Data || header.flags != 0 ||
        header.statusCode != protocol::FrameStatusCode::Ok ||
        header.offset != receivedBytes_ || logicalBytes == 0 || logicalBytes != header.payloadSize ||
        receivedBytes_ > current_.totalSize || logicalBytes > current_.totalSize - receivedBytes_) {
        return common::Status::invalidArgument("persistent DATA exceeds file boundary");
    }
    receivedBytes_ += logicalBytes;
    return common::Status::ok();
}

common::Result<PersistentFileIdentity> PersistentDataSession::end(
    const protocol::FrameHeader& header) {
    const common::Status identity = match(header);
    if (!identity.isOk()) {
        return identity;
    }
    if (!protocol::validateFrameHeader(header, 0).isOk() ||
        header.type != protocol::FrameType::FileEnd || header.offset != current_.totalSize ||
        receivedBytes_ != current_.totalSize) {
        return common::Status::invalidArgument("persistent FILE_END is incomplete");
    }
    PersistentFileIdentity completed = current_;
    active_ = false;
    current_ = {};
    receivedBytes_ = 0;
    return completed;
}

common::Status PersistentDataSession::fail(const protocol::FrameHeader& header,
                                           protocol::FrameStatusCode status) {
    const common::Status identity = match(header);
    if (!identity.isOk()) {
        return identity;
    }
    if (!protocol::validateFrameHeader(header, 0).isOk() ||
        header.type != protocol::FrameType::FileResult || header.statusCode != status ||
        status == protocol::FrameStatusCode::Ok) {
        return common::Status::invalidArgument("persistent FILE_RESULT is invalid");
    }
    active_ = false;
    current_ = {};
    receivedBytes_ = 0;
    return common::Status::ok();
}

}  // namespace cpnetflux::core::io


namespace cpnetflux::core::io {
namespace {
common::Status readHeader(FramedDataSocket* socket, protocol::FrameHeader* header,
                          std::uint32_t maxPayloadSize) {
    protocol::EncodedFrameHeader encoded{};
    auto status = socket->readAll(encoded.data(), encoded.size());
    if (!status.isOk()) return status;
    auto decoded = protocol::decodeFrameHeader(encoded);
    if (!decoded.isOk()) return decoded.status();
    status = protocol::validateFrameHeader(decoded.value(), maxPayloadSize);
    if (!status.isOk()) return status;
    *header = decoded.value();
    return common::Status::ok();
}
}

common::Status PersistentDataSession::writeFrame(FramedDataSocket* socket,
                                                  protocol::FrameHeader header,
                                                  const std::uint8_t* payload,
                                                  std::size_t length) {
    if (socket == nullptr || !socket->valid() || length > kMaxPayload ||
        (length != 0 && payload == nullptr)) {
        return common::Status::invalidArgument("invalid persistent frame output");
    }
    header.payloadSize = static_cast<std::uint32_t>(length);
    auto valid = protocol::validateFrameHeader(header, kMaxPayload);
    if (!valid.isOk()) return valid;
    const auto encoded = protocol::encodeFrameHeader(header);
    std::vector<std::uint8_t> bytes(encoded.begin(), encoded.end());
    if (length != 0) bytes.insert(bytes.end(), payload, payload + length);
    return socket->writeAll(bytes.data(), bytes.size());
}

common::Status PersistentDataSession::writeBegin(FramedDataSocket* socket,
                                                  const PersistentFileIdentity& identity) {
    if (identity.fileId == 0 || identity.generation == 0 || !safeRelativePath(identity.relativePath) ||
        !checkpoint::isValidTransferId(identity.transferId) || identity.transferId.size() > 128 ||
        identity.totalSize > static_cast<std::uint64_t>(INT64_MAX)) {
        return common::Status::invalidArgument("invalid persistent file identity");
    }
    protocol::FrameHeader header;
    header.type = protocol::FrameType::FileBegin;
    header.streamId = identity.fileId;
    header.chunkId = identity.generation;
    header.totalSize = identity.totalSize;
    protocol::SessionInitPayload metadata;
    metadata.transferId = identity.transferId;
    metadata.totalSize = identity.totalSize;
    metadata.chunkSize = identity.chunkSize;
    metadata.checksumAlgorithm = checksum::ChecksumAlgorithm::None;
    metadata.sourcePath = identity.relativePath;
    auto encoded = protocol::encodeSessionInitPayload(metadata);
    if (!encoded.isOk()) return encoded.status();
    auto bytes = std::move(encoded.value());
    if (identity.mtimeUnixSeconds < 0 || bytes.size() > 8184)
        return common::Status::invalidArgument("invalid persistent metadata");
    for (int shift = 56; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(identity.mtimeUnixSeconds) >> shift));
    return writeFrame(socket, header, bytes.data(), bytes.size());
}

common::Status PersistentDataSession::writeData(FramedDataSocket* socket,
                                                 const PersistentFileIdentity& identity,
                                                 std::uint64_t offset,
                                                 const std::uint8_t* payload,
                                                 std::size_t length) {
    if (identity.fileId == 0 || identity.generation == 0 || (payload == nullptr && length != 0) ||
        length == 0 || length > kMaxPayload ||
        offset > identity.totalSize || length > identity.totalSize - offset) {
        return common::Status::invalidArgument("invalid persistent data range");
    }
    protocol::FrameHeader header;
    header.type = protocol::FrameType::Data;
    header.streamId = identity.fileId;
    header.chunkId = identity.generation;
    header.offset = offset;
    header.totalSize = identity.totalSize;
    return writeFrame(socket, header, payload, length);
}

common::Status PersistentDataSession::writeEnd(FramedDataSocket* socket,
                                                const PersistentFileIdentity& identity) {
    protocol::FrameHeader header;
    header.type = protocol::FrameType::FileEnd;
    header.streamId = identity.fileId;
    header.chunkId = identity.generation;
    header.totalSize = identity.totalSize;
    header.offset = identity.totalSize;
    return writeFrame(socket, header, nullptr, 0);
}

common::Status PersistentDataSession::writeResult(FramedDataSocket* socket,
                                                   const PersistentFileIdentity& identity,
                                                   protocol::FrameStatusCode statusCode) {
    protocol::FrameHeader header;
    header.type = protocol::FrameType::FileResult;
    header.streamId = identity.fileId;
    header.chunkId = identity.generation;
    header.totalSize = identity.totalSize;
    header.statusCode = statusCode;
    return writeFrame(socket, header, nullptr, 0);
}

common::Result<PersistentFrame> PersistentDataSession::readNext(FramedDataSocket* socket,
                                                                  std::uint32_t maxPayloadSize) {
    if (socket == nullptr || !socket->valid() || maxPayloadSize > kMaxPayload)
        return common::Status::invalidArgument("invalid persistent input");
    PersistentFrame frame;
    auto status = readHeader(socket, &frame.header, maxPayloadSize);
    if (!status.isOk()) return status;
    frame.payload.resize(frame.header.payloadSize);
    if (!frame.payload.empty()) {
        status = socket->readAll(frame.payload.data(), frame.payload.size());
        if (!status.isOk()) return status;
    }
    return frame;
}

common::Result<PersistentFileIdentity> PersistentDataSession::decodeBegin(const PersistentFrame& frame) {
    if (!protocol::validateFrameHeader(frame.header, 8192).isOk() ||
        frame.header.type != protocol::FrameType::FileBegin || frame.payload.size() < 8 ||
        frame.payload.size() > 8192 || frame.payload.size() != frame.header.payloadSize)
        return common::Status::invalidArgument("invalid FILE_BEGIN metadata");
    auto metadata = protocol::decodeSessionInitPayload(frame.payload.data(), frame.payload.size() - 8);
    if (!metadata.isOk()) return metadata.status();
    const auto& m = metadata.value();
    if (m.mode != protocol::SessionMode::New || m.checksumAlgorithm != checksum::ChecksumAlgorithm::None ||
        m.totalSize != frame.header.totalSize || m.totalSize > static_cast<std::uint64_t>(INT64_MAX) ||
        !safeRelativePath(m.sourcePath) || !checkpoint::isValidTransferId(m.transferId) || m.transferId.size() > 128)
        return common::Status::invalidArgument("unsupported FILE_BEGIN metadata");
    std::uint64_t mtime = 0;
    for (std::size_t i = frame.payload.size() - 8; i < frame.payload.size(); ++i)
        mtime = (mtime << 8) | frame.payload[i];
    if (mtime > static_cast<std::uint64_t>(INT64_MAX))
        return common::Status::invalidArgument("invalid persistent mtime");
    return PersistentFileIdentity{frame.header.streamId, frame.header.chunkId, m.totalSize,
                                  m.sourcePath, m.transferId, m.chunkSize, static_cast<std::int64_t>(mtime)};
}

common::Result<protocol::FrameStatusCode> PersistentDataSession::readResult(
    FramedDataSocket* socket, const PersistentFileIdentity& identity) {
    auto frame = readNext(socket, 0);
    if (!frame.isOk()) return frame.status();
    const auto& h = frame.value().header;
    if (h.type != protocol::FrameType::FileResult || h.streamId != identity.fileId ||
        h.chunkId != identity.generation || h.totalSize != identity.totalSize)
        return common::Status::invalidArgument("FILE_RESULT identity mismatch");
    return h.statusCode;
}

common::Status PersistentDataSession::writeDirectoryEnd(FramedDataSocket* socket, std::uint64_t count) {
    protocol::FrameHeader header;
    header.type = protocol::FrameType::DirectoryEnd;
    header.totalSize = count;
    return writeFrame(socket, header, nullptr, 0);
}

common::Status PersistentDataSession::setTimeout(FramedDataSocket* socket, int seconds) {
    if (!socket || !socket->valid() || seconds < 1 || seconds > 3600)
        return common::Status::invalidArgument("invalid persistent timeout");
    timeval timeout{seconds, 0};
    if (::setsockopt(socket->fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        ::setsockopt(socket->fd(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
        return common::Status::runtimeError("persistent socket timeout configuration failed");
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (::getsockname(socket->fd(), reinterpret_cast<sockaddr*>(&address), &length) != 0)
        return common::Status::runtimeError("persistent socket address lookup failed");
    if (address.ss_family == AF_INET || address.ss_family == AF_INET6) {
        int enabled = 1;
        if (::setsockopt(socket->fd(), IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0)
            return common::Status::runtimeError("persistent TCP_NODELAY configuration failed");
    }
    return common::Status::ok();
}

}  // namespace cpnetflux::core::io
