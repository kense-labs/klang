#pragma once

#include <cstdint>
#include <optional>

namespace klang::cordis {

struct MemoryBoundary {
    bool group_visible = false;
    std::optional<uint64_t> memory_max;
    bool swap_disabled = false;
};

MemoryBoundary ProcessMemoryBoundary();

}  // namespace klang::cordis
