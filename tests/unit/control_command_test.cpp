#include "cpnetflux/protocol/control/control_command.h"

#include <gtest/gtest.h>

namespace {

using cpnetflux::protocol::control::ControlCommandType;
using cpnetflux::protocol::control::parseControlCommand;

TEST(ControlCommandTest, ParsesCaseWhitespaceAndArguments) {
    auto parsed = parseControlCommand("  uSeR   cpnetflux  \r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::User);
    EXPECT_EQ(parsed.value().verb, "USER");
    EXPECT_EQ(parsed.value().argument, "cpnetflux");

    parsed = parseControlCommand("STOR nested/file.bin\r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Stor);
    EXPECT_EQ(parsed.value().argument, "nested/file.bin");

    parsed = parseControlCommand("  rEtR   nested/file.bin  \r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Retr);
    EXPECT_EQ(parsed.value().argument, "nested/file.bin");

    parsed = parseControlCommand("size nested/file.bin\r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Size);
    EXPECT_EQ(parsed.value().argument, "nested/file.bin");

    parsed = parseControlCommand("mDtM nested/file.bin\r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Mdtm);

    parsed = parseControlCommand(" CWD   nested  \r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Cwd);
    EXPECT_EQ(parsed.value().argument, "nested");

    parsed = parseControlCommand("CDUP\r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Cdup);

    parsed = parseControlCommand("LIST   nested  \r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::List);
    EXPECT_EQ(parsed.value().argument, "nested");

    parsed = parseControlCommand("NLST\r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Nlst);
    EXPECT_TRUE(parsed.value().argument.empty());
}

TEST(ControlCommandTest, ParsesEmptyAndUnknown) {
    auto parsed = parseControlCommand(" \r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Empty);

    parsed = parseControlCommand("SITE HELP\r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Unknown);
    EXPECT_EQ(parsed.value().verb, "SITE");
}

TEST(ControlCommandTest, ParsesParallelismOptions) {
    auto parsed = parseControlCommand("OPTS PARALLELISM=8\r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Opts);
    EXPECT_EQ(parsed.value().parallelism, 8U);

    parsed = parseControlCommand("OPTS RETR Parallelism=4\r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().parallelism, 4U);

    EXPECT_FALSE(parseControlCommand("OPTS PARALLELISM=0").isOk());
    EXPECT_FALSE(parseControlCommand("OPTS PARALLELISM=65").isOk());
}

TEST(ControlCommandTest, ParsesRestMarker) {
    auto parsed = parseControlCommand("REST GFID:phase3a-token_01\r\n");
    ASSERT_TRUE(parsed.isOk()) << parsed.status().message();
    EXPECT_EQ(parsed.value().type, ControlCommandType::Rest);
    EXPECT_EQ(parsed.value().transferId, "phase3a-token_01");

    EXPECT_FALSE(parseControlCommand("REST 1024").isOk());
    EXPECT_FALSE(parseControlCommand("REST GFID:bad/token").isOk());
}

}  // namespace
