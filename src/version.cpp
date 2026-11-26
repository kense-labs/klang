// K compiler — its version, a translation unit of its own: the header
// the build writes (cmake/version.cmake) changes with every commit, and
// only this file is compiled again for it.
#include "klang_version.h"

namespace klang {

const char *CompilerVersion() { return KLANG_VERSION; }

}  // namespace klang
