#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cpnetflux/common/status.h"
#include "cpnetflux/core/scheduler/link_profile.h"
#include "cpnetflux/core/scheduler/work_item.h"

namespace cpnetflux::core::scheduler {

struct LinkRuntimeState {
    std::uint64_t readyBytes = 0;
    std::uint64_t inflightBytes = 0;
    std::uint64_t connectionEpoch = 1;
    std::uint64_t retryCount = 0;
    std::uint64_t rttMs = 0;
    std::uint64_t bdpBytes = 0;
    bool healthy = true;
    bool draining = false;
};

struct LinkSetCapability {
    std::uint32_t maxLinks = 1;
    std::uint32_t maxPendingWindow = 1;
    std::uint64_t capabilityBits = 0;
};

struct LinkNegotiation {
    std::uint32_t requestedLinks = 0;
    std::uint32_t acceptedLinks = 0;
    std::uint32_t requestedPendingWindow = 0;
    std::uint32_t acceptedPendingWindow = 0;
    std::vector<std::string> acceptedLinkIds;
    std::vector<std::string> rejectedLinkIds;
    std::string reason;
};

// Stable, additive control-plane representation. The wire protocol can adopt this
// field without duplicating scheduler-specific parsing rules.
[[nodiscard]] std::string encodeLinkSetCapability(const LinkSetCapability& capability);
[[nodiscard]] common::Result<LinkSetCapability> decodeLinkSetCapability(std::string_view encoded);
[[nodiscard]] std::string encodeLinkSetControlFields(const LinkSetCapability& capability);
[[nodiscard]] common::Result<LinkSetCapability> decodeLinkSetControlFields(std::string_view encoded);
[[nodiscard]] std::string summarizeLinkNegotiation(const LinkNegotiation& negotiation);

class LinkSet {
   public:
    static constexpr std::uint32_t kMaxLinks = 10;

    explicit LinkSet(std::uint32_t pendingWindow = 1);

    [[nodiscard]] common::Status add(const LinkProfile& profile);
    [[nodiscard]] common::Status addLink(const LinkProfile& profile) { return add(profile); }
    [[nodiscard]] std::size_t size() const noexcept { return links_.size(); }
    [[nodiscard]] bool empty() const noexcept { return links_.empty(); }
    [[nodiscard]] std::vector<std::string> linkIds() const;
    [[nodiscard]] const LinkProfile* profile(const std::string& linkId) const;
    [[nodiscard]] const LinkRuntimeState* runtime(const std::string& linkId) const;
    [[nodiscard]] LinkRuntimeState* runtime(const std::string& linkId);
    [[nodiscard]] LinkSetCapability advertisedCapability() const noexcept;

    [[nodiscard]] common::Result<LinkNegotiation> negotiate(
        const LinkSetCapability& peer) const;
    [[nodiscard]] common::Result<std::string> choose(const WorkItem& item) const;

    [[nodiscard]] common::Status reserve(const std::string& linkId,
                                         const std::string& leaseId,
                                         std::uint64_t bytes);
    [[nodiscard]] common::Status release(const std::string& linkId,
                                         const std::string& leaseId,
                                         std::uint64_t bytes);
    [[nodiscard]] common::Status setHealthy(const std::string& linkId, bool healthy);
    [[nodiscard]] common::Status setDraining(const std::string& linkId, bool draining);
    [[nodiscard]] common::Status setConnectionEpoch(const std::string& linkId,
                                                     std::uint64_t epoch);
    [[nodiscard]] common::Status setRuntime(const std::string& linkId,
                                            std::uint64_t readyBytes,
                                            std::uint64_t rttMs,
                                            std::uint64_t bdpBytes);

   private:
    struct LinkEntry {
        LinkProfile profile;
        LinkRuntimeState runtime;
    };
    struct Lease {
        std::string linkId;
        std::uint64_t bytes = 0;
    };

    [[nodiscard]] LinkEntry* entry(const std::string& linkId);
    [[nodiscard]] const LinkEntry* entry(const std::string& linkId) const;
    [[nodiscard]] static std::string endpointKey(const LinkProfile& profile);
    [[nodiscard]] std::uint64_t admissionLimit(const LinkEntry& entry) const noexcept;

    std::uint32_t pendingWindow_;
    std::vector<LinkEntry> links_;
    std::unordered_map<std::string, Lease> leases_;
};

}  // namespace cpnetflux::core::scheduler
