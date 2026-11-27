# The K programming language

K is a memory-safe systems programming language with checked reference
lifetimes and explicit control over storage. It stores growable values on
address-stable data stacks and reclaims their storage at scope exit. K
compiles to C. Its command-line tool is `k`, and source files use `.k`.

The long-term goal is to make K a first choice for operating system
components, cloud services, and AI infrastructure. The first production
target is a cloud inference gateway. A freestanding boot target tests the
same core on bare metal. A project-selected Cordis runtime profile will
pursue Node.js compatibility with existing TypeScript projects.

## Design philosophy

The Cordis microkernel is K's primary architectural principle. A small core
owns language semantics, memory safety, and component lifecycles. Users
select compiler extensions, I/O services, guest-language runtimes, and device
backends through versioned interfaces. A deployment can link its selection
statically or load supported components dynamically. A resource keeps its
owner and lifetime rules across either form of composition.
[Architecture](docs/architecture.md) · [Development roadmap](docs/roadmap.md)

## Project status

K supports generics, tagged enums and pattern matching, concrete closures,
specialized higher-order functions, isolated workers with typed queues, and
C interop with checked handle transfer.
The `k` command checks, runs, builds, and formats programs, resolves packages,
and runs a language server.

The language server can replace a native Cordis diagnostics provider from
an explicitly selected manifest. The Cordis resolver reports the selected
providers, dependency edges, and input digests before activation. An
explicit lock holds the same diagnostics selection for `k check` and
`k build`. An `async fn` can await child tasks and yield cooperatively in
branches and loops. `.join()` drives a task from synchronous K code.
`task_scope` runs multiple tasks with lexical cancellation and drain. A task
can take ownership of a C handle at construction and use a leading `defer`
to release it even if it is cancelled before its first poll. An optional
Cordis TCP provider delivers bounded streams with backpressure, readiness
notification, and cancellation. Hosted K applications can select and replace
it together with an optional queue policy using verified Cordis locks.
The Linux cloud profile runs a CPU inference gateway with a replaceable
ONNX Runtime provider and supervised memory-limit recovery.

Synchronous `fn&` and `fn[N]` values support runtime selection of closure
layouts. Typed `@` decorators support metadata and checked synchronous
and asynchronous wrappers. The roadmap puts Node.js compatibility in a
Cordis runtime profile and guest-language `using` blocks in a Cordis source
extension. An optional Cordis build driver checks and builds the output of
a locked source transform. Separate CPython and QuickJS adapters run fenced
Python and TypeScript blocks with typed inputs and results. The TypeScript
adapter uses the official compiler and runs without Node.js. The optional
`k-cordis-ts` runner executes local TypeScript ESM projects with top-level
`await`. The roadmap tracks Node.js compatibility and the freestanding boot target.
[Concrete closures](docs/reference.md#concrete-closures) ·
[Owning C handles](docs/reference.md#owning-c-handles) ·
[Async functions](docs/design/async_and_decorators.md#sequential-suspension) ·
[Cordis graph resolution](docs/design/cordis_microkernel.md#resolving-a-provider-graph) ·
[Locked builds](docs/design/cordis_microkernel.md#locked-checks-and-builds) ·
[Native operation tasks](docs/design/async_and_decorators.md#native-operation-tasks) ·
[Bounded TCP streams](docs/design/cordis_microkernel.md#bounded-tcp-streams) ·
[Application activation](docs/design/cordis_microkernel.md#locked-application-activation) ·
[CPU inference](docs/design/cordis_microkernel.md#locked-cpu-inference) ·
[Loopback inference gateway](docs/design/cordis_microkernel.md#loopback-inference-gateway) ·
[CPython adapter](docs/design/embedded_languages.md#optional-cpython-adapter) ·
[TypeScript adapter](docs/design/embedded_languages.md#optional-typescript-adapter) ·
[TypeScript project runner](docs/design/functions_and_typescript_runtime.md#optional-project-runner) ·
[Development roadmap](docs/roadmap.md#milestones)

## Build from source

Use CMake 3.27 or newer, a C++20 compiler, Git, and Python 3 for the tests.
Run the build from the repository root.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Add `-DK_BUILTIN_LIBS=all` at configure time to include the graphics,
audio, UI, and physics packages. On macOS, install Xcode or the Xcode Command
Line Tools. With the Visual Studio generator, the compiler is
`build/Release/k.exe`. [Toolchain configuration](docs/command_line.md) ·
[Native package dependencies](docs/packages.md)

## Install a release

When a prebuilt archive is available for your platform, check its SHA-256
checksum against the release's `SHA256SUMS`, extract it, and put the directory
containing `k` on `PATH`. Keep the runtime files beside the executable. On
Linux, in-process execution needs the development headers for the system C
library. On macOS it needs the SDK from Xcode or the Command Line Tools.
[GitHub releases](https://github.com/kense-labs/klang/releases)

Choose the Linux `cloud` archive for the prebuilt Cordis inference gateway
and ONNX Runtime CPU provider. Supply a model and resolve the provider
lock with the `k` from the archive.
[Gateway setup](docs/design/cordis_microkernel.md#loopback-inference-gateway)

## Run a program

Save this program as `hello.k`.

```klang
import std;

fn main() {
    let numbers: i64[4] = [1, 2, 3, 4];
    print(numbers.map() { it * it });
}
```

```sh
build/k check hello.k
build/k run hello.k
build/k build -o build/hello hello.k
./build/hello
```

Both runs print `[1, 4, 9, 16]`. `k run` executes generated C in-process
through TinyCC. `k build` uses a C toolchain to produce a native executable
and supports programs with worker threads. Use `k` in place of `build/k`
after adding it to `PATH`. [More programs](samples/README.md)

## Develop K

- Learn the language with the [tutorial](docs/tutorial.md), [language reference](docs/reference.md), and [standard library](docs/stdlib.md).
- Work on the compiler with the [specification](docs/k_spec.md), [implementation notes](docs/implementation.md), and [testing guide](docs/testing.md).
- Connect an editor with the [VS Code extension](vscode/README.md) or the [language server guide](docs/editors.md).
- Follow the [architecture](docs/architecture.md), [roadmap](docs/roadmap.md), and [design notes](docs/index.md#design-notes).

Run the baseline compiler regression suite with Clang available.

```sh
python3 test/run_tests.py --profile baseline --cc native --require-clang
```

## License

K's original contributions are available under the [MIT License](LICENSE).
Inherited code is subject to the Apache License 2.0. Components with their
own license notices are distributed under those notices.
[Apache License 2.0](LICENSE-APACHE) · [Source and attribution notices](NOTICE)
