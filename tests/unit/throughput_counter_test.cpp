#include "gridflux/common/throughput_counter.h"

#include <gtest/gtest.h>

TEST(ThroughputCounterTest, ComputesRatesWithProvidedClock) {
    using Counter = gridflux::common::ThroughputCounter;
    const Counter::TimePoint start{};
    const Counter::TimePoint end = start + std::chrono::seconds(2);

    Counter counter;
    counter.start(start);
    counter.addBytes(1'000'000'000);
    counter.stop(end);

    EXPECT_EQ(counter.bytes(), 1'000'000'000U);
    EXPECT_DOUBLE_EQ(counter.elapsedSeconds(end), 2.0);
    EXPECT_DOUBLE_EQ(counter.bytesPerSecond(end), 500'000'000.0);
    EXPECT_DOUBLE_EQ(counter.gigabitsPerSecond(end), 4.0);
}

TEST(ThroughputCounterTest, HandlesZeroElapsedTime) {
    using Counter = gridflux::common::ThroughputCounter;
    const Counter::TimePoint now{};

    Counter counter;
    counter.start(now);
    counter.addBytes(128);

    EXPECT_DOUBLE_EQ(counter.bytesPerSecond(now), 0.0);
    EXPECT_DOUBLE_EQ(counter.gigabitsPerSecond(now), 0.0);
}
