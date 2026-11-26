# K development roadmap

K aims to become a first choice for operating system components, cloud
services, and AI infrastructure. The first production workload is a cloud
inference gateway. A bootable freestanding target tests the same core under
bare-metal constraints. An optional TypeScript runtime expands adoption by
running existing projects as their Node.js compatibility requirements are met.
[Architecture](architecture.md)

## Design principles

1. **Compose through the Cordis microkernel.** Keep language semantics and
   safety checks in the compiler core. Give users a project configuration
   that selects versioned compiler extensions, host services, guest-language
   runtimes, and device backends. Publish the resolved selection with the
   build. Support static composition for freestanding deployments and dynamic
   composition for hosted ones.
2. **Make resource contracts explicit.** Memory, files, sockets, and device
   buffers have an owner, a lifetime, and a capacity or access policy. The
   compiler checks ownership and valid borrows. The host accounts for dynamic
   quotas and grants access through typed handles. A foreign call must state
   whether it copies, borrows, or retains a resource.
3. **Give the compiler enough information to check behavior.** Preserve the
   reference and data-stack rules across closures, suspension, C calls, and
   device operations. Distinguish completion, cancellation, and recoverable
   errors. Require an operation to finish using a resource before reclamation,
   including after the caller has been cancelled.
4. **Build for explicit targets.** A freestanding program can use a fixed
   memory region supplied at startup. Hosted services can use virtual memory
   and an event loop. Optional components carry the JavaScript engine and
   device backends. Each target's available facilities and native dependencies
   are visible in its build configuration.
5. **Enforce trust at the execution boundary.** Hosted components get explicit
   capabilities. Native libraries share the host's privileges. Untrusted
   extensions execute across a process or Wasm boundary.
6. **Reuse existing ecosystems through stable boundaries.** Use C headers and
   ABI checks for native integration, existing inference engines for the first
   AI workload, and a conforming JavaScript engine for TypeScript. Verify each
   boundary with real workloads and explicit compatibility tests.

## Milestones

The milestones below are planned. The [language reference](reference.md) and
[CLI reference](command_line.md) describe the features available today.

| Order | Runnable result | Critical dependency |
| --- | --- | --- |
| 0 | A versioned release can be rebuilt and measured from locked inputs | Today's compiler, toolchain, and release scripts |
| 1 | A returned closure completes or cancels a task and closes an owned C handle once | Checked capture layout, resource lifetime, and task frames |
| 2 | A live language server replaces a diagnostic provider while a hosted service exercises bounded I/O | A Cordis registry can be prototyped during milestone 0. Production I/O uses the resource rules from milestone 1. |
| 3 | A deployed inference gateway swaps a CPU or GPU provider during a live request | Cordis lifecycle and bounded, cancellable streams |
| 4 | An existing TypeScript CLI runs from its locked dependency tree without Node.js installed | An early embedded-engine prototype and the hosted services from milestone 2 |
| 5 | A freestanding image boots, handles an interrupt, and runs a driver | A QEMU smoke test from milestone 0 and explicit target layout and storage |

Use milestone 1's resource test as the first language implementation slice.
In parallel, keep the Cordis diagnostics, JavaScript execution, and QEMU boot
experiments small enough to reveal requirements for the production design.
Each experiment records the ABI, target assumptions, and failure behavior it
exercises. Promote interfaces when their reference workload passes.

### 0 Reproducible baseline

- Record supported host and compiler versions, confirm clean source builds and
  release archives, and exercise the JIT and native paths in continuous
  integration. Preserve machine-readable diagnostics and stable source spans.
- Pin dependency contents and toolchain versions. Establish cold and warm
  measurements for compile time, disk use, and runtime behavior. Measure the
  core install, optional components, dependency store, and cache separately.
- Exit when a clean machine can build, check, and run the baseline fixtures and
  another build with the same locked inputs can reproduce the release artifact.
  Publish the pinned setup and benchmark commands. [Testing](testing.md)

### 1 First-class functions and asynchronous resource ownership

- Implement typed function values that can be stored, returned, passed, and
  selected at runtime. Add concrete closure environments, lifetime-checked
  captures, and explicit callable representations for mixed capture layouts.
  Preserve the specialized trailing-block calls used by `map`, `filter`, and
  `fold`. Add checked effects such as purity once the call model is stable.
- Add typed resource handles and scope cleanup. Implement `async fn`, `await`,
  and task scopes with cancellation, join, and cleanup. Add typed decorators
  with metadata and signature-preserving wrappers. Use familiar TypeScript and
  C# surface conventions where they fit these semantics.
