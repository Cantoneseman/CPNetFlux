// Explicit experiment supervisor. No setuid installation and no arbitrary child commands.
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sched.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
volatile sig_atomic_t cancelled = 0;
void cancel(int) { cancelled = 1; }
void require(bool value, const std::string& why) {
    if (!value) throw std::runtime_error(why);
}
void systemCheck(bool value, const char* operation) {
    if (!value) throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}
double now() {
    timespec ts{};
    systemCheck(::clock_gettime(CLOCK_MONOTONIC, &ts) == 0, "clock_gettime");
    return double(ts.tv_sec) + double(ts.tv_nsec) / 1e9;
}
std::string quote(const std::string& text) {
    std::ostringstream out; out << '"';
    for (unsigned char ch : text) {
        if (ch == '"' || ch == '\\') out << '\\' << char(ch);
        else if (ch < 32) out << "\\u00" << "0123456789abcdef"[ch >> 4] << "0123456789abcdef"[ch & 15];
        else out << char(ch);
    }
    return out.str() + '"';
}
std::string number(double value) {
    std::ostringstream out; out << std::setprecision(17) << value; return out.str();
}
std::uint64_t integer(const std::string& text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    require(error == std::errc{} && end == text.data() + text.size(), "invalid integer");
    return value;
}
struct Fd {
    int value = -1;
    explicit Fd(int fd = -1) : value(fd) {}
    ~Fd() { if (value >= 0) ::close(value); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};
struct Options {
    fs::path payload, evidence;
    std::string mode, commit, hostNamespace;
    uid_t uid = 0; gid_t gid = 0; std::uint64_t inode = 0;
    double setupStarted = 0;
    bool enabled = false, pacing = false;
};
Options parse(int argc, char** argv) {
    std::map<std::string, std::string> values;
    Options o;
    const std::vector<std::string> keys = {"--payload-root", "--evidence", "--mode", "--commit",
        "--uid", "--gid", "--inode", "--host-namespace", "--setup-started"};
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--enable-private-tmpfs") { require(!o.enabled, "duplicate opt-in"); o.enabled = true; }
        else if (key == "--kernel-tcp-pacing") { require(!o.pacing, "duplicate pacing"); o.pacing = true; }
        else {
            require(std::find(keys.begin(), keys.end(), key) != keys.end(), "unknown option: " + key);
            require(i + 1 < argc && !values.contains(key), "missing or duplicate option: " + key);
            values[key] = argv[++i];
        }
    }
    require(o.enabled, "explicit --enable-private-tmpfs is required");
    require(values.size() == keys.size(), "incomplete fixed supervisor arguments");
    o.payload = values.at("--payload-root"); o.evidence = values.at("--evidence");
    o.mode = values.at("--mode"); o.commit = values.at("--commit");
    o.hostNamespace = values.at("--host-namespace");
    const auto uid = integer(values.at("--uid")), gid = integer(values.at("--gid"));
    require(uid <= UINT32_MAX && gid <= UINT32_MAX, "uid/gid overflow");
    o.uid = uid; o.gid = gid; o.inode = integer(values.at("--inode"));
    std::size_t consumed = 0;
    o.setupStarted = std::stod(values.at("--setup-started"), &consumed);
    require(consumed == values.at("--setup-started").size() && std::isfinite(o.setupStarted) &&
            o.setupStarted > 0 && o.setupStarted <= now(), "invalid setup clock");
    require(o.mode == "v3" || o.mode == "setup-only" || o.mode == "smoke" || o.mode == "smoke-fail" ||
            o.mode == "smoke-timeout" || o.mode == "smoke-cancel" || o.mode == "smoke-v3" ||
            o.mode == "smoke-descendant", "unsupported fixed child mode");
    require(!o.pacing || o.mode == "v3", "kernel pacing requires V3");
    require(o.mode != "v3" || std::regex_match(o.commit, std::regex("[0-9a-f]{40}")), "V3 needs fixed commit");
    return o;
}
struct stat checkedDirectory(const fs::path& path, uid_t uid) {
    struct stat st{};
    systemCheck(::lstat(path.c_str(), &st) == 0, "lstat directory");
    require(S_ISDIR(st.st_mode) && st.st_uid == uid && path.is_absolute() &&
            fs::canonical(path) == path, "directory identity/canonical path rejected");
    return st;
}
void validate(const Options& o) {
    require(::geteuid() == 0 && ::getuid() == 0, "supervisor requires explicit sudo");
    const passwd* owner = ::getpwnam("Su");
    require(owner && o.uid != 0 && o.uid == owner->pw_uid && o.gid == owner->pw_gid, "only Su workload allowed");
    require(o.payload.parent_path() == "/dev/shm" &&
            std::regex_match(o.payload.filename().string(), std::regex("cpnetflux-s0-private55-[a-zA-Z0-9-]+")),
            "payload path rejected");
    {
        std::ifstream mounts("/proc/self/mountinfo");
        require(bool(mounts), "cannot inspect existing mounts");
        std::string line;
        while (std::getline(mounts, line))
            require(line.find(" " + o.payload.string() + " ") == std::string::npos, "already mounted payload rejected");
    }
    const auto st = checkedDirectory(o.payload, o.uid);
    require(st.st_ino == o.inode && fs::is_empty(o.payload), "payload inode or empty directory rejected");
    const auto root = fs::path("/home/Su/experiments/CPNetFlux");
    const auto rel = o.evidence.lexically_relative(root);
    require(!rel.empty() && !rel.is_absolute() && *rel.begin() != ".." && o.evidence != root,
            "evidence path rejected");
    checkedDirectory(o.evidence, o.uid);
    require(fs::read_symlink("/proc/self/ns/mnt").string() == o.hostNamespace, "host namespace mismatch");
}
void save(int dir, const char* name, const std::string& json, const Options& o) {
    Fd fd(::openat(dir, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    systemCheck(fd.value >= 0, "create exclusive receipt");
    systemCheck(::fchown(fd.value, o.uid, o.gid) == 0, "receipt owner");
    const auto text = json + "\n";
    std::size_t done = 0;
    while (done < text.size()) {
        const auto n = ::write(fd.value, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR) continue;
        systemCheck(n > 0, "write receipt"); done += n;
    }
}
std::string mountInfo(const fs::path& path) {
    std::ifstream input("/proc/self/mountinfo"); std::string line;
    while (std::getline(input, line))
        if (line.find(" " + path.string() + " ") != std::string::npos && line.find(" - tmpfs ") != std::string::npos)
            return line;
    throw std::runtime_error("actual private tmpfs mount proof missing");
}
std::vector<pid_t> children() {
    std::ifstream input("/proc/self/task/" + std::to_string(::getpid()) + "/children");
    require(bool(input), "cannot enumerate owned children");
    std::vector<pid_t> result; pid_t pid;
    while (input >> pid) result.push_back(pid);
    return result;
}
bool reapDescendants() {
    const double deadline = now() + 5;
    while (true) {
        // These are unreaped direct/adopted children of this subreaper; never unrelated host processes.
        for (pid_t pid : children()) ::kill(pid, SIGKILL);
        int status = 0; pid_t result;
        do { result = ::waitpid(-1, &status, WNOHANG); } while (result > 0 || (result < 0 && errno == EINTR));
        if (result < 0 && errno == ECHILD) return true;
        if (now() > deadline) return false;
        ::usleep(5000);
    }
}
struct ChildResult {
    int code = 1; std::string reason = "not_started"; bool reaped = false;
    pid_t pid = -1; double wall = 0;
};
ChildResult workload(const Options& o, int evidence) {
    Fd log(::openat(evidence, "child.log", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    systemCheck(log.value >= 0, "create child log");
    systemCheck(::fchown(log.value, o.uid, o.gid) == 0, "child log owner");
    std::vector<std::string> args = {"/usr/bin/python3"};
    const std::string script = std::string(CPNETFLUX_SOURCE_DIR) + "/tools/perf/";
    if (o.mode == "v3") {
        args.insert(args.end(), {script + "run_private_tmpfs_v3.py", "--action", "run",
            "--evidence", o.evidence.string(), "--payload-root", o.payload.string(),
            "--commit", o.commit, "--enable-private-tmpfs", "--native-setup"});
        if (o.pacing) args.push_back("--kernel-tcp-pacing");
    } else args.insert(args.end(), {script + "private_tmpfs.py", "--action", "probe", "--payload-root",
        o.payload.string(), "--evidence", o.evidence.string(), "--mode", o.mode});
    std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    ChildResult r; const double started = now();
    r.pid = ::fork(); systemCheck(r.pid >= 0, "fork workload");
    if (r.pid == 0) {
        ::signal(SIGTERM, SIG_DFL); ::signal(SIGINT, SIG_DFL);
        if (::setsid() < 0 || ::dup2(log.value, STDOUT_FILENO) < 0 || ::dup2(log.value, STDERR_FILENO) < 0 ||
            ::setgroups(0, nullptr) != 0 || ::setgid(o.gid) != 0 || ::setuid(o.uid) != 0 ||
            ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) _exit(126);
        ::execv(argv[0], argv.data()); _exit(127);
    }
    int status = 0; bool stopping = false; double stopStarted = 0;
    r.reason = "exit";
    try {
        while (true) {
            const auto result = ::waitpid(r.pid, &status, WNOHANG);
            if (result == r.pid) break;
            if (result < 0 && errno == EINTR) continue;
            systemCheck(result >= 0, "wait workload");
            const double elapsed = now() - started;
            const bool request = cancelled || fs::exists(o.evidence / "cancel.request") ||
                (o.mode == "smoke-cancel" && elapsed >= .3);
            const bool timeout = elapsed >= (o.mode == "smoke-timeout" ? .3 : 600);
            if (!stopping && (request || timeout)) {
                r.reason = request ? "cancelled" : "timeout";
                // Leader remains our unreaped child until waitpid observes exit.
                ::kill(-r.pid, SIGTERM); stopping = true; stopStarted = now();
            }
            if (stopping && now() - stopStarted >= 30) ::kill(-r.pid, SIGKILL);
            ::usleep(10000);
        }
        r.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        r.reaped = reapDescendants();
        require(r.reaped, "owned descendants did not drain");
    } catch (...) {
        r.reaped = reapDescendants();
        throw;
    }
    r.wall = now() - started;
    return r;
}
int supervise(const Options& o) {
    validate(o);
    Fd evidence(::open(o.evidence.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    systemCheck(evidence.value >= 0, "open evidence");
    systemCheck(::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) == 0, "subreaper");
    struct sigaction action{}; action.sa_handler = cancel; sigemptyset(&action.sa_mask);
    systemCheck(::sigaction(SIGTERM, &action, nullptr) == 0 && ::sigaction(SIGINT, &action, nullptr) == 0, "signals");
    systemCheck(::unshare(CLONE_NEWNS) == 0, "unshare mount namespace");
    systemCheck(::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) == 0, "private propagation");
    const std::string privateNamespace = fs::read_symlink("/proc/self/ns/mnt").string();
    require(privateNamespace != o.hostNamespace, "namespace did not change");
    bool mounted = false, unmounted = false; std::string error, options, remaining = "[]"; double mountSeconds = 0, setupSeconds = 0;
    ChildResult child;
    try {
        const double started = now();
        const auto flags = "size=1280M,uid=" + std::to_string(o.uid) + ",gid=" + std::to_string(o.gid) +
            ",mode=0700,huge=within_size";
        systemCheck(::mount("cpnetflux-private57", o.payload.c_str(), "tmpfs", MS_NOSUID | MS_NODEV, flags.c_str()) == 0, "mount tmpfs");
        mounted = true; mountSeconds = now() - started;
        options = mountInfo(o.payload);
        require(options.find("huge=within_size") != std::string::npos, "huge policy not effective");
        setupSeconds = now() - o.setupStarted;
        const auto prefix = "{\"host_namespace\":" + quote(o.hostNamespace) + ",\"private_namespace\":" + quote(privateNamespace) +
            ",\"uid\":" + std::to_string(o.uid) + ",\"gid\":" + std::to_string(o.gid) +
            ",\"setup_strategy\":\"native_syscalls\",\"supervisor_euid\":0,\"mount_setup_seconds\":" + number(mountSeconds) +
            ",\"target_setup_seconds\":" + number(setupSeconds) + ",\"mount_options\":" + quote(options);
        save(evidence.value, "mount-ready.json", prefix + "}", o);
        child = workload(o, evidence.value);
    } catch (const std::exception& e) { error = e.what(); }
    if (mounted) {
        remaining = "["; bool first = true;
        for (const auto& entry : fs::directory_iterator(o.payload)) {
            if (!first) remaining += ",";
            first = false; remaining += quote(entry.path().filename().string());
        }
        remaining += "]";
    }
    // Do not detach a live namespace or clean an arbitrary directory on error.
    if (mounted) {
        if (::umount2(o.payload.c_str(), 0) == 0) unmounted = true;
        else { if (!error.empty()) error += "; "; error += std::string("umount: ") + std::strerror(errno); }
    }
    std::string record = "{\"host_namespace\":" + quote(o.hostNamespace) + ",\"private_namespace\":" + quote(privateNamespace) +
        ",\"uid\":" + std::to_string(o.uid) + ",\"gid\":" + std::to_string(o.gid) +
        ",\"setup_strategy\":\"native_syscalls\",\"supervisor_euid\":0,\"mount_setup_seconds\":" + number(mountSeconds) +
        ",\"target_setup_seconds\":" + number(setupSeconds) + ",\"mount_options\":" + quote(options) +
        ",\"child\":{\"exit_code\":" + std::to_string(child.code) + ",\"reason\":" + quote(child.reason) +
        ",\"reaped\":" + (child.reaped ? "true" : "false") + ",\"pid\":" + std::to_string(child.pid) +
        ",\"wall_seconds\":" + number(child.wall) + "},\"unmounted\":" + (unmounted ? "true" : "false");
    if (!error.empty()) record += ",\"error\":" + quote(error);
    record += ",\"remaining_names\":" + remaining + "}";
    save(evidence.value, "namespace.json", record, o);
    if (!error.empty()) { std::cerr << error << '\n'; return 1; }
    return child.reason == "exit" ? child.code : 124;
}
} // namespace
int main(int argc, char** argv) {
    try { return supervise(parse(argc, argv)); }
    catch (const std::exception& e) { std::cerr << "CPNetFlux private setup: " << e.what() << '\n'; return 2; }
}
