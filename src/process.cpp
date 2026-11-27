// K compiler — other processes (process.h), over subprocess.h. This is
// kept apart so the platform headers it needs stay out of the compiler.

#include "includes.h"
#include "utils.h"
#include "process.h"
#include "progress.h"

#ifdef _MSC_VER
    #pragma warning(push, 0)
#endif
#include "subprocess.h"
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <io.h>
#else
    #include <unistd.h>
    #include <dlfcn.h>
    #include <fcntl.h>
    #include <sys/file.h>
    #include <sys/stat.h>
#endif

namespace klang {

ProcessResult RunProcess(const vector<string> &argv, const string &cwd, bool echo,
                         string *captured_stderr) {
    ProcessResult r;
    vector<const char *> args;
    for (auto &a : argv) args.push_back(a.c_str());
    args.push_back(nullptr);
    // Combine streams by default to preserve their order. When the caller
    // needs stdout separately, drain both pipes while the child is alive.
    auto options = subprocess_option_inherit_environment |
                   subprocess_option_search_user_path | subprocess_option_enable_async |
                   subprocess_option_no_window;
    if (captured_stderr) captured_stderr->clear();
    else options |= subprocess_option_combined_stdout_stderr;
    subprocess_s p;
    if (subprocess_create_ex(args.data(), options, nullptr, cwd.empty() ? nullptr : cwd.c_str(),
                             &p)) {
        // subprocess.h leaves the reason in GetLastError on Windows, in
        // errno elsewhere.
        auto what = cat("cannot start ", argv.empty() ? string("(nothing)") : argv[0]);
        #ifdef _WIN32
            r.output = cat(what, " (Windows error ", (unsigned)GetLastError(), ")");
        #else
            r.output = cat(what, ": ", strerror(errno));
        #endif
        return r;
    }
    r.started = true;
    // No input is sent. Give the child EOF before reading its output, since
    // it may wait for input before it prints anything or exits.
    fclose(p.stdin_file);
    p.stdin_file = nullptr;
    jthread drain;
    if (captured_stderr) drain = jthread([&] {
        char buf[4096];
        while (auto n = subprocess_read_stderr(&p, buf, sizeof buf)) captured_stderr->append(buf, n);
    });
    char buf[4096];
    for (;;) {
        auto n = subprocess_read_stdout(&p, buf, sizeof buf);
        if (!n) break;
        r.output.append(buf, n);
        if (echo) fwrite(buf, 1, n, stderr);
        ProgressOutput(buf, n);
    }
    if (drain.joinable()) drain.join();
    int code = -1;
    if (subprocess_join(&p, &code)) code = -1;
    subprocess_destroy(&p);
    r.code = code;
    return r;
}

static bool IsFile(const string &path) {
    #ifdef _WIN32
        auto attr = GetFileAttributesA(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    #else
        // A directory one may search is X_OK too.
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
               access(path.c_str(), X_OK) == 0;
    #endif
}

string FindOnPath(const string &name, const char *path) {
    if (name.find_first_of("/\\") != string::npos) return IsFile(name) ? name : "";
    vector<string> exts = { "" };
    #ifdef _WIN32
        constexpr char sep = ';';
        if (name.find('.') == string::npos) {
            auto pathext = getenv("PATHEXT");
            string list = pathext ? pathext : ".COM;.EXE;.BAT;.CMD";
            for (size_t i = 0; i < list.size();) {
                auto end = list.find(';', i);
                if (end == string::npos) end = list.size();
                auto ext = list.substr(i, end - i);
                for (auto &c : ext) c = (char)tolower((unsigned char)c);
                if (!ext.empty()) exts.push_back(ext);
                i = end + 1;
            }
        }
    #else
        constexpr char sep = ':';
    #endif
    if (!path) path = getenv("PATH");
    if (!path) return "";
    string dirs = path;
    for (size_t i = 0; i <= dirs.size();) {
        auto end = dirs.find(sep, i);
        if (end == string::npos) end = dirs.size();
        auto dir = dirs.substr(i, end - i);
        i = end + 1;
        if (dir.empty()) continue;
        for (auto &ext : exts) {
            #ifdef _WIN32
                const char *slash = "\\";
            #else
                const char *slash = "/";
            #endif
            auto cand = cat(dir, dir.back() == '/' || dir.back() == '\\' ? "" : slash, name, ext);
            if (IsFile(cand)) return cand;
        }
    }
    return "";
}

void SetEnvVar(const string &name, const string &value) {
    #ifdef _WIN32
        // Both the C runtime's copy, which getenv reads, and the process's
        // own block, which a started process inherits.
        _putenv_s(name.c_str(), value.c_str());
        SetEnvironmentVariableA(name.c_str(), value.c_str());
    #else
        setenv(name.c_str(), value.c_str(), 1);
    #endif
}

FileLock::FileLock(const string &path, const string &waiting, Mode mode) {
    optional<ProgressStep> step;
    if (!waiting.empty()) step.emplace(waiting);
    #ifdef _WIN32
        auto h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        auto error = h == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        if (h != INVALID_HANDLE_VALUE) {
            OVERLAPPED offset = {};
            DWORD flags = mode == Mode::Shared ? 0 : LOCKFILE_EXCLUSIVE_LOCK;
            if (mode == Mode::TryExclusive) flags |= LOCKFILE_FAIL_IMMEDIATELY;
            if (LockFileEx(h, flags, 0, 1, 0, &offset)) {
                handle = reinterpret_cast<intptr_t>(h);
                return;
            }
            error = GetLastError();
            CloseHandle(h);
            if (mode == Mode::TryExclusive && error == ERROR_LOCK_VIOLATION) return;
        }
        throw CompileError { cat("cannot lock ", path, " (Windows error ", (unsigned)error, ")") };
    #else
        auto fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (fd >= 0) {
            int flags = mode == Mode::Shared ? LOCK_SH : LOCK_EX;
            if (mode == Mode::TryExclusive) flags |= LOCK_NB;
            int result;
            do { result = flock(fd, flags); } while (result < 0 && errno == EINTR);
            if (result == 0) {
                handle = fd;
                return;
            }
            auto error = errno;
            close(fd);
            if (mode == Mode::TryExclusive && (error == EAGAIN || error == EWOULDBLOCK)) return;
            errno = error;
        }
        throw CompileError { cat("cannot lock ", path, ": ", strerror(errno)) };
    #endif
}

FileLock::~FileLock() {
    if (handle == -1) return;
    #ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(handle));
    #else
        close((int)handle);
    #endif
}

string MacSdkPath() {
    string sdk;
    #ifdef __APPLE__
        // As TinyCC finds the SDK's libraries (tccmacho.c's
        // tcc_add_macos_sdkpath): the SDK xcode-select made active, which
        // libxcselect knows without running xcrun.
        if (auto xcs = dlopen("libxcselect.dylib", RTLD_GLOBAL | RTLD_LAZY)) {
            auto f = (int (*)(unsigned, char **))dlsym(xcs, "xcselect_host_sdk_path");
            char *path = nullptr;
            if (f) f(1, &path);
            if (path) {
                sdk = path;
                free(path);
            }
        }
    #endif
    return sdk;
}

string BuildPathIdentity(const string &path, bool include_ctime) {
    #ifdef _WIN32
        auto h = CreateFileA(path.c_str(), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            auto error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? "missing" : "";
        }
        BY_HANDLE_FILE_INFORMATION info = {};
        auto got_info = GetFileInformationByHandle(h, &info);
        CloseHandle(h);
        if (!got_info) return {};
        auto id = cat(info.dwVolumeSerialNumber, ":", info.nFileIndexHigh, ":", info.nFileIndexLow,
                      ":", info.dwFileAttributes, ":", info.nFileSizeHigh, ":", info.nFileSizeLow,
                      ":", info.ftLastWriteTime.dwHighDateTime, ":", info.ftLastWriteTime.dwLowDateTime);
        if (include_ctime)
            Append(id, ":", info.ftCreationTime.dwHighDateTime, ":", info.ftCreationTime.dwLowDateTime);
        return id;
    #else
        struct stat st;
        if (stat(path.c_str(), &st) != 0) return errno == ENOENT ? "missing" : string();
        #ifdef __APPLE__
            const auto &modified = st.st_mtimespec;
            const auto &changed = st.st_ctimespec;
        #else
            const auto &modified = st.st_mtim;
            const auto &changed = st.st_ctim;
        #endif
        auto id = cat(st.st_dev, ":", st.st_ino, ":", st.st_mode, ":", st.st_size, ":",
                      modified.tv_sec, ":", modified.tv_nsec);
        if (include_ctime) Append(id, ":", changed.tv_sec, ":", changed.tv_nsec);
        return id;
    #endif
}

string MakeBuildTempDir([[maybe_unused]] string path) {
    #ifdef __APPLE__
        if (mkdtemp(path.data())) return path;
    #endif
    return {};
}

string CommandLineText(const vector<string> &argv) {
    string s;
    for (auto &a : argv) {
        if (!s.empty()) s += ' ';
        if (!a.empty() && a.find_first_of(" \t\"'&|<>^();$`\\*?[") == string::npos) {
            s += a;
            continue;
        }
        // Backslashes stand, as Windows paths are full of them and only a
        // quote needs the escape on either platform's reading.
        s += '"';
        for (auto c : a) {
            if (c == '"') s += '\\';
            s += c;
        }
        s += '"';
    }
    return s;
}

}  // namespace klang
