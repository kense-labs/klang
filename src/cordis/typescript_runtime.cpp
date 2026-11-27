// A synchronous QuickJS bridge for Cordis TypeScript guest blocks.
#include "guest_call.h"
#include "typescript_engine.h"
#include "typescript_sourcemap.h"
#include "diagnostics.h"

#include "../includes.h"
#include "../utils.h"
#include "../json.h"

namespace {
using namespace klang;
using namespace klang::cordis::guest;
using namespace klang::cordis::typescript;

constexpr string_view generated = "<cordis-ts-generated>";

Value Bytes(JSContext *ctx, string_view bytes) {
    static constexpr uint8_t empty = 0;
    auto *data = bytes.empty() ? &empty : reinterpret_cast<const uint8_t *>(bytes.data());
    Value buffer(ctx, JS_NewArrayBufferCopy(ctx, data, bytes.size()));
    if (buffer.Failed()) return buffer;
    // QuickJS's typed-array constructor reads the optional byte offset and length.
    JSValueConst args[] { buffer.value, JS_UNDEFINED, JS_UNDEFINED };
    return { ctx, JS_NewTypedArray(ctx, size(args), args, JS_TYPED_ARRAY_UINT8) };
}

Value Bind(JSContext *ctx, const Input &input) {
    switch (input.kind) {
        case Kind::Integer: return { ctx, JS_NewBigInt64(ctx, input.number) };
        case Kind::Text: return String(ctx, input.bytes);
        case Kind::Bytes: return Bytes(ctx, input.bytes);
    }
    return { ctx, JS_UNDEFINED };
}

bool Evaluate(Call &call, string_view packet, string_view file, int64_t first, Kind kind) {
    auto decoded = ParseJson(string(packet));
    auto script = decoded.Str("javascript");
    auto map = ParseJson(decoded.Str("source_map"));
    auto lines = OriginLines(map.Str("mappings"));
    Engine engine;
    auto *ctx = engine.context;
    if (!ctx) {
        call.result = "cannot create a TypeScript runtime";
        return false;
    }
    auto fail = [&] {
        call.result = Exception(ctx);
        Remap(call.result, generated, file, first, lines);
        return false;
    };
    Value global(ctx, JS_GetGlobalObject(ctx));
    Value exports(ctx, JS_NewObject(ctx));
    auto exemplar = Bytes(ctx, {});
    if (exports.Failed() || exemplar.Failed()) return fail();
    constexpr int flags = JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE | JS_PROP_ENUMERABLE;
    if (JS_DefinePropertyValueStr(ctx, global.value, "exports", JS_DupValue(ctx, exports.value), flags) < 0)
        return fail();
    for (const auto &input : call.inputs) {
        if (input.kind == Kind::Text && !Utf8(input.bytes)) {
            call.result = "TypeScript text binding is not UTF-8";
            return false;
        }
        auto value = Bind(ctx, input);
        if (value.Failed()) return fail();
        if (JS_DefinePropertyValueStr(ctx, global.value, input.name.c_str(), JS_DupValue(ctx, value.value), flags) < 0)
            return fail();
    }
    Value execution(ctx, JS_Eval(ctx, script.data(), script.size(), generated.data(), JS_EVAL_TYPE_GLOBAL));
    if (execution.Failed()) return fail();
    Value answer(ctx, JS_GetPropertyStr(ctx, exports.value, "answer"));
    if (answer.Failed()) return fail();
    if (JS_IsUndefined(answer.value)) {
        call.result = "TypeScript block did not export answer";
        return false;
    }
    if (kind == Kind::Integer && JS_IsBigInt(ctx, answer.value)) {
        auto number = Text(ctx, answer.value);
        if (JS_HasException(ctx)) return fail();
        auto read = from_chars(number.data(), number.data() + number.size(), call.number);
        if (read.ec != errc{} || read.ptr != number.data() + number.size()) {
            call.result = "TypeScript answer is outside i64";
            return false;
        }
        return true;
    }
    if (kind == Kind::Text && JS_IsString(answer.value)) {
        call.result = Text(ctx, answer.value);
        if (JS_HasException(ctx)) return fail();
        if (!Utf8(call.result)) {
            call.result = "TypeScript answer is not UTF-8";
            return false;
        }
        return true;
    }
    if (kind == Kind::Bytes && JS_GetClassID(answer.value) == JS_GetClassID(exemplar.value)) {
        size_t offset = 0, length = 0, width = 0, capacity = 0;
        Value buffer(ctx, JS_GetTypedArrayBuffer(ctx, answer.value, &offset, &length, &width));
        if (buffer.Failed()) return fail();
        auto *bytes = JS_GetArrayBuffer(ctx, &capacity, buffer.value);
        if (JS_HasException(ctx)) return fail();
        if (offset <= capacity && length <= capacity - offset && (bytes || !length)) {
            if (length) call.result.assign(reinterpret_cast<const char *>(bytes + offset), length);
            return true;
        }
    }
    call.result = "TypeScript answer has the wrong type";
    return false;
}
} // namespace

extern "C" {
K_CORDIS_EXPORT Handle k_cordis_typescript_new_v1() { return New(); }
K_CORDIS_EXPORT int32_t k_cordis_typescript_valid_v1(Handle call) { return call.token != 0; }
K_CORDIS_EXPORT void k_cordis_typescript_drop_v1(Handle call) { Drop(call); }

K_CORDIS_EXPORT int32_t k_cordis_typescript_integer_v1(Handle call, Slice name, int64_t value) {
    return AddInteger(call, name, value);
}

K_CORDIS_EXPORT int32_t k_cordis_typescript_slice_v1(Handle call, Slice name, Slice value, int32_t kind) {
    return AddSlice(call, name, value, kind);
}

K_CORDIS_EXPORT int32_t k_cordis_typescript_eval_v1(Handle id, Slice code, Slice file, int64_t line, int32_t kind) {
    try {
        if (!id.token || !Valid(code) || !Valid(file) || line < 1 || line > INT32_MAX ||
            kind < int(Kind::Integer) || kind > int(Kind::Bytes)) return -1;
        auto &call = *Pointer(id);
        call.result.clear();
        return Evaluate(call, View(code), View(file), line, static_cast<Kind>(kind));
    } catch (...) {
        return -1;
    }
}

K_CORDIS_EXPORT int64_t k_cordis_typescript_result_integer_v1(Handle call) { return Integer(call); }
K_CORDIS_EXPORT int64_t k_cordis_typescript_result_size_v1(Handle call) { return Size(call); }
K_CORDIS_EXPORT void k_cordis_typescript_result_copy_v1(Handle call, Buffer out) { Copy(call, out); }
} // extern "C"
