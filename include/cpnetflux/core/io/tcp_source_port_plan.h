#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include "cpnetflux/common/status.h"
namespace cpnetflux::core::io {
struct TcpSourcePortEntry {
    std::uint32_t targetQueue = 0;
    std::vector<std::uint16_t> candidates;
};
// Explicit immutable deployment plan; snapshotSha is provenance, not live NIC verification.
class TcpSourcePortPlan {
 public:
    static common::Result<TcpSourcePortPlan> parse(std::string_view text,
        std::string_view local, std::string_view peer, std::uint32_t channels);
    static common::Result<TcpSourcePortPlan> load(const std::string& path,
        std::string_view local, std::string_view peer, std::uint32_t channels);
    const TcpSourcePortEntry* find(std::uint32_t channel, std::uint16_t peerPort) const;
    const std::string& snapshotSha() const noexcept { return snapshotSha_; }
 private:
    std::string snapshotSha_;
    std::map<std::pair<std::uint32_t,std::uint16_t>,TcpSourcePortEntry> entries_;
};
} // namespace cpnetflux::core::io
