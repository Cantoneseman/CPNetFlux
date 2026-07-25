#include "gridflux/core/metrics/event_log.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path tempPath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

}  // namespace

TEST(EventLogTest, WritesEscapedJsonlAndCreatesParentDirectory) {
    const auto root = tempPath("gridflux-event-log-test");
    std::filesystem::remove_all(root);
    const auto path = root / "nested" / "events.jsonl";
    auto logger = gridflux::core::metrics::EventLogger::open(path.string());
    ASSERT_TRUE(logger.isOk()) << logger.status().message();
    auto status = logger.value().write(gridflux::core::metrics::EventRecord{
        "component",
        "event",
        "abc",
        "upload",
        "path with \"quote\"",
        "fail",
        gridflux::core::metrics::ErrorCode::ProtocolError,
        "line\nmessage",
        1.25,
        42});
    ASSERT_TRUE(status.isOk()) << status.message();
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();
    EXPECT_NE(text.find("\"error_code\":\"protocol_error\""), std::string::npos);
    EXPECT_NE(text.find("path with \\\"quote\\\""), std::string::npos);
    EXPECT_NE(text.find("line\\nmessage"), std::string::npos);
    std::filesystem::remove_all(root);
}

TEST(EventLogTest, RejectsDirectoryAsLogPath) {
    const auto root = tempPath("gridflux-event-log-directory");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto status = gridflux::core::metrics::validateEventLogPath(root.string());
    EXPECT_FALSE(status.isOk());
    std::filesystem::remove_all(root);
}

TEST(EventLogTest, ClassifiesCommonErrors) {
    using gridflux::core::metrics::ErrorCode;
    using gridflux::core::metrics::classifyMessage;
    EXPECT_EQ(classifyMessage("Please login with USER and PASS"), ErrorCode::AuthRequired);
    EXPECT_EQ(classifyMessage("Login incorrect"), ErrorCode::AuthFailed);
    EXPECT_EQ(classifyMessage("TLS required before control command"), ErrorCode::TlsRequired);
    EXPECT_EQ(classifyMessage("TLS handshake failed"), ErrorCode::TlsFailed);
    EXPECT_EQ(classifyMessage("data TLS required for framed socket"),
              ErrorCode::DataTlsRequired);
    EXPECT_EQ(classifyMessage("data TLS handshake failed"), ErrorCode::DataTlsFailed);
    EXPECT_EQ(classifyMessage("path rejected outside root"), ErrorCode::PathRejected);
    EXPECT_EQ(classifyMessage("manifest corrupt body checksum"), ErrorCode::ManifestCorrupt);
    EXPECT_EQ(classifyMessage("checksum mismatch"), ErrorCode::ChecksumMismatch);
    EXPECT_EQ(classifyMessage("source file changed"), ErrorCode::ChangedFile);
    EXPECT_EQ(classifyMessage("remote artifact sync failed"), ErrorCode::RemoteSyncFailed);
}

TEST(EventLogTest, ThreadSafeMultiWriteSmoke) {
    const auto root = tempPath("gridflux-event-log-threaded");
    std::filesystem::remove_all(root);
    const auto path = root / "events.jsonl";
    auto logger = gridflux::core::metrics::EventLogger::open(path.string());
    ASSERT_TRUE(logger.isOk()) << logger.status().message();
    std::vector<std::future<void>> futures;
    for (int thread = 0; thread < 4; ++thread) {
        futures.push_back(std::async(std::launch::async, [&logger, thread]() {
            for (int index = 0; index < 25; ++index) {
                const auto status = logger.value().write(gridflux::core::metrics::EventRecord{
                    "component",
                    "event",
                    std::to_string(thread),
                    "download",
                    "path",
                    "pass",
                    gridflux::core::metrics::ErrorCode::Ok,
                    "",
                    0.0,
                    1});
                ASSERT_TRUE(status.isOk()) << status.message();
            }
        }));
    }
    for (auto& future : futures) {
        future.get();
    }
    std::ifstream input(path);
    std::size_t lines = 0;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            ++lines;
        }
    }
    EXPECT_EQ(lines, 100U);
    std::filesystem::remove_all(root);
}
