// K host paths shared by the compiler, package resolver, and workspace.
#pragma once

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace klang {

inline std::string DirOf(const std::string &path) {
    auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? "" : path.substr(0, pos + 1);
}

inline bool FileExists(const std::string &path) {
    auto f = std::fopen(path.c_str(), "rb");
    if (f) std::fclose(f);
    return f != nullptr;
}

// A path split on either separator, with "." and cancelled "x/.." dropped. A
// path with a leading separator keeps a leading empty component ("/a" ->
// {"", "a"}), so two rooted paths share it and a rooted and a relative one
// never do; "" and "./" are the relative {}.
inline std::vector<std::string> PathParts(const std::string &path) {
    std::vector<std::string> parts;
    std::string cur;
    for (std::size_t i = 0; i <= path.size(); i++) {
        auto c = i < path.size() ? path[i] : '/';
        if (c != '/' && c != '\\') { cur += c; continue; }
        if (i == 0 && i < path.size()) parts.push_back(cur);
        else if (cur == "." || cur.empty()) {}
        else if (cur == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
        else parts.push_back(cur);
        cur.clear();
    }
    return parts;
}

// `path` as reached from `dir`. Returns it unchanged where no relative path
// exists or one cannot be computed by name alone: an absolute path against a
// relative directory or the other way round, two absolute paths under
// different Windows drives, or a leftover ".." that only the filesystem could
// resolve.
inline std::string RelativeTo(const std::string &path, const std::string &dir) {
    auto p = PathParts(path), d = PathParts(dir);
    if (p.empty()) return path;
    auto updir = [](const std::vector<std::string> &parts) {
        for (auto &part : parts) if (part == "..") return true;
        return false;
    };
    if (updir(p) || updir(d)) return path;
    // A leading "" (rooted) or "C:" (drive) both sit in the first component.
    auto rooted = [](const std::vector<std::string> &parts) {
        return !parts.empty() && (parts[0].empty() || parts[0].back() == ':');
    };
    if (rooted(p) != rooted(d)) return path;
    if (rooted(p) && p[0] != d[0]) return path;
    // The last component of `path` is the file itself, never a shared directory.
    std::size_t i = 0;
    while (i + 1 < p.size() && i < d.size() && p[i] == d[i]) i++;
    std::string rel;
    for (std::size_t up = i; up < d.size(); up++) rel += "../";
    for (; i < p.size(); i++) {
        rel += p[i];
        if (i + 1 < p.size()) rel += '/';
    }
    return rel;
}

inline std::string AbsolutePath(const std::string &path) {
    std::error_code ec;
    auto abs = std::filesystem::absolute(std::filesystem::path(path), ec);
    return ec ? path : abs.lexically_normal().string();
}

}  // namespace klang
