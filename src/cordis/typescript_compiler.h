// Pinned TypeScript compiler frontend shared by the optional Cordis adapters.
#pragma once

#include "typescript_engine.h"
#include "../includes.h"
#include "../utils.h"
#include "../json.h"

namespace klang::cordis::typescript {

inline constexpr string_view transform_script = R"JS(
(function (source, file, esm) {
    const result = ts.transpileModule(source, {
        fileName: file,
        reportDiagnostics: true,
        compilerOptions: {
            target: ts.ScriptTarget.ES2022,
            module: esm ? ts.ModuleKind.ESNext : ts.ModuleKind.CommonJS,
            verbatimModuleSyntax: esm,
            sourceMap: true,
            inlineSources: true
        }
    });
    const issue = result.diagnostics.find(d => d.category === ts.DiagnosticCategory.Error);
    if (issue) {
        return JSON.stringify({
            error: ts.flattenDiagnosticMessageText(issue.messageText, "\n"),
            offset: issue.start === undefined ? 0 : issue.start
        });
    }
    return JSON.stringify({ javascript: result.outputText, source_map: result.sourceMapText });
})
)JS";

inline size_t ByteOffset(string_view text, size_t units) {
    size_t pos = 0;
    while (units && pos < text.size()) {
        size_t bytes = 1, width = 1;
        if (static_cast<unsigned char>(text[pos]) >= 128) {
            auto character = Utf8CharAt(text.data() + pos, text.data() + text.size());
            if (!character.empty()) {
                bytes = character.size();
                width = bytes == 4 ? 2 : 1;
            }
        }
        if (units < width) break;
        units -= width;
        pos += bytes;
    }
    return pos;
}

class Transpiler {
    Engine engine;
    optional<Value> transform;

public:
    void Load(string_view contents) {
        auto *ctx = engine.context;
        if (!ctx) throw CompileError { "cannot create the TypeScript compiler context" };
        string script(contents);
        Value loaded(ctx, JS_Eval(ctx, script.data(), script.size(), "typescript.js", JS_EVAL_TYPE_GLOBAL));
        if (loaded.Failed()) throw CompileError { cat("cannot load TypeScript compiler: ", Exception(ctx)) };
        transform.emplace(ctx, JS_Eval(ctx, transform_script.data(), transform_script.size(),
                                       "cordis-typescript-transform.js", JS_EVAL_TYPE_GLOBAL));
        if (transform->Failed())
            throw CompileError { cat("cannot load TypeScript transform: ", Exception(ctx)) };
    }

    // Keep the JSON packet intact for the embedded runtime bridge.
    string Compile(string_view source, string_view name, bool esm) {
        auto *ctx = engine.context;
        auto script = String(ctx, source);
        auto file = String(ctx, name);
        if (script.Failed() || file.Failed())
            throw CompileError { cat("cannot allocate TypeScript compiler inputs: ", Exception(ctx)) };
        JSValueConst args[] = { script.value, file.value, JS_NewBool(ctx, esm) };
        Value emitted(ctx, JS_Call(ctx, transform->value, JS_UNDEFINED, 3, args));
        if (emitted.Failed())
            throw CompileError { cat("TypeScript transform failed: ", Exception(ctx)) };
        auto packet = Text(ctx, emitted.value);
        if (JS_HasException(ctx))
            throw CompileError { cat("cannot read TypeScript transform output: ", Exception(ctx)) };
        return packet;
    }
};

inline size_t ErrorOffset(const Json &result, string_view source) {
    const auto *offset = result.Get("offset");
    if (!offset || offset->kind != Json::NUMBER || offset->num < 0 || offset->num > source.size()) return 0;
    return ByteOffset(source, static_cast<size_t>(offset->num));
}

} // namespace klang::cordis::typescript
