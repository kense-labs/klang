# The K programming language

K is a memory-safe systems programming language. It stores growable data on
address-stable data stacks and reclaims storage at scope exit. The compiler
checks types and reference lifetimes, then generates C. Its command-line tool
is `klang`, and K source files use the `.k` extension.

## Build from source

Use CMake 3.27 or newer, a C++20 compiler, Git, and Python 3 for the tests.
Run these commands from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DKLANG_BUILTIN_LIBS=all
cmake --build build --config Release --parallel
```

`KLANG_BUILTIN_LIBS=all` fetches the pinned graphics, audio, UI, and physics
packages. For a compiler-only build, omit that option. On macOS, install
Xcode or the Xcode Command Line Tools. See the [package guide](docs/packages.md)
for native dependencies and the [command-line reference](docs/command_line.md)
for toolchain configuration.

## Run a program

```sh
build/klang check samples/01_tour.k
build/klang run samples/01_tour.k
build/klang build -o build/tour samples/01_tour.k
./build/tour
```

`run` executes generated C in-process through TinyCC. `build` uses the host
C toolchain and supports programs with worker threads. With the Visual
Studio generator, the compiler is `build/Release/klang.exe`.

## Develop K

- [Tutorial](docs/tutorial.md), [language reference](docs/reference.md),
  [specification](docs/klang_spec.md), and [standard library](docs/stdlib.md)
- [Compiler architecture](docs/implementation.md) and [testing](docs/testing.md)
- [VS Code extension](vscode/README.md) and [other editors](docs/editors.md)

Run the compiler regression suite with:

```sh
python3 test/run_tests.py --profile baseline --cc native --require-clang
```

## Origins and license

K builds on the [Goose programming language](https://github.com/aardappel/goose),
designed by Wouter van Oortmerssen. See [LICENSE](LICENSE) for the Apache 2.0
license and copyright notice.
