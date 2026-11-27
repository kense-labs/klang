#include "python_engine.h"
#include "diagnostics.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

namespace {
using namespace klang::cordis::python;

using namespace klang::cordis::guest;

const std::string &Ready() {
    static std::once_flag once;
    static std::string error;
    std::call_once(once, [] { if (!Py_IsInitialized()) error = Initialize(); });
    return error;
}

std::string PythonError() {
    Object error(PyErr_GetRaisedException());
    if (!error) return "Python operation failed";
    Object traceback(PyImport_ImportModule("traceback"));
    if (traceback) {
        Object lines(PyObject_CallMethod(traceback.get(), "format_exception", "O", error.get()));
        Object empty(PyUnicode_New(0, 0));
        if (lines && empty) {
            Object joined(PyUnicode_Join(empty.get(), lines.get()));
            auto text = Unicode(joined.get());
            if (!text.empty()) return text;
        }
    }
    PyErr_Clear();
    Object fallback(PyObject_Str(error.get()));
    auto text = Unicode(fallback.get());
    PyErr_Clear();
    return text.empty() ? "Python operation failed" : text;
}

Object Value(const Input &input) {
    if (input.kind == Kind::Integer) return Object(PyLong_FromLongLong(input.number));
    auto size = static_cast<Py_ssize_t>(input.bytes.size());
    if (input.kind == Kind::Text) return Object(PyUnicode_DecodeUTF8(input.bytes.data(), size, "strict"));
    return Object(PyBytes_FromStringAndSize(input.bytes.data(), size));
}

bool Evaluate(Call &call, std::string_view code, const std::string &file, size_t line, Kind kind) {
    if (const auto &error = Ready(); !error.empty()) {
        call.result = error;
        return false;
    }
    Gil gil;
    auto fail = [&] { call.result = PythonError(); return false; };
    Object globals(PyDict_New());
    Object program(Py_CompileString(Padded(code, line).c_str(), file.c_str(), Py_file_input));
    if (!globals || !program) return fail();
    for (const auto &input : call.inputs) {
        auto value = Value(input);
        if (!value || PyDict_SetItemString(globals.get(), input.name.c_str(), value.get()) < 0) return fail();
    }
    Object result(PyEval_EvalCode(program.get(), globals.get(), globals.get()));
    if (!result) return fail();
    auto *answer = PyDict_GetItemString(globals.get(), "answer");
    if (!answer) {
        call.result = "Python block did not assign answer";
        return false;
    }
    if (kind == Kind::Integer && PyLong_CheckExact(answer)) {
        int overflow = 0;
        auto number = PyLong_AsLongLongAndOverflow(answer, &overflow);
        if (overflow || PyErr_Occurred()) {
            PyErr_Clear();
            call.result = "Python answer is outside i64";
            return false;
        }
        call.number = number;
        return true;
    }
    if (kind == Kind::Text && PyUnicode_Check(answer)) {
        call.result = Unicode(answer);
        if (PyErr_Occurred()) return fail();
        return true;
    }
    if (kind == Kind::Bytes && PyBytes_Check(answer)) {
        call.result.assign(PyBytes_AS_STRING(answer), static_cast<size_t>(PyBytes_GET_SIZE(answer)));
        return true;
    }
    call.result = "Python answer has the wrong type";
    return false;
}
} // namespace

extern "C" {
K_CORDIS_EXPORT Handle k_cordis_python_new_v1() { return New(); }
K_CORDIS_EXPORT int32_t k_cordis_python_valid_v1(Handle call) { return call.token != 0; }
K_CORDIS_EXPORT void k_cordis_python_drop_v1(Handle call) { Drop(call); }

K_CORDIS_EXPORT int32_t k_cordis_python_integer_v1(Handle call, Slice name, int64_t value) {
    return AddInteger(call, name, value);
}

K_CORDIS_EXPORT int32_t k_cordis_python_slice_v1(Handle call, Slice name, Slice value, int32_t kind) {
    return AddSlice(call, name, value, kind);
}

// 1 for success, 0 for a guest or conversion error, -1 for bridge failure.
K_CORDIS_EXPORT int32_t k_cordis_python_eval_v1(Handle id, Slice code, Slice file, int64_t line, int32_t kind) {
    try {
        if (!id.token) return -1;
        auto &call = *Pointer(id);
        call.result.clear();
        if (!Valid(code) || !Valid(file) || line < 1 || line > INT32_MAX ||
            kind < int(Kind::Integer) || kind > int(Kind::Bytes) ||
            View(code).find('\0') != std::string_view::npos || View(file).find('\0') != std::string_view::npos)
            return -1;
        return Evaluate(call, View(code), std::string(View(file)), static_cast<size_t>(line), static_cast<Kind>(kind));
    } catch (...) {
        return -1;
    }
}

K_CORDIS_EXPORT int64_t k_cordis_python_result_integer_v1(Handle call) { return Integer(call); }
K_CORDIS_EXPORT int64_t k_cordis_python_result_size_v1(Handle call) { return Size(call); }

K_CORDIS_EXPORT void k_cordis_python_result_copy_v1(Handle call, Buffer out) {
    Copy(call, out);
}
} // extern "C"
