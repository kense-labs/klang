// QuickJS project engine. Files and output go through the Cordis runner.
#include "typescript_project.h"
#include "typescript_engine.h"
#include "typescript_sourcemap.h"
#include "../json.h"

#include <filesystem>
#include <list>

namespace klang::cordis::typescript {
namespace {

k_cordis_text Slice(string_view text) { return { text.data(), text.size() }; }
string_view View(k_cordis_text text) { return text.size ? string_view(text.data, text.size) : string_view(); }

struct Compiled {
    string script, source_map, error;
    bool emitted = false;

    static int32_t Emit(void *sink, const k_cordis_ts_compiled_v1 *result) {
        try {
            auto &self = *static_cast<Compiled *>(sink);
            if (self.emitted) return 0;
            self.script = View(result->javascript);
            self.source_map = View(result->source_map);
            self.error = View(result->error);
            self.emitted = true;
            return 1;
        } catch (...) {
            return 0;
        }
    }
};

class Project {
    const k_cordis_ts_project_request_v1 &host;
    Engine engine;
    map<string, vector<int64_t>> source_maps;
    list<pair<Value, Value>> rejections;
    bool lost_rejection = false;

    k_cordis_source_v1 Read(string_view name) const {
        k_cordis_source_v1 module {};
        k_cordis_text error {};
        if (host.read(host.host, Slice(name), &module, &error))
            throw CompileError { error.size ? string(View(error)) : cat("cannot read module ", name) };
        return module;
    }

    Value Compile(k_cordis_source_v1 module) {
        auto *ctx = engine.context;
        string path(View(module.path));
        auto code = View(module.contents);
        if (!Utf8(code)) throw CompileError { cat(path, ": module source is not UTF-8") };
        string script;
        if (path.ends_with(".ts") || path.ends_with(".mts")) {
            Compiled output;
            if (host.compile(host.host, module, Compiled::Emit, &output) || !output.emitted)
                throw CompileError { cat(path, ": the TypeScript frontend failed") };
            if (!output.error.empty()) throw CompileError { std::move(output.error) };
            auto map = ParseJson(output.source_map);
            source_maps.insert_or_assign(path, OriginLines(map.Str("mappings")));
            script = std::move(output.script);
        } else {
            script = code;
        }
        return { ctx, JS_Eval(ctx, script.data(), script.size(), path.c_str(),
                              JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY) };
    }

    string Mapped(string error) const {
        for (const auto &[path, lines] : source_maps) Remap(error, path, path, 1, lines);
        return error;
    }

    static char *Normalize(JSContext *ctx, const char *base, const char *specifier, void *opaque) {
        try {
            string_view name(specifier);
            if (!name.starts_with("./") && !name.starts_with("../")) {
                JS_ThrowReferenceError(ctx, "%s: unsupported module specifier '%s'", base, specifier);
                return nullptr;
            }
            auto path = (filesystem::path(base).parent_path() / specifier).lexically_normal().generic_string();
            auto &self = *static_cast<Project *>(opaque);
            auto canonical = View(self.Read(path).path);
            return js_strndup(ctx, canonical.data(), canonical.size());
        } catch (const CompileError &error) {
            JS_ThrowReferenceError(ctx, "%s: %s", base, error.msg.c_str());
        } catch (...) {
            JS_ThrowInternalError(ctx, "cannot resolve the requested module");
        }
        return nullptr;
    }

    static JSModuleDef *Load(JSContext *ctx, const char *name, void *opaque, JSValueConst) {
        try {
            auto &self = *static_cast<Project *>(opaque);
            auto module = self.Compile(self.Read(name));
            // The context retains the compiled module after the JSValue is freed.
            return module.Failed() ? nullptr : static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(module.value));
        } catch (const CompileError &error) {
            JS_ThrowSyntaxError(ctx, "%s", error.msg.c_str());
        } catch (...) {
            JS_ThrowInternalError(ctx, "cannot compile the requested module");
        }
        return nullptr;
    }

    static int CheckAttributes(JSContext *ctx, void *, JSValueConst attributes) {
        JSPropertyEnum *props;
        uint32_t count;
        if (JS_GetOwnPropertyNames(ctx, &props, &count, attributes, JS_GPN_STRING_MASK) < 0) return -1;
        JS_FreePropertyEnum(ctx, props, count);
        if (!count) return 0;
    JS_ThrowTypeError(ctx, "import attributes are unavailable in this Cordis profile");
        return -1;
    }

