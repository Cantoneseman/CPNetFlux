#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#include "cpnetflux/checkpoint/manifest_store.h"
#include "cpnetflux/core/session/download_session.h"
#include "cpnetflux/core/session/transfer_session.h"

namespace {
namespace session = cpnetflux::core::session;
namespace checksum = cpnetflux::checksum;
namespace checkpoint = cpnetflux::checkpoint;

double threadCpuSeconds() {
    timespec value{};
    if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0)
        throw std::runtime_error("thread CPU clock unavailable");
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_nsec) / 1e9;
}

void require(const cpnetflux::common::Status& status) {
    if (!status.isOk()) throw std::runtime_error(status.message());
}

template <class Session>
void measure(Session& receiver, std::uint64_t records, const char* role,
             session::ManifestFlushPolicy policy) {
    session::VerifiedChunkRecordStats lookup;
    checkpoint::ManifestSaveStats saved;
    receiver.setRecordStats(&lookup);
    receiver.setManifestSaveStats(&saved);
    constexpr std::uint64_t chunkBytes = 1024 * 1024;
    const checksum::ChecksumValue crc{checksum::ChecksumAlgorithm::Crc32c, 0x12345678U};
    const auto start = std::chrono::steady_clock::now();
    const double cpu = threadCpuSeconds();
    require(receiver.save());
    for (std::uint64_t index = 0; index < records; ++index)
        require(receiver.recordVerifiedChunk(index, index * chunkBytes, chunkBytes, crc));
    require(receiver.flushManifest());
    const double cpuElapsed = threadCpuSeconds() - cpu;
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (receiver.manifest().verifiedChunks.size() != records || !receiver.missingRanges().empty())
        throw std::runtime_error("record coverage mismatch");
    const auto loaded = [&] {
        if constexpr (std::is_same_v<Session, session::DownloadSession>)
            return checkpoint::loadDownloadManifest(receiver.manifestPath());
        else
            return checkpoint::ManifestStore::load(receiver.manifestPath());
    }();
    if (!loaded.isOk()) throw std::runtime_error(loaded.status().message());
    if (loaded.value().verifiedChunks.size() != records)
        throw std::runtime_error("persisted record count mismatch");
    for (std::uint64_t index = 0; index < records; ++index) {
        const auto& record = loaded.value().verifiedChunks[index];
        if (record.chunkId != index || record.offset != index * chunkBytes || record.length != chunkBytes ||
            record.checksum.algorithm != crc.algorithm || record.checksum.value != crc.value)
            throw std::runtime_error("persisted record identity mismatch");
    }
    std::cout << std::setprecision(17)
              << "{\"benchmark\":\"session_metadata_only\",\"role\":\"" << role
              << "\",\"records\":" << records
              << ",\"represented_chunk_bytes\":" << chunkBytes
              << ",\"payload_bytes_written\":0,\"network_transfer\":false,\"manifest_flush_policy\":\""
              << session::manifestFlushPolicyName(policy) << "\",\"manifest_flush_interval_chunks\":16"
              << ",\"wall_seconds\":" << elapsed << ",\"thread_cpu_seconds\":" << cpuElapsed
              << ",\"lookup_calls\":" << lookup.lookupCalls
              << ",\"candidate_records_examined\":" << lookup.candidateRecordsExamined
              << ",\"lookup_seconds\":" << lookup.lookupSeconds
              << ",\"session_sort_seconds\":" << lookup.sortSeconds
              << ",\"save_attempts\":" << saved.attempts
              << ",\"save_seconds\":" << saved.saveSeconds
              << ",\"serialize_seconds\":" << saved.serializeSeconds
              << ",\"serialized_bytes\":" << saved.serializedBytes
              << ",\"temp_write_seconds\":" << saved.tempWriteSeconds
              << ",\"publish_seconds\":" << saved.renameSeconds
              << ",\"manifest_flush_count\":" << receiver.stats().manifestFlushCount
              << ",\"persisted_coverage_verified\":true}\n";
}
}  // namespace

int main(int argc, const char* argv[]) {
    try {
        if (argc != 5) throw std::runtime_error(
            "Usage: cpnetflux-session-record-bench download|upload records every_n_chunks|final_only fresh-directory");
        const std::string_view role(argv[1]), count(argv[2]);
        std::uint64_t records = 0;
        const auto parsed = std::from_chars(count.data(), count.data() + count.size(), records);
        if ((role != "download" && role != "upload") || parsed.ec != std::errc{} ||
            parsed.ptr != count.data() + count.size() || records == 0 || records > 1048576)
            throw std::runtime_error("invalid role or record count (1..1048576)");
        auto policy = session::parseManifestFlushPolicy(argv[3]);
        if (!policy.isOk()) throw std::runtime_error(policy.status().message());
        const auto directory = std::filesystem::path(argv[4]);
        if (!std::filesystem::is_directory(directory) || !std::filesystem::is_empty(directory))
            throw std::runtime_error("benchmark directory must exist and be empty");
        const auto output = (directory / "output.bin").string();
        constexpr std::uint64_t chunkBytes = 1024 * 1024;
        if (role == "download") {
            auto receiver = session::DownloadSession::createNew(output, "source.bin", "record-bench",
                records * chunkBytes, chunkBytes, checksum::ChecksumAlgorithm::Crc32c,
                checksum::ChecksumBackend::Auto, policy.value(), 16);
            if (!receiver.isOk()) throw std::runtime_error(receiver.status().message());
            measure(receiver.value(), records, "download", policy.value());
        } else {
            auto receiver = session::TransferSession::createNew(output, "record-bench",
                records * chunkBytes, chunkBytes, checksum::ChecksumAlgorithm::Crc32c,
                checksum::ChecksumBackend::Auto, policy.value(), 16);
            if (!receiver.isOk()) throw std::runtime_error(receiver.status().message());
            measure(receiver.value(), records, "upload", policy.value());
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
