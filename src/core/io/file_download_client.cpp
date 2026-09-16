#include "cpnetflux/core/io/file_download_client.h"

#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cpnetflux/checkpoint/download_manifest.h"
#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/common/throughput_counter.h"
#include "cpnetflux/core/chunk/chunk_planner.h"
#include "cpnetflux/core/io/framed_data_socket.h"
#include "cpnetflux/core/io/socket_utils.h"
#include "cpnetflux/core/metrics/transfer_phase_stats.h"
#include "cpnetflux/core/protocol/compressed_data.h"
#include "cpnetflux/core/protocol/frame.h"
#include "cpnetflux/core/session/download_session.h"
#include "cpnetflux/storage/file_io.h"
#include "cpnetflux/storage/posix_file.h"

namespace cpnetflux::core::io {
namespace {

common::Status applyCommitSyncPolicy(const std::string& outputPath,
                                     session::CommitSyncPolicy policy) {
    if (policy == session::CommitSyncPolicy::None) {
        return common::Status::ok();
    }
    const common::Status fileStatus = storage::PosixFile::fsyncPath(outputPath);
    if (!fileStatus.isOk()) {
        return fileStatus;
    }
    if (policy == session::CommitSyncPolicy::FsyncFileAndDir) {
        return storage::PosixFile::fsyncParentDirectory(outputPath);
    }
    return common::Status::ok();
}

struct DownloadStats {
    std::uint64_t receivedBytes = 0;
    std::uint64_t wireBytes = 0;
    std::uint64_t verifiedBytes = 0;
    std::uint64_t compressedFrames = 0;
    std::uint64_t decompressionFailures = 0;
    std::uint64_t compressedLogicalBytes = 0;
    std::uint64_t compressedWireBytes = 0;
    double cpuSeconds = 0.0;
};

double threadCpuSeconds() noexcept {
    rusage usage{};
    if (::getrusage(RUSAGE_THREAD, &usage) != 0) {
        return 0.0;
    }
    return static_cast<double>(usage.ru_utime.tv_sec) +
           static_cast<double>(usage.ru_utime.tv_usec) / 1'000'000.0 +
           static_cast<double>(usage.ru_stime.tv_sec) +
           static_cast<double>(usage.ru_stime.tv_usec) / 1'000'000.0;
}

class ThreadCpuScope {
   public:
    explicit ThreadCpuScope(double* output) noexcept
        : output_(output), start_(threadCpuSeconds()) {}

    ~ThreadCpuScope() {
        if (output_ != nullptr) {
            *output_ = std::max(0.0, threadCpuSeconds() - start_);
        }
    }

