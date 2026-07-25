#include "gridflux/core/io/file_transfer_server.h"

#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gridflux/checkpoint/transfer_manifest.h"
#include "gridflux/checksum/checksum.h"
#include "gridflux/common/throughput_counter.h"
#include "gridflux/core/io/connection_context.h"
#include "gridflux/core/io/framed_data_socket.h"
#include "gridflux/core/io/socket_utils.h"
#include "gridflux/core/metrics/transfer_phase_stats.h"
#include "gridflux/core/protocol/frame.h"
#include "gridflux/core/session/transfer_session.h"
#include "gridflux/core/session/transfer_session_config.h"
#include "gridflux/storage/file_io.h"
#include "gridflux/storage/posix_file.h"

namespace gridflux::core::io {
namespace {

struct ListenerToken {};

void maybeDelayBeforeDataCompleteForTest() {
    const char* value = std::getenv("GRIDFLUX_TEST_DELAY_BEFORE_DATA_COMPLETE_MS");
    if (value == nullptr || *value == '\0') {
        return;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 600000) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(parsed));
}

enum class ReadState {
    Header,
    Payload,
    Done,
};

struct FileServerConnection {
    UniqueFd fd;
    FramedDataSocket socket;
    ConnectionContext context;
    ReadState readState = ReadState::Header;
    protocol::EncodedFrameHeader encodedHeader{};
    protocol::FrameHeader currentHeader;
    std::vector<std::uint8_t> payloadBuffer;
    std::size_t headerBytesRead = 0;
    std::size_t payloadBytesRead = 0;
    std::uint32_t streamId = 0;
    bool hasStream = false;
    bool sessionReady = false;
    bool finReceived = false;
    bool chunkActive = false;
    std::uint64_t activeChunkId = 0;
    std::uint64_t activeChunkOffset = 0;
    std::uint64_t activeChunkNextOffset = 0;
    checksum::ChecksumComputer checksumComputer{checksum::ChecksumAlgorithm::None};
    std::unique_ptr<storage::BufferedFileWriter> writer;
};

struct TransferState {
    bool hasSession = false;
    protocol::SessionMode mode = protocol::SessionMode::New;
    std::string transferId;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::Crc32c;
    checksum::ChecksumBackend checksumBackend = checksum::ChecksumBackend::Software;
    std::uint64_t totalSize = 0;
    std::uint64_t chunkSize = 0;
    std::uint64_t initialVerifiedBytes = 0;
    std::uint64_t bytesWritten = 0;
    std::uint32_t finConnections = 0;
    std::vector<bool> streamsSeen;
    session::TransferSession session;
    storage::PosixFile outputFile;
    storage::FileIoStats fileIoStats;
    common::ThroughputCounter counter;
    metrics::TransferPhaseStats phaseStats;
    protocol::FrameStatusCode errorCode = protocol::FrameStatusCode::InternalError;
    bool outputOpen = false;
    bool counterStarted = false;
};

common::Status systemStatus(const char* operation, int errorNumber) {
    return common::Status::systemError(std::string(operation) + ": " + std::strerror(errorNumber),
                                       errorNumber);
}

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

common::Status sendAllNonBlocking(int fd, const std::uint8_t* data, std::size_t length,
                                  metrics::TransferPhaseStats* phaseStats = nullptr) {
    metrics::ScopedPhaseTimer timer(phaseStats, metrics::TransferPhase::Send, length);
    std::size_t completed = 0;
    while (completed < length) {
        const ssize_t sent = ::send(fd, data + completed, length - completed, MSG_NOSIGNAL);
        if (sent > 0) {
            completed += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pollFd{};
            pollFd.fd = fd;
            pollFd.events = POLLOUT;
            const int ready = ::poll(&pollFd, 1, 10000);
            if (ready > 0) {
                continue;
            }
            if (ready == 0) {
                return common::Status::runtimeError("send timed out");
            }
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("poll send", errno);
        }
        if (sent < 0) {
            return systemStatus("send", errno);
        }
        return common::Status::runtimeError("send returned zero bytes");
    }
    return common::Status::ok();
}

common::Status sendFrame(int fd, const protocol::FrameHeader& header,
                         const std::vector<std::uint8_t>& payload,
                         metrics::TransferPhaseStats* phaseStats = nullptr) {
    const protocol::EncodedFrameHeader encoded = protocol::encodeFrameHeader(header);
    const common::Status headerStatus =
        sendAllNonBlocking(fd, encoded.data(), encoded.size(), phaseStats);
    if (!headerStatus.isOk()) {
        return headerStatus;
    }
    if (!payload.empty()) {
        return sendAllNonBlocking(fd, payload.data(), payload.size(), phaseStats);
    }
    return common::Status::ok();
}

common::Status sendAll(FramedDataSocket* socket, const std::uint8_t* data, std::size_t length,
                       metrics::TransferPhaseStats* phaseStats = nullptr) {
    metrics::ScopedPhaseTimer timer(phaseStats, metrics::TransferPhase::Send, length);
    return socket->writeAll(data, length);
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

common::Status sendStatusFrame(FramedDataSocket* socket, protocol::FrameType type,
                               protocol::FrameStatusCode statusCode, std::uint64_t totalSize,
                               metrics::TransferPhaseStats* phaseStats = nullptr) {
    protocol::FrameHeader header;
    header.type = type;
    header.statusCode = statusCode;
    header.payloadSize = 0;
    header.totalSize = totalSize;
    return sendFrame(socket, header, {}, phaseStats);
}

common::Status sendStatusFrame(int fd, protocol::FrameType type,
                               protocol::FrameStatusCode statusCode, std::uint64_t totalSize,
                               metrics::TransferPhaseStats* phaseStats = nullptr) {
    protocol::FrameHeader header;
    header.type = type;
    header.statusCode = statusCode;
    header.payloadSize = 0;
    header.totalSize = totalSize;
    return sendFrame(fd, header, {}, phaseStats);
}

common::Status sendResumeResponse(FileServerConnection& connection, TransferState& transfer,
                                  std::uint32_t maxPayloadSize) {
    protocol::ResumeResponsePayload payload;
    payload.statusCode = protocol::FrameStatusCode::Ok;
    payload.missingRanges = transfer.session.missingRanges();
    auto encodedPayload = protocol::encodeResumeResponsePayload(payload);
    if (!encodedPayload.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::InternalError;
        return encodedPayload.status();
    }
    if (encodedPayload.value().size() > maxPayloadSize) {
        transfer.errorCode = protocol::FrameStatusCode::InternalError;
        return common::Status::runtimeError("resume response exceeds buffer size");
    }

    protocol::FrameHeader header;
    header.type = protocol::FrameType::ResumeResponse;
    header.streamId = connection.streamId;
    header.payloadSize = static_cast<std::uint32_t>(encodedPayload.value().size());
    header.totalSize = transfer.totalSize;
    if (connection.socket.valid()) {
        return sendFrame(&connection.socket, header, encodedPayload.value(),
                         &transfer.phaseStats);
    }
    return sendFrame(connection.context.fd(), header, encodedPayload.value(), &transfer.phaseStats);
}

common::Status sendStatusToConnections(std::vector<FileServerConnection>& connections,
                                       protocol::FrameType type,
                                       protocol::FrameStatusCode statusCode,
                                       std::uint64_t totalSize, bool bestEffort) {
    common::Status firstError = common::Status::ok();
    for (FileServerConnection& connection : connections) {
        if (!connection.fd.isValid() && !connection.socket.valid()) {
            continue;
        }
        common::Status status =
            connection.socket.valid()
                ? sendStatusFrame(&connection.socket, type, statusCode, totalSize)
                : sendStatusFrame(connection.fd.get(), type, statusCode, totalSize);
        if (!status.isOk() && firstError.isOk()) {
            firstError = status;
        }
    }

    if (bestEffort) {
        return common::Status::ok();
    }
    return firstError;
}

void stopReadingConnection(int epollFd, FileServerConnection& connection) {
    (void)deleteEpollFd(epollFd, connection.context.fd());
    connection.context.markClosing();
    connection.readState = ReadState::Done;
}

common::Status validateStream(FileServerConnection& connection, TransferState& transfer,
                              std::uint32_t streamId, std::uint32_t expectedConnections) {
    if (streamId >= expectedConnections) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("frame stream_id exceeds connection count");
    }

    if (!connection.hasStream) {
        if (transfer.streamsSeen[streamId]) {
            transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
            return common::Status::invalidArgument("duplicate stream_id");
        }
        transfer.streamsSeen[streamId] = true;
        connection.streamId = streamId;
        connection.hasStream = true;
        return common::Status::ok();
    }

    if (connection.streamId != streamId) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("stream_id changed on one connection");
    }

