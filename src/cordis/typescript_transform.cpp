// The TypeScript compiler is a locked JavaScript input evaluated by QuickJS.
#include "source_guest.h"
#include "typescript_compiler.h"
#include <quickjs_license.h>

namespace klang::cordis::typescript {
namespace {
using namespace guest;

constexpr Profile profile {
    "typescript", "TypeScript", "\"typescript\"", "exports", "cordis_typescript_guest_v1", ".ts"
};

struct Compiler {
    optional<Transpiler> frontend;

    string Compile(const Block &block, string_view compiler, string_view prefix, vector<File> &files) {
        try {
            if (!frontend) {
                if (compiler.empty()) throw CompileError { "TypeScript requires the tracked compiler input" };
                frontend.emplace().Load(compiler);
                files.push_back({ "typescript/quickjs-license.txt", quickjs_license });
            }
            auto file = cat(prefix.substr(prefix.find_last_of('/') + 1), ".ts");
            auto packet = frontend->Compile(block.code, file, false);
            auto result = ParseJson(packet);
            if (auto message = result.Str("error"); !message.empty())
                throw Failure { block.body + ErrorOffset(result, block.code), cat("TypeScript: ", message) };
            files.push_back({ cat(prefix, ".js"), result.Str("javascript") });
            files.push_back({ cat(prefix, ".js.map"), result.Str("source_map") });
            return packet;
        } catch (const CompileError &error) {
            throw Failure { block.start, error.msg };
        }
    }
};

int32_t Start(void **state) {
    *state = new (nothrow) Diagnostic;
    return *state ? 0 : 1;
}

void Stop(void *state) { delete static_cast<Diagnostic *>(state); }

int32_t LastError(void *state, k_cordis_emit_diagnostic_v1 emit, void *sink) {
    return static_cast<Diagnostic *>(state)->Report(emit, sink);
}

int32_t Transform(void *state, const k_cordis_source_request_v1 *request, k_cordis_emit_source_v1 emit, void *sink) {
    Compiler compiler;
    return Lower(profile, *static_cast<Diagnostic *>(state), request, emit, sink,
                 [&](const Block &block, string_view, const string &, string_view prefix, vector<File> &files) {
                     return compiler.Compile(block, Tracked(*request, "compiler"), prefix, files);
                 });
}
} // namespace
} // namespace klang::cordis::typescript

extern "C" K_CORDIS_EXPORT const k_cordis_source_transform_v1_api *k_cordis_source_transform_v1() {
    using namespace klang::cordis::typescript;
    static const k_cordis_source_transform_v1_api api {
        K_CORDIS_SOURCE_TRANSFORM_ABI, sizeof(api), Start, Stop, Transform, LastError
    };
    return &api;
}