   private:
    double* output_;
    double start_;
};

double normalizedCpuPercent(double cpuSeconds, double elapsedSeconds) noexcept {
    if (elapsedSeconds <= 0.0) {
        return 0.0;
    }
    const long cpuCount = ::sysconf(_SC_NPROCESSORS_ONLN);
    const double onlineCpus = static_cast<double>(std::max<long>(1, cpuCount));
    return std::max(0.0, cpuSeconds / elapsedSeconds / onlineCpus * 100.0);
}

struct SharedDownloadState {
    std::mutex mutex;
    bool sessionReady = false;
    bool sizeKnown = false;
    std::uint64_t knownTotalSize = 0;
    std::uint64_t knownChunkSize = 0;
    std::string sourcePath;
    std::vector<bool> streamsSeen;
    storage::PosixFile outputFile;
    storage::FileIoStats fileIoStats;
    session::DownloadSession session;
    metrics::TransferPhaseStats phaseStats;
    std::uint64_t verifiedChunks = 0;
};

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
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

common::Status sendStatusFrame(FramedDataSocket* socket, protocol::FrameType type,
                               protocol::FrameStatusCode statusCode, std::uint64_t totalSize,
                               metrics::TransferPhaseStats* phaseStats = nullptr) {
    protocol::FrameHeader header;
    header.type = type;
    header.statusCode = statusCode;
    header.totalSize = totalSize;
    return sendFrame(socket, header, {}, phaseStats);
}

common::Status sendResumeResponse(FramedDataSocket* socket, std::uint32_t streamId,
                                  std::uint64_t totalSize,
                                  std::uint32_t bufferSize,
                                  const std::vector<chunk::CompletedRange>& missingRanges,
                                  metrics::TransferPhaseStats* phaseStats) {
    protocol::ResumeResponsePayload response;
    response.statusCode = protocol::FrameStatusCode::Ok;
    response.missingRanges = missingRanges;

    auto payload = protocol::encodeResumeResponsePayload(response);
    if (!payload.isOk()) {
        return payload.status();
    }
    if (payload.value().size() > bufferSize) {
        return common::Status::runtimeError("ResumeResponse payload exceeds buffer size");
    }

    protocol::FrameHeader header;
    header.type = protocol::FrameType::ResumeResponse;
    header.streamId = streamId;
    header.payloadSize = static_cast<std::uint32_t>(payload.value().size());
    header.totalSize = totalSize;
    return sendFrame(socket, header, payload.value(), phaseStats);
}

common::Result<protocol::SessionInitPayload> readSessionInit(
    FramedDataSocket* socket, std::uint32_t bufferSize, const config::FileDownloadOptions& options,
    std::uint32_t* negotiatedStreamId, metrics::TransferPhaseStats* phaseStats) {
    auto header = recvHeader(socket, bufferSize, phaseStats);
    if (!header.isOk()) {
        return header.status();
    }
    if (header.value().type != protocol::FrameType::SessionInit) {
        return common::Status::invalidArgument("expected SessionInit");
    }
    if (header.value().streamId >= options.connections) {
        return common::Status::invalidArgument("stream_id exceeds connection count");
    }
    *negotiatedStreamId = header.value().streamId;

    std::vector<std::uint8_t> payload(header.value().payloadSize);
    const common::Status recvStatus = recvAll(socket, payload.data(), payload.size(), phaseStats);
    if (!recvStatus.isOk()) {
        return recvStatus;
    }
    auto decoded = protocol::decodeSessionInitPayload(payload.data(), payload.size());
    if (!decoded.isOk()) {
        return decoded.status();
    }
    if (options.resume && decoded.value().mode != protocol::SessionMode::Resume) {
        return common::Status::invalidArgument("expected resume SessionInit");
    }
    if (!options.resume && decoded.value().mode != protocol::SessionMode::New) {
        return common::Status::invalidArgument("unexpected resume SessionInit");
    }
    if (decoded.value().transferId != options.transferId) {
        return common::Status::invalidArgument("unexpected transfer_id");
    }
    if (decoded.value().checksumAlgorithm != options.checksumAlgorithm) {
        return common::Status::invalidArgument("checksum algorithm mismatch");
    }
    return decoded.value();
}

common::Result<std::vector<chunk::CompletedRange>> prepareDownloadSession(
    const config::FileDownloadOptions& options, const protocol::SessionInitPayload& init,
    checksum::ChecksumBackend checksumBackend, SharedDownloadState* state) {
    std::lock_guard<std::mutex> lock(state->mutex);

    const std::uint64_t totalSize = init.totalSize;
    const std::uint64_t chunkSize = init.chunkSize;
    const std::string sourcePath = init.sourcePath.empty() ? "<unknown>" : init.sourcePath;

    if (!state->sessionReady) {
        state->knownTotalSize = totalSize;
        state->knownChunkSize = chunkSize;
        state->sourcePath = sourcePath;
        state->sizeKnown = true;
        if (options.resume) {
            auto resumed = session::DownloadSession::resume(
                options.path, sourcePath, options.transferId, totalSize, chunkSize,
                options.checksumAlgorithm, checksumBackend,
                options.manifestFlushPolicy, options.manifestFlushIntervalChunks);
            if (!resumed.isOk()) {
                return resumed.status();
            }
            state->session = std::move(resumed.value());
            state->session.setPhaseStats(&state->phaseStats);
            auto fileResult = storage::PosixFile::openReadWrite(state->session.manifest().tempPath);
            if (!fileResult.isOk()) {
                return fileResult.status();
            }
            state->outputFile = std::move(fileResult.value());
            const common::Status adviceStatus =
                storage::applyFileIoAdvice(state->outputFile, options.fileIo.advice, 0,
                                           totalSize);
            if (!adviceStatus.isOk()) {
                return adviceStatus;
            }
            metrics::ScopedPhaseTimer timer(&state->phaseStats,
                                            metrics::TransferPhase::ResumePrecheck,
                                            state->session.bytesCompleted());
            const common::Status verifyStatus = state->session.verifyTempChunks(state->outputFile);
            timer.stop();
            if (!verifyStatus.isOk()) {
                return verifyStatus;
            }
        } else {
            auto outputExists = storage::PosixFile::pathExists(options.path);
            if (!outputExists.isOk()) {
                return outputExists.status();
            }
            if (outputExists.value() && !options.overwrite) {
                return common::Status::invalidArgument(
                    "output file already exists; use --overwrite");
            }
            const std::string manifestPath =
                checkpoint::downloadManifestPathForOutput(options.path);
            if (!options.overwrite) {
                auto manifestExists = storage::PosixFile::pathExists(manifestPath);
                if (!manifestExists.isOk()) {
                    return manifestExists.status();
                }
                if (manifestExists.value()) {
                    return common::Status::invalidArgument(
                        "download manifest already exists; use --resume");
                }
            }
            auto created = session::DownloadSession::createNew(
                options.path, sourcePath, options.transferId, totalSize, chunkSize,
                options.checksumAlgorithm, checksumBackend,
                options.manifestFlushPolicy, options.manifestFlushIntervalChunks);
            if (!created.isOk()) {
                return created.status();
            }
            state->session = std::move(created.value());
            state->session.setPhaseStats(&state->phaseStats);
            if (options.overwrite) {
                (void)storage::PosixFile::removePath(options.path);
                (void)storage::PosixFile::removePath(manifestPath);
            }
            (void)storage::PosixFile::removePath(state->session.manifest().tempPath);
            auto outputResult =
                storage::PosixFile::openReadWriteExclusive(state->session.manifest().tempPath);
            if (!outputResult.isOk()) {
                return outputResult.status();
            }
            state->outputFile = std::move(outputResult.value());
            if (totalSize > 0 && options.preallocateMode == storage::PreallocateMode::Full) {
                const common::Status preallocateStatus = state->outputFile.preallocate(totalSize);
                if (!preallocateStatus.isOk()) {
                    return preallocateStatus;
                }
            }
            common::Status resizeStatus;
            {
                metrics::ScopedPhaseTimer timer(&state->phaseStats, metrics::TransferPhase::Write);
                resizeStatus = state->outputFile.resize(totalSize);
            }
            if (!resizeStatus.isOk()) {
                return resizeStatus;
            }
            const common::Status adviceStatus =
                storage::applyFileIoAdvice(state->outputFile, options.fileIo.advice, 0,
                                           totalSize);
            if (!adviceStatus.isOk()) {
                return adviceStatus;
            }
            const common::Status saveStatus = state->session.save();
            if (!saveStatus.isOk()) {
                return saveStatus;
            }
        }
        state->sessionReady = true;
    } else {
        if (state->knownTotalSize != totalSize) {
            return common::Status::invalidArgument("session total_size mismatch");
        }
        if (state->knownChunkSize != chunkSize) {
            return common::Status::invalidArgument("session chunk_size mismatch");
        }
        if (state->sourcePath != sourcePath) {
            return common::Status::invalidArgument("session source_path mismatch");
        }
    }
    return state->session.missingRanges();
}

common::Status receiveStream(const config::FileDownloadOptions& options,
                             checksum::ChecksumBackend checksumBackend, SharedDownloadState* state,
                             DownloadStats* stats) {
    ThreadCpuScope cpuScope(stats == nullptr ? nullptr : &stats->cpuSeconds);
    auto connectionResult = connectFramedDataSocket(options.host.c_str(), options.port, options.dataTlsMode, options.dataTls);
    if (!connectionResult.isOk()) {
        return connectionResult.status();
    }
    FramedDataSocket connection = std::move(connectionResult.value());
    const common::Status timeoutStatus = setReceiveTimeout(connection.fd());
    if (!timeoutStatus.isOk()) {
        return timeoutStatus;
    }

    std::uint32_t streamId = 0;
    auto session =
        readSessionInit(&connection, options.bufferSize, options, &streamId,
                        &state->phaseStats);
    if (!session.isOk()) {
        (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                              protocol::FrameStatusCode::InvalidFrame, 0, &state->phaseStats);
        return session.status();
    }
    if (session.value().chunkSize == 0) {
        (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                              protocol::FrameStatusCode::InvalidFrame, session.value().totalSize,
                              &state->phaseStats);
        return common::Status::invalidArgument("chunk_size must be greater than zero");
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->streamsSeen[streamId]) {
            (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                  protocol::FrameStatusCode::InvalidFrame,
                                  session.value().totalSize, &state->phaseStats);
            return common::Status::invalidArgument("duplicate stream_id");
        }
        state->streamsSeen[streamId] = true;
    }

