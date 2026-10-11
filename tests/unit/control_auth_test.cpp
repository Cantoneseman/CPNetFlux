#include "cpnetflux/protocol/control/control_auth.h"

#include <gtest/gtest.h>

namespace {

using cpnetflux::protocol::control::AccessPolicy;
using cpnetflux::protocol::control::accessPolicyAllowsRead;
using cpnetflux::protocol::control::accessPolicyAllowsWrite;
using cpnetflux::protocol::control::constantTimeEquals;
using cpnetflux::protocol::control::parseAccessPolicy;

TEST(ControlAuthTest, ParsesAccessPolicies) {
    EXPECT_EQ(parseAccessPolicy("read-write").value(), AccessPolicy::ReadWrite);
    EXPECT_EQ(parseAccessPolicy("read-only").value(), AccessPolicy::ReadOnly);
    EXPECT_EQ(parseAccessPolicy("write-only").value(), AccessPolicy::WriteOnly);
    EXPECT_FALSE(parseAccessPolicy("none").isOk());
    EXPECT_TRUE(accessPolicyAllowsRead(AccessPolicy::ReadWrite));
    EXPECT_TRUE(accessPolicyAllowsWrite(AccessPolicy::ReadWrite));
    EXPECT_TRUE(accessPolicyAllowsRead(AccessPolicy::ReadOnly));
    EXPECT_FALSE(accessPolicyAllowsWrite(AccessPolicy::ReadOnly));
    EXPECT_FALSE(accessPolicyAllowsRead(AccessPolicy::WriteOnly));
    EXPECT_TRUE(accessPolicyAllowsWrite(AccessPolicy::WriteOnly));
}

TEST(ControlAuthTest, ComparesCredentialsWithoutAcceptingDifferentLengths) {
    EXPECT_TRUE(constantTimeEquals("shared-token", "shared-token"));
    EXPECT_FALSE(constantTimeEquals("shared-token", "shared-tokeN"));
    EXPECT_FALSE(constantTimeEquals("shared-token", "shared-token-extra"));
    EXPECT_FALSE(constantTimeEquals("", "x"));
    EXPECT_FALSE(constantTimeEquals(std::string(4097, 'x'), std::string(4097, 'x')));
}

}  // namespace
