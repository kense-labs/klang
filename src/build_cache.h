// Reuse a linked executable when the Apple C driver's inputs are still current.
#pragma once

namespace klang {

#ifdef __APPLE__

inline string BuildInputPath(const string &path) {
    std::error_code ec;
    auto absolute = filesystem::absolute(filesystem::path(path), ec);
    return ec ? path : absolute.string();
}
#endif

// Give ld a private output on the destination filesystem. After publishing
// it, the stamp may describe only this file, even if another program
// building to the same name replaces it before the stamp is written.
struct PendingExecutable {
    string exe;
    string dir;
    string path;

    explicit PendingExecutable(const string &output) : exe(output) {
        auto dest = filesystem::path(exe);
        dir = MakeBuildTempDir((dest.parent_path() / ".k-link-XXXXXX").string());
        if (!dir.empty()) path = (filesystem::path(dir) / dest.filename()).string();
    }
    ~PendingExecutable() {
        if (dir.empty()) return;
        std::error_code ec;
        filesystem::remove_all(filesystem::path(dir), ec);
    }
    PendingExecutable(const PendingExecutable &) = delete;
    PendingExecutable &operator=(const PendingExecutable &) = delete;

    string Publish() const {
        auto own = BuildPathIdentity(path, false);
        auto ec = RenamePath(path, exe);
        if (ec) throw CompileError { cat("cannot publish ", exe, ": ", ec.message()) };
        auto published = BuildPathIdentity(exe);
        return published.starts_with(own + ":") ? published : string();
    }
};

#ifdef __APPLE__
struct ExecutableCache {
    string dir;
    string key;
    string sdk;
    string toolchain;
    vector<string> inputs;

    // This lane uses the system driver and its selected SDK. An alternate
    // driver, header search path or linker configuration must run normally.
    static ExecutableCache Open(const Toolchain &tc, const string &dir, const string &cfile,
                                const string &runtime, const string &exe,
                                const vector<string> &flags, const vector<string> &libs,
                                string_view cordis_identity) {
        if ((tc.cc != "/usr/bin/cc" && tc.cc != "/usr/bin/clang") ||
            !tc.banner.starts_with("Apple clang version "))
            return {};
        for (auto name : { "CPATH", "C_INCLUDE_PATH", "LIBRARY_PATH", "SDKROOT", "DEVELOPER_DIR",
                           "TOOLCHAINS", "CCC_OVERRIDE_OPTIONS", "CLANG_CONFIG_FILE_SYSTEM_DIR",
                           "CLANG_CONFIG_FILE_USER_DIR", "COMPILER_PATH", "LD_LIBRARY_PATH",
                           "DYLD_LIBRARY_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "DYLD_INSERT_LIBRARIES",
                           "LD_FORCE_CLASSIC", "LD_NO_CLASSIC_LINKER" })
            if (getenv(name)) return {};
        auto start = tc.banner.find("\nInstalledDir: ");
        if (start == string::npos) return {};
        start += sizeof("\nInstalledDir: ") - 1;
        auto bin = tc.banner.substr(start, tc.banner.find('\n', start) - start);
        auto sdk = MacSdkPath();
        if (sdk.empty() || bin.empty() || bin[0] != '/') return {};
        std::error_code ec;
        auto alias = cat(DirOf(sdk), "MacOSX.sdk");
        if (filesystem::equivalent(sdk, alias, ec)) sdk = std::move(alias);
        auto toolchain = filesystem::path(bin).parent_path().string();
        for (auto &overlay : { string("/usr/local/include"), string("/usr/local/lib"),
                               cat(sdk, "/usr/local/include"), cat(sdk, "/usr/local/lib") })
            if (BuildPathIdentity(overlay) != "missing") return {};
        auto output = BuildInputPath(exe);
        auto status = filesystem::symlink_status(output, ec);
        if (status.type() != filesystem::file_type::not_found && !filesystem::is_regular_file(status))
            return {};
        string key = "apple-executable-v2";
        for (auto &s : { tc.cc, tc.banner, sdk, output, AbsolutePath(".") })
            Append(key, '\0', s);
        for (auto &flag : flags) Append(key, '\0', flag);
        for (auto &lib : libs) Append(key, '\0', lib);
        for (auto name : { "PATH", "MACOSX_DEPLOYMENT_TARGET", "SOURCE_DATE_EPOCH" })
            Append(key, '\0', getenv(name) ? getenv(name) : "");
        if (!cordis_identity.empty()) Append(key, '\0', "cordis", '\0', cordis_identity);
        return { dir, std::move(key), sdk, std::move(toolchain),
                 { output, BuildInputPath(cfile), BuildInputPath(runtime),
                 tc.cc, cat(bin, "/clang"), cat(bin, "/ld"), sdk, cat(sdk, "/SDKSettings.json"),
                 cat(sdk, "/usr/include"), cat(sdk, "/usr/lib"), "/usr/local/include",
                 "/usr/local/lib", cat(sdk, "/usr/local/include"), cat(sdk, "/usr/local/lib") } };
    }