    return common::Status::ok();
}

common::Status validateExpectedChunkRange(TransferState& transfer,
                                          const protocol::ChunkCompletePayload& payload) {
    if (transfer.checksumAlgorithm == checksum::ChecksumAlgorithm::None) {
        return common::Status::ok();
    }
    if (payload.chunkId > std::numeric_limits<std::uint64_t>::max() / transfer.chunkSize) {
        transfer.errorCode = protocol::FrameStatusCode::RangeOutOfBounds;
        return common::Status::invalidArgument("chunk_id overflows expected offset");
    }

    const std::uint64_t expectedOffset = payload.chunkId * transfer.chunkSize;
    if (expectedOffset >= transfer.totalSize && transfer.totalSize != 0) {
        transfer.errorCode = protocol::FrameStatusCode::RangeOutOfBounds;
        return common::Status::invalidArgument("chunk_id exceeds transfer size");
    }
    if (transfer.totalSize == 0) {
        transfer.errorCode = protocol::FrameStatusCode::RangeOutOfBounds;
        return common::Status::invalidArgument("zero-byte transfer must not carry chunks");
    }

    const std::uint64_t expectedLength =
        std::min<std::uint64_t>(transfer.chunkSize, transfer.totalSize - expectedOffset);
    if (payload.offset != expectedOffset || payload.length != expectedLength) {
        transfer.errorCode = protocol::FrameStatusCode::RangeOutOfBounds;
        return common::Status::invalidArgument("chunk complete range does not match chunk plan");
    }
    return common::Status::ok();
}

common::Status openOutputForSession(TransferState& transfer,
                                    const config::FileTransferOptions& options, bool resume) {
    if (resume) {
        auto outputResult = storage::PosixFile::openReadWrite(transfer.session.manifest().tempPath);
        if (!outputResult.isOk()) {
            transfer.errorCode = protocol::FrameStatusCode::WriteFailed;
            return outputResult.status();
        }
        transfer.outputFile = std::move(outputResult.value());
        transfer.outputOpen = true;
        const common::Status adviceStatus =
            storage::applyFileIoAdvice(transfer.outputFile, options.fileIo.advice, 0,
                                       transfer.totalSize);
        if (!adviceStatus.isOk()) {
            transfer.errorCode = protocol::FrameStatusCode::WriteFailed;
            return adviceStatus;
        }
        return common::Status::ok();
    }

    auto outputResult =
        storage::PosixFile::openReadWriteExclusive(transfer.session.manifest().tempPath);
    if (!outputResult.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::WriteFailed;
        return outputResult.status();
    }
    transfer.outputFile = std::move(outputResult.value());
    transfer.outputOpen = true;
    if (transfer.totalSize > 0 && options.preallocateMode == storage::PreallocateMode::Full) {
        const common::Status preallocateStatus = transfer.outputFile.preallocate(transfer.totalSize);
        if (!preallocateStatus.isOk()) {
            transfer.errorCode = protocol::FrameStatusCode::WriteFailed;
            return preallocateStatus;
        }
    }
    const common::Status resizeStatus = transfer.outputFile.resize(transfer.totalSize);
    if (!resizeStatus.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::WriteFailed;
        return resizeStatus;
    }
    const common::Status adviceStatus =
        storage::applyFileIoAdvice(transfer.outputFile, options.fileIo.advice, 0,
                                   transfer.totalSize);
    if (!adviceStatus.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::WriteFailed;
        return adviceStatus;
    }
    return common::Status::ok();
}

