// Shared fenced syntax, typed wrappers and source maps for optional Cordis guests.
#pragma once

#include "source_transform.h"
#include "guest_call.h"

#include "../includes.h"
#include "../utils.h"

namespace klang::cordis::guest {

struct Profile {
    string_view language, title, quoted_language, reserved, module, extension;
};

struct Failure {
    size_t offset;
    string message;
};

inline bool WordStart(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}
inline bool WordPart(char ch) { return WordStart(ch) || (ch >= '0' && ch <= '9'); }
inline bool Space(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

struct Binding {
    string name;
    Kind kind;
    size_t start, end;
};

struct Block {
    size_t start, end, body;
    string_view code;
    vector<Binding> bindings;
    Kind result;
};

struct Reader {
    string_view text;
    const Profile &profile;
    size_t pos = 0;

    char Next() const { return pos < text.size() ? text[pos] : '\0'; }
    bool At(string_view needle) const { return text.substr(pos).starts_with(needle); }
    bool Take(string_view needle) {
        if (!At(needle)) return false;
        pos += needle.size();
        return true;
    }
    [[noreturn]] void Error(string message) const { throw Failure { pos, std::move(message) }; }

    bool Comment() {
        if (Take("//")) {
            while (pos < text.size() && Next() != '\n' && Next() != '\r') ++pos;
            return true;
        }
        if (!Take("/*")) return false;
        for (size_t depth = 1; depth && pos < text.size();) {
            if (Take("/*")) ++depth;
            else if (Take("*/")) --depth;
            else ++pos;
        }
        return true;
    }

    bool Literal() {
        if (Take("\"\"\"")) {
            auto end = text.find("\"\"\"", pos);
            pos = end == string_view::npos ? text.size() : end + 3;
            return true;
        }
        char quote = Next();
        if (quote != '"' && quote != '\'') return false;
        ++pos;
        while (pos < text.size()) {
            char ch = text[pos++];
            if (ch == '\\' && pos < text.size()) ++pos;
            else if (ch == quote || ch == '\n') break;
        }
        return true;
    }

    void Trivia() {
        while (pos < text.size()) {
            if (Space(Next())) ++pos;
            else if (!Comment()) break;
        }
    }

    void Need(string_view token) {
        Trivia();
        if (!Take(token)) Error(cat("expected ", token, " in ", profile.title, " using header"));
    }

    string_view Word() {
        auto start = pos;
        if (WordStart(Next())) {
            ++pos;
            while (WordPart(Next())) ++pos;
        }
        return text.substr(start, pos - start);
    }

    Kind Type() {
        Trivia();
        auto start = pos;
        auto name = Word();
        if (name == "i64") return Kind::Integer;
        if (name == "text") return Kind::Text;
        if (name == "bytes") return Kind::Bytes;
        throw Failure { start, cat("expected i64, text or bytes in ", profile.title, " using header") };
    }

    size_t ArgumentEnd() {
        string pending;
        while (pos < text.size()) {
            if (Comment() || Literal()) continue;
            char next = Next();
            if (pending.empty() && (next == ')' || next == ',')) return pos;
            if (next == '(') pending += ')';
            else if (next == '[') pending += ']';
            else if (next == '{') pending += '}';
            else if (next == ')' || next == ']' || next == '}') {
                if (pending.empty() || pending.back() != next) Error(cat("unbalanced ", profile.title, " binding expression"));
                pending.pop_back();
            }
            ++pos;
        }
        Error(cat("unterminated ", profile.title, " binding expression"));
    }

    Block Guest(size_t start) {
        Block block { start, 0, 0, {}, {}, Kind::Integer };
        Need("(");
        Trivia();
        set<string> names;
        while (!Take(")")) {
            auto name_start = pos;
            auto name = string(Word());
            if (name.empty()) Error(cat("expected a ", profile.title, " binding name"));
            if (name == "answer" || name == profile.reserved || !names.insert(name).second)
                throw Failure { name_start, cat("invalid or duplicate ", profile.title, " binding ", name) };
            Need(":");
            auto kind = Type();
            Need("=");
            Trivia();
            auto expression = pos;
            auto end = ArgumentEnd();
            if (end == expression) Error(cat("expected a K expression for the ", profile.title, " binding"));
            block.bindings.push_back({ std::move(name), kind, expression, end });
            if (Take(",")) Trivia();
            else if (!At(")")) Error("expected comma or closing parenthesis");
        }
        Need("->");
        block.result = Type();
        Need("{|");
        while (Next() == ' ' || Next() == '\t' || Next() == '\r') ++pos;
        if (!Take("\n")) Error(cat("expected a newline after the ", profile.title, " opening fence"));
        block.body = pos;
        while (pos < text.size()) {
            size_t line = pos;
            while (Next() == ' ' || Next() == '\t') ++pos;
            if (Take("|}")) {
                block.code = text.substr(block.body, line - block.body);
                block.end = pos;
                return block;
            }
            auto end = text.find('\n', pos);
            if (end == string_view::npos) break;
            pos = end + 1;
        }
        throw Failure { start, cat("missing closing ", profile.title, " fence |}") };
    }

    vector<Block> Scan() {
        vector<Block> blocks;
        while (pos < text.size()) {
            if (Comment() || Literal()) continue;
            if (!WordStart(Next())) { ++pos; continue; }
            // A token can have digits before a suffix. Consume that token's
            // complete word before deciding whether this is the extension.
            auto start = pos;
            auto name = Word();
            if (name != "using" || (start && WordPart(text[start - 1]))) continue;
            auto resume = pos;
            Trivia();
            if (Take(profile.quoted_language)) blocks.push_back(Guest(start));
            else pos = resume;
        }
        return blocks;
    }
};

inline k_cordis_text Text(string_view text) { return { text.data(), text.size() }; }

inline string_view View(k_cordis_text text) {
    return text.size ? string_view(text.data, static_cast<size_t>(text.size)) : string_view();
}

inline pair<int32_t, int32_t> Position(string_view text, size_t offset) {
    int32_t line = 1, column = 1;
    for (char ch : text.substr(0, offset)) {
        if (ch == '\n') ++line, column = 1;
        else ++column;
    }
    return { line, column };
}

inline string Quoted(string_view value) {
    constexpr string_view hex = "0123456789abcdef";
    string out = "\"";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') Append(out, '\\', static_cast<char>(ch));
        else if (ch == '\n') out += "\\n";
        else if (ch >= 32 && ch <= 126) out += static_cast<char>(ch);
        else Append(out, "\\x", hex[ch >> 4], hex[ch & 15]);
    }
    return out + '"';
}

inline string ModulePath(string_view path) {
    size_t dot = path.find_last_of('.');
    auto slash = path.find_last_of('/');
    if (dot != string_view::npos && (slash == string_view::npos || dot > slash + 1) && dot != 0)
        path = path.substr(0, dot);
    return cat(path, ".k");
}

inline string RuntimePath(const Profile &profile) {
#if defined(_WIN32)
    return cat(profile.language, "/runtime.dll");
#elif defined(__APPLE__)
    return cat(profile.language, "/runtime.dylib");
#else
    return cat(profile.language, "/runtime.so");
#endif
}

inline string Bridge(const Profile &profile, string_view runtime_path) {
    string bridge = cat("namespace ", profile.module, ";\nextern \"assets/", runtime_path, "\" {\n");
    const pair<string_view, string_view> declarations[] = {
        { "new", "new() -> own Call" },
        { "valid", "valid(borrow call: Call) -> i32" },
        { "drop", "drop(call: own Call)" },
        { "integer", "integer(borrow call: Call, name: const u8[:], value: i64) -> i32" },
        { "slice", "slice(borrow call: Call, name: const u8[:], value: const u8[:], kind: i32) -> i32" },
        { "eval", "eval(borrow call: Call, code: const u8[:], file: const u8[:], line: i64, kind: i32) -> i32" },
        { "result_integer", "value(borrow call: Call) -> i64" },
        { "result_size", "size(borrow call: Call) -> i64" },
        { "result_copy", "copy(borrow call: Call, out: u8[:])" },
    };
    for (const auto &[symbol, declaration] : declarations)
        Append(bridge, "    fn bridge_", declaration, " = \"k_cordis_", profile.language, "_", symbol, "_v1\";\n");
    bridge += R"K(}
struct Call { token: u64 }

fn message(text: const u8[:]) -> u8[>..] {
    var copy: u8[>..] = [];
    copy.append(text);
    return copy;
}
)K";
    return bridge;
}

