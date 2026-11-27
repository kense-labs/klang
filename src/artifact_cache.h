// Inventory K_HOME and reclaim unused artifacts under a shared byte budget.
#pragma once
#include <tuple>

namespace klang {

inline bool CachePathMissing(const std::error_code &ec) {
    return ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory;
}

inline void CheckCacheRead(const filesystem::path &path, const std::error_code &ec) {
    if (ec && !CachePathMissing(ec))
        throw CompileError { cat("cannot inspect ", path.string(), ": ", ec.message()) };
}

inline bool CacheDirectory(const filesystem::path &path) {
    std::error_code ec;
    auto status = filesystem::symlink_status(path, ec);
    CheckCacheRead(path, ec);
    return filesystem::is_directory(status);
}

inline uintmax_t CacheBytes(const filesystem::path &path) {
    if (!CacheDirectory(path)) return 0;
    uintmax_t bytes = 0;
    std::error_code ec;
    for (filesystem::recursive_directory_iterator it(path, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code stat_error;
        auto status = it->symlink_status(stat_error);
        if (filesystem::is_regular_file(status) && !stat_error) {
            auto size = it->file_size(stat_error);
            if (!stat_error) bytes += size;
        }
        CheckCacheRead(it->path(), stat_error);
    }
    CheckCacheRead(path, ec);
    return bytes;
}

inline constexpr string_view RuntimeCachePrefix = "v1-";
inline constexpr string_view NativeCachePrefix = "v1-";
inline constexpr string_view NativeOutputDir = "output";
inline constexpr string_view NativeDependencyDir = "dependencies";
inline constexpr string_view NativeRuntimeMarker = "aot-runtime-dependencies";

inline bool IsManagedEntry(string_view name, string_view prefix) {
    return name.starts_with(prefix) && name.size() == prefix.size() + 64 &&
           name.substr(prefix.size()).find_first_not_of("0123456789abcdef") ==
               string_view::npos;
}

inline string ArtifactLeasePath(const filesystem::path &entry) {
    return entry.string() + ".lock";
}

// CMake's FetchContent keeps source trees alongside disposable build and
// population directories. Retain the sources so the next configure can
// rebuild offline.
template<typename F> inline void EachNativeDependencyOutput(const filesystem::path &entry, F visit) {
    auto deps = entry / NativeDependencyDir;
    if (!CacheDirectory(deps)) return;
    std::error_code ec;
    for (filesystem::directory_iterator it(deps, ec), end; !ec && it != end; it.increment(ec)) {
        auto path = it->path();
        if (path.filename().string().ends_with("-src") && CacheDirectory(path)) continue;
        visit(path);
    }
    CheckCacheRead(deps, ec);
}

inline uintmax_t NativeCacheBytes(const filesystem::path &entry) {
    auto output = entry / NativeOutputDir;
    if (PathExists((output / NativeRuntimeMarker).string())) return 0;
    auto bytes = CacheBytes(output);
    EachNativeDependencyOutput(entry, [&](const auto &path) {
        std::error_code ec;
        if (filesystem::is_regular_file(filesystem::symlink_status(path, ec))) {
            auto size = filesystem::file_size(path, ec);
            if (!ec) bytes += size;
        } else if (!ec) {
            bytes += CacheBytes(path);
        }
        CheckCacheRead(path, ec);
    });
    return bytes;
}

enum class CacheKind { Runtime, Native };

struct CacheEntry {
    filesystem::path path;
    filesystem::file_time_type written;
    uintmax_t bytes;
    CacheKind kind;
};

inline void ReadCacheEntries(vector<CacheEntry> &entries, const filesystem::path &root,
                             CacheKind kind) {
    if (!CacheDirectory(root.parent_path()) || !CacheDirectory(root)) return;
    auto native = kind == CacheKind::Native;
    std::error_code ec;
    for (filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        auto path = it->path();
        if (!IsManagedEntry(path.filename().string(), native ? NativeCachePrefix : RuntimeCachePrefix) ||
            !CacheDirectory(path)) continue;
        auto bytes = native ? NativeCacheBytes(path) : CacheBytes(path);
        if (!bytes) continue;
        std::error_code time_error;
        auto output = path / NativeOutputDir;
        auto written = filesystem::last_write_time(native && CacheDirectory(output) ? output : path,
                                                   time_error);
        CheckCacheRead(path, time_error);
        if (!time_error) entries.push_back({ path, written, bytes, kind });
    }
    CheckCacheRead(root, ec);
}

inline vector<CacheEntry> ManagedCacheEntries(const filesystem::path &build) {
    vector<CacheEntry> entries;
    ReadCacheEntries(entries, build / "runtime", CacheKind::Runtime);
    ReadCacheEntries(entries, build / "native", CacheKind::Native);
    return entries;
}

inline void CacheByteMember(JsonOut &out, string_view key, uintmax_t bytes) {
    out.Key(key);
    out.Raw(to_string(bytes));
}

inline string CacheStats(const string &home, bool json) {
    filesystem::path root(home);
    bool has_build = CacheDirectory(root / "build");
    const pair<const char *, uintmax_t> groups[] = {
        { "git_checkouts", CacheBytes(root / "pkg") },
        { "native_packages", has_build ? CacheBytes(root / "build/pkg") +
                                         CacheBytes(root / "build/native") : 0 },
        { "bundled_packages", has_build ? CacheBytes(root / "build/builtin") : 0 },
        { "runtime", has_build ? CacheBytes(root / "build/runtime") : 0 },
        { "project_builds", has_build ? CacheBytes(root / "build/prog") : 0 },
        { "toolchain", CacheBytes(root / "toolchain") }
    };
    uintmax_t runtime = 0, native = 0;
    for (const auto &entry : ManagedCacheEntries(root / "build"))
        (entry.kind == CacheKind::Native ? native : runtime) += entry.bytes;
    uintmax_t total = 0;
    JsonOut out;
    string report;
    if (json) {
        out.BeginObject();
        out.Member("home", home);
        out.Key("groups_bytes");
        out.BeginObject();
    } else {
        Append(report, "K_HOME ", home, "\n");
    }
    for (const auto &[group, bytes] : groups) {
        total += bytes;
        if (json) CacheByteMember(out, group, bytes);
        else Append(report, group, " ", bytes, " bytes\n");
    }
    if (!json) return cat(report, "total ", total, " bytes\nmanaged runtime ", runtime,
                          " bytes\nmanaged native packages ", native, " bytes\n");
    out.EndObject();
    CacheByteMember(out, "total_bytes", total);
    CacheByteMember(out, "managed_runtime_bytes", runtime);
    CacheByteMember(out, "managed_native_bytes", native);
    out.EndObject();
    return out.s + '\n';
}

inline void RemoveRuntimeEntry(const filesystem::path &path) {
    std::error_code ec;
    // Unpublish the object before removing compiler side files. An
    // interrupted prune then leaves an entry that is safe to rebuild.
    for (auto config : { "release", "debug" }) {
        auto dir = path / config;
        if (CacheDirectory(dir)) {
            for (auto name : { "k_runtime.o", "k_runtime.obj" }) {
                filesystem::remove(dir / name, ec);
                if (ec) throw CompileError { cat("cannot remove ", (dir / name).string(), ": ", ec.message()) };
            }
        }
    }
    filesystem::remove_all(path, ec);
    if (ec) throw CompileError { cat("cannot remove ", path.string(), ": ", ec.message()) };
}

inline void RemoveNativeEntry(const filesystem::path &entry) {
    auto output = entry / NativeOutputDir;
    std::error_code ec;
    if (CacheDirectory(output)) {
        filesystem::remove(output / "stamp", ec);
        if (ec) throw CompileError { cat("cannot unpublish ", output.string(), ": ", ec.message()) };
    }
    auto remove = [&](const filesystem::path &path) {
        filesystem::remove_all(path, ec);
        if (ec) throw CompileError { cat("cannot remove ", path.string(), ": ", ec.message()) };
    };
    remove(output);
    vector<filesystem::path> outputs;
    EachNativeDependencyOutput(entry, [&](const auto &path) { outputs.push_back(path); });
    for (const auto &path : outputs) remove(path);
    // Empty entries have no sources to retain. The lease file stays beside
    // the directory, including when the next configuration recreates it.
    filesystem::remove(entry / NativeDependencyDir, ec);
    if (ec && ec != std::errc::directory_not_empty)
        throw CompileError { cat("cannot remove ", entry.string(), ": ", ec.message()) };
    filesystem::remove(entry, ec);
    if (ec && ec != std::errc::directory_not_empty)
        throw CompileError { cat("cannot remove ", entry.string(), ": ", ec.message()) };
}

inline string PruneArtifactCache(const string &home, uintmax_t budget, bool json) {
    auto build = filesystem::path(home) / "build";
    uintmax_t before = 0, remaining = 0, reclaimed = 0, in_use = 0;
    if (CacheDirectory(build) && (CacheDirectory(build / "runtime") || CacheDirectory(build / "native"))) {
        FileLock prune((build / "cache-prune.lock").string(), "waiting for another cache prune");
        auto entries = ManagedCacheEntries(build);
        sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            return tie(a.written, a.path) < tie(b.written, b.path);
        });
        for (const auto &entry : entries) before += entry.bytes;
        auto estimated = before;
        for (const auto &entry : entries) {
            if (estimated <= budget) break;
            FileLock lease(ArtifactLeasePath(entry.path), "", FileLock::Mode::TryExclusive);
            auto native = entry.kind == CacheKind::Native;
            auto bytes = native ? NativeCacheBytes(entry.path) : CacheBytes(entry.path);
            if (!bytes) {
                estimated -= entry.bytes;
                continue;
            }
            if (!lease) {
                in_use += bytes;
                continue;
            }
            if (native) RemoveNativeEntry(entry.path);
            else RemoveRuntimeEntry(entry.path);
            reclaimed += bytes;
            estimated -= entry.bytes;
        }
        for (const auto &entry : ManagedCacheEntries(build)) remaining += entry.bytes;
    }
    if (!json)
        return cat("managed artifacts ", before, " -> ", remaining, " bytes\n",
                   "reclaimed ", reclaimed, " bytes, skipped in-use ", in_use, " bytes\n",
                   "budget ", budget, " bytes ", remaining <= budget ? "met\n" : "exceeded\n");
    JsonOut out;
    out.BeginObject();
    out.Member("home", home);
    CacheByteMember(out, "budget_bytes", budget);
    CacheByteMember(out, "before_bytes", before);
    CacheByteMember(out, "remaining_bytes", remaining);
    CacheByteMember(out, "reclaimed_bytes", reclaimed);
    CacheByteMember(out, "skipped_in_use_bytes", in_use);
    out.Member("budget_met", remaining <= budget);
    out.EndObject();
    return out.s + '\n';
}

}  // namespace klang
