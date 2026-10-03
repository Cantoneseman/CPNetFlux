#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cpnetflux/common/status.h"
#include "cpnetflux/protocol/control/control_auth.h"

namespace cpnetflux::protocol::control {

enum class ControlCommandType {
    Empty,
    Unknown,
    User,
    Pass,
    Type,
    Syst,
    Feat,
    Pwd,
    Noop,
    Quit,
    Epsv,
    Pasv,
    Opts,
    Rest,
    Stor,
    Retr,
    Size,
    Mdtm,
    Cwd,
    Cdup,
    List,
    Nlst,
};

struct ControlCommand {
    ControlCommandType type = ControlCommandType::Unknown;
    std::string verb;
    std::string argument;
    std::uint32_t parallelism = 0;
    bool hasPipelineOption = false;
    bool pipelineEnabled = false;
    std::string transferId;
};

enum class ControlAction {
    None,
    OpenPassiveEpsv,
    OpenPassivePasv,
    StartStor,
    StartRetr,
    QuerySize,
    QueryMdtm,
    ChangeDirectory,
    StartList,
    StartNlst,
    Quit,
};

struct ControlResponse {
    std::vector<std::string> lines;
    ControlAction action = ControlAction::None;
    std::string path;
    std::string transferId;
    std::uint32_t connections = 1;
    bool resume = false;
};

class ControlSession {
   public:
    ControlSession(std::string user, std::string password, std::uint32_t defaultConnections);
    ControlSession(ControlAuthConfig auth, std::uint32_t defaultConnections);

    [[nodiscard]] ControlResponse handleCommand(const ControlCommand& command);
    void markPassiveReady() noexcept;
    void clearPassiveReady() noexcept;

    [[nodiscard]] bool authenticated() const noexcept;
    [[nodiscard]] bool binaryType() const noexcept;
    [[nodiscard]] bool passiveReady() const noexcept;
    [[nodiscard]] bool pipelineOptIn() const noexcept;
    [[nodiscard]] std::uint32_t connections() const noexcept;
    [[nodiscard]] const std::string& workingDirectory() const noexcept;

    void setWorkingDirectory(std::string directory);

   private:
    std::string expectedUser_;
    std::string expectedPassword_;
    ControlAuthConfig auth_;
    std::uint32_t connections_ = 1;
    bool userAccepted_ = false;
    bool authenticated_ = false;
    bool binaryType_ = false;
    bool passiveReady_ = false;
    bool pipelineOptIn_ = false;
    std::string restartTransferId_;
    std::string workingDirectory_ = "/";
};

[[nodiscard]] common::Result<ControlCommand> parseControlCommand(const std::string& line);
[[nodiscard]] std::string formatReply(int code, const std::string& text);
[[nodiscard]] int replyCode(const ControlResponse& response) noexcept;

}  // namespace cpnetflux::protocol::control
