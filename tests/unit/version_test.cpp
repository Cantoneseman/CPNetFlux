#include "cpnetflux/version.h"

#include <gtest/gtest.h>

TEST(VersionTest, ExposesProjectIdentity) {
    EXPECT_EQ(cpnetflux::projectName(), "CPNetFlux");
    EXPECT_EQ(cpnetflux::projectVersion(), "0.1.0");
}

TEST(VersionTest, UsesCxx20Toolchain) {
    EXPECT_GE(__cplusplus, cpnetflux::kRequiredCxxStandard);
}
