#include "gridflux/config/sink_options.h"

#include <gtest/gtest.h>

namespace {

gridflux::common::Result<gridflux::config::SinkOptions> parse(
    std::initializer_list<const char*> args, gridflux::config::SinkRole role) {
    return gridflux::config::parseSinkOptions(static_cast<int>(args.size()), args.begin(), role);
}

}  // namespace

TEST(SinkOptionsTest, AppliesServerDefaults) {
    const auto result =
        parse({"gridflux-server", "--bytes", "1024"}, gridflux::config::SinkRole::Server);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value().host, "0.0.0.0");
    EXPECT_EQ(result.value().port, 9000);
    EXPECT_EQ(result.value().connections, 1U);
    EXPECT_EQ(result.value().bytes, 1024U);
    EXPECT_EQ(result.value().bufferSize, 65536U);
}

TEST(SinkOptionsTest, AppliesClientDefaults) {
    const auto result =
        parse({"gridflux-client", "--bytes", "1024"}, gridflux::config::SinkRole::Client);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value().host, "127.0.0.1");
}

TEST(SinkOptionsTest, ParsesAllOptions) {
    const auto result = parse({"gridflux-client", "--host", "<redacted>", "--port", "19000",
                               "--connections", "8", "--bytes", "4096", "--buffer-size", "131072"},
                              gridflux::config::SinkRole::Client);

    ASSERT_TRUE(result.isOk()) << result.status().message();
    EXPECT_EQ(result.value().host, "<redacted>");
    EXPECT_EQ(result.value().port, 19000);
    EXPECT_EQ(result.value().connections, 8U);
    EXPECT_EQ(result.value().bytes, 4096U);
    EXPECT_EQ(result.value().bufferSize, 131072U);
}

TEST(SinkOptionsTest, RejectsMissingBytes) {
    const auto result = parse({"gridflux-client"}, gridflux::config::SinkRole::Client);

    EXPECT_FALSE(result.isOk());
}

TEST(SinkOptionsTest, RejectsInvalidPort) {
    const auto result = parse({"gridflux-client", "--bytes", "1", "--port", "70000"},
                              gridflux::config::SinkRole::Client);

    EXPECT_FALSE(result.isOk());
}

TEST(SinkOptionsTest, RejectsInvalidConnections) {
    const auto result = parse({"gridflux-client", "--bytes", "1", "--connections", "65"},
                              gridflux::config::SinkRole::Client);

    EXPECT_FALSE(result.isOk());
}

TEST(SinkOptionsTest, RejectsInvalidBufferSize) {
    const auto result = parse({"gridflux-client", "--bytes", "1", "--buffer-size", "16777217"},
                              gridflux::config::SinkRole::Client);

    EXPECT_FALSE(result.isOk());
}

TEST(SinkOptionsTest, RejectsUnknownOption) {
    const auto result = parse({"gridflux-client", "--bytes", "1", "--bogus", "1"},
                              gridflux::config::SinkRole::Client);

    EXPECT_FALSE(result.isOk());
}
