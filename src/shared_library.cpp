#include "includes.h"
#include "utils.h"
#include "shared_library.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace klang {

SharedLibrary::SharedLibrary(const string &path) {
    #ifdef _WIN32
        auto wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, nullptr, 0);
        if (!wide) throw CompileError { cat("invalid UTF-8 library path ", path) };
        vector<wchar_t> wpath((size_t)wide);
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1, wpath.data(), wide))
            throw CompileError { cat("invalid UTF-8 library path ", path) };
        handle = LoadLibraryW(wpath.data());
        if (!handle)
            throw CompileError { cat("cannot load ", path, " (Windows error ", (unsigned)GetLastError(), ")") };
    #else
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) {
            auto why = dlerror();
            throw CompileError { cat("cannot load ", path, ": ", why ? why : "unknown error") };
        }
    #endif
}

SharedLibrary::~SharedLibrary() {
    if (!handle) return;
    #ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
    #else
        dlclose(handle);
    #endif
}

void *SharedLibrary::Symbol(const char *name) const {
    #ifdef _WIN32
        return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle), name));
    #else
        return dlsym(handle, name);
    #endif
}

// The table's entry, as native.h's k_symbols.c lays it out.
struct KlangSymbolEntry {
    const char *name;
    const void *addr;
};

bool LoadSharedSymbols(const string &path, vector<pair<string, const void *>> &out, string &err) {
    try {
        SharedLibrary lib(path);
        using TableFn = const KlangSymbolEntry *(*)();
        auto table = reinterpret_cast<TableFn>(lib.Symbol("k_symbols"));
        if (!table) throw CompileError { cat(path, " has no k_symbols table") };
        for (auto e = table(); e->name; e++) out.emplace_back(e->name, e->addr);
        lib.handle = nullptr;
        return true;
    } catch (const CompileError &e) {
        err = e.msg;
        return false;
    }
}

}  // namespace klang