    auto missingRanges = prepareDownloadSession(options, session.value(), checksumBackend, state);
    if (!missingRanges.isOk()) {
        (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                              protocol::FrameStatusCode::InvalidFrame, session.value().totalSize,
                              &state->phaseStats);
        return missingRanges.status();
    }

    const common::Status responseStatus =
        sendResumeResponse(&connection, streamId, session.value().totalSize,
                           options.bufferSize, missingRanges.value(), &state->phaseStats);
    if (!responseStatus.isOk()) {
        return responseStatus;
    }

    std::vector<std::uint8_t> buffer(options.bufferSize);
    storage::BufferedFileWriter writer(state->outputFile, options.fileIo, &state->fileIoStats);
    bool chunkActive = false;
    std::uint64_t activeChunkId = 0;
    std::uint64_t activeChunkOffset = 0;
    std::uint64_t activeChunkNextOffset = 0;
    checksum::ChecksumComputer checksumComputer(options.checksumAlgorithm, checksumBackend);

    while (true) {
        auto header = recvHeader(&connection, options.bufferSize, &state->phaseStats);
        if (!header.isOk()) {
            return header.status();
        }
        if (header.value().type == protocol::FrameType::Fin) {
            if (chunkActive) {
                (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::InvalidFrame,
                                      session.value().totalSize, &state->phaseStats);
                return common::Status::invalidArgument("FIN received before ChunkComplete");
            }
            break;
        }
        if (header.value().type == protocol::FrameType::Data) {
            if (header.value().streamId != streamId ||
                header.value().totalSize != session.value().totalSize) {
                (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::InvalidFrame,
                                      session.value().totalSize, &state->phaseStats);
                return common::Status::invalidArgument("invalid DATA header");
            }
            if (!chunkActive) {
                chunkActive = true;
                activeChunkId = header.value().chunkId;
                activeChunkOffset = header.value().offset;
                activeChunkNextOffset = header.value().offset;
                checksumComputer =
                    checksum::ChecksumComputer(options.checksumAlgorithm, checksumBackend);
            }
            if (activeChunkId != header.value().chunkId ||
                activeChunkNextOffset != header.value().offset) {
                (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::InvalidFrame,
                                      session.value().totalSize, &state->phaseStats);
                return common::Status::invalidArgument("chunk DATA frames must be contiguous");
            }
            if (header.value().payloadSize > buffer.size()) {
                (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::InvalidFrame,
                                      session.value().totalSize, &state->phaseStats);
                return common::Status::invalidArgument("DATA payload exceeds buffer");
            }
            const common::Status recvStatus =
                recvAll(&connection, buffer.data(), header.value().payloadSize,
                        &state->phaseStats);
            if (!recvStatus.isOk()) {
                return recvStatus;
            }
            const bool compressed = (header.value().flags & protocol::kDataCompressed) != 0;
            const common::Status payloadStatus = protocol::validateDataFramePayload(
                header.value(), buffer.data(), header.value().payloadSize,
                static_cast<std::uint32_t>(buffer.size()));
            if (!payloadStatus.isOk()) {
                if (compressed) {
                    ++stats->decompressionFailures;
                }
                (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::InvalidFrame,
                                      session.value().totalSize, &state->phaseStats);
                return payloadStatus;
            }

            const std::uint8_t* logicalData = buffer.data();
            std::uint64_t logicalSize = header.value().payloadSize;
            std::vector<std::uint8_t> decompressed;
            if (compressed) {
                auto logicalLength = protocol::compressedDataPayloadLogicalLength(
                    buffer.data(), header.value().payloadSize);
                if (!logicalLength.isOk()) {
                    ++stats->decompressionFailures;
                    (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::InvalidFrame,
                                          session.value().totalSize, &state->phaseStats);
                    return logicalLength.status();
                }
                auto decoded = protocol::decodeCompressedDataPayload(
                    buffer.data(), header.value().payloadSize, logicalLength.value(), buffer.size());
                if (!decoded.isOk()) {
                    ++stats->decompressionFailures;
                    (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::InvalidFrame,
                                          session.value().totalSize, &state->phaseStats);
                    return decoded.status();
                }
                decompressed = std::move(decoded.value());
                logicalData = decompressed.data();
                logicalSize = decompressed.size();
                ++stats->compressedFrames;
                stats->compressedLogicalBytes += logicalSize;
                stats->compressedWireBytes += header.value().payloadSize;
            }
            common::Status writeStatus;
            {
                metrics::ScopedPhaseTimer timer(&state->phaseStats, metrics::TransferPhase::Write,
                                                logicalSize);
                writeStatus = writer.write(header.value().offset, logicalData, logicalSize);
            }
            if (!writeStatus.isOk()) {
                (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::WriteFailed,
                                      session.value().totalSize, &state->phaseStats);
                return writeStatus;
            }
            {
                metrics::ScopedPhaseTimer timer(&state->phaseStats,
                                                metrics::TransferPhase::Checksum,
                                                logicalSize);
                checksumComputer.update(logicalData, logicalSize);
            }
            activeChunkNextOffset += logicalSize;
            stats->receivedBytes += logicalSize;
            stats->wireBytes += header.value().payloadSize;
            continue;
        }
        if (header.value().type == protocol::FrameType::ChunkComplete) {
            std::vector<std::uint8_t> payload(header.value().payloadSize);
            const common::Status recvStatus =
                recvAll(&connection, payload.data(), payload.size(), &state->phaseStats);
            if (!recvStatus.isOk()) {
                return recvStatus;
            }
            auto complete = protocol::decodeChunkCompletePayload(payload.data(), payload.size());
            if (!complete.isOk()) {
                (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::InvalidFrame,
                                      session.value().totalSize, &state->phaseStats);
                return complete.status();
            }
            if (!chunkActive || complete.value().chunkId != activeChunkId ||
                complete.value().offset != activeChunkOffset ||
                complete.value().length != activeChunkNextOffset - activeChunkOffset ||
                complete.value().checksum.algorithm != options.checksumAlgorithm) {
                (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::InvalidFrame,
                                      session.value().totalSize, &state->phaseStats);
                return common::Status::invalidArgument("invalid ChunkComplete");
            }
            {
                metrics::ScopedPhaseTimer timer(&state->phaseStats, metrics::TransferPhase::Write);
                const common::Status flushStatus = writer.flush();
                if (!flushStatus.isOk()) {
                    (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::WriteFailed,
                                          session.value().totalSize, &state->phaseStats);
                    return flushStatus;
                }
            }
            checksum::ChecksumValue actual;
            {
                metrics::ScopedPhaseTimer timer(&state->phaseStats,
                                                metrics::TransferPhase::Checksum);
                actual = checksumComputer.finalize();
            }
            if (actual.algorithm != complete.value().checksum.algorithm ||
                actual.value != complete.value().checksum.value) {
                (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::ChecksumMismatch,
                                      session.value().totalSize, &state->phaseStats);
                return common::Status::invalidArgument("chunk checksum mismatch");
            }
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                const common::Status recordStatus = state->session.recordVerifiedChunk(
                    complete.value().chunkId, complete.value().offset, complete.value().length,
                    complete.value().checksum);
                if (!recordStatus.isOk()) {
                    (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::ChecksumMismatch,
                                          session.value().totalSize, &state->phaseStats);
                    return recordStatus;
                }
                state->verifiedChunks += 1;
                if (options.maxChunks != 0 && state->verifiedChunks >= options.maxChunks) {
                    return common::Status::runtimeError("max chunks reached");
                }
            }
            stats->verifiedBytes += complete.value().length;
            chunkActive = false;
            continue;
        }
        (void)sendStatusFrame(&connection, protocol::FrameType::Error,
                              protocol::FrameStatusCode::InvalidFrame, session.value().totalSize,
                              &state->phaseStats);
        return common::Status::invalidArgument("unexpected frame type from sender");
    }

