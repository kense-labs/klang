# Embedded language blocks

Design proposal. K can provide a first-class `using` block for guest code.
Projects select a Cordis language adapter and its execution mode explicitly.
The K compiler owns the block's boundaries, typed arguments, results, and
diagnostics. Each adapter owns its language frontend and execution engine.
[Cordis microkernel](cordis_microkernel.md#user-selected-components)

## Source boundary

`using "python" { ... }` and `using "typescript" { ... }` are useful shorthand
once the parser can delegate brace matching to a lexer for that language.
Python strings can contain braces. TypeScript template strings and regular
expression literals can do the same. A general extension surface needs a
boundary the K lexer can recognize on its own. Start with an explicit fence,
with support for changing the delimiter when it appears in the guest source.
Preserve each guest line and column so editors and runtime errors can point
back to the enclosing `.k` file. Use normal external source files for larger
modules, with the same interface and execution semantics.

The following sketch shows the intended binding and result contract. The
syntax, including the fence, is provisional.

<!-- fragment -->
```klang
let result = using "python" (left: i64 = x, right: i64 = y) -> i64 {|
answer = left + right
|};

let same_result = using "typescript" (left: i64 = x, right: i64 = y) -> i64 {|
export const answer: bigint = left + right;
|};
```

In this sketch, the Python adapter reads `answer` from the execution scope
and the TypeScript adapter reads the module export. `-> i64` is the expected
success type. The K expression yields a result that can also carry a guest
error. The block receives K values and services through explicit arguments
and imports. Bind values as data in the guest runtime. The guest program text
remains fixed.

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
- `k check` verifies the K-side signature and declared adapter requirements.
  A configured adapter can additionally validate the guest source. Build and
  check avoid executing the block. Cache compilation by source, adapter
  and engine versions, target, configuration, and locked dependency inputs.
- Admission depends on the selected target. A freestanding target can run
  precompiled components with a supported ABI and resources. A hosted target
  can also load a process, interpreter, or Wasm component. Pin the adapter and
  record the mode with the build so behavior can be reproduced.

## Adapter implementations

For trusted Python code, embed CPython through its C API and let the adapter
own interpreter state and Python objects. For TypeScript, reuse the optional
runtime's transform, source-map, and JavaScript engine services. Treat module
resolution and Node.js APIs as explicit host contracts. For other languages,
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

## First experiment

Select one Python adapter and one TypeScript adapter through Cordis. Run a
short function in each with the same typed inputs, and compare its result
with a native K function. Check embedded braces, guest errors mapped to K
source, missing adapters, integer overflow, and how isolated cancellation
releases resources. A K project that selects neither adapter installs and
links neither engine.
