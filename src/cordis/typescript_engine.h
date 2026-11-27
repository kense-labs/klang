// QuickJS ownership helpers for the optional TypeScript component.
#pragma once

#include <quickjs.h>

#include <string>
#include <string_view>
#include <utility>

namespace klang::cordis::typescript {

struct Engine {
    JSRuntime *runtime = JS_NewRuntime();
    JSContext *context = runtime ? JS_NewContext(runtime) : nullptr;

    Engine() = default;
    Engine(const Engine &) = delete;
    ~Engine() {
        if (context) JS_FreeContext(context);
        if (runtime) JS_FreeRuntime(runtime);
    }
};

struct Value {
    JSContext *context;
    JSValue value;

    Value(JSContext *ctx, JSValue v) : context(ctx), value(v) {}
    Value(Value &&other) noexcept : context(other.context), value(std::exchange(other.value, JS_UNDEFINED)) {}
    Value(const Value &) = delete;
    ~Value() { JS_FreeValue(context, value); }
    bool Failed() const { return JS_IsException(value); }
};

inline std::string Text(JSContext *ctx, JSValueConst value) {
    size_t size;
    auto *bytes = JS_ToCStringLen(ctx, &size, value);
    if (!bytes) return {};
    struct Owned {
        JSContext *context;
        const char *bytes;
        ~Owned() { JS_FreeCString(context, bytes); }
    } owned{ctx, bytes};
    return std::string(bytes, size);
}

inline std::string Describe(JSContext *ctx, JSValueConst error) {
    auto report = Text(ctx, error);
    if (JS_IsError(ctx, error)) {
        Value property(ctx, JS_GetPropertyStr(ctx, error, "stack"));
        if (JS_IsString(property.value)) {
            auto stack = Text(ctx, property.value);
            if (!stack.empty()) report += "\n" + stack;
        }
    }
    return report.empty() ? "JavaScript operation failed" : report;
}

inline std::string Exception(JSContext *ctx) {
    Value error(ctx, JS_GetException(ctx));
    return Describe(ctx, error.value);
}

inline Value String(JSContext *ctx, std::string_view text) {
    return { ctx, JS_NewStringLen(ctx, text.empty() ? "" : text.data(), text.size()) };
}

} // namespace klang::cordis::typescript
