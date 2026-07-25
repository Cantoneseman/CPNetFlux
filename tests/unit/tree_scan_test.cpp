#include "gridflux/core/tree/tree_scan.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(TreeScanTest, ScansRegularFilesInStableOrder) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gridflux-tree-scan-stable";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "b");
    std::filesystem::create_directories(root / "a");
    std::ofstream(root / "b" / "two.bin").put('2');
    std::ofstream(root / "a" / "one.bin").put('1');
    std::ofstream(root / "empty.bin");

    auto scanned = gridflux::core::tree::scanLocalTree(root.string());
    ASSERT_TRUE(scanned.isOk()) << scanned.status().message();
    ASSERT_EQ(scanned.value().size(), 3U);
    EXPECT_EQ(scanned.value()[0].relativePath, "a/one.bin");
    EXPECT_EQ(scanned.value()[1].relativePath, "b/two.bin");
    EXPECT_EQ(scanned.value()[2].relativePath, "empty.bin");

    std::filesystem::remove_all(root);
}

TEST(TreeScanTest, ValidatesTreeRelativePath) {
    EXPECT_TRUE(gridflux::core::tree::validateTreeRelativePath("nested/file.bin").isOk());
    EXPECT_FALSE(gridflux::core::tree::validateTreeRelativePath("").isOk());
    EXPECT_FALSE(gridflux::core::tree::validateTreeRelativePath("/abs").isOk());
    EXPECT_FALSE(gridflux::core::tree::validateTreeRelativePath("../escape").isOk());
    EXPECT_FALSE(gridflux::core::tree::validateTreeRelativePath("C:/drive").isOk());
    EXPECT_FALSE(gridflux::core::tree::validateTreeRelativePath("bad\\path").isOk());
}

TEST(TreeScanTest, RejectsSymlinkByDefault) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gridflux-tree-scan-symlink";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / "target.bin").put('x');
    std::error_code error;
    std::filesystem::create_symlink(root / "target.bin", root / "link.bin", error);
    if (error) {
        GTEST_SKIP() << "symlink creation unavailable";
    }
    auto scanned = gridflux::core::tree::scanLocalTree(root.string());
    EXPECT_FALSE(scanned.isOk());
    std::filesystem::remove_all(root);
}