    return sendStatusFrame(&connection, protocol::FrameType::Complete,
                           protocol::FrameStatusCode::Ok, session.value().totalSize,
                           &state->phaseStats);
}

}  // namespace

common::Status runFileDownloadClient(const config::FileDownloadOptions& options) {
    auto resolvedBackend =
        checksum::resolveChecksumBackend(options.checksumAlgorithm, options.checksumBackend);
    if (!resolvedBackend.isOk()) {
        return resolvedBackend.status();
    }
    const storage::FileIoContext fileIoContext(options.fileIo);
    const common::Status fileIoStatus = fileIoContext.validateAvailable();
    if (!fileIoStatus.isOk()) {
        return fileIoStatus;
    }

    std::vector<common::Status> statuses(options.connections);
    std::vector<DownloadStats> streamStats(options.connections);
    std::vector<std::thread> threads;
    threads.reserve(options.connections);
    SharedDownloadState state;
    state.streamsSeen.assign(options.connections, false);
    metrics::ScopedPhaseTimer overallTimer(&state.phaseStats, metrics::TransferPhase::Overall);

    common::ThroughputCounter counter;
    counter.start(common::ThroughputCounter::Clock::now());

    for (std::uint32_t streamId = 0; streamId < options.connections; ++streamId) {
        threads.emplace_back([&, streamId]() {
            statuses[streamId] =
                receiveStream(options, resolvedBackend.value(), &state, &streamStats[streamId]);
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    for (const common::Status& status : statuses) {
        if (!status.isOk()) {
            if (state.sessionReady) {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (options.maxChunks == 0) {
                    (void)state.session.markFailed();
                } else {
                    (void)state.session.save();
                }
            }
            return status;
        }
    }
    if (!state.sizeKnown || !state.sessionReady) {
        return common::Status::runtimeError("download size was not negotiated");
    }
    common::Status resizeStatus;
    {
        metrics::ScopedPhaseTimer timer(&state.phaseStats, metrics::TransferPhase::Write);
        resizeStatus = state.outputFile.resize(state.knownTotalSize);
    }
    if (!resizeStatus.isOk()) {
        (void)state.session.markFailed();
        return resizeStatus;
    }
    if (!state.session.missingRanges().empty()) {
        (void)state.session.markFailed();
        return common::Status::runtimeError("download is missing verified chunks");
    }
    std::string finalVerifyPolicyEffective = "full";
    if (session::canUseVerifiedChunksFinalVerify(options.finalVerifyPolicy,
                                                 options.checksumAlgorithm, state.knownTotalSize,
                                                 state.session.bytesCompleted(), false)) {
        const common::Status flushStatus = state.session.flushManifest();
        if (!flushStatus.isOk()) {
            (void)state.session.markFailed();
            return flushStatus;
        }
        finalVerifyPolicyEffective = "verified_chunks";
    } else {
        metrics::ScopedPhaseTimer timer(&state.phaseStats, metrics::TransferPhase::FinalVerify);
        timer.addBytes(state.knownTotalSize);
        const common::Status verifyStatus = state.session.verifyTempChunks(state.outputFile);
        timer.stop();
        if (!verifyStatus.isOk()) {
            (void)state.session.markFailed();
            return verifyStatus;
        }
        if (!state.session.missingRanges().empty()) {
            (void)state.session.markFailed();
            return common::Status::runtimeError("download temp preflight found missing chunks");
        }
    }
    state.outputFile = storage::PosixFile();

    auto outputExists = storage::PosixFile::pathExists(options.path);
    if (!outputExists.isOk()) {
        (void)state.session.markFailed();
        return outputExists.status();
    }
    if (outputExists.value() && !options.overwrite) {
        (void)state.session.markFailed();
        return common::Status::invalidArgument("output file already exists; use --overwrite");
    }
    if (outputExists.value() && options.overwrite) {
        common::Status removeStatus;
        {
            metrics::ScopedPhaseTimer timer(&state.phaseStats,
                                            metrics::TransferPhase::RenameCommit);
            removeStatus = storage::PosixFile::removePath(options.path);
        }
        if (!removeStatus.isOk()) {
            (void)state.session.markFailed();
            return removeStatus;
        }
    }
    {
        metrics::ScopedPhaseTimer timer(&state.phaseStats, metrics::TransferPhase::RenameCommit);
        const common::Status renameStatus =
            storage::PosixFile::renamePath(state.session.manifest().tempPath, options.path);
        if (!renameStatus.isOk()) {
            (void)state.session.markFailed();
            return renameStatus;
        }
        const common::Status syncStatus = applyCommitSyncPolicy(options.path, options.commitSyncPolicy);
        if (!syncStatus.isOk()) {
            (void)state.session.markFailed();
            return syncStatus;
        }
        const common::Status committedStatus = state.session.markCommitted();
        if (!committedStatus.isOk()) {
            return committedStatus;
        }
    }

    std::uint64_t receivedBytes = 0;
    std::uint64_t wireBytes = 0;
    std::uint64_t verifiedBytes = 0;
    std::uint64_t compressedFrames = 0;
    std::uint64_t decompressionFailures = 0;
    std::uint64_t compressedLogicalBytes = 0;
    std::uint64_t compressedWireBytes = 0;
    for (const DownloadStats& stats : streamStats) {
        receivedBytes += stats.receivedBytes;
        wireBytes += stats.wireBytes;
        verifiedBytes += stats.verifiedBytes;
        compressedFrames += stats.compressedFrames;
        decompressionFailures += stats.decompressionFailures;
        compressedLogicalBytes += stats.compressedLogicalBytes;
        compressedWireBytes += stats.compressedWireBytes;
    }
    counter.addBytes(receivedBytes);
    const auto end = common::ThroughputCounter::Clock::now();
    counter.stop(end);
    overallTimer.stop();
    if (options.runtimeMetrics != nullptr) {
        double cpuSeconds = 0.0;
        for (const DownloadStats& stats : streamStats) {
            cpuSeconds += stats.cpuSeconds;
        }
        const double elapsed = counter.elapsedSeconds(end);
        options.runtimeMetrics->elapsedSeconds = elapsed;
        options.runtimeMetrics->cpuPercent = normalizedCpuPercent(cpuSeconds, elapsed);
        options.runtimeMetrics->sendSeconds = state.phaseStats.seconds(metrics::TransferPhase::Send);
        options.runtimeMetrics->recvSeconds = state.phaseStats.seconds(metrics::TransferPhase::Recv);
        options.runtimeMetrics->readSeconds = state.phaseStats.seconds(metrics::TransferPhase::Read);
        options.runtimeMetrics->writeSeconds = state.phaseStats.seconds(metrics::TransferPhase::Write);
        options.runtimeMetrics->checksumSeconds =
            state.phaseStats.seconds(metrics::TransferPhase::Checksum);
        options.runtimeMetrics->logicalBytes = state.knownTotalSize;
        options.runtimeMetrics->wireBytes = wireBytes;
        options.runtimeMetrics->compressedFrames = compressedFrames;
        options.runtimeMetrics->decompressionFailures = decompressionFailures;
        options.runtimeMetrics->compressedLogicalBytes = compressedLogicalBytes;
        options.runtimeMetrics->compressedWireBytes = compressedWireBytes;
    }

    const char* backendName = options.checksumAlgorithm == checksum::ChecksumAlgorithm::None
                                  ? "none"
                                  : checksum::checksumBackendName(resolvedBackend.value());
    std::cout << "file_download_client received_bytes=" << receivedBytes
              << " wire_bytes=" << wireBytes
              << " elapsed_seconds=" << counter.elapsedSeconds(end)
              << " throughput_gbps=" << counter.gigabitsPerSecond(end)
              << " transfer_id=" << options.transferId << " checksum_backend=" << backendName
              << " skipped_bytes=" << state.session.stats().skippedBytes
              << " resent_bytes=" << state.session.stats().resentBytes
              << " verified_bytes=" << state.session.stats().verifiedBytes
              << " compressed_frames=" << compressedFrames
              << " decompression_failures=" << decompressionFailures
              << " compressed_logical_bytes=" << compressedLogicalBytes
              << " compressed_wire_bytes=" << compressedWireBytes
              << " removed_corrupt_chunks=" << state.session.stats().removedCorruptChunks
              << " manifest_flush_policy="
              << session::manifestFlushPolicyName(state.session.manifestFlushPolicy())
              << " manifest_flush_interval_chunks="
              << state.session.manifestFlushIntervalChunks()
              << " legacy_manifest_flush_policy=every_"
              << state.session.manifestFlushIntervalChunks() << "_chunks"
              << " manifest_flush_count=" << state.session.stats().manifestFlushCount
              << " final_verify_policy="
              << session::finalVerifyPolicyName(options.finalVerifyPolicy)
              << " final_verify_policy_effective=" << finalVerifyPolicyEffective
              << " commit_sync_policy=" << session::commitSyncPolicyName(options.commitSyncPolicy)
              << " preallocate=" << storage::preallocateModeName(options.preallocateMode)
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
    metrics::appendPhaseStats(std::cout, state.phaseStats);
    metrics::appendRetrReceiverAliases(std::cout, state.phaseStats);
    storage::appendFileIoStats(std::cout, state.fileIoStats);
    std::cout << '\n';
    return common::Status::ok();
}

}  // namespace cpnetflux::core::io
