#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <linux/magic.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;

constexpr std::string_view usage =
    "usage: k-inference-supervisor --worker-cgroup DIR --memory-budget-mib MIB\n"
    "       [--reload MARKER PROVIDER LOCK --rollback-file FILE] [--workspace]\n"
    "       -- MANIFEST LOCK PORT PENDING DEADLINE_MS MAX_ELEMENTS CHUNK_ELEMENTS REQUESTS\n";

[[noreturn]] void SystemError(const char *operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

struct File {
    int fd;
    explicit File(int fd) : fd(fd) {}
    ~File() { if (fd >= 0) close(fd); }
    File(const File &) = delete;
    File &operator=(const File &) = delete;
};

uint64_t Number(std::string_view text) {
    uint64_t number;
    const char *end = text.data() + text.size();
    auto read = std::from_chars(text.data(), end, number);
    if (read.ec != std::errc() || read.ptr != end)
        throw std::runtime_error("expected an integer, got " + std::string(text));
    return number;
}

struct Options {
    fs::path group, rollback;
    uint64_t budget = 0;
    bool workspace = false;
    std::vector<std::string> reload, arguments;
};

Options Parse(int argc, char **argv) {
    Options result;
    int i = 1;
    auto next = [&]() -> std::string {
        if (++i >= argc) throw std::runtime_error("missing supervisor option value");
        return argv[i];
    };
    for (; i < argc && std::string_view(argv[i]) != "--"; ++i) {
        std::string_view flag = argv[i];
        if (flag == "--worker-cgroup" && result.group.empty()) result.group = next();
        else if (flag == "--memory-budget-mib" && !result.budget) {
            result.budget = Number(next());
            if (!result.budget || result.budget > UINT32_MAX) throw std::runtime_error("invalid memory budget");
        } else if (flag == "--workspace" && !result.workspace) result.workspace = true;
        else if (flag == "--rollback-file" && result.rollback.empty()) result.rollback = next();
        else if (flag == "--reload" && result.reload.empty()) result.reload = {next(), next(), next()};
        else throw std::runtime_error("invalid or duplicate supervisor option " + std::string(flag));
    }
    if (i == argc || argc - i - 1 != 8 || result.group.empty() || !result.budget ||
        result.reload.empty() != result.rollback.empty()) throw std::runtime_error(std::string(usage));
    result.arguments.assign(argv + i + 1, argv + argc);
    auto port = Number(result.arguments[2]);
    if (!port || port > 65535) throw std::runtime_error("the supervisor requires a fixed nonzero port");
    return result;
}

std::string Attribute(int group, const char *name) {
    File file(openat(group, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (file.fd < 0) SystemError(name);
    char data[1024];
    auto size = read(file.fd, data, sizeof data);
    if (size < 0) SystemError(name);
    if (!size || size == static_cast<ssize_t>(sizeof data) || data[size - 1] != '\n')
        throw std::runtime_error(std::string("invalid cgroup attribute ") + name);
    return std::string(data, static_cast<size_t>(size) - 1);
}

uint64_t Counter(int group, const char *attribute, std::string_view key) {
    std::istringstream lines(Attribute(group, attribute));
    for (std::string name, value; lines >> name >> value;)
        if (name == key) return Number(value);
    throw std::runtime_error(std::string("missing cgroup counter ") + std::string(key));
}

bool RolledBack(int directory, const char *name) {
    if (directory < 0) return false;
    struct stat info {};
    if (fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) return false;
        SystemError("stat rollback file");
    }
    if (!S_ISREG(info.st_mode)) throw std::runtime_error("rollback file must be a regular file");
    return true;
}

void RecordRollback(int directory, const char *name) {
    File file(openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (file.fd < 0) SystemError("create rollback file");
    if (fsync(file.fd) != 0 || fsync(directory) != 0) SystemError("persist rollback file");
}

bool StopSignal(int number) { return number == SIGINT || number == SIGTERM; }

struct Signals {
    sigset_t wait, original, stop;

    Signals() {
        sigemptyset(&stop);
        sigaddset(&stop, SIGINT);
        sigaddset(&stop, SIGTERM);
        wait = stop;
        sigaddset(&wait, SIGCHLD);
        if (sigprocmask(SIG_BLOCK, &wait, &original) != 0) SystemError("block supervisor signals");
        // PID 1 accepts termination signals with an installed disposition.
        struct sigaction action {};
        sigemptyset(&action.sa_mask);
        action.sa_handler = +[](int) {};
        if (sigaction(SIGINT, &action, nullptr) != 0 || sigaction(SIGTERM, &action, nullptr) != 0)
            SystemError("install supervisor signals");
        action.sa_handler = SIG_DFL;
        if (sigaction(SIGCHLD, &action, nullptr) != 0) SystemError("enable child reaping");
    }

    int PendingStop() const {
        timespec now {};
        auto signal = sigtimedwait(&stop, nullptr, &now);
        return StopSignal(signal) ? signal : 0;
    }
};

[[noreturn]] void ChildError(const char *operation) {
    perror(operation);
    _exit(125);
}

pid_t StartWorker(int group, const Signals &signals, std::vector<std::string> &arguments) {
    File membership(openat(group, "cgroup.procs", O_WRONLY | O_CLOEXEC | O_NOFOLLOW));
    if (membership.fd < 0) SystemError("open worker cgroup.procs");
    std::vector<char *> command;
    for (auto &argument : arguments) command.push_back(argument.data());
    command.push_back(nullptr);
    auto parent = getpid();
    auto pid = fork();
    if (pid < 0) SystemError("fork inference worker");
    if (pid) return pid;
    if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0) ChildError("configure worker parent death");
    if (getppid() != parent) _exit(125);
    // Moving the writing process before exec puts all model allocations
    // and any descendants inside the worker budget.
    if (write(membership.fd, "0\n", 2) != 2) ChildError("enter worker cgroup");
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    auto mask = signals.original;
    sigdelset(&mask, SIGTERM);
    sigdelset(&mask, SIGINT);
    if (sigprocmask(SIG_SETMASK, &mask, nullptr) != 0) ChildError("restore worker signals");
    execv(command[0], command.data());
    ChildError("exec inference worker");
}

int WaitWorker(pid_t pid, const Signals &signals, int &stop) {
    int status;
    for (;;) {
        auto result = waitpid(pid, &status, WNOHANG);
        if (result == pid) return status;
        if (result < 0) {
            if (errno == EINTR) continue;
            SystemError("wait for inference worker");
        }
        auto event = sigwaitinfo(&signals.wait, nullptr);
        if (event < 0 && errno != EINTR) SystemError("wait for supervisor signal");
        if (StopSignal(event)) {
            stop = event;
            kill(pid, event);
        }
    }
}

int Supervise(const Options &options) {
    auto executable = fs::read_symlink("/proc/self/exe").parent_path() / "k-inference-gateway";
    File group(open(options.group.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (group.fd < 0) SystemError("open worker cgroup");
    struct statfs fs_info {};
    if (fstatfs(group.fd, &fs_info) != 0) SystemError("stat worker cgroup");
    if (fs_info.f_type != CGROUP2_SUPER_MAGIC) throw std::runtime_error("worker cgroup must use cgroup v2");
    auto maximum = Attribute(group.fd, "memory.max");
    if (maximum == "max" || Number(maximum) > options.budget * 1048576 ||
        Attribute(group.fd, "memory.swap.max") != "0" || Attribute(group.fd, "memory.oom.group") != "1")
        throw std::runtime_error("worker cgroup needs its own memory cap, no swap and memory.oom.group=1");

    auto directory = options.rollback.parent_path();
    if (directory.empty()) directory = ".";
    auto filename = options.rollback.filename();
    if (!options.rollback.empty() && filename.empty())
        throw std::runtime_error("expected a rollback file name");
    File state(options.rollback.empty() ? -1 : open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!options.rollback.empty() && state.fd < 0) SystemError("open rollback directory");
    bool allow_reload = !options.reload.empty() && !RolledBack(state.fd, filename.c_str());
    if (allow_reload && faccessat(state.fd, ".", W_OK, 0) != 0) SystemError("check rollback directory");
    Signals signals;
    for (int attempt = 0;; ++attempt) {
        if (auto stop = signals.PendingStop()) return 128 + stop;
        if (Counter(group.fd, "cgroup.events", "populated") != 0)
            throw std::runtime_error("the worker cgroup must be empty and outside the supervisor's cgroup");
        auto before = Counter(group.fd, "memory.events.local", "oom_kill");
        std::vector<std::string> command {executable.string()};
        command.insert(command.end(), options.arguments.begin(), options.arguments.end());
        if (allow_reload) command.insert(command.end(), options.reload.begin(), options.reload.end());
        if (options.workspace) command.emplace_back("--workspace");
        command.emplace_back("--memory-budget-mib");
        command.push_back(std::to_string(options.budget));

        int stop = 0;
        auto status = WaitWorker(StartWorker(group.fd, signals, command), signals, stop);
        if (!stop) stop = signals.PendingStop();
        if (stop) return 128 + stop;
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WTERMSIG(status) != SIGKILL || attempt != 0) return 128 + WTERMSIG(status);
        auto after = Counter(group.fd, "memory.events.local", "oom_kill");
        if (after <= before) return 128 + SIGKILL;
        if (allow_reload) {
            RecordRollback(state.fd, filename.c_str());
            allow_reload = false;
        }
        std::cerr << "gateway supervisor: oom_kill " << before << " -> " << after
                  << ", restarting with the stable lock\n";
    }
}
}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage;
        return 0;
    }
    try {
        return Supervise(Parse(argc, argv));
    } catch (const std::exception &error) {
        std::cerr << "gateway supervisor: " << error.what() << '\n';
        return 1;
    }
}
