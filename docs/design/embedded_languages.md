# Embedded language blocks

The optional Cordis source extension runs fenced guest-language blocks.
Projects select the `using` syntax extension and guest runtime
in the Cordis graph. The source extension owns the extended syntax and
emits ordinary K. Each adapter owns its guest frontend and execution
engine. The K compiler checks generated calls with its existing rules.
[Cordis microkernel](cordis_microkernel.md#extension-points)

## Source boundary

The Cordis build driver resolves and pins the source extension before
passing extended source to it. The extension parses the `using` header
and preserves the guest text and its source offsets. Start with an explicit
fence whose delimiter can be changed. The extension can also expose
`using "python" { ... }` and `using "typescript" { ... }` when the selected
adapter can find the closing boundary with its guest lexer. Python strings,
TypeScript template strings and regular expression literals can all contain
braces. Use ordinary external source files for larger guest modules with
the same interface and execution semantics.

The extension returns generated K modules, guest artifacts and a source
map to the driver. The driver passes the modules through the normal compiler
and maps its diagnostics back to the extended source. A future editor
integration can use the same locked transform for its overlays. The default
compiler receives ordinary K, and the core grammar reserves no keyword for
the extension.

The Python and TypeScript adapters implement the same fenced syntax. These
examples use separate project locks to select their respective adapters.

<!-- fragment -->
```klang
let result, error = using "python" (left: i64 = x, right: i64 = y) -> i64 {|
answer = left + right
|};

let same_result, error = using "typescript" (left: i64 = x, right: i64 = y) -> i64 {|
export const answer: bigint = left + right;
|};
```

The Python adapter reads `answer` from the execution scope
and the TypeScript adapter reads the module export. `-> i64` is the expected
success type. The extension lowers the expression to a call that can also
return a guest error. The block receives K values and services through
explicit arguments and imports. Bind values as data in the guest runtime.
The guest program text remains fixed.

## Optional build driver

Build and install the driver and its C ABI headers as a separate component.
Use the compiler from an existing K installation.

```sh
cmake -S . -B build-source -DK_BUILD_CORDIS_SOURCE=ON
cmake --build build-source --target k-cordis-source
cmake --install build-source --component cordis-source --prefix ./cordis-tools
```

Select a native service named `source-transform` in a version 2 Cordis
manifest. Its library exports `k_cordis_source_transform_v1` from
`include/cordis/source_transform.h`. Pin it and its declared inputs before
calling the driver. The project inputs are captured for each invocation.

```sh
k cordis resolve source-transform --manifest project/cordis.json > project/cordis.lock
cordis-tools/bin/k-cordis-source check --manifest project/cordis.json \
  --lock project/cordis.lock --root project --input helper.src program.ext
cordis-tools/bin/k-cordis-source build --manifest project/cordis.json \
  --lock project/cordis.lock --root project --input helper.src -o app-bundle program.ext
```

The v1 host activates one trusted native transform. It passes the entry,
any `--input` files in declaration order, and the locked provider inputs as
byte slices. The transform emits one generated entry, ordinary `.k`
modules, optional opaque artifacts, and non-overlapping byte spans. Use
relative paths with ASCII letters, digits, dots, hyphens, and underscores.
Diagnostic locations use 1-based UTF-8 byte columns. The driver remaps
locations in the compiler's JSON diagnostic array where spans are supplied.
An unmapped generated location uses `source/` followed by its module name.
The optional `last_error` ABI suffix reports transform diagnostics directly
in the original request's source coordinates.

The compiler runs with `--offline`. By default it uses a temporary cache
and resolves imports from the emitted modules and installed library. Set
`K_HOME` outside the bundle to use an existing offline package and artifact store.
Specify `--compiler PATH` or use `k` on `PATH`. A native build can also
forward `--cc`. A successful build publishes a new bundle directory with
`program` (`program.exe` on Windows), generated `source/` files, optional
`assets/`, and `source-build.json`. The receipt records the Cordis graph,
source map, and content digests. Input drift or a failed check leaves the
destination unpublished. Choose a fresh output directory for each build.

## Typed execution boundary

- Begin with checked scalar, UTF-8 string, and byte buffer values. Add records
  and named resource handles once their ownership rules can be enforced. Copy
  by default. A zero-copy buffer needs a borrow or retain contract that both
  runtimes can enforce. Python integers outside `i64` fail conversion.
  TypeScript maps exact `i64` values through `bigint`.
- Return a typed result containing either the converted value or a foreign
  error with the language, source range, and guest stack. Document the instance
  and state lifetime so repeated evaluations have defined behavior. An async
  block returns a task whose cancellation and cleanup follow the adapter's
  execution contract. A running block retains the adapter generation it
  started with through completion and cleanup.
- The Cordis driver verifies that declared adapters are present and compatible.
  The standard K checker validates the generated K-side signature. A selected
  adapter can validate guest source without executing the block. The build lock
  records the source extension, adapter and engine identities, effective
  configuration and declared inputs. Cache entries also cover generated
  sources and artifacts and the target. The driver can call the existing K CLI
  in the first implementation.
- Admission depends on the selected target. A freestanding target can run
  precompiled components with a supported ABI and resources. A hosted target
  can also load a process, interpreter, or Wasm component. Pin the adapter and
  record the mode with the build so behavior can be reproduced.

## Adapter implementations

The TypeScript adapter embeds QuickJS to transpile and evaluate each block.
The runtime profile can reuse this work and supply module resolution and
Node.js APIs through host services. For other languages,
an adapter can marshal calls to a process or compile code to a Wasm component
with a declared interface. [CPython embedding](https://docs.python.org/3/extending/embedding.html) ·
[TypeScript runtime](functions_and_typescript_runtime.md#runtime-architecture) ·
[Wasm Component Model interfaces](https://component-model.bytecodealliance.org/design/wit.html)

An in-process engine shares the host's process privileges. Workloads that
require a hard timeout or tenant isolation should run in a constrained process
or Wasm sandbox. The host supplies only the granted file, network, memory,
or device capabilities. An isolated adapter can be stopped at its execution
boundary. An in-process adapter must cooperate with cancellation and retain
any resources the guest is still using. [WASI capabilities](https://wasi.dev/security)

## Optional CPython adapter

Build against a local CPython 3.12 or newer with its embedding headers and
library. CMake installs two native components alongside the optional source
driver. The build bundle copies the locked bridge library and requires the
compatible CPython installation and standard library at run time.

```sh
cmake -S . -B build-python -DK_BUILD_CORDIS_SOURCE=ON -DK_BUILD_CORDIS_PYTHON=ON
cmake --build build-python --target k-cordis-source k_cordis_python_transform k_cordis_python_runtime
cmake --install build-python --component cordis-source --prefix ./cordis-tools
cmake --install build-python --component cordis-python --prefix ./cordis-tools
```

Use the installed transform library as the selected `source-transform`
provider. Set its `inputs.runtime` to the matching bridge library. This
macOS example places `cordis.json` in the same directory as `cordis-tools`.
Use the installed filenames for your host.

```json
{
  "version": 2,
  "selections": { "source-transform": "python" },
  "providers": {
    "python": {
      "service": "source-transform",
      "version": 1,
      "library": "cordis-tools/lib/cordis/libk_cordis_python_transform.so",
      "inputs": { "runtime": "cordis-tools/lib/cordis/libk_cordis_python_runtime.so" }
    }
  }
}
```

Resolve `source-transform` with `k cordis resolve` and build with
`k-cordis-source` as above. The opening fence ends its line. The closing
`|}` starts a line after optional indentation. Each block preserves its
Python indentation and is embedded in generated K. The bundle also retains
the original Python as a readable artifact. K binding expressions keep
their ordinary types and evaluation order. Parenthesize an expression whose
spelling contains a comma outside parentheses, brackets, or braces.

The `i64`, `text`, and `bytes` types map to a Python `int`, `str`, and
`bytes`. The K text and byte results are owned byte arrays. UTF-8 text
conversion is strict and Python integers must fit `i64`. A block must
assign `answer`. Destructure the result into the value and an owned error
string. The error is empty on success. On failure the value is zero for
`i64` or an empty array for text and bytes. A Python exception returns its
traceback and the original source location. Type and range errors return
an explanatory message. Each call uses a fresh globals dictionary and
shares the process interpreter and module cache.

During `check` or `build`, the adapter compiles the Python source to validate
syntax. It reports errors in the original file's UTF-8 byte columns. Guest
statements run when the built K program evaluates the block. This adapter
executes synchronously in the program's process and uses the CPython
installation selected when the adapter was built. Process isolation and
cancellation can be supplied by a future Cordis runtime adapter.
[CPython configuration](https://docs.python.org/3.12/c-api/init_config.html)

## Optional TypeScript adapter

Enable the adapter explicitly to fetch the pinned QuickJS and TypeScript
archives. CMake verifies their SHA-256 digests and installs the compiler
script and licenses with the optional components. QuickJS runs the compiler
script during transformation and runs the emitted JavaScript in the bundle.
[QuickJS C API](https://bellard.org/quickjs/quickjs.html#QuickJS-C-API) ·
[TypeScript Compiler API](https://github.com/microsoft/TypeScript/wiki/Using-the-Compiler-API)

```sh
cmake -S . -B build-ts -DK_BUILD_CORDIS_SOURCE=ON -DK_BUILD_CORDIS_TYPESCRIPT=ON
cmake --build build-ts --target k-cordis-source cordis-typescript
cmake --install build-ts --component cordis-source --prefix ./cordis-tools
cmake --install build-ts --component cordis-typescript --prefix ./cordis-tools
```

Select this transform in the project's Cordis manifest. Put the manifest
beside `cordis-tools` and use the installed library filenames for your host.
Resolve and build it with the same commands as the optional build driver.

```json
{
  "version": 2,
  "selections": { "source-transform": "typescript" },
  "providers": {
    "typescript": {
      "service": "source-transform",
      "version": 1,
      "library": "cordis-tools/lib/cordis/libk_cordis_typescript_transform.so",
      "inputs": {
        "runtime": "cordis-tools/lib/cordis/libk_cordis_typescript_runtime.so",
        "compiler": "cordis-tools/share/cordis/typescript/typescript.js"
      }
    }
  }
}
```

`i64`, `text`, and `bytes` become `bigint`, a UTF-8 string, and `Uint8Array`.
Each block evaluates synchronously in a fresh QuickJS runtime and exports
its result as `answer`. Results are copied into owned K values. A missing
result, wrong type, integer overflow, or guest exception returns an error
through the same pair convention as the Python adapter.

During `check` and `build`, `transpileModule` validates syntax and lowers
constructs such as `enum`. The K checker validates the generated K-side
binding types. Full TypeScript semantic checking belongs to a separate
project check. The transform keeps `.ts`, emitted `.js`, and `.js.map` in
`assets/typescript`, along with the locked runtime bridge and the QuickJS
license. Syntax errors
use K UTF-8 byte columns. Runtime exceptions map JavaScript stack lines
back to the original K file using the TypeScript source map.
[TypeScript Compiler API](https://github.com/microsoft/TypeScript/wiki/Using-the-Compiler-API) ·
[Source map format](https://tc39.es/ecma426/)

Module resolution, the Node.js compatibility layer, and isolated cancellation
are future milestones. Projects with both guest languages in the same source
graph require a composing transform.

## Verification

The `cordis_source_transform` test exercises the versioned contract with
a fixture that emits modules, a binary artifact and a source map. The
`cordis_python_guest` test builds real Python blocks and checks value copies,
UTF-8 syntax diagnostics, `SystemExit`, integer overflow, and runtime lock
invalidation. `cordis_typescript_guest` exercises real TypeScript lowering,
BigInt limits, binary views, UTF-8 diagnostics, stack mapping, and locked
compiler input. It checks and launches a relocated bundle with an empty
executable search path.
