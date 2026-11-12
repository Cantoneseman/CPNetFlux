#include <gtest/gtest.h>
#include "cpnetflux/core/io/tcp_source_port_plan.h"
#include <string>
using cpnetflux::core::io::TcpSourcePortPlan;
namespace {
std::string header="CPNETFLUX_TCP_SOURCE_PORT_PLAN_V1 127.0.0.1 127.0.0.2 2 "+std::string(64,'a')+"\n";
std::string valid=header+"0 61000 9 32000 32001\n1 61000 27 32002 32003\n";
}
TEST(TcpSourcePortPlanTest, ImmutableLookupPreservesCandidateOrderAndProvenance) {
 auto p=TcpSourcePortPlan::parse(valid,"127.0.0.1","127.0.0.2",2);ASSERT_TRUE(p.isOk())<<p.status().message();
 ASSERT_NE(p.value().find(0,61000),nullptr);EXPECT_EQ(p.value().find(0,61000)->targetQueue,9U);
 EXPECT_EQ(p.value().find(0,61000)->candidates,(std::vector<std::uint16_t>{32000,32001}));
 EXPECT_EQ(p.value().snapshotSha(),std::string(64,'a'));EXPECT_EQ(p.value().find(0,61001),nullptr);
 EXPECT_FALSE(TcpSourcePortPlan::parse(valid,"127.0.0.3","127.0.0.2",2).isOk());
 EXPECT_FALSE(TcpSourcePortPlan::parse(valid,"127.0.0.1","127.0.0.2",3).isOk());
}
TEST(TcpSourcePortPlanTest, RejectsMalformedRowsAndCollidingChannels) {
 for (const auto& rows:std::vector<std::string>{"", "0 61000 9 32000\n", "0 61000 9\n1 61000 27 32002\n",
 "0 61000 9 32000 32000\n1 61000 27 32002\n", "0 61000 9 32000\n1 61000 9 32002\n",
 "0 61000 9 32000\n0 61000 9 32001\n1 61000 27 32002\n", "0 61000 9 32000\n0 61001 8 32001\n1 61000 27 32002\n",
 "0 61000 9 -1\n1 61000 27 32002\n", "0 61000 9 65536\n1 61000 27 32002\n", "2 61000 9 32000\n1 61000 27 32002\n"})
  EXPECT_FALSE(TcpSourcePortPlan::parse(header+rows,"127.0.0.1","127.0.0.2",2).isOk())<<rows;
 std::string huge=valid+std::string(2*1024*1024,' ');EXPECT_FALSE(TcpSourcePortPlan::parse(huge,"127.0.0.1","127.0.0.2",2).isOk());
 std::string many=header+"0 61000 9";for(int i=0;i<129;++i)many+=" "+std::to_string(32000+i);
 many+="\n1 61000 27 33000\n";EXPECT_FALSE(TcpSourcePortPlan::parse(many,"127.0.0.1","127.0.0.2",2).isOk());
}
