# k_physics

<!-- Adapted for K. -->

Rigid body physics for the [K](../../README.md)
programming language, on [Box3D](https://github.com/erincatto/box3d): worlds,
bodies, shapes of every kind Box3D has, joints, events, queries, a character
mover, recording and replay. A K *package*: the module `physics.k`,
its C layer under `src/`, and the CMake build that resolves a pinned Box3D
archive and links it with the layer.

The API reference is K's
[standard library documentation](../../docs/stdlib.md#physics),
and the [physics design](../../docs/design/physics.md) describes how the
layer is built.

## Using it

Physics is one of K's blessed packages: a program imports it by name,

```klang
import physics;
```

The package ships with K. A compiler built with `-DK_BUILTIN_LIBS=all`
has the package compiled in. Otherwise `k` builds the package's native
dependencies with CMake and a C compiler on first use and caches the build
under `K_HOME`.

To use a particular version, a fork, or a checkout of your own, declare the
package in the program's root file instead:

```klang
import physics from "../my-physics";
```

`k build program.k` links the layer into the executable, and `k
program.k` runs it in-process. Programs in K's
[samples](../../samples/README.md) show it in
use.

## Layout

| | |
|---|---|
| `physics.k` | the module: constants, the structs that cross to C, the `extern fn`s and thin wrappers |
| `physics.build` | the package's sidecar: the CMake target to build, and that its functions run on the main thread only |
| `CMakeLists.txt` | builds Box3D and `k_physics`, the layer, as static libraries |
| `src/` | the layer: `physics_api.h` lists every function the module declares; the test suite in K's repository checks the two agree (`test/api_check.py`) |
| `LICENSE-Box3D.txt` | license for the pinned Box3D dependency |

## License

The package retains the Apache License 2.0. Box3D has its own license.
