// Native library ownership shared by Cordis and the JIT.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace klang {

class SharedLibrary {
    void *handle = nullptr;
public:
    explicit SharedLibrary(const std::string &path);
    ~SharedLibrary();
    SharedLibrary(const SharedLibrary &) = delete;
    SharedLibrary &operator=(const SharedLibrary &) = delete;
    void *Symbol(const char *name) const;

    // The JIT's native symbols stay valid for the process lifetime.
    friend bool LoadSharedSymbols(const std::string &,
                                  std::vector<std::pair<std::string, const void *>> &, std::string &);
};

// Append the names and addresses from a JIT package's k_symbols table.
// On failure, return false and put the loader error in err.
bool LoadSharedSymbols(const std::string &path,
                       std::vector<std::pair<std::string, const void *>> &out, std::string &err);

}  // namespace klang
