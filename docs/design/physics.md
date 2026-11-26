# The physics module (`physics`)

<!-- Adapted for K. -->

This document describes the physics package's implementation, test
coverage, and remaining work. See K's `docs/stdlib.md` for the API
reference and `physics.k` for the module source. The package lived in
the K repository until it became a package of its own
(`docs/design/packages.md` there).

## What it is

`import physics;` gives a K program Box3D, Erin Catto's 3D rigid body
engine: worlds, bodies, spheres, capsules, convex hulls, triangle meshes,
height fields and baked compounds, nine kinds of joints, contact, sensor,
hit, move and joint events, ray and shape casts, overlap queries, a
character mover, and recording and replay. It is built the way `gfx` is
(`docs/design/gfx.md`), with Box3D in the place of SDL3:

    K program --extern fn--> k_phys_* (k_physics, native C) --> Box3D (static)

The layer, `k_physics` (`src/`), is native C compiled once into a
static library: `k` links it for JIT runs and hands its functions to
TinyCC with `tcc_add_symbol` (`AddPhysicsSymbols`, `src/jit.h`), and a program
built from the generated C links the same archive. Neither the generated C
nor TinyCC sees a Box3D header.

The API stays Box3D's, under K names -- `b3Body_GetPosition` is
`physics::position(body)` -- with its definitions as structs whose field
defaults are Box3D's own, so `physics::BodyDef { body_type: physics::DYNAMIC,
position: p }` is a whole definition. The layer covers nearly all of
`box3d.h` and the geometry parts of `collision.h`: about 500 functions.

## How it is built

* **Box3D** comes from a pinned source archive verified by SHA-256. The
  pinned revision is from September 2026. Release v0.1.0 of
  June 2026, has a different API in places (the replay player's functions
  and the center-of-mass getters were renamed since) and misses fixes made
  since; the next release should replace the pin. `CMakeLists.txt` adds
  it with `add_subdirectory`, which leaves out its samples, tests and
  documentation (options of a top-level build only), in single precision,
  static, and on the release C runtime everywhere: Box3D asks for the debug
  one in a debug build, which does not link into a `/MT` program. Box3D is
  C17 with no dependencies beyond the C runtime, libm and threads; its CMake
  needs 3.22, and an older CMake takes the opt-out path.
* **A package** (`docs/design/packages.md`): this repository holds the
  module `physics.k`, the sidecar `physics.build` naming the CMake
  target `k_physics` and `main_thread_only`, the layer under `src/` and
  the CMakeLists that builds it with Box3D. `import physics;` finds it
  through the blessed table (`src/blessed.txt`). A compiler configured with
  `-DK_BUILTIN_LIBS=all` has it compiled in (`cmake/package.cmake`):
  its functions reach a JIT run through the symbol table CMake generates
  from the module's `extern fn`s, and the link closure of a probe
  executable, read from CMake's file API reply once the compiler is built
  (`cmake/linkclosure.cmake`), is the response file
  `build/pkg/physics/<config>/link-{cc,msvc}.rsp` that `k build` links
  programs with (`k --physics-link msvc|cc` prints its path). A
  compiler without it builds the package into `K_HOME` the first time a
  program uses it, through the wrapper project of `src/native.h`, and loads
  the shared library that build makes for JIT runs.

Omitting physics from `K_BUILTIN_LIBS` leaves it out of the compiler.
Physics programs still typecheck and generate C. Running or building one
then needs CMake and a C compiler. The first build resolves Box3D.

## The compiler's side

The compiler integration is small: physics has no compile-time processing
comparable to shader compilation by `embed_shader`.

* **Calls into the layer** are extern fns of the physics package: codegen
  notes the packages a program's extern calls reach (`NativePackagesUsed`),
  and a JIT run takes their functions from the compiled-in table or the
  package's library.
* **A `thread_fn` never reaches the layer**, as with gfx: the layer keeps
  its error text, misuse count and geometry tables for the process, and
  Box3D's world creation is not thread-safe. Box3D does its own threading
  inside a step, on as many workers as `WorldDef.workers` asks for.

## The layer (`src/`)

`physics_api.h` lists every function once (`K_PHYS_API`) and every constant
(`K_PHYS_CONSTANTS`); `test/api_check.py` checks `physics.k`
declares exactly those functions with the same C shapes, the same struct
fields in the same order, and the same constant values, as it does for gfx.

* **Structs use K's layout.** Box3D's definitions carry pointers, names
  and callbacks and are naturally aligned; the layer's are packed, hold only
  what crosses, and are converted field by field onto Box3D's defaults
  (`b3Default*Def`), so its internal cookie and anything not exposed keep
  Box3D's values. `default_*_def()` hands Box3D's defaults back, and the test
  compares them with the K field defaults. Pointers become handles or
  separate arguments: names are set with `set_name`, user data is a `u64`.
* **Only structs with compatible TinyCC and native C calling conventions pass by value.**
  Definitions, transforms and results are passed and returned by value,
  which is simple on the K side. TinyCC classifies a struct of up to 16
  bytes as a whole where the System V ABI classifies each eightbyte, so a
  small struct with floats in one half and integers in the other would
  arrive in the wrong registers; `test/api_check.py` rejects such a struct
  crossing by value, and one with a misaligned field. Everything larger than
  16 bytes goes through memory on every ABI.
* **Handles** are Box3D's own ids, stored whole: `World` a `u32`, `Body`,
  `Shape` and `Joint` a `u64` (`b3Store*Id`), `Contact` Box3D's three words.
  Each carries a generation, and every entry point checks its ids with
  `b3*_IsValid` before Box3D sees them, so a destroyed object is a misuse,
  not a crash, in a release build of Box3D as in a debug one. A joint's
  kind is a handle type of its own (`RevoluteJoint { joint: Joint }`), which
  makes calling a revolute function on a prismatic joint a compile error;
  the layer still checks the kind, for a handle built by hand.
* **Geometry the layer owns** -- hulls, meshes, height fields, compounds,
  recordings, players -- lives in slot tables with generations of their own,
  as gfx's resources do. Box3D copies a hull into each world that uses it,
  but mesh, height field and compound shapes keep pointing at the data they
  were made from: destroying one of those releases the handle at once, and
  the data is freed when no shape uses it any more (`phys_sweep`, after
  anything that destroys shapes). A recording a world is writing into is
  stopped before it is destroyed.
* **Callbacks become arrays.** Events and query results are copied into a
  slice the program provides, and the call returns how many there were; the
  K wrappers call twice, the second time with room for all. Ray and
  shape casts gather every hit and sort them nearest first, ties by shape,
  for the same order everywhere. Friction and restitution mixing, callbacks
  in Box3D, are one of six rules (`MIX_*`), each a C function of the layer.
* **Errors** follow gfx: a failure outside the program's control returns
  false or a zero handle with the reason in `error()`; a misuse is skipped,
  printed as it happens, counted, and the K side aborts with it at the
  next `step()`, `destroy(world)` or `check()`. The layer checks what Box3D
  would only assert: handles, joint kinds, bodies of one world, static-only
  shapes, index and count ranges, mesh indices within the vertices. Box3D's
  own asserts, active in a debug build, are routed into the same misuse path
  instead of breaking into a debugger, and its log goes to stderr, never
  into a program's stdout.

Not exposed, because each needs a callback into K code or is Box3D's own
tooling: custom contact filters and pre-solve callbacks, debug drawing, the
standalone dynamic tree, the low-level collide-pair functions, and the
replay player's query inspection.

## The K module (`physics.k`)

Namespace `physics`: constants, handles, definitions with Box3D's defaults,
the `extern` declarations, thin wrappers (events, queries and lists as fresh
arrays; `name()`; two results as two values), and quaternion and transform
helpers (`quat_axis_angle`, `mul`, `rotate`, `transform_point`, `inv_mul`,
`local_frame`, `joint_def`). Names avoid the global ones a namespaced
declaration would shadow inside the module (`rest_length` rather than
`length`, `shape_filter` rather than `filter`), and K keywords
(`body_type`, `shape_type`).

## Testing

* **`test/physics/`** in the regular suite, run AOT at -O0 and -O2 and
  through TinyCC where the compiler has the layer, and only typechecked and
  turned into C where it does not:
  `physics_world` (the library, defaults, settings, sleeping, statistics, an
  explosion, and the same pile stepped with one and with four workers coming
  out identical), `physics_bodies`, `physics_shapes` (every kind of shape and
  geometry, filters at work, the deferred freeing of a mesh measured through
  Box3D's byte count), `physics_joints` (all nine kinds, each doing what it is
  for), `physics_queries` (world queries, the character mover, geometry on
  its own, the rotation helpers), `physics_events` and `physics_recording`
  (a recording saved, loaded, validated and replayed to the same final
  transforms). The tests call 502 of the layer's 502 functions.
  `physics_misuse` checks a destroyed body aborts the program at the next
  step with the reason, and `physics_err_thread` that a `thread_fn` reaching
  physics is a compile error.
* **`samples/28_physics_boxes.k`** rains thousands of boxes into an arena
  with the gfx module, and runs headless in the samples runner for 120
  frames, 3,000 boxes, JIT and AOT.

Box3D is deterministic across platforms and worker counts, and the layer
adds no floating point of its own on the way, so the tests print physics
results, rounded to hundredths or thousandths.

### Platforms tested

| | Windows 11 | Linux (Ubuntu 24.04 under WSL2) | macOS |
|---|---|---|---|
| Build | MSVC | clang++ for the compiler, gcc for the C | not run |
| Suite, incl. `test/physics/` | MSVC, TinyCC | gcc, clang, TinyCC | not run |
| Sample | JIT, cl | JIT, gcc | not run |

On Windows the physics tests and the sample also match their expected output
built with clang and with clang-cl: Box3D comes out the same to the last
printed digit on every toolchain, and with one worker thread or four.
As with gfx, nothing has run on macOS; the ABI rule above was reasoned out
for System V x86-64 and Windows x64 and is unverified for arm64.

## Follow-up work

* Replace the pinned commit with Box3D's next release.
* Debug drawing through the layer, handing K the lines and shapes to
  draw with gfx, and a K-side immediate-mode renderer for any world.
* Custom contact filters and pre-solve callbacks, which need a way for C to
  call a K function.
* The dynamic tree as a spatial index for game data.
* Physics from `thread_fn`s, one world per thread, once the layer's state is
  per thread and Box3D's world creation is serialized.
