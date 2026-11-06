#include "cpnetflux/core/io/detail/rdma_buffer_pool.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <sys/mman.h>
#include <type_traits>

using cpnetflux::core::io::detail::RdmaBufferPool;

TEST(RdmaBufferPoolTest, AlignedZeroedStorageKeepsExactLogicalBounds) {
    RdmaBufferPool pool;
    constexpr std::size_t bytes = (4U << 20) + 17;
    ASSERT_TRUE(pool.allocate(bytes).isOk());
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pool.data()) % (2U << 20), 0U);
    EXPECT_EQ(pool.size(), bytes);
    EXPECT_TRUE(std::all_of(pool.data(), pool.data() + bytes,
                            [](auto byte) { return byte == 0; }));
    pool.data()[0] = 0x59;
    pool.data()[bytes - 1] = 0xa7;
    EXPECT_EQ(pool.data()[0], 0x59);
    EXPECT_EQ(pool.data()[bytes - 1], 0xa7);
    auto* original = pool.data();
    EXPECT_FALSE(pool.allocate(4096).isOk());
    EXPECT_EQ(pool.data(), original);
    EXPECT_EQ(pool.data()[bytes - 1], 0xa7);
}

TEST(RdmaBufferPoolTest, OrdinaryPagePathIsWritableAndZeroed) {
    RdmaBufferPool pool;
    ASSERT_TRUE(pool.allocate(8193, false).isOk());
    EXPECT_FALSE(pool.hugePageHintAccepted());
    EXPECT_TRUE(std::all_of(pool.data(), pool.data() + pool.size(),
                            [](auto byte) { return byte == 0; }));
    std::fill(pool.data(), pool.data() + pool.size(), 0xac);
    EXPECT_EQ(pool.data()[8192], 0xac);
}

TEST(RdmaBufferPoolTest, InvalidAndOverflowRequestsDoNotAcquireStorage) {
    RdmaBufferPool pool;
    EXPECT_FALSE(pool.allocate(0).isOk());
    EXPECT_FALSE(pool.allocate(std::numeric_limits<std::size_t>::max()).isOk());
    EXPECT_EQ(pool.data(), nullptr);
    EXPECT_EQ(pool.size(), 0U);
    EXPECT_TRUE(pool.allocate(17, false).isOk());
}

TEST(RdmaBufferPoolTest, UniqueOwnerUnmapsStorageAtDestruction) {
    static_assert(!std::is_copy_constructible_v<RdmaBufferPool>);
    static_assert(!std::is_move_constructible_v<RdmaBufferPool>);
    void* address = nullptr;
    {
        RdmaBufferPool pool;
        ASSERT_TRUE(pool.allocate(4096, false).isOk());
        address = pool.data();
        unsigned char residency = 0;
        ASSERT_EQ(::mincore(address, 4096, &residency), 0);
        EXPECT_EQ(residency & 1, 1);
    }
    unsigned char residency = 0;
    errno = 0;
    EXPECT_EQ(::mincore(address, 4096, &residency), -1);
    EXPECT_EQ(errno, ENOMEM);
}