common::Status initializeSession(TransferState& transfer,
                                 const config::FileTransferOptions& options,
                                 const protocol::SessionInitPayload& payload) {
    if (!checkpoint::isValidTransferId(payload.transferId)) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("invalid transfer_id");
    }
    if (!options.transferId.empty() && payload.transferId != options.transferId) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("unexpected transfer_id");
    }
    const bool payloadResume = payload.mode == protocol::SessionMode::Resume;
    if (payloadResume != options.resume) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("client/server resume mode mismatch");
    }
    if (payload.checksumAlgorithm != options.checksumAlgorithm) {
        transfer.errorCode = protocol::FrameStatusCode::ChecksumUnsupported;
        return common::Status::invalidArgument("client/server checksum algorithm mismatch");
    }
    auto resolvedBackend =
        checksum::resolveChecksumBackend(payload.checksumAlgorithm, options.checksumBackend);
    if (!resolvedBackend.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::ChecksumUnsupported;
        return resolvedBackend.status();
    }

    if (transfer.hasSession) {
        if (transfer.mode != payload.mode || transfer.transferId != payload.transferId ||
            transfer.totalSize != payload.totalSize || transfer.chunkSize != payload.chunkSize ||
            transfer.checksumAlgorithm != payload.checksumAlgorithm ||
            transfer.checksumBackend != resolvedBackend.value()) {
            transfer.errorCode = protocol::FrameStatusCode::SizeMismatch;
            return common::Status::invalidArgument("session init changed during transfer");
        }
        return common::Status::ok();
    }

    auto sessionResult = payloadResume
                             ? session::TransferSession::resume(
                                   options.path, payload.transferId, payload.totalSize,
                                   payload.chunkSize, payload.checksumAlgorithm,
                                   options.checksumBackend, options.manifestFlushPolicy,
                                   options.manifestFlushIntervalChunks)
                             : session::TransferSession::createNew(
                                   options.path, payload.transferId, payload.totalSize,
                                   payload.chunkSize, payload.checksumAlgorithm,
                                   options.checksumBackend, options.manifestFlushPolicy,
                                   options.manifestFlushIntervalChunks);
    if (!sessionResult.isOk()) {
        transfer.errorCode = sessionResult.status().message().find("manifest") != std::string::npos
                                 ? protocol::FrameStatusCode::ManifestCorrupt
                                 : protocol::FrameStatusCode::InvalidFrame;
        return sessionResult.status();
    }

    transfer.session = std::move(sessionResult.value());
    transfer.session.setPhaseStats(&transfer.phaseStats);
    transfer.hasSession = true;
    transfer.mode = payload.mode;
    transfer.transferId = payload.transferId;
    transfer.checksumAlgorithm = payload.checksumAlgorithm;
    transfer.checksumBackend = transfer.session.checksumBackend();
    transfer.totalSize = payload.totalSize;
    transfer.chunkSize = payload.chunkSize;
    transfer.bytesWritten = transfer.session.bytesCompleted();
    transfer.initialVerifiedBytes = transfer.bytesWritten;

    const common::Status openStatus = openOutputForSession(transfer, options, payloadResume);
    if (!openStatus.isOk()) {
        return openStatus;
    }
    if (payloadResume) {
        metrics::ScopedPhaseTimer timer(&transfer.phaseStats,
                                        metrics::TransferPhase::ResumePrecheck,
                                        transfer.session.bytesCompleted());
        const common::Status verifyStatus = transfer.session.verifyTempChunks(transfer.outputFile);
        timer.stop();
        if (!verifyStatus.isOk()) {
            transfer.errorCode = protocol::FrameStatusCode::ChecksumMismatch;
            return verifyStatus;
        }
        transfer.bytesWritten = transfer.session.bytesCompleted();
        transfer.initialVerifiedBytes = transfer.bytesWritten;
    }

    const common::Status saveStatus = transfer.session.save();
    if (!saveStatus.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::InternalError;
        return saveStatus;
    }

    return common::Status::ok();
}

common::Status processSessionInitPayload(FileServerConnection& connection, TransferState& transfer,
                                         const config::FileTransferOptions& options) {
    auto decoded = protocol::decodeSessionInitPayload(connection.payloadBuffer.data(),
                                                      connection.currentHeader.payloadSize);
    if (!decoded.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return decoded.status();
    }

    const common::Status initStatus = initializeSession(transfer, options, decoded.value());
    if (!initStatus.isOk()) {
        return initStatus;
    }

    connection.sessionReady = true;
    const common::Status responseStatus =
        sendResumeResponse(connection, transfer, options.bufferSize);
    if (!responseStatus.isOk()) {
        return responseStatus;
    }

    connection.payloadBytesRead = 0;
    connection.readState = ReadState::Header;
    return common::Status::ok();
}

common::Status validateTransferHeader(FileServerConnection& connection, TransferState& transfer,
                                      const protocol::FrameHeader& header) {
    if (!connection.sessionReady) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("DATA/FIN received before SessionInit");
    }
    if (!transfer.hasSession) {
        transfer.errorCode = protocol::FrameStatusCode::InternalError;
        return common::Status::runtimeError("transfer session is not initialized");
    }
    if (header.totalSize != transfer.totalSize) {
        transfer.errorCode = protocol::FrameStatusCode::SizeMismatch;
        return common::Status::invalidArgument("frame total_size changed during transfer");
    }
    if (connection.streamId != header.streamId) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("stream_id changed on one connection");
    }
    return common::Status::ok();
}

