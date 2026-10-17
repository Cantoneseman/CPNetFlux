#include "cpnetflux/core/scheduler/link_set.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace cpnetflux::core::scheduler {
namespace {

common::Status unknownLink(const std::string& linkId) {
    return common::Status::invalidArgument("unknown link_id: " + linkId);
}

bool parseUnsigned(std::string_view text, std::uint64_t* value) {
    if (text.empty() || value == nullptr) {
        return false;
    }
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, *value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::string joinIds(const std::vector<std::string>& ids) {
    if (ids.empty()) {
        return "-";
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << ids[index];
    }
    return output.str();
}

}  // namespace

std::string encodeLinkSetCapability(const LinkSetCapability& capability) {
    return "links=" + std::to_string(capability.maxLinks) +
           " pending_window=" + std::to_string(capability.maxPendingWindow) +
           " capabilities=" + std::to_string(capability.capabilityBits);
}

common::Result<LinkSetCapability> decodeLinkSetCapability(std::string_view encoded) {
    LinkSetCapability capability;
    bool hasLinks = false;
    bool hasPendingWindow = false;
    bool hasCapabilities = false;
    std::istringstream input{std::string(encoded)};
    std::string token;
    while (input >> token) {
        const auto separator = token.find('=');
        if (separator == std::string::npos || separator == 0 ||
            separator + 1 == token.size()) {
            return common::Status::invalidArgument("invalid link capability token: " + token);
        }
        const auto key = std::string_view(token).substr(0, separator);
        const auto value = std::string_view(token).substr(separator + 1);
        std::uint64_t parsed = 0;
        if (!parseUnsigned(value, &parsed)) {
            return common::Status::invalidArgument("invalid link capability value: " + token);
        }
        if (key == "links") {
            if (hasLinks || parsed == 0 || parsed > LinkSet::kMaxLinks) {
                return common::Status::invalidArgument("invalid link capability links");
            }
            capability.maxLinks = static_cast<std::uint32_t>(parsed);
            hasLinks = true;
        } else if (key == "pending_window") {
            if (hasPendingWindow || parsed == 0 || parsed > 16) {
                return common::Status::invalidArgument(
                    "invalid link capability pending_window");
            }
            capability.maxPendingWindow = static_cast<std::uint32_t>(parsed);
            hasPendingWindow = true;
        } else if (key == "capabilities") {
            if (hasCapabilities) {
                return common::Status::invalidArgument("duplicate link capability capabilities");
            }
            capability.capabilityBits = parsed;
            hasCapabilities = true;
        } else {
            return common::Status::invalidArgument("unknown link capability key: " +
                                                   std::string(key));
        }
    }
    if (!hasLinks || !hasPendingWindow || !hasCapabilities) {
        return common::Status::invalidArgument("incomplete link capability advertisement");
    }
    return capability;
}

std::string encodeLinkSetControlFields(const LinkSetCapability& capability) {
    return "LINKS=" + std::to_string(capability.maxLinks) +
           " LINK_CAPS=" + std::to_string(capability.capabilityBits);
}

common::Result<LinkSetCapability> decodeLinkSetControlFields(std::string_view encoded) {
    LinkSetCapability capability;
    capability.maxPendingWindow = 1;
    bool hasLinks = false;
    bool hasCapabilities = false;
    std::istringstream input{std::string(encoded)};
    std::string token;
    while (input >> token) {
        const auto separator = token.find('=');
        if (separator == std::string::npos || separator == 0 ||
            separator + 1 == token.size()) {
            return common::Status::invalidArgument("invalid link control field: " + token);
        }
        const auto key = std::string_view(token).substr(0, separator);
        const auto value = std::string_view(token).substr(separator + 1);
        std::uint64_t parsed = 0;
        if (!parseUnsigned(value, &parsed)) {
            return common::Status::invalidArgument("invalid link control value: " + token);
        }
        if (key == "LINKS") {
            if (hasLinks || parsed == 0 || parsed > LinkSet::kMaxLinks) {
                return common::Status::invalidArgument("invalid link control LINKS");
            }
            capability.maxLinks = static_cast<std::uint32_t>(parsed);
            hasLinks = true;
        } else if (key == "LINK_CAPS") {
            if (hasCapabilities) {
                return common::Status::invalidArgument("duplicate link control LINK_CAPS");
            }
            capability.capabilityBits = parsed;
            hasCapabilities = true;
        } else {
            return common::Status::invalidArgument("unknown link control key: " +
                                                   std::string(key));
        }
    }
    if (!hasLinks || !hasCapabilities) {
        return common::Status::invalidArgument("incomplete link control fields");
    }
    return capability;
}

std::string summarizeLinkNegotiation(const LinkNegotiation& negotiation) {
    return "requested_links=" + std::to_string(negotiation.requestedLinks) +
           " accepted_links=" + std::to_string(negotiation.acceptedLinks) +
           " requested_pending_window=" + std::to_string(negotiation.requestedPendingWindow) +
           " accepted_pending_window=" + std::to_string(negotiation.acceptedPendingWindow) +
           " accepted_ids=" + joinIds(negotiation.acceptedLinkIds) +
           " rejected_ids=" + joinIds(negotiation.rejectedLinkIds) +
           " reason=" + (negotiation.reason.empty() ? "-" : negotiation.reason);
}

LinkSet::LinkSet(std::uint32_t pendingWindow) : pendingWindow_(pendingWindow) {}

common::Status LinkSet::add(const LinkProfile& profile) {
    if (profile.linkId.empty()) {
        return common::Status::invalidArgument("link_id must not be empty");
    }
    if (profile.capacityGbps <= 0.0) {
        return common::Status::invalidArgument("link capacity must be greater than zero");
    }
    if (profile.initialConnections == 0 || profile.maxConnections == 0 ||
        profile.initialConnections > profile.maxConnections) {
        return common::Status::invalidArgument("invalid link connection bounds");
    }
    if (profile.pendingWindow == 0 || profile.pendingWindow > 16) {
        return common::Status::invalidArgument("invalid link pending window");
    }
    if (links_.size() >= kMaxLinks) {
        return common::Status::invalidArgument("link set exceeds safe maximum of 10 links");
    }
    if (entry(profile.linkId) != nullptr) {
        return common::Status::invalidArgument("duplicate link_id: " + profile.linkId);
    }
    const auto endpoint = endpointKey(profile);
    if (!endpoint.empty()) {
        for (const auto& link : links_) {
            if (endpointKey(link.profile) == endpoint) {
                return common::Status::invalidArgument("duplicate link endpoint: " + endpoint);
            }
        }
    }
    LinkEntry item;
    item.profile = profile;
    item.runtime.rttMs = profile.rttMs;
    item.runtime.bdpBytes = profile.bdpBytes;
    links_.push_back(std::move(item));
    return common::Status::ok();
}

std::vector<std::string> LinkSet::linkIds() const {
    std::vector<std::string> ids;
    ids.reserve(links_.size());
    for (const auto& link : links_) {
        ids.push_back(link.profile.linkId);
    }
    return ids;
}

const LinkProfile* LinkSet::profile(const std::string& linkId) const {
    const auto* item = entry(linkId);
    return item == nullptr ? nullptr : &item->profile;
}

const LinkRuntimeState* LinkSet::runtime(const std::string& linkId) const {
    const auto* item = entry(linkId);
    return item == nullptr ? nullptr : &item->runtime;
}

LinkRuntimeState* LinkSet::runtime(const std::string& linkId) {
    auto* item = entry(linkId);
    return item == nullptr ? nullptr : &item->runtime;
}

LinkSetCapability LinkSet::advertisedCapability() const noexcept {
    LinkSetCapability capability;
    capability.maxLinks = static_cast<std::uint32_t>(links_.size());
    capability.maxPendingWindow = pendingWindow_;
    for (const auto& link : links_) {
        capability.capabilityBits |= link.profile.capabilityBits;
    }
    return capability;
}

common::Result<LinkNegotiation> LinkSet::negotiate(const LinkSetCapability& peer) const {
    if (links_.empty()) {
        return common::Status::invalidArgument("cannot negotiate an empty link set");
    }
    if (pendingWindow_ == 0 || pendingWindow_ > 16) {
        return common::Status::invalidArgument("invalid local pending window");
    }
    if (peer.maxLinks == 0 || peer.maxPendingWindow == 0) {
        return common::Status::invalidArgument("peer advertises zero link capacity");
    }
    LinkNegotiation result;
    result.requestedLinks = static_cast<std::uint32_t>(links_.size());
    result.requestedPendingWindow = pendingWindow_;
    result.acceptedPendingWindow = std::min(pendingWindow_, peer.maxPendingWindow);
    if (links_.size() > peer.maxLinks) {
        return common::Status::invalidArgument(
            "requested links exceed peer capability: requested=" +
            std::to_string(links_.size()) + " peer_max=" + std::to_string(peer.maxLinks));
    }
    result.acceptedLinks = result.requestedLinks;
    result.acceptedLinkIds = linkIds();
    result.reason = result.acceptedPendingWindow == pendingWindow_
                        ? "fully_supported"
                        : "pending_window_clamped_to_peer";
    return result;
}

common::Result<std::string> LinkSet::choose(const WorkItem& item) const {
    if (links_.empty()) {
        return common::Status::runtimeError("cannot choose from an empty link set");
    }
    const LinkEntry* best = nullptr;
    long double bestScore = std::numeric_limits<long double>::infinity();
    for (const auto& candidate : links_) {
        if (!candidate.runtime.healthy || candidate.runtime.draining) {
            continue;
        }
        const auto capacity = std::max(candidate.profile.capacityGbps, 0.0000001);
        const auto load = static_cast<long double>(
            candidate.runtime.readyBytes + candidate.runtime.inflightBytes);
        const auto score = load / static_cast<long double>(capacity);
        if (best == nullptr || score < bestScore ||
            (score == bestScore && candidate.profile.linkId < best->profile.linkId)) {
            best = &candidate;
            bestScore = score;
        }
    }
    if (best == nullptr) {
        return common::Status::runtimeError("no healthy non-draining link is available");
    }
    (void)item;
    return best->profile.linkId;
}

common::Status LinkSet::reserve(const std::string& linkId, const std::string& leaseId,
                                std::uint64_t bytes) {
    if (leaseId.empty()) {
        return common::Status::invalidArgument("lease_id must not be empty");
    }
    auto* item = entry(linkId);
    if (item == nullptr) {
        return unknownLink(linkId);
    }
    if (bytes == 0) {
        return common::Status::invalidArgument("reserved bytes must be greater than zero");
    }
    if (leases_.find(leaseId) != leases_.end()) {
        return common::Status::invalidArgument("duplicate lease_id: " + leaseId);
    }
    const auto limit = admissionLimit(*item);
    if (bytes > limit || item->runtime.inflightBytes > limit - bytes) {
        return common::Status::runtimeError("link reserve exceeds admission limit: " + linkId);
    }
    item->runtime.inflightBytes += bytes;
    leases_.emplace(leaseId, Lease{linkId, bytes});
    return common::Status::ok();
}

common::Status LinkSet::release(const std::string& linkId, const std::string& leaseId,
                                std::uint64_t bytes) {
    auto* item = entry(linkId);
    if (item == nullptr) {
        return unknownLink(linkId);
    }
    const auto found = leases_.find(leaseId);
    if (found == leases_.end() || found->second.linkId != linkId) {
        return common::Status::invalidArgument("unknown lease_id: " + leaseId);
    }
    if (bytes == 0 || bytes > found->second.bytes || bytes > item->runtime.inflightBytes) {
        return common::Status::invalidArgument("release exceeds lease accounting: " + leaseId);
    }
    item->runtime.inflightBytes -= bytes;
    found->second.bytes -= bytes;
    if (found->second.bytes == 0) {
        leases_.erase(found);
    }
    return common::Status::ok();
}

common::Status LinkSet::setHealthy(const std::string& linkId, bool healthy) {
    auto* item = entry(linkId);
    if (item == nullptr) {
        return unknownLink(linkId);
    }
    item->runtime.healthy = healthy;
    return common::Status::ok();
}

common::Status LinkSet::setDraining(const std::string& linkId, bool draining) {
    auto* item = entry(linkId);
    if (item == nullptr) {
        return unknownLink(linkId);
    }
    item->runtime.draining = draining;
    return common::Status::ok();
}

common::Status LinkSet::setRuntime(const std::string& linkId, std::uint64_t readyBytes,
                                   std::uint64_t rttMs, std::uint64_t bdpBytes) {
    auto* item = entry(linkId);
    if (item == nullptr) {
        return unknownLink(linkId);
    }
    item->runtime.readyBytes = readyBytes;
    item->runtime.rttMs = rttMs;
    item->runtime.bdpBytes = bdpBytes;
    return common::Status::ok();
}

LinkSet::LinkEntry* LinkSet::entry(const std::string& linkId) {
    auto iter = std::find_if(links_.begin(), links_.end(), [&](const LinkEntry& item) {
        return item.profile.linkId == linkId;
    });
    return iter == links_.end() ? nullptr : &*iter;
}

const LinkSet::LinkEntry* LinkSet::entry(const std::string& linkId) const {
    auto iter = std::find_if(links_.begin(), links_.end(), [&](const LinkEntry& item) {
        return item.profile.linkId == linkId;
    });
    return iter == links_.end() ? nullptr : &*iter;
}

std::string LinkSet::endpointKey(const LinkProfile& profile) {
    if (profile.localBindAddr.empty() && profile.remoteHost.empty() &&
        profile.dataPortRange.empty()) {
        return {};
    }
    return profile.localBindAddr + "|" + profile.remoteHost + "|" + profile.dataPortRange;
}

std::uint64_t LinkSet::admissionLimit(const LinkEntry& item) const noexcept {
    if (item.profile.targetQueueBytes != 0) {
        return item.profile.targetQueueBytes;
    }
    constexpr std::uint64_t kDefaultAdmissionBytes = 64ULL * 1024ULL * 1024ULL;
    return kDefaultAdmissionBytes;
}

}  // namespace cpnetflux::core::scheduler
