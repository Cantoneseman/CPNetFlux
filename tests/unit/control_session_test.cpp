#include <gtest/gtest.h>

#include <string>

#include "cpnetflux/protocol/control/control_command.h"
#include "cpnetflux/protocol/control/control_auth.h"

namespace {

using cpnetflux::protocol::control::ControlAction;
using cpnetflux::protocol::control::ControlSession;
using cpnetflux::protocol::control::parseControlCommand;
using cpnetflux::protocol::control::replyCode;

cpnetflux::protocol::control::ControlResponse handle(ControlSession* session, const char* line) {
    auto command = parseControlCommand(line);
    EXPECT_TRUE(command.isOk()) << command.status().message();
    return session->handleCommand(command.value());
}

TEST(ControlSessionTest, RequiresLoginForTransferCommands) {
    ControlSession session("cpnetflux", "secret", 4);
    EXPECT_EQ(replyCode(handle(&session, "STOR file.bin")), 530);
    EXPECT_EQ(replyCode(handle(&session, "RETR file.bin")), 530);
    EXPECT_EQ(replyCode(handle(&session, "EPSV")), 530);
    EXPECT_EQ(replyCode(handle(&session, "OPTS PARALLELISM=2")), 530);
    EXPECT_EQ(replyCode(handle(&session, "SIZE file.bin")), 530);
    EXPECT_EQ(replyCode(handle(&session, "MDTM file.bin")), 530);
    EXPECT_EQ(replyCode(handle(&session, "CWD subdir")), 530);
    EXPECT_EQ(replyCode(handle(&session, "CDUP")), 530);
    EXPECT_EQ(replyCode(handle(&session, "LIST")), 530);
    EXPECT_EQ(replyCode(handle(&session, "NLST")), 530);
}

TEST(ControlSessionTest, LogsInAndSetsType) {
    ControlSession session("cpnetflux", "secret", 4);
    EXPECT_EQ(replyCode(handle(&session, "USER cpnetflux")), 331);
    EXPECT_EQ(replyCode(handle(&session, "PASS secret")), 230);
    EXPECT_TRUE(session.authenticated());
    EXPECT_EQ(replyCode(handle(&session, "TYPE I")), 200);
    EXPECT_TRUE(session.binaryType());
}

TEST(ControlSessionTest, TokenAuthAllowsPublicCommandsAndProtectsTransfers) {
    cpnetflux::protocol::control::ControlAuthConfig auth;
    auth.mode = cpnetflux::protocol::control::AuthMode::Token;
    auth.token = "alpha-token";
    ControlSession session(auth, 4);

    EXPECT_EQ(replyCode(handle(&session, "FEAT")), 211);
    EXPECT_EQ(replyCode(handle(&session, "SYST")), 215);
    EXPECT_EQ(replyCode(handle(&session, "NOOP")), 200);
    EXPECT_EQ(replyCode(handle(&session, "SIZE file.bin")), 530);

    EXPECT_EQ(replyCode(handle(&session, "USER cpnetflux")), 530);
    EXPECT_FALSE(session.authenticated());
    EXPECT_EQ(replyCode(handle(&session, "USER token")), 331);
    EXPECT_EQ(replyCode(handle(&session, "PASS wrong-token")), 530);
    EXPECT_FALSE(session.authenticated());
    EXPECT_EQ(replyCode(handle(&session, "USER token")), 331);
    EXPECT_EQ(replyCode(handle(&session, "PASS alpha-token")), 230);
    EXPECT_TRUE(session.authenticated());
    EXPECT_EQ(replyCode(handle(&session, "TYPE I")), 200);
}

TEST(ControlSessionTest, PassiveAndStorFlow) {
    ControlSession session("cpnetflux", "secret", 4);
    EXPECT_EQ(replyCode(handle(&session, "USER cpnetflux")), 331);
    EXPECT_EQ(replyCode(handle(&session, "PASS secret")), 230);
    EXPECT_EQ(replyCode(handle(&session, "TYPE I")), 200);

    auto response = handle(&session, "EPSV");
    EXPECT_EQ(response.action, ControlAction::OpenPassiveEpsv);
    session.markPassiveReady();
    EXPECT_TRUE(session.passiveReady());

    response = handle(&session, "STOR file.bin");
    EXPECT_EQ(response.action, ControlAction::StartStor);
    EXPECT_EQ(response.path, "file.bin");
    EXPECT_FALSE(response.resume);
    EXPECT_FALSE(session.passiveReady());
}

TEST(ControlSessionTest, PassiveAndRetrFlow) {
    ControlSession session("cpnetflux", "secret", 4);
    EXPECT_EQ(replyCode(handle(&session, "USER cpnetflux")), 331);
    EXPECT_EQ(replyCode(handle(&session, "PASS secret")), 230);

    EXPECT_EQ(replyCode(handle(&session, "RETR file.bin")), 550);
    EXPECT_EQ(replyCode(handle(&session, "TYPE I")), 200);
    EXPECT_EQ(replyCode(handle(&session, "RETR file.bin")), 550);

    auto response = handle(&session, "EPSV");
    EXPECT_EQ(response.action, ControlAction::OpenPassiveEpsv);
    session.markPassiveReady();

    response = handle(&session, "RETR file.bin");
    EXPECT_EQ(response.action, ControlAction::StartRetr);
    EXPECT_EQ(response.path, "file.bin");
    EXPECT_FALSE(response.resume);
    EXPECT_FALSE(session.passiveReady());
}

TEST(ControlSessionTest, MetadataActionsAndWorkingDirectory) {
    ControlSession session("cpnetflux", "secret", 4);
    EXPECT_EQ(replyCode(handle(&session, "USER cpnetflux")), 331);
    EXPECT_EQ(replyCode(handle(&session, "PASS secret")), 230);

    auto response = handle(&session, "PWD");
    EXPECT_EQ(replyCode(response), 257);
    EXPECT_NE(response.lines.front().find("\"/\""), std::string::npos);

    response = handle(&session, "SIZE file.bin");
    EXPECT_EQ(response.action, ControlAction::QuerySize);
    EXPECT_EQ(response.path, "file.bin");

    response = handle(&session, "MDTM file.bin");
    EXPECT_EQ(response.action, ControlAction::QueryMdtm);
    EXPECT_EQ(response.path, "file.bin");

    response = handle(&session, "CWD subdir");
    EXPECT_EQ(response.action, ControlAction::ChangeDirectory);
    EXPECT_EQ(response.path, "subdir");
    session.setWorkingDirectory("/subdir");
    EXPECT_EQ(session.workingDirectory(), "/subdir");

    response = handle(&session, "CDUP");
    EXPECT_EQ(response.action, ControlAction::ChangeDirectory);
    EXPECT_EQ(response.path, "..");
}

TEST(ControlSessionTest, ListAndNlstRequirePassive) {
    ControlSession session("cpnetflux", "secret", 4);
    EXPECT_EQ(replyCode(handle(&session, "USER cpnetflux")), 331);
    EXPECT_EQ(replyCode(handle(&session, "PASS secret")), 230);
    EXPECT_EQ(replyCode(handle(&session, "LIST")), 550);
    EXPECT_EQ(replyCode(handle(&session, "NLST")), 550);

    auto response = handle(&session, "EPSV");
    EXPECT_EQ(response.action, ControlAction::OpenPassiveEpsv);
    session.markPassiveReady();
    response = handle(&session, "LIST subdir");
    EXPECT_EQ(response.action, ControlAction::StartList);
    EXPECT_EQ(response.path, "subdir");
    EXPECT_FALSE(session.passiveReady());

    response = handle(&session, "PASV");
    EXPECT_EQ(response.action, ControlAction::OpenPassivePasv);
    session.markPassiveReady();
    response = handle(&session, "NLST");
    EXPECT_EQ(response.action, ControlAction::StartNlst);
    EXPECT_TRUE(response.path.empty());
    EXPECT_FALSE(session.passiveReady());
}

TEST(ControlSessionTest, OptionsAndRestAffectNextStor) {
    ControlSession session("cpnetflux", "secret", 1);
    EXPECT_EQ(replyCode(handle(&session, "USER cpnetflux")), 331);
    EXPECT_EQ(replyCode(handle(&session, "PASS secret")), 230);
    EXPECT_EQ(replyCode(handle(&session, "TYPE I")), 200);
    EXPECT_EQ(replyCode(handle(&session, "OPTS RETR Parallelism=8")), 200);
    EXPECT_EQ(session.connections(), 8U);
    EXPECT_EQ(replyCode(handle(&session, "REST GFID:resume-token")), 350);

    auto response = handle(&session, "PASV");
    EXPECT_EQ(response.action, ControlAction::OpenPassivePasv);
    session.markPassiveReady();

    response = handle(&session, "STOR file.bin");
    EXPECT_EQ(response.action, ControlAction::StartStor);
    EXPECT_TRUE(response.resume);
    EXPECT_EQ(response.transferId, "resume-token");
    EXPECT_EQ(response.connections, 8U);

    EXPECT_EQ(replyCode(handle(&session, "REST GFID:download-token")), 350);
    response = handle(&session, "PASV");
    EXPECT_EQ(response.action, ControlAction::OpenPassivePasv);
    session.markPassiveReady();
    response = handle(&session, "RETR file.bin");
    EXPECT_EQ(response.action, ControlAction::StartRetr);
    EXPECT_TRUE(response.resume);
    EXPECT_EQ(response.transferId, "download-token");
}

TEST(ControlSessionTest, UnsupportedReturns502) {
    ControlSession session("cpnetflux", "secret", 1);
    EXPECT_EQ(replyCode(handle(&session, "SITE HELP")), 502);
}

}  // namespace
