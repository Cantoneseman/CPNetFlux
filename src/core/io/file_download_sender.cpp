#include "cpnetflux/core/io/file_download_sender.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cpnetflux/checkpoint/transfer_manifest.h"
#include "cpnetflux/common/throughput_counter.h"
#include "cpnetflux/core/chunk/chunk_planner.h"
#include "cpnetflux/core/io/framed_data_socket.h"
#include "cpnetflux/core/metrics/transfer_phase_stats.h"
#include "cpnetflux/core/protocol/compressed_data.h"
#include "cpnetflux/core/protocol/frame.h"
#include "cpnetflux/storage/file_io.h"
#include "cpnetflux/storage/posix_file.h"

namespace cpnetflux::core::io {
namespace {

struct SenderStats {
    std::uint64_t sentBytes = 0;
    std::uint64_t wireBytes = 0;
    std::uint64_t skippedBytes = 0;
    std::uint64_t resentBytes = 0;
    std::uint64_t verifiedBytes = 0;
    std::uint64_t compressionAttempts = 0;
    std::uint64_t compressedFrames = 0;
    std::uint64_t rawFallbackFrames = 0;
    std::uint64_t compressionFailures = 0;
    std::uint64_t compressedLogicalBytes = 0;
    std::uint64_t compressedWireBytes = 0;
    std::string compressionFallbackReason;
};

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

bool isPayloadLimitFailure(const common::Status& status) {
    return status.message().find("payload") != std::string::npos;
}

common::Status setReceiveTimeout(int fd) {
    timeval timeout{};
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        return systemStatus("setsockopt(SO_RCVTIMEO)", errno);
    }
    return common::Status::ok();
}

common::Status sendAll(FramedDataSocket* socket, const std::uint8_t* data, std::size_t length,
                       metrics::TransferPhaseStats* phaseStats = nullptr) {
    metrics::ScopedPhaseTimer timer(phaseStats, metrics::TransferPhase::Send, length);
    return socket->writeAll(data, length);
}

common::Status recvAll(FramedDataSocket* socket, std::uint8_t* data, std::size_t length,
                       metrics::TransferPhaseStats* phaseStats = nullptr) {
    metrics::ScopedPhaseTimer timer(phaseStats, metrics::TransferPhase::Recv, length);
    return socket->readAll(data, length);
}

common::Status sendFrame(FramedDataSocket* socket, const protocol::FrameHeader& header,
                         const std::vector<std::uint8_t>& payload,
                         metrics::TransferPhaseStats* phaseStats = nullptr) {
    const protocol::EncodedFrameHeader encoded = protocol::encodeFrameHeader(header);
    const common::Status headerStatus = sendAll(socket, encoded.data(), encoded.size(), phaseStats);
    if (!headerStatus.isOk()) {
        return headerStatus;
    }
    if (!payload.empty()) {
        return sendAll(socket, payload.data(), payload.size(), phaseStats);
    }
    return common::Status::ok();
}

common::Result<protocol::FrameHeader> recvHeader(FramedDataSocket* socket,
                                                 std::uint32_t maxPayloadSize,
                                                 metrics::TransferPhaseStats* phaseStats) {
    protocol::EncodedFrameHeader encoded{};
    const common::Status recvStatus =
        recvAll(socket, encoded.data(), encoded.size(), phaseStats);
    if (!recvStatus.isOk()) {
        return recvStatus;
    }
    auto decoded = protocol::decodeFrameHeader(encoded);
    if (!decoded.isOk()) {
        return decoded.status();
    }
    const common::Status validateStatus =
        protocol::validateFrameHeader(decoded.value(), maxPayloadSize);
    if (!validateStatus.isOk()) {
        return validateStatus;
    }
    return decoded.value();
}

common::Result<std::vector<chunk::CompletedRange>> waitForResumeResponse(FramedDataSocket* socket,
                                                                         std::uint32_t bufferSize,
                                                                         metrics::TransferPhaseStats*
                                                                             phaseStats) {
    auto header = recvHeader(socket, bufferSize, phaseStats);
    if (!header.isOk()) {
        return header.status();
    }
    if (header.value().type == protocol::FrameType::Error) {
        return common::Status::runtimeError("receiver rejected download session");
    }
    if (header.value().type != protocol::FrameType::ResumeResponse) {
        return common::Status::runtimeError("receiver returned unexpected session response");
    }
    std::vector<std::uint8_t> payload(header.value().payloadSize);
    const common::Status recvStatus = recvAll(socket, payload.data(), payload.size(), phaseStats);
    if (!recvStatus.isOk()) {
        return recvStatus;
    }
    auto decoded = protocol::decodeResumeResponsePayload(payload.data(), payload.size());
    if (!decoded.isOk()) {
        return decoded.status();
    }
    if (decoded.value().statusCode != protocol::FrameStatusCode::Ok) {
        return common::Status::runtimeError("receiver returned non-OK resume response");
    }
    return decoded.value().missingRanges;
}

common::Status waitForFinalStatus(FramedDataSocket* socket, std::uint32_t bufferSize,
                                  metrics::TransferPhaseStats* phaseStats) {
    auto header = recvHeader(socket, bufferSize, phaseStats);
    if (!header.isOk()) {
        return header.status();
    }
    if (header.value().type == protocol::FrameType::Complete &&
        header.value().statusCode == protocol::FrameStatusCode::Ok) {
        return common::Status::ok();
    }
    if (header.value().type == protocol::FrameType::Error) {
        return common::Status::runtimeError("receiver returned transfer error status");
    }
    return common::Status::runtimeError("receiver returned unexpected final status");
}

common::Status sendSessionInit(FramedDataSocket* socket, const FileDownloadSenderOptions& options,
                               std::uint32_t streamId, std::uint64_t totalSize,
                               metrics::TransferPhaseStats* phaseStats) {
    protocol::SessionInitPayload init;
    init.mode = options.resume ? protocol::SessionMode::Resume : protocol::SessionMode::New;
    init.transferId = options.transferId;
    init.totalSize = totalSize;
    init.chunkSize = options.chunkSize;
    init.checksumAlgorithm = options.checksumAlgorithm;
    init.sourcePath = options.sourcePath;
    auto payload = protocol::encodeSessionInitPayload(init);
    if (!payload.isOk()) {
        return payload.status();
    }
    if (payload.value().size() > options.bufferSize) {
        return common::Status::invalidArgument("SessionInit payload exceeds buffer size");
    }

    protocol::FrameHeader header;
    header.type = protocol::FrameType::SessionInit;
    header.streamId = streamId;
    header.payloadSize = static_cast<std::uint32_t>(payload.value().size());
    header.totalSize = totalSize;
    return sendFrame(socket, header, payload.value(), phaseStats);
}

bool rangesIntersect(std::uint64_t leftBegin, std::uint64_t leftEnd, std::uint64_t rightBegin,
                     std::uint64_t rightEnd) noexcept {
    return leftBegin < rightEnd && rightBegin < leftEnd;
}

bool shouldSendChunk(const chunk::ChunkRange& chunk,
                     const std::vector<chunk::CompletedRange>& missingRanges) noexcept {
    const std::uint64_t chunkEnd = chunk.offset + chunk.length;
    return std::any_of(
        missingRanges.begin(), missingRanges.end(), [&](const chunk::CompletedRange& missing) {
            return rangesIntersect(chunk.offset, chunkEnd, missing.begin, missing.end);
        });
}

common::Status sendChunk(FramedDataSocket* socket, const FileDownloadSenderOptions& options,
                         const storage::PosixFile& inputFile, const chunk::ChunkRange& chunk,
                         checksum::ChecksumBackend checksumBackend, std::uint64_t totalSize,
                         std::vector<std::uint8_t>& buffer,
                         metrics::TransferPhaseStats* phaseStats,
                         const storage::FileIoContext& fileIoContext,
                         storage::FileIoStats* fileIoStats, SenderStats* stats) {
    checksum::ChecksumComputer checksumComputer(options.checksumAlgorithm, checksumBackend);
    std::uint64_t completed = chunk.offset;
    const std::uint64_t end = chunk.offset + chunk.length;
    while (completed < end) {
        const std::uint64_t remaining = end - completed;
        const std::uint32_t payloadSize =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        common::Status readStatus;
        {
            metrics::ScopedPhaseTimer timer(phaseStats, metrics::TransferPhase::Read,
                                            payloadSize);
            readStatus = storage::readAtAll(inputFile, completed, buffer.data(), payloadSize,
                                            fileIoContext, fileIoStats);
        }
        if (!readStatus.isOk()) {
            return readStatus;
        }
        {
            metrics::ScopedPhaseTimer timer(phaseStats, metrics::TransferPhase::Checksum,
                                            payloadSize);
            checksumComputer.update(buffer.data(), payloadSize);
        }

        const std::uint8_t* payloadData = buffer.data();
        std::uint32_t wirePayloadSize = payloadSize;
        std::uint16_t flags = 0;
        std::vector<std::uint8_t> compressedPayload;
        const bool canAttemptCompression = options.hotPathCompression.enabled &&
                                           options.hotPathCompression.candidate &&
                                           !options.hotPathCompression.forceRaw;
        if (canAttemptCompression && stats != nullptr) {
            ++stats->compressionAttempts;
        }
        if (canAttemptCompression && !options.hotPathCompression.testForceCompressFailure) {
            const std::uint32_t maxPayloadBytes = options.hotPathCompression.maxPayloadBytes == 0
                                                      ? options.bufferSize
                                                      : std::min(options.bufferSize,
                                                                 options.hotPathCompression
                                                                     .maxPayloadBytes);
            auto encoded = protocol::encodeCompressedDataPayload(buffer.data(), payloadSize,
                                                                 maxPayloadBytes);
            if (encoded.isOk() && encoded.value().size() < payloadSize) {
                compressedPayload = std::move(encoded.value());
                if (options.hotPathCompression.testCorruptCompressedPayload &&
                    compressedPayload.size() > protocol::kCompressedDataPrefixSize) {
                    compressedPayload[protocol::kCompressedDataPrefixSize] ^= 0xFFU;
                }
                payloadData = compressedPayload.data();
                wirePayloadSize = static_cast<std::uint32_t>(compressedPayload.size());
                flags = protocol::kDataCompressed;
                if (stats != nullptr) {
                    ++stats->compressedFrames;
                    stats->compressedLogicalBytes += payloadSize;
                    stats->compressedWireBytes += wirePayloadSize;
                }
            } else if (stats != nullptr) {
                ++stats->rawFallbackFrames;
                if (stats->compressionFallbackReason.empty()) {
                    stats->compressionFallbackReason =
                        !encoded.isOk()
                            ? (encoded.status().message().find("payload limit") !=
                                       std::string::npos
                                   ? "payload_limit"
                                   : "compression_failure")
                            : "poor_ratio";
                }
                if (!encoded.isOk() &&
                    !isPayloadLimitFailure(encoded.status())) {
                    ++stats->compressionFailures;
                }
            }
        } else if (canAttemptCompression && stats != nullptr) {
            ++stats->rawFallbackFrames;
            ++stats->compressionFailures;
            stats->compressionFallbackReason = "compression_failure";
        }

        protocol::FrameHeader header;
        header.type = protocol::FrameType::Data;
        header.flags = flags;
        header.streamId = chunk.streamId;
        header.chunkId = chunk.chunkId;
        header.offset = completed;
        header.payloadSize = wirePayloadSize;
        header.totalSize = totalSize;
        const common::Status headerStatus = sendFrame(socket, header, {}, phaseStats);
        if (!headerStatus.isOk()) {
            return headerStatus;
        }
        const common::Status payloadStatus = sendAll(socket, payloadData, wirePayloadSize, phaseStats);
        if (!payloadStatus.isOk()) {
            return payloadStatus;
        }
        if (stats != nullptr) {
            stats->wireBytes += wirePayloadSize;
        }
        completed += payloadSize;
    }

    protocol::ChunkCompletePayload complete;
    complete.chunkId = chunk.chunkId;
    complete.offset = chunk.offset;
    complete.length = chunk.length;
    {
        metrics::ScopedPhaseTimer timer(phaseStats, metrics::TransferPhase::Checksum);
        complete.checksum = checksumComputer.finalize();
    }
    auto payload = protocol::encodeChunkCompletePayload(complete);
    if (!payload.isOk()) {
        return payload.status();
    }

    protocol::FrameHeader header;
    header.type = protocol::FrameType::ChunkComplete;
    header.streamId = chunk.streamId;
    header.chunkId = chunk.chunkId;
    header.offset = chunk.offset;
    header.payloadSize = static_cast<std::uint32_t>(payload.value().size());
    header.totalSize = totalSize;
    return sendFrame(socket, header, payload.value(), phaseStats);
}

common::Status sendStream(FramedDataSocket connection, const FileDownloadSenderOptions& options,
                          const storage::PosixFile& inputFile,
                          const std::vector<chunk::ChunkRange>& chunks,
                          checksum::ChecksumBackend checksumBackend, std::uint32_t streamId,
                          std::uint64_t totalSize, SenderStats* stats,
                          metrics::TransferPhaseStats* phaseStats,
                          const storage::FileIoContext& fileIoContext,
                          storage::FileIoStats* fileIoStats) {
    const common::Status timeoutStatus = setReceiveTimeout(connection.fd());
    if (!timeoutStatus.isOk()) {
        return timeoutStatus;
    }
    const common::Status initStatus =
        sendSessionInit(&connection, options, streamId, totalSize, phaseStats);
    if (!initStatus.isOk()) {
        return initStatus;
    }
    auto response = waitForResumeResponse(&connection, options.bufferSize, phaseStats);
    if (!response.isOk()) {
        return response.status();
    }
    const std::vector<chunk::CompletedRange> missingRanges = std::move(response.value());

    std::vector<std::uint8_t> buffer(options.bufferSize);
    for (const chunk::ChunkRange& chunk : chunks) {
        if (chunk.streamId != streamId) {
            continue;
        }
        if (!shouldSendChunk(chunk, missingRanges)) {
            stats->skippedBytes += chunk.length;
            continue;
        }
        const common::Status sendStatus = sendChunk(&connection, options, inputFile, chunk,
                                                    checksumBackend, totalSize, buffer,
                                                    phaseStats, fileIoContext, fileIoStats, stats);
        if (!sendStatus.isOk()) {
            return sendStatus;
        }
        stats->sentBytes += chunk.length;
        stats->resentBytes += chunk.length;
        stats->verifiedBytes += chunk.length;
    }

    protocol::FrameHeader fin;
    fin.type = protocol::FrameType::Fin;
    fin.streamId = streamId;
    fin.totalSize = totalSize;
    const common::Status finStatus = sendFrame(&connection, fin, {}, phaseStats);
    if (!finStatus.isOk()) {
        return finStatus;
    }
    return waitForFinalStatus(&connection, options.bufferSize, phaseStats);
}

}  // namespace

