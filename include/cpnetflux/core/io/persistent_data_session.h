#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_set>

#include "cpnetflux/core/io/framed_data_socket.h"

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/protocol/frame.h"
#include "cpnetflux/checksum/checksum.h"
#include "cpnetflux/core/chunk/range_list.h"

namespace cpnetflux::core::io {

struct PersistentFileIdentity {
    std::uint32_t fileId = 0;
    std::uint64_t generation = 0;
    std::uint64_t totalSize = 0;
    std::string relativePath;
    std::string transferId;
    std::uint64_t chunkSize = 1048576;
    std::int64_t mtimeUnixSeconds = 0;
    checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::None;
    checksum::ChecksumValue checksum;
    std::uint64_t rangeId = 0;
    std::uint64_t rangeOffset = 0;
    std::uint64_t rangeLength = 0;
    std::uint64_t rangeCount = 0;
    std::uint64_t attempt = 0;
};

struct PersistentFrame {
    protocol::FrameHeader header;
    std::vector<std::uint8_t> payload;
};

class PersistentDataSession {
   public:
    explicit PersistentDataSession(bool unordered = false) : unordered_(unordered) {}
    static constexpr std::uint32_t kMaxPayload = 65536;
    [[nodiscard]] common::Status begin(const protocol::FrameHeader& header,
                                       const std::string& relativePath,
                                       checksum::ChecksumAlgorithm checksumAlgorithm = checksum::ChecksumAlgorithm::None);
    [[nodiscard]] common::Status begin(const protocol::FrameHeader& header,
                                       const PersistentFileIdentity& identity);
    [[nodiscard]] common::Status data(const protocol::FrameHeader& header,
                                      std::size_t logicalBytes,
                                      const std::uint8_t* payload = nullptr);
    [[nodiscard]] common::Result<PersistentFileIdentity> end(const protocol::FrameHeader& header,
                                                             const std::uint8_t* digest = nullptr,
                                                             std::size_t digestSize = 0);
    [[nodiscard]] common::Status fail(const protocol::FrameHeader& header,
                                      protocol::FrameStatusCode status);

    [[nodiscard]] static common::Status writeBegin(FramedDataSocket* socket,
                                                   const PersistentFileIdentity& identity);
    [[nodiscard]] static common::Status writeData(FramedDataSocket* socket,
                                                  const PersistentFileIdentity& identity,
                                                  std::uint64_t offset,
                                                  const std::uint8_t* payload,
                                                  std::size_t length);
    [[nodiscard]] static common::Status writeEnd(FramedDataSocket* socket,
                                                 const PersistentFileIdentity& identity,
                                                 checksum::ChecksumValue checksum = {}, bool skipped = false);
    [[nodiscard]] static common::Status writeResumeResponse(FramedDataSocket* socket, const PersistentFileIdentity& identity, const std::vector<core::chunk::CompletedRange>& missingRanges);
    [[nodiscard]] static common::Result<protocol::ResumeResponsePayload> readResumeResponse(FramedDataSocket* socket, const PersistentFileIdentity& identity);
    [[nodiscard]] static common::Status writeResult(FramedDataSocket* socket,
                                                    const PersistentFileIdentity& identity,
                                                    protocol::FrameStatusCode status);
    [[nodiscard]] static common::Result<PersistentFrame> readNext(FramedDataSocket* socket,
                                                                  std::uint32_t maxPayloadSize);
    [[nodiscard]] static common::Result<PersistentFileIdentity> decodeBegin(const PersistentFrame& frame);
    [[nodiscard]] static common::Result<protocol::FrameStatusCode> readResult(
        FramedDataSocket* socket, const PersistentFileIdentity& identity);
    [[nodiscard]] static common::Status writeDirectoryEnd(FramedDataSocket* socket,
                                                         std::uint64_t fileCount);
    [[nodiscard]] static common::Status setTimeout(FramedDataSocket* socket, int seconds);

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const PersistentFileIdentity& current() const noexcept { return current_; }
    [[nodiscard]] std::uint64_t receivedBytes() const noexcept { return receivedBytes_; }

   private:
    [[nodiscard]] common::Status match(const protocol::FrameHeader& header) const;
    [[nodiscard]] static bool safeRelativePath(const std::string& path) noexcept;
    [[nodiscard]] static common::Status writeFrame(FramedDataSocket* socket,
                                                   protocol::FrameHeader header,
                                                   const std::uint8_t* payload,
                                                   std::size_t length);

    bool unordered_ = false;
    std::unordered_set<std::uint32_t> seenFiles_;
    std::unordered_set<std::uint64_t> seenGenerations_;
    std::unordered_set<std::string> seenRangeIdentities_;
    bool active_ = false;
    PersistentFileIdentity current_;
    std::uint64_t receivedBytes_ = 0;
    checksum::ChecksumAlgorithm checksumAlgorithm_ = checksum::ChecksumAlgorithm::None;
    checksum::ChecksumComputer checksumComputer_{checksum::ChecksumAlgorithm::None};
    std::uint32_t lastFileId_ = 0;
    std::uint64_t lastGeneration_ = 0;
};

}  // namespace cpnetflux::core::io