inline void Wrapper(string &bridge, const Profile &profile, const Block &block, size_t number,
                    string_view path, int32_t line, string_view program) {
    const bool integer = block.result == Kind::Integer;
    const auto empty = integer ? "0" : "[]";
    Append(bridge, "\nfn block_", number, '(');
    for (size_t i = 0; i < block.bindings.size(); ++i)
        Append(bridge, i ? ", " : "", "p", i, ": ",
               block.bindings[i].kind == Kind::Integer ? "i64" : "const u8[:]");
    Append(bridge, ") -> ", integer ? "i64" : "u8[>..]", ", u8[>..] {\n",
           "    let call = bridge_new();\n    defer bridge_drop(call);\n",
           "    if bridge_valid(call) == 0 { return ", empty, ", message(\"cannot allocate ",
           profile.title, " call\"); }\n");
    for (size_t i = 0; i < block.bindings.size(); ++i) {
        const auto &binding = block.bindings[i];
        Append(bridge, "    if ", binding.kind == Kind::Integer ? "bridge_integer" : "bridge_slice",
               "(call, ", Quoted(binding.name), ", p", i);
        if (binding.kind != Kind::Integer) Append(bridge, ", ", int(binding.kind));
        Append(bridge, ") == 0 { return ", empty, ", message(\"cannot copy ", profile.title, " binding\"); }\n");
    }
    Append(bridge, "    let status = bridge_eval(call, ", Quoted(program), ", ", Quoted(path), ", ",
           line, ", ", int(block.result), ");\n",
           "    if status < 0 { return ", empty, ", message(\"", profile.title, " bridge failed\"); }\n",
           "    var payload: u8[>..] = [];\n    payload.resize(bridge_size(call), 0);\n",
           "    bridge_copy(call, payload[..]);\n",
           "    if status == 0 { return ", empty, ", payload; }\n",
           "    return ", integer ? "bridge_value(call)" : "payload", ", [];\n}\n");
}