common::Status runFramedFileSenderOnListener(const FileDownloadSenderOptions& options,
                                             UniqueFd listener) {
    if (!checkpoint::isValidTransferId(options.transferId)) {
        return common::Status::invalidArgument("invalid transfer_id");
    }
    auto fileResult = storage::PosixFile::openReadOnly(options.path);
    if (!fileResult.isOk()) {
        return fileResult.status();
    }
    storage::PosixFile inputFile = std::move(fileResult.value());
    auto sizeResult = inputFile.fileSize();
    if (!sizeResult.isOk()) {
        return sizeResult.status();
    }
    const std::uint64_t totalSize = sizeResult.value();
    const common::Status adviceStatus =
        storage::applyFileIoAdvice(inputFile, options.fileIo.advice, 0, totalSize);
    if (!adviceStatus.isOk()) {
        return adviceStatus;
    }
    auto chunksResult = chunk::planChunks(totalSize, options.chunkSize, options.connections);
    if (!chunksResult.isOk()) {
        return chunksResult.status();
    }
    const std::vector<chunk::ChunkRange> chunks = std::move(chunksResult.value());
    auto resolvedBackend =
        checksum::resolveChecksumBackend(options.checksumAlgorithm, options.checksumBackend);
    if (!resolvedBackend.isOk()) {
        return resolvedBackend.status();
    }

    std::vector<FramedDataSocket> accepted;
    accepted.reserve(options.connections);
    while (accepted.size() < options.connections) {
        pollfd pollFd{};
        pollFd.fd = listener.get();
        pollFd.events = POLLIN;
        const int ready = ::poll(&pollFd, 1, 60000);
        if (ready == 0) {
            return common::Status::runtimeError("timed out waiting for data connections");
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("poll listener", errno);
        }
        const int fd = ::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("accept4", errno);
        }
        auto socket = acceptFramedDataSocket(UniqueFd(fd), options.dataTlsMode, options.dataTls);
        if (!socket.isOk()) {
            return socket.status();
        }
        accepted.emplace_back(std::move(socket.value()));
    }
    listener.reset();

    std::vector<common::Status> statuses(options.connections);
    std::vector<SenderStats> streamStats(options.connections);
    std::vector<std::thread> threads;
    threads.reserve(options.connections);
    metrics::TransferPhaseStats phaseStats;
    storage::FileIoStats fileIoStats;
    const storage::FileIoContext fileIoContext(options.fileIo);
    const common::Status fileIoStatus = fileIoContext.validateAvailable();
    if (!fileIoStatus.isOk()) {
        return fileIoStatus;
    }
    metrics::ScopedPhaseTimer overallTimer(&phaseStats, metrics::TransferPhase::Overall);

    common::ThroughputCounter counter;
    counter.start(common::ThroughputCounter::Clock::now());
    for (std::uint32_t streamId = 0; streamId < options.connections; ++streamId) {
        threads.emplace_back([&, streamId, connection = std::move(accepted[streamId])]() mutable {
            statuses[streamId] =
                sendStream(std::move(connection), options, inputFile, chunks,
                           resolvedBackend.value(), streamId, totalSize, &streamStats[streamId],
                           &phaseStats, fileIoContext, &fileIoStats);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (const common::Status& status : statuses) {
        if (!status.isOk()) {
            return status;
        }
    }

    std::uint64_t sentBytes = 0;
    std::uint64_t wireBytes = 0;
    std::uint64_t skippedBytes = 0;
    std::uint64_t resentBytes = 0;
    std::uint64_t verifiedBytes = 0;
    std::uint64_t compressionAttempts = 0;
    std::uint64_t compressedFrames = 0;
    std::uint64_t rawFallbackFrames = 0;
    std::uint64_t compressionFailures = 0;
    std::uint64_t compressedLogicalBytes = 0;
    std::uint64_t compressedWireBytes = 0;
    std::string compressionFallbackReason;
    for (const SenderStats& stats : streamStats) {
        sentBytes += stats.sentBytes;
        wireBytes += stats.wireBytes;
        skippedBytes += stats.skippedBytes;
        resentBytes += stats.resentBytes;
        verifiedBytes += stats.verifiedBytes;
        compressionAttempts += stats.compressionAttempts;
        compressedFrames += stats.compressedFrames;
        rawFallbackFrames += stats.rawFallbackFrames;
        compressionFailures += stats.compressionFailures;
        compressedLogicalBytes += stats.compressedLogicalBytes;
        compressedWireBytes += stats.compressedWireBytes;
        if (compressionFallbackReason.empty() && !stats.compressionFallbackReason.empty()) {
            compressionFallbackReason = stats.compressionFallbackReason;
        }
    }
    counter.addBytes(sentBytes);
    const auto end = common::ThroughputCounter::Clock::now();
    counter.stop(end);
    overallTimer.stop();

    const char* backendName = options.checksumAlgorithm == checksum::ChecksumAlgorithm::None
                                  ? "none"
                                  : checksum::checksumBackendName(resolvedBackend.value());
    if (options.runtimeMetrics != nullptr) {
        options.runtimeMetrics->elapsedSeconds = counter.elapsedSeconds(end);
        options.runtimeMetrics->sendSeconds = phaseStats.seconds(metrics::TransferPhase::Send);
        options.runtimeMetrics->recvSeconds = phaseStats.seconds(metrics::TransferPhase::Recv);
        options.runtimeMetrics->readSeconds = phaseStats.seconds(metrics::TransferPhase::Read);
        options.runtimeMetrics->writeSeconds = phaseStats.seconds(metrics::TransferPhase::Write);
        options.runtimeMetrics->checksumSeconds =
            phaseStats.seconds(metrics::TransferPhase::Checksum);
        options.runtimeMetrics->logicalBytes = totalSize;
        options.runtimeMetrics->wireBytes = wireBytes;
        options.runtimeMetrics->compressionAttempts = compressionAttempts;
        options.runtimeMetrics->compressedFrames = compressedFrames;
        options.runtimeMetrics->rawFallbackFrames = rawFallbackFrames;
        options.runtimeMetrics->compressionFailures = compressionFailures;
        options.runtimeMetrics->compressedLogicalBytes = compressedLogicalBytes;
        options.runtimeMetrics->compressedWireBytes = compressedWireBytes;
        options.runtimeMetrics->compressionFallbackReason = compressionFallbackReason;
    }
    std::cout << "file_download_sender sent_bytes=" << sentBytes
              << " wire_bytes=" << wireBytes
              << " elapsed_seconds=" << counter.elapsedSeconds(end)
              << " throughput_gbps=" << counter.gigabitsPerSecond(end)
              << " transfer_id=" << options.transferId << " checksum_backend=" << backendName
              << " skipped_bytes=" << skippedBytes << " resent_bytes=" << resentBytes
              << " verified_bytes=" << verifiedBytes
              << " compression_attempts=" << compressionAttempts
              << " compressed_frames=" << compressedFrames
              << " raw_fallback_frames=" << rawFallbackFrames
              << " compression_failures=" << compressionFailures
              << " compressed_logical_bytes=" << compressedLogicalBytes
              << " compressed_wire_bytes=" << compressedWireBytes
              << " file_io_backend=" << storage::fileIoBackendName(options.fileIo.backend)
              << " file_io_buffer_size=" << options.fileIo.bufferSize
              << " file_io_queue_depth=" << options.fileIo.queueDepth
              << " file_io_batch_size=" << options.fileIo.batchSize
              << " file_io_advice=" << storage::fileIoAdviceName(options.fileIo.advice)
              << " posix_write_strategy="
              << storage::posixWriteStrategyName(options.fileIo.posixWriteStrategy)
              << " posix_write_strategy_effective="
              << storage::posixWriteStrategyName(
                     storage::effectivePosixWriteStrategy(options.fileIo))
              << " data_tls_mode=" << dataTlsModeName(options.dataTlsMode);
    metrics::appendPhaseStats(std::cout, phaseStats);
    metrics::appendRetrSenderAliases(std::cout, phaseStats);
    storage::appendFileIoStats(std::cout, fileIoStats);
    std::cout << '\n' << std::flush;
    return common::Status::ok();
}

}  // namespace cpnetflux::core::io
