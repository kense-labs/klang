// K compiler — its version, a translation unit of its own: the header
// the build writes (cmake/version.cmake) changes with every commit, and
// only this file is compiled again for it.
#include "k_version.h"

namespace klang {

const char *CompilerVersion() { return K_VERSION; }

}  // namespace klang
