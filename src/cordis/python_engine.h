// CPython support shared by the optional transform and runtime bridge.
#pragma once

#include <Python.h>

#include "guest_call.h"

#include <memory>
#include <string>
#include <string_view>

namespace klang::cordis::python {

using guest::Kind;

struct Release {
    void operator()(PyObject *object) const { Py_DECREF(object); }
};
using Object = std::unique_ptr<PyObject, Release>;

struct Gil {
    PyGILState_STATE state = PyGILState_Ensure();
    ~Gil() { PyGILState_Release(state); }
    Gil() = default;
    Gil(const Gil &) = delete;
};

inline std::string Initialize() {
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.site_import = 0;
    config.write_bytecode = 0;
    config.buffered_stdio = 0;
    auto status = PyConfig_SetBytesString(&config, &config.program_name, K_CORDIS_PYTHON_EXECUTABLE);
    if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        auto message = status.err_msg ? status.err_msg : "cannot initialize CPython";
        std::string error(message);
        PyConfig_Clear(&config);
        return error;
    }
    PyConfig_Clear(&config);
    PyEval_SaveThread();
    return {};
}

inline std::string Unicode(PyObject *value) {
    if (!value) return {};
    Py_ssize_t size = 0;
    auto *text = PyUnicode_AsUTF8AndSize(value, &size);
    return text ? std::string(text, static_cast<size_t>(size)) : std::string();
}

inline std::string Padded(std::string_view code, size_t first_line) {
    std::string source(first_line - 1, '\n');
    source += code;
    return source;
}

} // namespace klang::cordis::python
