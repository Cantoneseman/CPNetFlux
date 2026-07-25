#include "gridflux/version.h"

#include <gtest/gtest.h>

TEST(VersionTest, ExposesProjectIdentity) {
    EXPECT_EQ(gridflux::projectName(), "GridFlux");
    EXPECT_EQ(gridflux::projectVersion(), "0.1.0");
}

TEST(VersionTest, UsesCxx20Toolchain) {
    EXPECT_GE(__cplusplus, gridflux::kRequiredCxxStandard);
}
