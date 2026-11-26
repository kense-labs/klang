# k_gfx

<!-- Adapted for K. -->

Graphics, audio and user interfaces for the
[K](../../README.md) programming language: `gfx`, a
window, input, GPU buffers, textures, graphics and compute pipelines on
SDL3's GPU API; `audio`, procedural PCM sound effects through SDL3; and
`ui`, immediate-mode windows and widgets on Nuklear, drawn through gfx. One
K *package* with three modules, since the three share one SDL build
and ui draws through gfx: `gfx.k`, `audio.k` and `ui.k`, their
C layers under `src/`, and the CMakeLists that builds the layers with SDL3
and Nuklear. CMake resolves these dependencies from pinned archives.

The API references are in K's standard library documentation:
[gfx](../../docs/stdlib.md#gfx),
[audio](../../docs/stdlib.md#audio)
and [ui](../../docs/stdlib.md#ui).
[Graphics](../../docs/design/gfx.md), [audio](../../docs/design/audio.md) and
[UI](../../docs/design/ui.md) describe how the layers are built.

## Using it

The three are blessed packages of K: a program imports them by name,

```klang
import gfx;
import audio;
import ui;
```

The package ships with K. A compiler built with `-DK_BUILTIN_LIBS=all`
has the package compiled in. Otherwise `k` builds the package's native
dependencies with CMake and a C compiler on first use and caches the build
under `K_HOME`.

To use a particular version, a fork, or a checkout of your own, declare the
package in the program's root file instead, under its package name `gfx`;
the other two modules are then `gfx.audio` and `gfx.ui`:

```klang
import gfx from "../my-gfx";
import gfx.audio;
import gfx.ui;
```

`k build program.k` links the layers into the executable, and
`k program.k` runs it in-process. Shaders are GLSL, compiled into
the program by the compiler's `embed_shader`. Programs in K's
[samples](../../samples/README.md) show all
three modules in use.

## Layout

| | |
|---|---|
| `gfx.k`, `audio.k`, `ui.k` | the modules: constants, the structs that cross to C, the `extern fn`s and thin wrappers |
| `gfx.build` | the package's sidecar: the CMake target to build, and that its functions run on the main thread only |
| `CMakeLists.txt` | builds SDL3, `k_gfx`, `k_audio` and `k_ui` as static libraries, carried by the target `k_gfx_package` |
| `src/gfx/`, `src/audio/`, `src/ui/` | the layers; each `*_api.h` lists every function its module declares, and `test/api_check.py` checks the two agree |
| `src/gfx/gfx_blob.h` | the shader blob the compiler writes and the layer reads, a copy of the compiler's header kept in step by the blob's version |
| `test/audio_device.c` | the audio layer's own test, the CMake target `k_audio_test` |
| `test/window/` | the gfx showcase and input test, which need a display and a GPU; run from a K checkout |
| `LICENSE-*.txt` | licenses for the pinned native dependencies |

The headless tests of the modules run in K's own test suite.

## License

The package retains the Apache License 2.0. SDL and Nuklear have their own
licenses.
