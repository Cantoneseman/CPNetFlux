#include "cpnetflux/protocol/control/control_command.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>

#include "cpnetflux/checkpoint/transfer_manifest.h"

namespace cpnetflux::protocol::control {
namespace {

std::string trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return std::string(value);
}

std::string upper(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return result;
}

common::Result<std::uint32_t> parseParallelism(std::string_view value) {
    if (value.empty()) {
        return common::Status::invalidArgument("parallelism must not be empty");
    }
    std::uint64_t parsed = 0;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end || parsed == 0 || parsed > 64) {
        return common::Status::invalidArgument("parallelism must be in range 1..64");
    }
    return static_cast<std::uint32_t>(parsed);
}

ControlCommandType commandTypeForVerb(const std::string& verb) noexcept {
    if (verb == "USER") {
        return ControlCommandType::User;
    }
    if (verb == "PASS") {
        return ControlCommandType::Pass;
    }
    if (verb == "TYPE") {
        return ControlCommandType::Type;
    }
    if (verb == "SYST") {
        return ControlCommandType::Syst;
    }
    if (verb == "FEAT") {
        return ControlCommandType::Feat;
    }
    if (verb == "PWD") {
        return ControlCommandType::Pwd;
    }
    if (verb == "NOOP") {
        return ControlCommandType::Noop;
    }
    if (verb == "QUIT") {
        return ControlCommandType::Quit;
    }
    if (verb == "EPSV") {
        return ControlCommandType::Epsv;
    }
    if (verb == "PASV") {
        return ControlCommandType::Pasv;
    }
    if (verb == "OPTS") {
        return ControlCommandType::Opts;
    }
    if (verb == "REST") {
        return ControlCommandType::Rest;
    }
    if (verb == "STOR") {
        return ControlCommandType::Stor;
    }
    if (verb == "RETR") {
        return ControlCommandType::Retr;
    }
    if (verb == "SIZE") {
        return ControlCommandType::Size;
    }
    if (verb == "MDTM") {
        return ControlCommandType::Mdtm;
    }
    if (verb == "CWD") {
        return ControlCommandType::Cwd;
    }
    if (verb == "CDUP") {
        return ControlCommandType::Cdup;
    }
    if (verb == "LIST") {
        return ControlCommandType::List;
    }
    if (verb == "NLST") {
        return ControlCommandType::Nlst;
    }
    return ControlCommandType::Unknown;
}

ControlResponse singleLine(int code, std::string text) {
    ControlResponse response;
    response.lines.push_back(formatReply(code, text));
    return response;
}

bool isLoginRequired(ControlCommandType type) noexcept {
    return type == ControlCommandType::Type || type == ControlCommandType::Epsv ||
           type == ControlCommandType::Pasv || type == ControlCommandType::Opts ||
           type == ControlCommandType::Rest || type == ControlCommandType::Stor ||
           type == ControlCommandType::Retr || type == ControlCommandType::Size ||
           type == ControlCommandType::Mdtm || type == ControlCommandType::Cwd ||
           type == ControlCommandType::Cdup || type == ControlCommandType::List ||
           type == ControlCommandType::Nlst;
}

}  // namespace

common::Result<ControlCommand> parseControlCommand(const std::string& line) {
    std::string stripped = trim(line);
    while (!stripped.empty() && (stripped.back() == '\n' || stripped.back() == '\r')) {
        stripped.pop_back();
        stripped = trim(stripped);
    }
    if (stripped.empty()) {
        return ControlCommand{ControlCommandType::Empty, "", ""};
    }

    const std::size_t split = stripped.find_first_of(" \t");
    const std::string verb =
        upper(split == std::string::npos ? stripped : stripped.substr(0, split));
    const std::string argument =
        split == std::string::npos ? std::string() : trim(std::string_view(stripped).substr(split));

    ControlCommand command;
    command.type = commandTypeForVerb(verb);
    command.verb = verb;
    command.argument = argument;

    if (command.type == ControlCommandType::Opts) {
        const std::string normalized = upper(argument);
        std::string value;
        constexpr std::string_view kDirectPrefix = "PARALLELISM=";
        constexpr std::string_view kRetrPrefix = "RETR PARALLELISM=";
        constexpr std::string_view kPipelinePrefix = "PIPELINE=";
        if (normalized.rfind(kPipelinePrefix, 0) == 0) {
            const std::string value = normalized.substr(kPipelinePrefix.size());
            if (value != "0" && value != "1") {
                return common::Status::invalidArgument("OPTS PIPELINE must be 0 or 1");
            }
            command.hasPipelineOption = true;
            command.pipelineEnabled = value == "1";
            return command;
        }
        if (normalized.rfind(kDirectPrefix, 0) == 0) {
            value = argument.substr(kDirectPrefix.size());
        } else if (normalized.rfind(kRetrPrefix, 0) == 0) {
            value = argument.substr(kRetrPrefix.size());
        } else {
            return command;
        }
        auto parsed = parseParallelism(value);
        if (!parsed.isOk()) {
            return parsed.status();
        }
        command.parallelism = parsed.value();
    }

    if (command.type == ControlCommandType::Rest) {
        constexpr std::string_view kMarkerPrefix = "GFID:";
        const std::string normalized =
            upper(argument.substr(0, std::min<std::size_t>(5, argument.size())));
        if (normalized != kMarkerPrefix || argument.size() <= kMarkerPrefix.size()) {
            return common::Status::invalidArgument("REST marker must be GFID:<transfer_id>");
        }
        command.transferId = argument.substr(kMarkerPrefix.size());
        if (!checkpoint::isValidTransferId(command.transferId)) {
            return common::Status::invalidArgument("REST transfer_id is invalid");
        }
    }

    return command;
}

