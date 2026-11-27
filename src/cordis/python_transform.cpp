// The optional CPython source transform.
#include "python_engine.h"
#include "source_guest.h"

namespace klang::cordis::python {
namespace {
using namespace guest;

constexpr Profile profile { "python", "Python", "\"python\"", "__builtins__", "cordis_python_guest_v1", ".py" };

// Python's SyntaxError uses character columns. The driver uses byte columns.
size_t OriginalOffset(string_view text, long row, long column) {
    size_t start = 0;
    for (long line = 1; line < row; ++line) {
        auto end = text.find('\n', start);
        if (end == string_view::npos) return text.size();
        start = end + 1;
    }
    for (long point = 1; point < column && start < text.size() && text[start] != '\n'; ++point) {
        ++start;
        while (start < text.size() && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) ++start;
    }
    return start;
}

long Attribute(PyObject *error, const char *name, long fallback) {
    Object object(PyObject_GetAttrString(error, name));
    if (object && PyLong_Check(object.get())) {
        long value = PyLong_AsLong(object.get());
        if (!PyErr_Occurred() && value > 0) return value;
    }
    PyErr_Clear();
    return fallback;
}

struct State : Diagnostic {
    bool owns_python = false;

    string Compile(const Block &block, string_view source, const string &path) {
        if (block.code.find('\0') != string_view::npos) throw Failure { block.body, "NUL in Python source" };
        if (!Py_IsInitialized()) {
            auto message = Initialize();
            if (!message.empty()) throw Failure { block.start, std::move(message) };
            owns_python = true;
        }
        const auto first = Position(source, block.body).first;
        Gil gil;
        Object program(Py_CompileString(Padded(block.code, first).c_str(), path.c_str(), Py_file_input));
        if (program) return string(block.code);
        Object error(PyErr_GetRaisedException());
        if (!error) throw Failure { block.body, "Python compilation failed" };
        auto offset = OriginalOffset(source, Attribute(error.get(), "lineno", first), Attribute(error.get(), "offset", 1));
        Object message(PyObject_GetAttrString(error.get(), "msg"));
        if (!message) {
            PyErr_Clear();
            message.reset(PyObject_Str(error.get()));
        }
        auto detail = Unicode(message.get());
        PyErr_Clear();
        throw Failure { offset, cat("Python syntax error: ", detail.empty() ? "cannot compile guest source" : detail) };
    }
};

int32_t Start(void **state) {
    *state = new (nothrow) State;
    return *state ? 0 : 1;
}

void Stop(void *opaque) {
    auto state = unique_ptr<State>(static_cast<State *>(opaque));
    if (state && state->owns_python) {
        PyGILState_Ensure();
        Py_FinalizeEx();
    }
}

int32_t LastError(void *state, k_cordis_emit_diagnostic_v1 emit, void *sink) {
    return static_cast<State *>(state)->Report(emit, sink);
}

int32_t Transform(void *opaque, const k_cordis_source_request_v1 *request, k_cordis_emit_source_v1 emit, void *sink) {
    auto &state = *static_cast<State *>(opaque);
    return Lower(profile, state, request, emit, sink,
                 [&](const Block &block, string_view source, const string &path, string_view, vector<File> &) {
                     return state.Compile(block, source, path);
                 });
}
} // namespace
} // namespace klang::cordis::python

extern "C" K_CORDIS_EXPORT const k_cordis_source_transform_v1_api *k_cordis_source_transform_v1() {
    using namespace klang::cordis::python;
    static const k_cordis_source_transform_v1_api api {
        K_CORDIS_SOURCE_TRANSFORM_ABI, sizeof(api), Start, Stop, Transform, LastError
    };
    return &api;
}