common::Status processCompletedHeader(FileServerConnection& connection, TransferState& transfer,
                                      const config::FileTransferOptions& options) {
    auto decoded = protocol::decodeFrameHeader(connection.encodedHeader);
    if (!decoded.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return decoded.status();
    }

    connection.currentHeader = decoded.value();
    const common::Status validateStatus =
        protocol::validateFrameHeader(connection.currentHeader, options.bufferSize);
    if (!validateStatus.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return validateStatus;
    }
    if (connection.currentHeader.type == protocol::FrameType::Complete ||
        connection.currentHeader.type == protocol::FrameType::Error ||
        connection.currentHeader.type == protocol::FrameType::ResumeResponse) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("client sent server-only frame");
    }

    const common::Status streamStatus = validateStream(
        connection, transfer, connection.currentHeader.streamId, options.connections);
    if (!streamStatus.isOk()) {
        return streamStatus;
    }

    connection.headerBytesRead = 0;
    if (connection.currentHeader.type == protocol::FrameType::SessionInit) {
        connection.payloadBytesRead = 0;
        connection.readState = ReadState::Payload;
        return common::Status::ok();
    }

    const common::Status transferHeaderStatus =
        validateTransferHeader(connection, transfer, connection.currentHeader);
    if (!transferHeaderStatus.isOk()) {
        return transferHeaderStatus;
    }

    if (connection.currentHeader.type == protocol::FrameType::Fin) {
        if (connection.chunkActive) {
            transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
            return common::Status::invalidArgument("FIN received before ChunkComplete");
        }
        connection.finReceived = true;
        transfer.finConnections += 1;
        connection.readState = ReadState::Done;
        return common::Status::ok();
    }

    connection.payloadBytesRead = 0;
    connection.readState = ReadState::Payload;
    return common::Status::ok();
}

common::Status processDataPayload(FileServerConnection& connection, TransferState& transfer) {
    const protocol::FrameHeader& header = connection.currentHeader;
    if (!connection.chunkActive) {
        connection.chunkActive = true;
        connection.activeChunkId = header.chunkId;
        connection.activeChunkOffset = header.offset;
        connection.activeChunkNextOffset = header.offset;
        connection.checksumComputer =
            checksum::ChecksumComputer(transfer.checksumAlgorithm, transfer.checksumBackend);
    }
    if (connection.activeChunkId != header.chunkId ||
        connection.activeChunkNextOffset != header.offset) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("chunk DATA frames must be contiguous");
    }

    common::Status writeStatus;
    {
        metrics::ScopedPhaseTimer timer(&transfer.phaseStats, metrics::TransferPhase::Write,
                                        header.payloadSize);
        writeStatus = connection.writer->write(header.offset, connection.payloadBuffer.data(),
                                               header.payloadSize);
    }
    if (!writeStatus.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::WriteFailed;
        return writeStatus;
    }

    {
        metrics::ScopedPhaseTimer timer(&transfer.phaseStats, metrics::TransferPhase::Checksum,
                                        header.payloadSize);
        connection.checksumComputer.update(connection.payloadBuffer.data(), header.payloadSize);
    }
    connection.activeChunkNextOffset += header.payloadSize;

    connection.context.addBytesReceived(header.payloadSize);
    transfer.bytesWritten = transfer.session.bytesCompleted();
    if (!transfer.counterStarted) {
        transfer.counter.start(common::ThroughputCounter::Clock::now());
        transfer.counterStarted = true;
    }
    transfer.counter.addBytes(header.payloadSize);

    connection.payloadBytesRead = 0;
    connection.readState = ReadState::Header;
    return common::Status::ok();
}

common::Status processChunkCompletePayload(FileServerConnection& connection,
                                           TransferState& transfer) {
    auto decoded = protocol::decodeChunkCompletePayload(connection.payloadBuffer.data(),
                                                        connection.currentHeader.payloadSize);
    if (!decoded.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return decoded.status();
    }
    const protocol::ChunkCompletePayload& payload = decoded.value();
    if (!connection.chunkActive || payload.chunkId != connection.activeChunkId ||
        payload.offset != connection.activeChunkOffset ||
        payload.length != connection.activeChunkNextOffset - connection.activeChunkOffset) {
        transfer.errorCode = protocol::FrameStatusCode::InvalidFrame;
        return common::Status::invalidArgument("chunk complete does not match active chunk");
    }
    if (payload.checksum.algorithm != transfer.checksumAlgorithm) {
        transfer.errorCode = protocol::FrameStatusCode::ChecksumUnsupported;
        return common::Status::invalidArgument("chunk checksum algorithm mismatch");
    }
    const common::Status expectedRangeStatus = validateExpectedChunkRange(transfer, payload);
    if (!expectedRangeStatus.isOk()) {
        return expectedRangeStatus;
    }
    {
        metrics::ScopedPhaseTimer timer(&transfer.phaseStats, metrics::TransferPhase::Write);
        const common::Status flushStatus = connection.writer->flush();
        if (!flushStatus.isOk()) {
            transfer.errorCode = protocol::FrameStatusCode::WriteFailed;
            return flushStatus;
        }
    }

    checksum::ChecksumValue actual;
    {
        metrics::ScopedPhaseTimer timer(&transfer.phaseStats, metrics::TransferPhase::Checksum);
        actual = connection.checksumComputer.finalize();
    }
    if (actual.value != payload.checksum.value || actual.algorithm != payload.checksum.algorithm) {
        transfer.errorCode = protocol::FrameStatusCode::ChecksumMismatch;
        return common::Status::invalidArgument("chunk checksum mismatch");
    }

    const common::Status verifiedStatus = transfer.session.recordVerifiedChunk(
        payload.chunkId, payload.offset, payload.length, payload.checksum);
    if (!verifiedStatus.isOk()) {
        transfer.errorCode = protocol::FrameStatusCode::ChecksumMismatch;
        return verifiedStatus;
    }

    connection.chunkActive = false;
    connection.payloadBytesRead = 0;
    connection.readState = ReadState::Header;
    transfer.bytesWritten = transfer.session.bytesCompleted();
    return common::Status::ok();
}

common::Status processCompletedPayload(FileServerConnection& connection, TransferState& transfer,
                                       const config::FileTransferOptions& options) {
    if (connection.currentHeader.type == protocol::FrameType::SessionInit) {
        return processSessionInitPayload(connection, transfer, options);
    }
    if (connection.currentHeader.type == protocol::FrameType::ChunkComplete) {
        return processChunkCompletePayload(connection, transfer);
    }
    return processDataPayload(connection, transfer);
}