struct File {
    string path, text;
};

struct Diagnostic {
    string error;
    int32_t file = -1, line = 0, column = 0;

    int32_t Report(k_cordis_emit_diagnostic_v1 emit, void *sink) const {
        if (error.empty()) return 0;
        const k_cordis_diagnostic_v1 issue { file, line, column, 0, 0, K_CORDIS_ERROR, Text(error) };
        return emit(sink, &issue);
    }
};

inline string_view Tracked(const k_cordis_source_request_v1 &request, string_view name) {
    for (size_t i = 0; i < request.input_count; ++i)
        if (View(request.inputs[i].path) == name) return View(request.inputs[i].contents);
    return {};
}

template<typename Compile>
int32_t Lower(const Profile &profile, Diagnostic &diagnostic, const k_cordis_source_request_v1 *request,
              k_cordis_emit_source_v1 emit, void *sink, Compile compile) {
    diagnostic.error.clear();
    diagnostic.file = -1;
    size_t index = 0;
    try {
        auto native = Tracked(*request, "runtime");
        auto runtime_path = RuntimePath(profile);
        vector<File> modules, guests;
        vector<k_cordis_source_span_v1> spans;
        modules.reserve(request->source_count + 1);
        string bridge;
        size_t blocks_emitted = 0;
        for (; index < request->source_count; ++index) {
            auto path = string(View(request->sources[index].path));
            auto source = View(request->sources[index].contents);
            auto blocks = Reader{source, profile}.Scan();
            auto &out = modules.emplace_back(File{ModulePath(path), {}});
            auto copy = [&](size_t start, size_t end) {
                if (end == start) return;
                spans.push_back({ Text(out.path), out.text.size(), index, start, end - start });
                out.text += source.substr(start, end - start);
            };
            if (!blocks.empty()) Append(out.text, "import ", profile.module, ";\n");
            size_t rest = 0;
            for (const auto &block : blocks) {
                if (native.empty()) throw Failure { block.start, cat(profile.title, " requires the tracked runtime input") };
                auto number = blocks_emitted++;
                auto prefix = cat(profile.language, "/block-", number);
                auto program = compile(block, source, path, prefix, guests);
                if (bridge.empty()) bridge = Bridge(profile, runtime_path);
                Wrapper(bridge, profile, block, number, path, Position(source, block.body).first, program);
                guests.push_back({ cat(prefix, profile.extension), string(block.code) });
                copy(rest, block.start);
                spans.push_back({ Text(out.path), out.text.size(), index, block.start, 5 });
                Append(out.text, profile.module, "::block_", number, '(');
                for (size_t arg = 0; arg < block.bindings.size(); ++arg) {
                    if (arg) out.text += ", ";
                    copy(block.bindings[arg].start, block.bindings[arg].end);
                }
                out.text += ')';
                rest = block.end;
            }
            copy(rest, source.size());
        }
        vector<k_cordis_source_v1> files, artifacts;
        if (!bridge.empty()) {
            modules.push_back({ cat(profile.module, ".k"), std::move(bridge) });
            artifacts.push_back({ Text(runtime_path), Text(native) });
            for (const auto &file : guests) artifacts.push_back({ Text(file.path), Text(file.text) });
        }
        for (const auto &module : modules) files.push_back({ Text(module.path), Text(module.text) });
        if (files.empty()) return 1;
        const k_cordis_source_result_v1 result { files.front().path, files.data(), artifacts.data(),
            files.size(), artifacts.size(), spans.data(), spans.size() };
        return emit(sink, &result) ? 0 : 1;
    } catch (Failure &failure) {
        diagnostic.file = static_cast<int32_t>(index);
        tie(diagnostic.line, diagnostic.column) = Position(View(request->sources[index].contents), failure.offset);
        diagnostic.error = std::move(failure.message);
    } catch (...) {
        // The C ABI never carries a native exception into the host.
        return 1;
    }
    return 1;
}

} // namespace klang::cordis::guest
