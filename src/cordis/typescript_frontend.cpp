// Cordis frontend service for TypeScript project modules.
#include "typescript_project.h"
#include "typescript_compiler.h"

namespace klang::cordis::typescript {
namespace {

k_cordis_text Slice(string_view text) { return { text.data(), text.size() }; }
string_view View(k_cordis_text text) { return text.size ? string_view(text.data, text.size) : string_view(); }

int32_t Start(void **state) {
    *state = new (nothrow) Transpiler;
    return *state ? 0 : 1;
}

void Stop(void *state) { delete static_cast<Transpiler *>(state); }

int32_t Configure(void *state, k_cordis_text compiler, k_cordis_ts_emit_text_v1 error, void *sink) {
    try {
        static_cast<Transpiler *>(state)->Load(View(compiler));
        return 0;
    } catch (const CompileError &failure) {
        error(sink, Slice(failure.msg));
    } catch (...) {
        error(sink, Slice("cannot initialize the TypeScript frontend"));
    }
    return 1;
}

string LocatedError(string_view path, string_view source, size_t offset, string_view error) {
    size_t line = 1, column = 1;
    for (char ch : source.substr(0, offset)) {
        if (ch == '\n') ++line, column = 1;
        else ++column;
    }
    return cat(path, ':', line, ':', column, ": TypeScript: ", error);
}

int32_t Compile(void *state, k_cordis_source_v1 module, k_cordis_ts_emit_module_v1 emit, void *sink) {
    string diagnostic;
    try {
        auto path = View(module.path), source = View(module.contents);
        auto packet = static_cast<Transpiler *>(state)->Compile(source, path, true);
        auto result = ParseJson(packet);
        if (auto error = result.Str("error"); !error.empty()) {
            diagnostic = LocatedError(path, source, ErrorOffset(result, source), error);
        } else {
            auto script = result.Str("javascript"), map = result.Str("source_map");
            const k_cordis_ts_compiled_v1 out { Slice(script), Slice(map), {} };
            return emit(sink, &out) ? 0 : 1;
        }
    } catch (const CompileError &failure) {
        const k_cordis_ts_compiled_v1 out { {}, {}, Slice(failure.msg) };
        return emit(sink, &out) ? 0 : 1;
    } catch (...) {
        return 1;
    }
    const k_cordis_ts_compiled_v1 out { {}, {}, Slice(diagnostic) };
    return emit(sink, &out) ? 0 : 1;
}

} // namespace
} // namespace klang::cordis::typescript

extern "C" K_CORDIS_EXPORT const k_cordis_typescript_frontend_v1_api *k_cordis_typescript_frontend_v1() {
    using namespace klang::cordis::typescript;
    static const k_cordis_typescript_frontend_v1_api api {
        K_CORDIS_TYPESCRIPT_ABI, sizeof(api), Start, Stop, Configure, Compile
    };
    return &api;
}