common::Status readFromConnection(FileServerConnection& connection, TransferState& transfer,
                                  const config::FileTransferOptions& options, int epollFd) {
    while (connection.readState != ReadState::Done) {
        if (connection.readState == ReadState::Header) {
            const std::size_t remaining =
                connection.encodedHeader.size() - connection.headerBytesRead;
            ssize_t received = 0;
            {
                metrics::ScopedPhaseTimer timer(&transfer.phaseStats,
                                                metrics::TransferPhase::Recv);
                received =
                    ::recv(connection.context.fd(),
                           connection.encodedHeader.data() + connection.headerBytesRead, remaining,
                           0);
                if (received > 0) {
                    timer.addBytes(static_cast<std::uint64_t>(received));
                }
            }
            if (received > 0) {
                connection.headerBytesRead += static_cast<std::size_t>(received);
                if (connection.headerBytesRead == connection.encodedHeader.size()) {
                    const common::Status status =
                        processCompletedHeader(connection, transfer, options);
                    if (!status.isOk()) {
                        return status;
                    }
                    if (connection.readState == ReadState::Done) {
                        stopReadingConnection(epollFd, connection);
                        break;
                    }
                }
                continue;
            }

            if (received == 0) {
                transfer.errorCode = protocol::FrameStatusCode::InternalError;
                return common::Status::runtimeError("connection closed before FIN frame");
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            connection.context.markError(errno);
            transfer.errorCode = protocol::FrameStatusCode::InternalError;
            return systemStatus("recv header", errno);
        }

        const std::size_t remaining =
            static_cast<std::size_t>(connection.currentHeader.payloadSize) -
            connection.payloadBytesRead;
        ssize_t received = 0;
        {
            metrics::ScopedPhaseTimer timer(&transfer.phaseStats, metrics::TransferPhase::Recv);
            received =
                ::recv(connection.context.fd(),
                       connection.payloadBuffer.data() + connection.payloadBytesRead, remaining,
                       0);
            if (received > 0) {
                timer.addBytes(static_cast<std::uint64_t>(received));
            }
        }
        if (received > 0) {
            connection.payloadBytesRead += static_cast<std::size_t>(received);
            if (connection.payloadBytesRead == connection.currentHeader.payloadSize) {
                const common::Status status =
                    processCompletedPayload(connection, transfer, options);
                if (!status.isOk()) {
                    return status;
                }
            }
            continue;
        }

        if (received == 0) {
            transfer.errorCode = protocol::FrameStatusCode::InternalError;
            return common::Status::runtimeError("connection closed before payload completed");
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        connection.context.markError(errno);
        transfer.errorCode = protocol::FrameStatusCode::InternalError;
        return systemStatus("recv payload", errno);
    }

    return common::Status::ok();
}

common::Status readAllBlocking(FramedDataSocket* socket, std::uint8_t* data, std::size_t length,
                               metrics::TransferPhaseStats* phaseStats) {
    metrics::ScopedPhaseTimer timer(phaseStats, metrics::TransferPhase::Recv, length);
    return socket->readAll(data, length);
}

common::Status readFromTlsConnection(FileServerConnection& connection, TransferState& transfer,
                                     const config::FileTransferOptions& options) {
    while (connection.readState != ReadState::Done) {
        if (connection.readState == ReadState::Header) {
            const std::size_t remaining =
                connection.encodedHeader.size() - connection.headerBytesRead;
            const common::Status readStatus =
                readAllBlocking(&connection.socket,
                                connection.encodedHeader.data() + connection.headerBytesRead,
                                remaining, &transfer.phaseStats);
            if (!readStatus.isOk()) {
                transfer.errorCode = protocol::FrameStatusCode::InternalError;
                return readStatus;
            }
            connection.headerBytesRead = connection.encodedHeader.size();
            const common::Status status = processCompletedHeader(connection, transfer, options);
            if (!status.isOk()) {
                return status;
            }
            continue;
        }

        const std::size_t remaining =
            static_cast<std::size_t>(connection.currentHeader.payloadSize) -
            connection.payloadBytesRead;
        const common::Status readStatus =
            readAllBlocking(&connection.socket,
                            connection.payloadBuffer.data() + connection.payloadBytesRead,
                            remaining, &transfer.phaseStats);
        if (!readStatus.isOk()) {
            transfer.errorCode = protocol::FrameStatusCode::InternalError;
            return readStatus;
        }
        connection.payloadBytesRead = connection.currentHeader.payloadSize;
        const common::Status status = processCompletedPayload(connection, transfer, options);
        if (!status.isOk()) {
            return status;
        }
        if (connection.writer == nullptr && transfer.outputOpen) {
            connection.writer = std::make_unique<storage::BufferedFileWriter>(
                transfer.outputFile, options.fileIo, &transfer.fileIoStats);
        }
    }
    return common::Status::ok();
}

bool transferComplete(const TransferState& transfer, std::uint32_t expectedConnections) noexcept {
    return transfer.hasSession && transfer.finConnections == expectedConnections;
}

void markFailedBestEffort(TransferState& transfer) {
    if (transfer.hasSession) {
        (void)transfer.session.markFailed();
    }
}

common::Status validateServerOutputState(const config::FileTransferOptions& options) {
    auto outputExists = storage::PosixFile::pathExists(options.path);
    if (!outputExists.isOk()) {
        return outputExists.status();
    }
    if (outputExists.value() && !options.overwrite) {
        return common::Status::invalidArgument("output file already exists; use --overwrite");
    }

    const std::string manifestPath = checkpoint::manifestPathForOutput(options.path);
    auto manifestExists = storage::PosixFile::pathExists(manifestPath);
    if (!manifestExists.isOk()) {
        return manifestExists.status();
    }
    if (manifestExists.value() && !options.resume) {
        return common::Status::invalidArgument("manifest already exists; use --resume");
    }
    if (!manifestExists.value() && options.resume) {
        return common::Status::invalidArgument("manifest is required for --resume");
    }
    return common::Status::ok();
}

common::Status runFileTransferServerOnListenerTls(const config::FileTransferOptions& options,
                                                  UniqueFd listener) {
    const common::Status outputStatus = validateServerOutputState(options);
    if (!outputStatus.isOk()) {
        return outputStatus;
    }
    const storage::FileIoContext fileIoContext(options.fileIo);
    const common::Status fileIoStatus = fileIoContext.validateAvailable();
    if (!fileIoStatus.isOk()) {
        return fileIoStatus;
    }

    std::vector<FileServerConnection> connections;
    connections.reserve(options.connections);
    while (connections.size() < options.connections) {
        pollfd pollFd{};
        pollFd.fd = listener.get();
        pollFd.events = POLLIN;
        const int ready = ::poll(&pollFd, 1, 60000);
        if (ready == 0) {
            return common::Status::runtimeError("timed out waiting for data TLS connections");
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("poll data TLS listener", errno);
        }
        const int acceptedFd = ::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC);
        if (acceptedFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemStatus("accept4 data TLS", errno);
        }
        auto socket =
            acceptFramedDataSocket(UniqueFd(acceptedFd), options.dataTlsMode, options.dataTls);
        if (!socket.isOk()) {
            return socket.status();
        }
        FileServerConnection connection;
        connection.socket = std::move(socket.value());
        connection.context.setFd(connection.socket.fd());
        connection.context.markConnected();
        connection.payloadBuffer.resize(options.bufferSize);
        connections.push_back(std::move(connection));
    }
    listener.reset();

    TransferState transfer;
    metrics::ScopedPhaseTimer overallTimer(&transfer.phaseStats, metrics::TransferPhase::Overall);
    transfer.streamsSeen.resize(options.connections, false);

    while (!transferComplete(transfer, options.connections)) {
        for (FileServerConnection& connection : connections) {
            if (connection.readState == ReadState::Done) {
                continue;
            }
            if (!transfer.outputOpen && transfer.hasSession) {
                return common::Status::runtimeError("transfer output not ready");
            }
            if (connection.writer == nullptr && transfer.outputOpen) {
                connection.writer = std::make_unique<storage::BufferedFileWriter>(
                    transfer.outputFile, options.fileIo, &transfer.fileIoStats);
            }
            const common::Status readStatus =
                readFromTlsConnection(connection, transfer, options);
            if (!readStatus.isOk()) {
                (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                              transfer.errorCode, transfer.totalSize, true);
                markFailedBestEffort(transfer);
                return readStatus;
            }
            if (connection.writer == nullptr && transfer.outputOpen) {
                connection.writer = std::make_unique<storage::BufferedFileWriter>(
                    transfer.outputFile, options.fileIo, &transfer.fileIoStats);
            }
        }
    }

    if (!transfer.session.missingRanges().empty()) {
        (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::MissingRange, transfer.totalSize,
                                      true);
        markFailedBestEffort(transfer);
        return common::Status::runtimeError("transfer has missing ranges");
    }

    std::string finalVerifyPolicyEffective = "full";
    if (session::canUseVerifiedChunksFinalVerify(
            options.finalVerifyPolicy, transfer.checksumAlgorithm, transfer.totalSize,
            transfer.session.bytesCompleted(), false)) {
        const common::Status flushBeforeSkipStatus = transfer.session.flushManifest();
        if (!flushBeforeSkipStatus.isOk()) {
            markFailedBestEffort(transfer);
            return flushBeforeSkipStatus;
        }
        finalVerifyPolicyEffective = "verified_chunks";
    } else {
        metrics::ScopedPhaseTimer timer(&transfer.phaseStats,
                                        metrics::TransferPhase::FinalVerify, transfer.totalSize);
        const common::Status finalVerifyStatus =
            transfer.session.verifyTempChunks(transfer.outputFile);
        timer.stop();
        if (!finalVerifyStatus.isOk()) {
            markFailedBestEffort(transfer);
            return finalVerifyStatus;
        }
        if (!transfer.session.missingRanges().empty()) {
            markFailedBestEffort(transfer);
            return common::Status::runtimeError("final verify found missing ranges");
        }
    }

    const common::Status flushStatus = transfer.session.flushManifest();
    if (!flushStatus.isOk()) {
        markFailedBestEffort(transfer);
        return flushStatus;
    }

    auto outputExists = storage::PosixFile::pathExists(options.path);
    if (!outputExists.isOk()) {
        markFailedBestEffort(transfer);
        return outputExists.status();
    }
    if (outputExists.value() && !options.overwrite) {
        markFailedBestEffort(transfer);
        return common::Status::invalidArgument("output file already exists; use --overwrite");
    }

    {
        metrics::ScopedPhaseTimer timer(&transfer.phaseStats,
                                        metrics::TransferPhase::RenameCommit);
        const common::Status renameStatus =
            storage::PosixFile::renamePath(transfer.session.manifest().tempPath, options.path);
        if (!renameStatus.isOk()) {
            markFailedBestEffort(transfer);
            return renameStatus;
        }
        const common::Status syncStatus =
            applyCommitSyncPolicy(options.path, options.commitSyncPolicy);
        if (!syncStatus.isOk()) {
            markFailedBestEffort(transfer);
            return syncStatus;
        }
        const common::Status committedStatus = transfer.session.markCommitted();
        if (!committedStatus.isOk()) {
            return committedStatus;
        }
    }

    maybeDelayBeforeDataCompleteForTest();
    const common::Status completeStatus =
        sendStatusToConnections(connections, protocol::FrameType::Complete,
                                protocol::FrameStatusCode::Ok, transfer.totalSize, false);
    if (!completeStatus.isOk()) {
        std::cerr << "data_complete_status_send_warning message="
                  << completeStatus.message() << '\n';
    }

    const auto end = common::ThroughputCounter::Clock::now();
    if (!transfer.counterStarted) {
        transfer.counter.start(end);
    }
    transfer.counter.stop(end);
    overallTimer.stop();

    const session::TransferSessionStats& stats = transfer.session.stats();
    const char* backendName = transfer.checksumAlgorithm == checksum::ChecksumAlgorithm::None
                                  ? "none"
                                  : checksum::checksumBackendName(transfer.checksumBackend);
    std::cout << "file_server received_bytes=" << transfer.bytesWritten
              << " elapsed_seconds=" << transfer.counter.elapsedSeconds(end)
              << " throughput_gbps=" << transfer.counter.gigabitsPerSecond(end)
              << " transfer_id=" << transfer.transferId << " checksum_backend=" << backendName
              << " skipped_bytes=" << transfer.initialVerifiedBytes
              << " resent_bytes=" << transfer.counter.bytes()
              << " verified_bytes=" << transfer.session.bytesCompleted()
              << " loaded_verified_chunks=" << stats.loadedVerifiedChunks
              << " removed_corrupt_chunks=" << stats.removedCorruptChunks
              << " missing_chunks=" << stats.missingChunks
              << " manifest_flush_policy="
              << session::manifestFlushPolicyName(transfer.session.manifestFlushPolicy())
              << " manifest_flush_interval_chunks="
              << transfer.session.manifestFlushIntervalChunks()
              << " legacy_manifest_flush_policy=every_"
              << transfer.session.manifestFlushIntervalChunks() << "_chunks"
              << " manifest_flush_count=" << stats.manifestFlushCount
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
    metrics::appendPhaseStats(std::cout, transfer.phaseStats);
    metrics::appendStorReceiverAliases(std::cout, transfer.phaseStats);
    storage::appendFileIoStats(std::cout, transfer.fileIoStats);
    std::cout << '\n' << std::flush;
    return common::Status::ok();
}

}  // namespace

