// Private C-compatible call storage shared by the optional guest bridges.
#pragma once

#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace klang::cordis::guest {

enum class Kind { Integer = 0, Text = 1, Bytes = 2 };

struct Slice { const char *data; int64_t len; };
struct Buffer { char *data; int64_t len; };
struct Handle { uint64_t token; };

struct Input {
    std::string name;
    Kind kind;
    int64_t number = 0;
    std::string bytes;
};

struct Call {
    std::vector<Input> inputs;
    int64_t number = 0;
    std::string result;
};

inline bool Valid(Slice text) {
    return text.len >= 0 && static_cast<uint64_t>(text.len) <= std::string().max_size() && (text.data || !text.len);
}

inline std::string_view View(Slice text) {
    return text.len ? std::string_view(text.data, static_cast<size_t>(text.len)) : std::string_view();
}

inline Call *Pointer(Handle handle) { return reinterpret_cast<Call *>(static_cast<uintptr_t>(handle.token)); }
inline Handle New() { return { reinterpret_cast<uintptr_t>(new (std::nothrow) Call) }; }
inline void Drop(Handle handle) { delete Pointer(handle); }

inline int32_t AddInteger(Handle handle, Slice name, int64_t value) {
    try {
        if (!handle.token || !Valid(name)) return 0;
        Pointer(handle)->inputs.push_back({ std::string(View(name)), Kind::Integer, value, {} });
        return 1;
    } catch (...) {
        return 0;
    }
}

inline int32_t AddSlice(Handle handle, Slice name, Slice value, int32_t kind) {
    try {
        if (!handle.token || !Valid(name) || !Valid(value) || (kind != int(Kind::Text) && kind != int(Kind::Bytes)))
            return 0;
        Pointer(handle)->inputs.push_back({ std::string(View(name)), static_cast<Kind>(kind), 0, std::string(View(value)) });
        return 1;
    } catch (...) {
        return 0;
    }
}

inline int64_t Integer(Handle handle) { return Pointer(handle)->number; }
inline int64_t Size(Handle handle) { return Pointer(handle)->result.size(); }

inline void Copy(Handle handle, Buffer out) {
    const auto &value = Pointer(handle)->result;
    if (out.data && out.len >= 0 && static_cast<uint64_t>(out.len) == value.size() && !value.empty())
        std::memcpy(out.data, value.data(), value.size());
}

} // namespace klang::cordis::guest