- Exercise C layout and ownership against compiled headers. Add typed wrappers
  for callbacks, with a defined thread and re-entry policy. Exit when both
  execution paths agree on closure behavior and an async callback releases
  its resources exactly once on success, error, and cancellation. The checker
  must reject escaped borrows. [Functions](design/functions_and_typescript_runtime.md#first-class-function-values) ·
  [Async functions and decorators](design/async_and_decorators.md)

### 2 Cordis and bounded host I/O

- Implement a shared registry for compiler and runtime extensions. Load native
  K and C components through a versioned ABI. Start with a diagnostic component
  for `k lsp`. Pin the component set for each compile and include extension
  versions, configuration, and tracked inputs in artifact cache keys.
- Resolve explicitly selected providers and record the graph with the build.
  Test one configuration with a custom provider and one with the default
  provider. A minimal installation must omit unselected runtime engines and
  backends. Missing or incompatible providers report their dependency chain.
- Build cancellable file, socket, timer, and subprocess operations with explicit
  resource ownership. Add bounded streams and backpressure as library contracts.
  Expose the active service graph, permissions, and structured failures to
  editor and agent harness integrations.
- Exit when a live language server can replace a diagnostics component and a
  running service can swap providers. Failed activation leaves the previous
  provider active. Calls already in flight complete or cancel and release
  their handles before the old library is unloaded. [Cordis microkernel](design/cordis_microkernel.md)

### 3 Cloud and AI reference workload

- Deploy a streaming inference gateway with an existing model backend reached
  through C. Expose deadlines, tenant quotas, and bounded request queues. Use
  Cordis to select model providers and swap them while requests are in flight.
- Model tensor element type, shape, stride, device, and ownership. Use DLPack
  for tensor exchange with Python frameworks. Keep a buffer alive until its
  device completion event, including on request cancellation. Add CPU and one
  GPU backend before expanding backend coverage.
- Publish an OCI image and collect OpenTelemetry traces, metrics, and logs.
  Exit when load tests show bounded queues and memory under overload, repeated
  cancellations release resources, and CPU and GPU results match a defined
  numerical tolerance. Record startup time, tail latency, throughput, peak
  memory, and artifact size against a pinned reference setup.

### 4 TypeScript runtime compatibility

- Prototype the embedded JavaScript engine and TypeScript transforms in
  parallel with milestone 1. Verify multi-module execution, top-level `await`,
  source maps, and a machine without a Node.js executable. Use the TypeScript
  compiler to produce reference JavaScript for syntax that requires lowering.
- Evaluate typed `using` blocks with Python and TypeScript adapters selected
  as Cordis components. Start with scalar and byte values and map guest errors
  back to K source. Verify adapter selection, exact value conversion, and an
  isolated cancellation path before adding handles and callbacks.
  [Embedded languages design](design/embedded_languages.md)
- With the host services from milestone 2, support the module resolution and
  built-ins exercised by an existing TypeScript CLI with both ESM and CommonJS
  dependencies. Compare stdout, stderr, exit status, files, and errors against
  a pinned Node.js release with the same locked dependency tree.
- Expand coverage to an existing TypeScript agent harness with streaming,
  subprocesses, and cancellation. Add workers, test APIs, and Node-API addons
  as the compatibility fixtures require. Publish a matrix of the runtime APIs,
  config options, and workloads that have passed. [TypeScript runtime design](design/functions_and_typescript_runtime.md#runtime-architecture)

### 5 Freestanding and operating system support

- Start a single RISC-V QEMU target during milestone 0. Link a small K program
  from a fixed-memory startup environment. Verify that it boots and writes to
  the serial console with no host libc dependency. Maintain this smoke test
  as the compiler and core runtime evolve.
- Add explicit target layout, linker sections, MMIO, interrupts, atomic
  operations, and memory ordering. Provide page-backed storage when the target
  can supply pages. Use fixed regions with checked growth on targets that
  cannot. Define which calls are valid inside interrupt handlers.
- Exit when the QEMU target handles a timer interrupt, allocates and recycles
  pages, and runs a small driver with a typed ownership boundary. Extend to
  a real board and verify device I/O and isolation there before committing to
  wider hardware support.

## Release criteria

- **Safety and correctness.** Run positive and negative tests for ownership,
  runtime bounds, C ABI layout, backend parity, and asynchronous teardown.
  Exercise cancellation, failed replacement, resource exhaustion, and cache
  invalidation on the normal host entry points.
- **Adoption.** Advance each supported target when the reference workload runs
  on a fresh installation with documented dependencies. Report compatibility
  by verified workload and platform.
- **Cost.** Compare initial download, installed bytes, deduplicated dependency
  and cache usage, cold and incremental build times, and runtime memory. Use
  content-addressed reuse and cache pruning to control growth. Set budgets
  against the measured baseline for each distribution profile.
- **Compatibility.** Version the language specification, diagnostic schema,
  component ABI, and adapter contracts. Publish tested host and runtime
  combinations. Test that upgrades reject unsupported component versions
  before they activate and preserve locked builds when inputs match.