common::Status runFileTransferServerOnListener(const config::FileTransferOptions& options,
                                               UniqueFd listener) {
    if (options.dataTlsMode == DataTlsMode::Required) {
        return runFileTransferServerOnListenerTls(options, std::move(listener));
    }
    const common::Status outputStatus = validateServerOutputState(options);
    if (!outputStatus.isOk()) {
        return outputStatus;
    }
    const storage::FileIoContext fileIoContext(options.fileIo);
    const common::Status fileIoStatus = fileIoContext.validateAvailable();
    if (!fileIoStatus.isOk()) {
        return fileIoStatus;
    }

    auto epollResult = createEpoll();
    if (!epollResult.isOk()) {
        return epollResult.status();
    }
    UniqueFd epollFd = std::move(epollResult.value());

    ListenerToken listenerToken;
    auto addListener = addEpollFd(epollFd.get(), listener.get(), EPOLLIN, &listenerToken);
    if (!addListener.isOk()) {
        return addListener;
    }

    std::vector<FileServerConnection> connections;
    connections.reserve(options.connections);
    std::vector<epoll_event> events(options.connections + 1);
    TransferState transfer;
    metrics::ScopedPhaseTimer overallTimer(&transfer.phaseStats, metrics::TransferPhase::Overall);
    transfer.streamsSeen.resize(options.connections, false);
    bool accepting = true;

    while (!transferComplete(transfer, options.connections)) {
        const int eventCount =
            ::epoll_wait(epollFd.get(), events.data(), static_cast<int>(events.size()), -1);
        if (eventCount < 0) {
            if (errno == EINTR) {
                continue;
            }
            markFailedBestEffort(transfer);
            return systemStatus("epoll_wait", errno);
        }

        for (int index = 0; index < eventCount; ++index) {
            if (events[index].data.ptr == &listenerToken) {
                while (accepting) {
                    const int acceptedFd =
                        ::accept4(listener.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (acceptedFd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        if (errno == EINTR) {
                            continue;
                        }
                        markFailedBestEffort(transfer);
                        return systemStatus("accept4", errno);
                    }

                    connections.emplace_back();
                    FileServerConnection& connection = connections.back();
                    connection.fd.reset(acceptedFd);
                    connection.context.setFd(acceptedFd);
                    connection.context.markConnected();
                    connection.payloadBuffer.resize(options.bufferSize);
                    connection.writer = std::make_unique<storage::BufferedFileWriter>(
                        transfer.outputFile, options.fileIo, &transfer.fileIoStats);

                    auto addConnection =
                        addEpollFd(epollFd.get(), acceptedFd,
                                   EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP, &connection);
                    if (!addConnection.isOk()) {
                        markFailedBestEffort(transfer);
                        return addConnection;
                    }

                    if (connections.size() >= options.connections) {
                        accepting = false;
                        auto deleteListener = deleteEpollFd(epollFd.get(), listener.get());
                        if (!deleteListener.isOk()) {
                            markFailedBestEffort(transfer);
                            return deleteListener;
                        }
                        listener.reset();
                        break;
                    }
                }
                continue;
            }

            auto* connection = static_cast<FileServerConnection*>(events[index].data.ptr);
            if ((events[index].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U &&
                (events[index].events & EPOLLIN) == 0U) {
                transfer.errorCode = protocol::FrameStatusCode::InternalError;
                (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                              transfer.errorCode, transfer.totalSize, true);
                markFailedBestEffort(transfer);
                return common::Status::runtimeError("connection closed before transfer completed");
            }

            const common::Status readStatus =
                readFromConnection(*connection, transfer, options, epollFd.get());
            if (!readStatus.isOk()) {
                (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                              transfer.errorCode, transfer.totalSize, true);
                markFailedBestEffort(transfer);
                return readStatus;
            }
        }

        if (!accepting && transfer.hasSession && transfer.finConnections == options.connections &&
            !transfer.session.missingRanges().empty()) {
            transfer.errorCode = protocol::FrameStatusCode::MissingRange;
            (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                          transfer.errorCode, transfer.totalSize, true);
            markFailedBestEffort(transfer);
            return common::Status::runtimeError("all streams finished with missing ranges");
        }
    }

    if (!transfer.session.missingRanges().empty()) {
        (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::MissingRange, transfer.totalSize,
                                      true);
        markFailedBestEffort(transfer);
        return common::Status::runtimeError("transfer has missing ranges");
    }

    std::string finalVerifyPolicyEffective = "full";
    if (session::canUseVerifiedChunksFinalVerify(
            options.finalVerifyPolicy, transfer.checksumAlgorithm, transfer.totalSize,
            transfer.session.bytesCompleted(), false)) {
        const common::Status flushBeforeSkipStatus = transfer.session.flushManifest();
        if (!flushBeforeSkipStatus.isOk()) {
            (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::InternalError,
                                          transfer.totalSize, true);
            markFailedBestEffort(transfer);
            return flushBeforeSkipStatus;
        }
        finalVerifyPolicyEffective = "verified_chunks";
    } else {
        metrics::ScopedPhaseTimer timer(&transfer.phaseStats,
                                        metrics::TransferPhase::FinalVerify, transfer.totalSize);
        const common::Status finalVerifyStatus =
            transfer.session.verifyTempChunks(transfer.outputFile);
        timer.stop();
        if (!finalVerifyStatus.isOk()) {
            (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::ChecksumMismatch,
                                          transfer.totalSize, true);
            markFailedBestEffort(transfer);
            return finalVerifyStatus;
        }
        if (!transfer.session.missingRanges().empty()) {
            (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::MissingRange,
                                          transfer.totalSize, true);
            markFailedBestEffort(transfer);
            return common::Status::runtimeError("final verify found missing ranges");
        }
    }

    const common::Status flushStatus = transfer.session.flushManifest();
    if (!flushStatus.isOk()) {
        (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::InternalError, transfer.totalSize,
                                      true);
        markFailedBestEffort(transfer);
        return flushStatus;
    }

    auto outputExists = storage::PosixFile::pathExists(options.path);
    if (!outputExists.isOk()) {
        (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::InternalError, transfer.totalSize,
                                      true);
        markFailedBestEffort(transfer);
        return outputExists.status();
    }
    if (outputExists.value() && !options.overwrite) {
        (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                      protocol::FrameStatusCode::OutputExists, transfer.totalSize,
                                      true);
        markFailedBestEffort(transfer);
        return common::Status::invalidArgument("output file already exists; use --overwrite");
    }

    {
        metrics::ScopedPhaseTimer timer(&transfer.phaseStats,
                                        metrics::TransferPhase::RenameCommit);
        const common::Status renameStatus =
            storage::PosixFile::renamePath(transfer.session.manifest().tempPath, options.path);
        if (!renameStatus.isOk()) {
            (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::WriteFailed,
                                          transfer.totalSize, true);
            markFailedBestEffort(transfer);
            return renameStatus;
        }

        const common::Status syncStatus =
            applyCommitSyncPolicy(options.path, options.commitSyncPolicy);
        if (!syncStatus.isOk()) {
            (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::WriteFailed,
                                          transfer.totalSize, true);
            markFailedBestEffort(transfer);
            return syncStatus;
        }

        const common::Status committedStatus = transfer.session.markCommitted();
        if (!committedStatus.isOk()) {
            (void)sendStatusToConnections(connections, protocol::FrameType::Error,
                                          protocol::FrameStatusCode::InternalError,
                                          transfer.totalSize, true);
            return committedStatus;
        }
    }

    maybeDelayBeforeDataCompleteForTest();
    const common::Status completeStatus =
        sendStatusToConnections(connections, protocol::FrameType::Complete,
                                protocol::FrameStatusCode::Ok, transfer.totalSize, false);
    if (!completeStatus.isOk()) {
        std::cerr << "data_complete_status_send_warning message="
                  << completeStatus.message() << '\n';
    }

    const auto end = common::ThroughputCounter::Clock::now();
    if (!transfer.counterStarted) {
        transfer.counter.start(end);
    }
    transfer.counter.stop(end);
    overallTimer.stop();

    const session::TransferSessionStats& stats = transfer.session.stats();
    const char* backendName = transfer.checksumAlgorithm == checksum::ChecksumAlgorithm::None
                                  ? "none"
                                  : checksum::checksumBackendName(transfer.checksumBackend);
    std::cout << "file_server received_bytes=" << transfer.bytesWritten
              << " elapsed_seconds=" << transfer.counter.elapsedSeconds(end)
              << " throughput_gbps=" << transfer.counter.gigabitsPerSecond(end)
              << " transfer_id=" << transfer.transferId << " checksum_backend=" << backendName
              << " skipped_bytes=" << transfer.initialVerifiedBytes
              << " resent_bytes=" << transfer.counter.bytes()
              << " verified_bytes=" << transfer.session.bytesCompleted()
              << " loaded_verified_chunks=" << stats.loadedVerifiedChunks
              << " removed_corrupt_chunks=" << stats.removedCorruptChunks
              << " missing_chunks=" << stats.missingChunks
              << " manifest_flush_policy="
              << session::manifestFlushPolicyName(transfer.session.manifestFlushPolicy())
              << " manifest_flush_interval_chunks="
              << transfer.session.manifestFlushIntervalChunks()
              << " legacy_manifest_flush_policy=every_"
              << transfer.session.manifestFlushIntervalChunks() << "_chunks"
              << " manifest_flush_count=" << stats.manifestFlushCount
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
    metrics::appendPhaseStats(std::cout, transfer.phaseStats);
    metrics::appendStorReceiverAliases(std::cout, transfer.phaseStats);
    storage::appendFileIoStats(std::cout, transfer.fileIoStats);
    std::cout << '\n' << std::flush;

    return common::Status::ok();
}

common::Status runFileTransferServer(const config::FileTransferOptions& options) {
    const common::Status outputStatus = validateServerOutputState(options);
    if (!outputStatus.isOk()) {
        return outputStatus;
    }

    auto listenerResult =
        createListener(options.host.c_str(), options.port, static_cast<int>(options.connections));
    if (!listenerResult.isOk()) {
        return listenerResult.status();
    }

    return runFileTransferServerOnListener(options, std::move(listenerResult.value()));
}

}  // namespace gridflux::core::io
