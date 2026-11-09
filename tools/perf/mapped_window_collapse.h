#pragma once
// Standalone diagnostic only. No transfer leases, checkpoints or certified progress.
#include <cerrno>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/mman.h>

namespace cpnetflux::diagnostic {
constexpr std::uintptr_t pmdBytes = 2ULL * 1024 * 1024;
constexpr int collapseAdvice = 25; // Linux MADV_COLLAPSE (headers may predate the kernel).
struct CollapseReport { std::uint64_t eligibleBytes = 0, edgeBytes = 0, seeds = 0; };
using Advice = std::function<int(void*, std::size_t, int)>;
using Cancel = std::function<bool()>;
// Caller exclusively owns the borrowed span until completion/exception. Read/write
// the same byte, never zero an existing byte. Failure may leave partially prepared
// pages; it does not write/verify/certify any payload and the owner must close it.
inline CollapseReport prepareCollapse(std::span<std::uint8_t> bytes,
    Advice advice = [](void* p, std::size_t n, int kind) { return ::madvise(p,n,kind); },
    Cancel cancelled = [] { return false; }) {
    const auto start = reinterpret_cast<std::uintptr_t>(bytes.data());
    if (!start || bytes.size() < pmdBytes || bytes.size() > 64ULL*1024*1024 ||
        start > std::numeric_limits<std::uintptr_t>::max()-bytes.size())
        throw std::invalid_argument("collapse requires bounded owned span");
    const auto end = start + bytes.size();
    const auto begin = (start + pmdBytes-1) & ~(pmdBytes-1);
    const auto stop = end & ~(pmdBytes-1);
    if (begin >= stop) throw std::invalid_argument("no complete PMD in owned span");
    CollapseReport report{stop-begin, bytes.size()-(stop-begin), 0};
    auto checkCancel = [&] { if (cancelled()) throw std::runtime_error("collapse cancelled"); };
    checkCancel();
    auto* address = reinterpret_cast<void*>(begin);
    if (advice(address, report.eligibleBytes, MADV_HUGEPAGE) != 0)
        throw std::runtime_error("MADV_HUGEPAGE errno="+std::to_string(errno));
    for (auto current=begin; current<stop; current+=pmdBytes) {
        checkCancel();
        auto* byte = reinterpret_cast<volatile std::uint8_t*>(current);
        const auto old = *byte;
        *byte = old; // Preserve even nonzero/resumed contents; exclusive ownership required.
        ++report.seeds;
    }
    checkCancel();
    if (advice(address, report.eligibleBytes, collapseAdvice) != 0)
        throw std::runtime_error("MADV_COLLAPSE errno="+std::to_string(errno));
    return report;
}
int runMappedLifecycleBench(const std::string& directory, std::uint64_t bytes,
    unsigned workers, std::uint64_t frameBytes, const std::string& mode,
    const std::string& initial, std::uint64_t cancelAfter);
}