    static JSValue Console(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv, int stream) {
        try {
            auto &self = *static_cast<Project *>(JS_GetContextOpaque(ctx));
            string line;
            for (int i = 0; i < argc; ++i) {
                if (i) line += ' ';
                line += Text(ctx, argv[i]);
                if (JS_HasException(ctx)) return JS_EXCEPTION;
            }
            line += '\n';
            if (self.host.write(self.host.host, stream, Slice(line))) return JS_UNDEFINED;
            return JS_ThrowInternalError(ctx, "cannot write console output");
        } catch (...) {
            return JS_ThrowInternalError(ctx, "cannot format console output");
        }
    }

    static void TrackRejection(JSContext *ctx, JSValueConst promise, JSValueConst reason, JS_BOOL handled, void *opaque) {
        auto &self = *static_cast<Project *>(opaque);
        try {
            if (handled) {
                self.rejections.remove_if([&](const auto &rejection) {
                    return JS_VALUE_GET_PTR(rejection.first.value) == JS_VALUE_GET_PTR(promise);
                });
            } else {
                self.rejections.emplace_back(Value(ctx, JS_DupValue(ctx, promise)), Value(ctx, JS_DupValue(ctx, reason)));
            }
        } catch (...) {
            self.lost_rejection = true;
        }
    }

public:
    explicit Project(const k_cordis_ts_project_request_v1 &request) : host(request) {}
    ~Project() {
        if (engine.runtime) JS_SetHostPromiseRejectionTracker(engine.runtime, nullptr, nullptr);
    }

    void Run() {
        auto *ctx = engine.context;
        if (!ctx) throw CompileError { "cannot create the TypeScript project runtime" };
        JS_SetContextOpaque(ctx, this);
        JS_SetModuleLoaderFunc2(engine.runtime, Normalize, Load, CheckAttributes, this);
        JS_SetHostPromiseRejectionTracker(engine.runtime, TrackRejection, this);
        JS_SetCanBlock(engine.runtime, false);
        auto fail = [&] { throw CompileError { Mapped(Exception(ctx)) }; };

        Value global(ctx, JS_GetGlobalObject(ctx)), console(ctx, JS_NewObject(ctx));
        if (console.Failed()) fail();
        if (JS_SetPropertyStr(ctx, console.value, "log", JS_NewCFunctionMagic(ctx, Console, "log", 0, JS_CFUNC_generic_magic, 1)) < 0 ||
            JS_SetPropertyStr(ctx, console.value, "error", JS_NewCFunctionMagic(ctx, Console, "error", 0, JS_CFUNC_generic_magic, 2)) < 0 ||
            JS_SetPropertyStr(ctx, global.value, "console", JS_DupValue(ctx, console.value)) < 0) fail();

        auto entry = Compile(Read(View(host.entry)));
        if (entry.Failed()) fail();
        Value evaluation(ctx, JS_EvalFunction(ctx, std::exchange(entry.value, JS_UNDEFINED)));
        if (evaluation.Failed()) fail();
        int ran;
        do { ran = JS_ExecutePendingJob(engine.runtime, nullptr); } while (ran > 0);
        if (ran < 0) fail();
        auto state = JS_PromiseState(ctx, evaluation.value);
        if (state == JS_PROMISE_REJECTED) {
            Value error(ctx, JS_PromiseResult(ctx, evaluation.value));
            throw CompileError { Mapped(Describe(ctx, error.value)) };
        }
        if (lost_rejection) throw CompileError { "cannot track a rejected JavaScript promise" };
        if (!rejections.empty())
            throw CompileError { cat("Unhandled promise rejection: ", Mapped(Describe(ctx, rejections.front().second.value))) };
        if (state == JS_PROMISE_PENDING)
            throw CompileError { cat(View(host.entry), ": top-level await remains pending with no jobs to run") };
    }
};

int32_t Start(void **) { return 0; }
void Stop(void *) {}

int32_t Run(void *, const k_cordis_ts_project_request_v1 *request) {
    try {
        Project(*request).Run();
        return 0;
    } catch (const CompileError &failure) {
        request->error(request->host, Slice(failure.msg));
    } catch (...) {
        request->error(request->host, Slice("cannot execute the TypeScript project"));
    }
    return 1;
}

} // namespace
} // namespace klang::cordis::typescript

extern "C" K_CORDIS_EXPORT const k_cordis_typescript_project_v1_api *k_cordis_typescript_project_v1() {
    using namespace klang::cordis::typescript;
    static const k_cordis_typescript_project_v1_api api { K_CORDIS_TYPESCRIPT_ABI, sizeof(api), Start, Stop, Run };
    return &api;
}
