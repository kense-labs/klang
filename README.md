# The K programming language

K is a memory-safe systems programming language with checked reference
lifetimes and explicit control over storage. It stores growable values on
address-stable data stacks and reclaims their storage at scope exit. K
compiles to C. Its command-line tool is `k`, and source files use `.k`.

The long-term goal is to make K a first choice for operating system
components, cloud services, and AI infrastructure. The first production
target is a cloud inference gateway. A freestanding boot target tests the
same core on bare metal. An optional TypeScript runtime will pursue Node.js
compatibility with existing projects.

## Design philosophy

The Cordis microkernel is K's primary architectural principle. A small core
owns language semantics, memory safety, and component lifecycles. Users
select compiler extensions, I/O services, guest-language runtimes, and device
backends through versioned interfaces. A deployment can link its selection
statically or load supported components dynamically. A resource keeps its
owner and lifetime rules across either form of composition.
[Architecture](docs/architecture.md) · [Development roadmap](docs/roadmap.md)

## Project status

K supports generics, tagged enums and pattern matching, specialized
higher-order functions, isolated workers with typed queues, and C interop.
The `k` command checks, runs, builds, and formats programs, resolves packages,
and runs a language server.

First-class closures, `async`/`await`, typed decorators, Cordis extension
loading, and embedded-language `using` blocks are planned. The first
extension test will replace a diagnostic provider in a running language
server. [Cordis microkernel](docs/design/cordis_microkernel.md) · [Embedded languages](docs/design/embedded_languages.md)

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