    string Path(const char *name) const { return cat(dir, "/", name); }

    bool Hit() const {
        string saved;
        return LoadFile(Path("exe.inputs"), saved) && saved.starts_with(key + '\0') &&
               saved.size() > key.size() + 1 &&
               Identities(string_view(saved).substr(key.size() + 1));
    }

    vector<string> CompileFlags() const {
        return { "-MD", "-MF", Path("exe.headers"), "-MT", "k" };
    }

    vector<string> LinkFlags() const {
        return { "-Xlinker", "-dependency_info", "-Xlinker", Path("exe.link-inputs") };
    }

    bool ReadHeaders(set<string> &files) const {
        string text;
        if (!LoadFile(Path("exe.headers"), text) || !text.starts_with("k:")) return false;
        string path;
        for (size_t i = 2; i < text.size(); i++) {
            auto c = text[i];
            if (c == '\\') {
                if (++i == text.size()) return false;
                if (text[i] != '\n') path += text[i];
            } else if (c == '$') {
                if (++i == text.size() || text[i] != '$') return false;
                path += '$';
            } else if (isspace((unsigned char)c)) {
                if (!path.empty()) {
                    auto absolute = BuildInputPath(path);
                    if (!PathExists(absolute)) return false;
                    files.insert(std::move(absolute));
                    path.clear();
                }
            } else {
                path += c;
            }
        }
        return path.empty();
    }

    bool ReadLinkInputs(set<string> &files) const {
        string records;
        if (!LoadFile(Path("exe.link-inputs"), records)) return false;
        auto app = BuildInputPath(Path("exe.app.o"));
        bool found = false;
        for (size_t i = 0; i < records.size();) {
            auto kind = (unsigned char)records[i++];
            auto end = records.find('\0', i);
            if (end == string::npos || end == i) return false;
            auto path = records.substr(i, end - i);
            i = end + 1;
            // ld's NUL-terminated dependency records: input, missing input,
            // version and output. clang deletes the temporary program object.
            if (kind == 0x10) {
                auto absolute = BuildInputPath(path);
                if (absolute == app) continue;
                std::error_code ec;
                if (filesystem::is_regular_file(path, ec)) {
                    files.insert(std::move(absolute));
                    found = true;
                } else if (ec != std::errc::no_such_file_or_directory ||
                           filesystem::path(path).extension() != ".o") {
                    return false;
                }
            } else if (kind == 0x11) {
                files.insert(BuildInputPath(path));
            } else if (kind != 0x00 && kind != 0x40) {
                return false;
            }
        }
        return found;
    }

    void Save(const string &output_identity) const {
        if (output_identity.empty()) return;
        set<string> files(inputs.begin(), inputs.end());
        if (!ReadHeaders(files) || !ReadLinkInputs(files)) return;
        auto saved = key + '\0';
        for (auto &path : files) {
            auto identity = path == inputs.front() ? output_identity : BuildPathIdentity(path);
            if (identity.empty()) return;
            if (path.starts_with(sdk + "/")) Append(saved, "S", path.substr(sdk.size()));
            else if (path.starts_with(toolchain + "/"))
                Append(saved, "T", path.substr(toolchain.size()));
            else Append(saved, path);
            Append(saved, '\0', identity, '\0');
        }
        WriteFileIfChanged(Path("exe.inputs"), saved);
    }

    void RemoveReports() const {
        std::error_code ec;
        filesystem::remove(filesystem::path(Path("exe.headers")), ec);
        filesystem::remove(filesystem::path(Path("exe.link-inputs")), ec);
        filesystem::remove(filesystem::path(Path("exe.app.o")), ec);
    }

private:
    bool Identities(string_view saved) const {
        while (!saved.empty()) {
            auto end = saved.find('\0');
            if (end == string_view::npos || end == 0) return false;
            string path(saved.substr(0, end));
            if (path[0] == 'S') path.replace(0, 1, sdk);
            else if (path[0] == 'T') path.replace(0, 1, toolchain);
            saved.remove_prefix(end + 1);
            end = saved.find('\0');
            if (end == string_view::npos || end == 0 ||
                BuildPathIdentity(path) != saved.substr(0, end)) return false;
            saved.remove_prefix(end + 1);
        }
        return true;
    }
};

#endif

}  // namespace klang
