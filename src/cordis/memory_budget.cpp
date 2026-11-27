#include "memory_budget.h"

#ifdef __linux__
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

#include <linux/magic.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif

namespace klang::cordis {

#ifdef __linux__
namespace {
using std::filesystem::path;

path MountPath(std::string_view field) {
    // The kernel escapes whitespace and backslashes in mountinfo paths.
    std::string decoded;
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size()) {
            const char *first = field.data() + i + 1;
            unsigned byte = 0;
            auto parsed = std::from_chars(first, first + 3, byte, 8);
            if (parsed.ec == std::errc() && parsed.ptr == first + 3 && byte && byte <= 255) {
                decoded += static_cast<char>(byte);
                i += 3;
                continue;
            }
        }
        decoded += field[i];
    }
    return decoded;
}

bool InGroup(const path &directory) {
    struct statfs info {};
    if (statfs(directory.c_str(), &info) != 0 || info.f_type != CGROUP2_SUPER_MAGIC) return false;
    std::ifstream members(directory / "cgroup.procs");
    for (pid_t member; members >> member;)
        if (member == getpid()) return true;
    return false;
}

std::optional<uint64_t> ReadLimit(const path &file) {
    std::ifstream input(file);
    std::string text;
    if (!(input >> text)) return {};
    uint64_t value;
    auto end = text.data() + text.size();
    auto parsed = std::from_chars(text.data(), end, value);
    if (parsed.ec != std::errc() || parsed.ptr != end) return {};
    return value;
}

void ReadBounds(MemoryBoundary &result, const path &leaf, const path &mount) {
    result.group_visible = true;
    for (auto group = leaf;; group = group.parent_path()) {
        auto maximum = ReadLimit(group / "memory.max");
        if (maximum && (!result.memory_max || *maximum < *result.memory_max)) result.memory_max = maximum;
        if (ReadLimit(group / "memory.swap.max") == 0) result.swap_disabled = true;
        if (group == mount) break;
    }
}
}  // namespace
#endif

MemoryBoundary ProcessMemoryBoundary() {
    MemoryBoundary result;
#ifdef __linux__
    std::ifstream groups("/proc/self/cgroup");
    path group;
    for (std::string line; std::getline(groups, line);) {
        if (!line.starts_with("0::")) continue;
        group = line.substr(3);
        break;
    }
    if (!group.is_absolute()) return result;
    for (const auto &part : group)
        if (part == "..") return result;
    group = group.lexically_normal();

    std::ifstream mounts("/proc/self/mountinfo");
    for (std::string line; std::getline(mounts, line);) {
        auto split = line.find(" - ");
        if (split == std::string::npos || !std::string_view(line).substr(split + 3).starts_with("cgroup2 "))
            continue;
        std::istringstream fields(line);
        std::string id, parent, device, root_field, mount_field;
        if (!(fields >> id >> parent >> device >> root_field >> mount_field)) continue;
        auto root = MountPath(root_field);
        auto mount = MountPath(mount_field);
        if (!root.is_absolute() || !mount.is_absolute()) continue;
        auto relative = group.lexically_relative(root);
        if (!relative.empty() && *relative.begin() != "..") {
            auto leaf = relative == "." ? mount : mount / relative;
            if (InGroup(leaf)) {
                ReadBounds(result, leaf, mount);
                continue;
            }
        }
        // A private cgroup namespace can name its root differently from
        // the mount's root. Confirm membership before trusting that view.
        auto leaf = group == "/" ? mount : mount / group.relative_path();
        if (InGroup(leaf)) ReadBounds(result, leaf, mount);
    }
#endif
    return result;
}

}  // namespace klang::cordis
