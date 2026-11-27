// Line-level positions for QuickJS stack frames, mapped through a v3 source map.
#pragma once

#include "../includes.h"
#include "../utils.h"

namespace klang::cordis::typescript {

inline constexpr string_view digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Decode the first mapped original line on each generated line of a
// single-source v3 source map, for line-level stack locations.
inline vector<int64_t> OriginLines(string_view mappings) {
    vector<int64_t> lines(1, -1);
    int64_t original = 0, source = 0;
    size_t pos = 0;
    while (pos < mappings.size()) {
        if (mappings[pos] == ';') {
            lines.push_back(-1);
            ++pos;
            continue;
        }
        int64_t values[5] {};
        size_t count = 0;
        while (pos < mappings.size() && mappings[pos] != ',' && mappings[pos] != ';') {
            if (count == size(values)) return {};
            uint64_t bits = 0;
            unsigned shift = 0;
            for (;;) {
                if (pos >= mappings.size() || shift >= 35) return {};
                auto digit = digits.find(mappings[pos++]);
                if (digit == string_view::npos) return {};
                bits |= static_cast<uint64_t>(digit & 31) << shift;
                if (!(digit & 32)) break;
                shift += 5;
            }
            if (bits > UINT32_MAX) return {};
            auto magnitude = static_cast<int64_t>(bits >> 1);
            values[count++] = bits & 1 ? -magnitude : magnitude;
        }
        if (count != 1 && count != 4 && count != 5) return {};
        if (count >= 4) {
            source += values[1];
            original += values[2];
            if (source == 0 && original >= 0 && lines.back() < 0) lines.back() = original;
        }
        if (pos < mappings.size() && mappings[pos] == ',') ++pos;
    }
    return lines;
}

inline void Remap(string &error, string_view generated, string_view file, int64_t first,
                  const vector<int64_t> &lines) {
    auto marker = cat(" (", generated, ':');
    size_t pos = 0;
    while ((pos = error.find(marker, pos)) != string::npos) {
        const char *start = error.data() + pos + marker.size();
        const char *end = error.data() + error.size();
        size_t row = 0;
        auto read = from_chars(start, end, row);
        if (read.ec != errc{} || row < 1 || row > lines.size() || lines[row - 1] < 0) {
            pos += marker.size();
            continue;
        }
        auto *location_end = read.ptr;
        if (location_end != end && *location_end == ':') {
            size_t column = 0;
            auto generated_column = from_chars(location_end + 1, end, column);
            if (generated_column.ec == errc{}) location_end = generated_column.ptr;
        }
        size_t stop = location_end - error.data();
        auto label = cat(" (", file, ':', first + lines[row - 1]);
        error.replace(pos, stop - pos, label);
        pos += label.size();
    }
}

inline bool Utf8(string_view text) {
    for (size_t pos = 0; pos < text.size();) {
        if (static_cast<unsigned char>(text[pos]) < 0x80) {
            ++pos;
            continue;
        }
        auto character = Utf8CharAt(text.data() + pos, text.data() + text.size());
        if (character.empty()) return false;
        pos += character.size();
    }
    return true;
}

} // namespace klang::cordis::typescript