std::string formatReply(int code, const std::string& text) {
    return std::to_string(code) + " " + text;
}

int replyCode(const ControlResponse& response) noexcept {
    if (response.lines.empty() || response.lines.front().size() < 3) {
        return 0;
    }
    int code = 0;
    const char* begin = response.lines.front().data();
    const char* end = begin + 3;
    const auto result = std::from_chars(begin, end, code, 10);
    if (result.ec != std::errc()) {
        return 0;
    }
    return code;
}

ControlSession::ControlSession(std::string user, std::string password,
                               std::uint32_t defaultConnections)
    : ControlSession(
          ControlAuthConfig{AuthMode::Anonymous, std::move(user), std::move(password), "", ""},
          defaultConnections) {}

ControlSession::ControlSession(ControlAuthConfig auth, std::uint32_t defaultConnections)
    : expectedUser_(auth.mode == AuthMode::Token ? "token" : auth.user),
      expectedPassword_(auth.mode == AuthMode::Token ? auth.token : auth.password),
      auth_(std::move(auth)),
      connections_(defaultConnections) {}

ControlResponse ControlSession::handleCommand(const ControlCommand& command) {
    if (command.type == ControlCommandType::Empty) {
        return singleLine(502, "Empty command");
    }
    if (command.type == ControlCommandType::Unknown) {
        return singleLine(502, "Command not implemented");
    }
    if (!authenticated_ && isLoginRequired(command.type)) {
        return singleLine(530, "Please login with USER and PASS");
    }

    switch (command.type) {
        case ControlCommandType::User:
            userAccepted_ = command.argument == expectedUser_;
            authenticated_ = false;
            pipelineOptIn_ = false;
            return userAccepted_ ? singleLine(331, "User name okay, need password")
                                 : singleLine(530, "Invalid user");
        case ControlCommandType::Pass:
            if (userAccepted_ && command.argument == expectedPassword_) {
                authenticated_ = true;
                return singleLine(230, "User logged in");
            }
            authenticated_ = false;
            return singleLine(530, "Login incorrect");
        case ControlCommandType::Type:
            if (upper(command.argument) == "I") {
                binaryType_ = true;
                return singleLine(200, "Type set to I");
            }
            return singleLine(502, "Only TYPE I is supported");
        case ControlCommandType::Syst:
            return singleLine(215, "UNIX Type: L8");
        case ControlCommandType::Feat: {
            ControlResponse response;
            response.lines = {"211-Features", " EPSV",
                              " PASV",        " REST GFID",
                              " AUTH TOKEN",
                              " RETR",        " RETR RESUME",
                              " STOR",        " SIZE",
                              " MDTM",        " LIST",
                              " NLST",        " CWD",
                              " CDUP",        " OPTS PARALLELISM",
                              " OPTS PIPELINE",
                              "211 End"};
            return response;
        }
        case ControlCommandType::Pwd:
            return singleLine(257, "\"" + workingDirectory_ + "\" is the current directory");
        case ControlCommandType::Noop:
            return singleLine(200, "NOOP ok");
        case ControlCommandType::Quit: {
            ControlResponse response = singleLine(221, "Goodbye");
            response.action = ControlAction::Quit;
            return response;
        }
        case ControlCommandType::Epsv: {
            ControlResponse response;
            response.action = ControlAction::OpenPassiveEpsv;
            return response;
        }
        case ControlCommandType::Pasv: {
            ControlResponse response;
            response.action = ControlAction::OpenPassivePasv;
            return response;
        }
        case ControlCommandType::Opts:
            if (command.hasPipelineOption) {
                pipelineOptIn_ = command.pipelineEnabled;
                return singleLine(200, std::string("Control pipeline ") +
                                             (pipelineOptIn_ ? "enabled" : "disabled"));
            }
            if (command.parallelism == 0) {
                return singleLine(502, "Only OPTS PARALLELISM is supported");
            }
            connections_ = command.parallelism;
            return singleLine(200, "Parallelism set to " + std::to_string(connections_));
        case ControlCommandType::Rest:
            restartTransferId_ = command.transferId;
            return singleLine(350,
                              "Restart marker accepted transfer_id=GFID:" + restartTransferId_);
        case ControlCommandType::Stor:
            if (!binaryType_) {
                return singleLine(550, "TYPE I is required before STOR");
            }
            if (!passiveReady_) {
                return singleLine(550, "EPSV or PASV is required before STOR");
            }
            if (command.argument.empty()) {
                return singleLine(550, "STOR path is required");
            }
            passiveReady_ = false;
            {
                ControlResponse response;
                response.action = ControlAction::StartStor;
                response.path = command.argument;
                response.connections = connections_;
                response.resume = !restartTransferId_.empty();
                response.transferId = restartTransferId_;
                restartTransferId_.clear();
                return response;
            }
        case ControlCommandType::Retr:
            if (!binaryType_) {
                return singleLine(550, "TYPE I is required before RETR");
            }
            if (!passiveReady_) {
                return singleLine(550, "EPSV or PASV is required before RETR");
            }
            if (command.argument.empty()) {
                return singleLine(550, "RETR path is required");
            }
            passiveReady_ = false;
            {
                ControlResponse response;
                response.action = ControlAction::StartRetr;
                response.path = command.argument;
                response.connections = connections_;
                response.resume = !restartTransferId_.empty();
                response.transferId = restartTransferId_;
                restartTransferId_.clear();
                return response;
            }
        case ControlCommandType::Size:
            if (command.argument.empty()) {
                return singleLine(550, "SIZE path is required");
            }
            {
                ControlResponse response;
                response.action = ControlAction::QuerySize;
                response.path = command.argument;
                return response;
            }
        case ControlCommandType::Mdtm:
            if (command.argument.empty()) {
                return singleLine(550, "MDTM path is required");
            }
            {
                ControlResponse response;
                response.action = ControlAction::QueryMdtm;
                response.path = command.argument;
                return response;
            }
        case ControlCommandType::Cwd:
            if (command.argument.empty()) {
                return singleLine(550, "CWD path is required");
            }
            {
                ControlResponse response;
                response.action = ControlAction::ChangeDirectory;
                response.path = command.argument;
                return response;
            }
        case ControlCommandType::Cdup: {
            ControlResponse response;
            response.action = ControlAction::ChangeDirectory;
            response.path = "..";
            return response;
        }
        case ControlCommandType::List: {
            if (!passiveReady_) {
                return singleLine(550, "EPSV or PASV is required before LIST");
            }
            passiveReady_ = false;
            ControlResponse response;
            response.action = ControlAction::StartList;
            response.path = command.argument;
            response.connections = connections_;
            return response;
        }
        case ControlCommandType::Nlst: {
            if (!passiveReady_) {
                return singleLine(550, "EPSV or PASV is required before NLST");
            }
            passiveReady_ = false;
            ControlResponse response;
            response.action = ControlAction::StartNlst;
            response.path = command.argument;
            response.connections = connections_;
            return response;
        }
        case ControlCommandType::Empty:
        case ControlCommandType::Unknown:
            break;
    }

    return singleLine(502, "Command not implemented");
}

void ControlSession::markPassiveReady() noexcept { passiveReady_ = true; }

void ControlSession::clearPassiveReady() noexcept { passiveReady_ = false; }

bool ControlSession::authenticated() const noexcept { return authenticated_; }

bool ControlSession::binaryType() const noexcept { return binaryType_; }

bool ControlSession::passiveReady() const noexcept { return passiveReady_; }
bool ControlSession::pipelineOptIn() const noexcept { return pipelineOptIn_; }

std::uint32_t ControlSession::connections() const noexcept { return connections_; }

const std::string& ControlSession::workingDirectory() const noexcept { return workingDirectory_; }

void ControlSession::setWorkingDirectory(std::string directory) {
    workingDirectory_ = std::move(directory);
    if (workingDirectory_.empty() || workingDirectory_.front() != '/') {
        workingDirectory_ = "/";
    }
}

}  // namespace cpnetflux::protocol::control
