# K development roadmap

K aims to become a first choice for operating system components, cloud
services, and AI infrastructure. The first production workload is a cloud
inference gateway. A bootable freestanding target tests the same core under
bare-metal constraints. An optional Cordis runtime profile expands adoption
by running existing TypeScript projects as their Node.js compatibility
requirements are met.
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

## Execution priorities

1. **Keep resources alive through their last use.** A borrow must survive
   each suspension where it is used. A socket, foreign buffer and provider
   generation stay live until every operation using them has completed, even
   when their caller has cancelled. This sets the order for task frames, host
   I/O, foreign runtimes and device backends.
2. **Make composition explicit and repeatable.** Select providers through
   Cordis and bind the selected versions, configuration and tracked inputs to
   the build. Exercise the same contract with linked and dynamically loaded
   providers. A running K application needs an observable activation and
   teardown path before it can serve as the integration point for adapters.
3. **Bound the cost of using K.** Verify the bytes and tools needed for a
   clean install. Reuse immutable inputs across projects and give users
   control of disk use. Measure builds and runtime workloads separately so
   changes can be attributed to the compiler, backend or service.

Use the CPU inference gateway as the first hosted reference workload for these
contracts. Its request queue, model call, cancellation and provider
replacement exercise the same boundaries that the TypeScript runtime and
agent harness will need. Use a small freestanding image to validate target
layout and static composition in parallel.

### Current implementation

| Area | Verified slice | Next boundary |
| --- | --- | --- |
| Release and measurement | The desktop CI matrix defines native and TinyCC coverage. Two local macOS arm64 core builds produced identical archives and passed isolated install checks. The verifier records input digests, sizes and timings. | Reproduce the bundled profile and qualify Linux as the first cloud release host. Collect runtime samples against a pinned Rust reference. |
| Cordis | The LSP and native hosts share activation, rollback and provider retirement. A K application verifies locks for startup and replacement of a TCP provider with a queue policy dependency. Its old connection keeps both generations alive while the replacement serves a new request. `k check` and `k build` verify and record the compile-time diagnostics graph. | Add service contracts for inference and extend executable cache coverage. |
| Concrete functions and C resources | Concrete closures can be stored and returned with checked captures. Borrowed and bounded owned callables select between different capture layouts at runtime and check read-only reference captures. A C ABI fixture counts releases when a linear handle is transferred into a concrete closure. Specialized trailing blocks retain their existing call path. | Check resource transfer and suspension for runtime-selected callables. Expand ABI fixtures to imported C headers and a second target layout. |
| Asynchrony | K tasks can yield or await children across expressions, branches and loops. Async decorator layers forward through checked child tasks. The C and TCP fixtures verify input cleanup before start and native drain before socket closure. | Define explicit task types and result storage for values with ownership or variable size. |
| Packages and disk | Independent projects share runtime objects and locked native package outputs. The cache reports both and applies one prune budget while preserving active links and JIT libraries. Offline builds recreate reclaimed outputs from retained inputs. | Measure core and optional provider sizes across release profiles. |

[Build and test recipes](testing.md) ·
[Cordis lifecycle](design/cordis_microkernel.md#shared-native-service-lifecycle) ·
[Scoped readers](stdlib.md#scoped-file-readers) ·
[Owning C handles](reference.md#owning-c-handles) ·
[Concrete closures](reference.md#concrete-closures) ·
[Cancellable TCP receive](design/cordis_microkernel.md#cancellable-tcp-receive)

### Implementation map

These are the delivery tracks and their exit tests. Use the next section to
pick an incremental change from a track.

| Step | Depends on | Implementation | Exit test |
| --- | --- | --- | --- |
| **P0.1 Measurable release** | Current suites and build scripts | Record source, toolchain and SDK versions, dependency digests, flags and archive timestamps. Capture a core release and a bundled release separately. Add a repeat-build check to release verification. | Two clean builds of the same profile and pinned inputs produce the same archive digest. An isolated install runs its samples. Report known skips and the size and timing baseline by profile. |
| **P0.2 Cordis in a second host** | Existing LSP prototype | Extract the host-independent activation and generation lifecycle from the diagnostics integration. Use it in a small native host with statically and dynamically registered versions of one service. | The same lifecycle checks pass in the LSP and native host. New requests see the replacement, in-flight requests keep the old provider, and failed activation preserves the active generation. A minimal build can use only the static provider. |
| **P1.1 Concrete closures** | Current callable arguments and reference checker | Specify closure capture and return semantics before locking syntax. Lower a fixed-size captured environment and its call through the existing C backend. Add borrowed captures once by-value captures work. | A returned closure can be stored, passed to a generic function, and called after its creator returns. Copy semantics and the closure's own `return` are observable. The checker accepts a sufficiently long-lived borrow and diagnoses a captured borrow of a dead local. Native and TinyCC outputs agree. |
| **P1.2 Transferable C resource** | Scoped C fixture | Extend the fixture with a typed single owner, validated C layout and an explicit foreign borrow or retain contract. Move a handle into a closure once P1.1 is available. | Counts show exactly one release across completion, early return and recoverable error after transfer. The checker rejects a second use after transfer and a borrow that escapes. Exercise both `k run` and `k build`. |
| **P1.3 Async and cancellation** | P1.1, P1.2, and the native task driver | Lower `async fn` to checked poll stages with fixed-size storage. Add task scopes, join, and cancellation, then connect real foreign completion to the same lifetime checks. | A task uses an owned C handle across suspension. Success, error and cancellation release it once. A cancelled task stops issuing work and retains the resource until foreign completion. Cover cancellation before start, while pending and during shutdown. |
| **P1.4 Runtime callable values** | P1.1 | Add checked borrowed callables and explicit bounded owned storage for different closure layouts. Exercise composition and `map`, `filter` and `fold`. | A branch can select closures of different layouts. The checker rejects mismatched signatures, escaped borrows and insufficient storage. Reused function arguments retain their specialized native path. |
| **P1.5 Typed decorators** | P1.1, then P1.3 for async wrappers | Use checked function wrappers and nominal metadata records with ordinary provider registration. Extend metadata to type and field declarations. | A decorated call preserves its signature, observable order and cleanup. Invalid applications are diagnosed at the declaration. Explicit and decorated registrations agree. Type and field metadata preserve identity and layout. |
| **P2.1 Locked Cordis graph** | P0.2 | Resolve a selection for `k check` and `k build` and hold that snapshot for the invocation. Record provider versions, configuration and tracked inputs with the build and use the same identity in the artifact cache. Expose the graph and failures through the CLI as structured data. | Changing one provider input invalidates its dependent artifacts. An unchanged graph can build offline from locked inputs. A missing or incompatible provider reports its dependency path before activation. |
| **P2.2 Bounded hosted services** | P0.2 and P1.3. P2.1 gates release. | Implement one bounded stream service and cancellable I/O. Extend to files, timers, sockets and subprocesses as the reference workloads require them. Carry resource and provider ownership through completion. | A slow consumer reaches the configured queue limit and delays its producer. Cancelling the consumer and replacing the provider with work pending releases every handle once, after completion. |
| **P2.3 Bounded installation and caches** | P0.1 and P2.1 | Package the core and optional providers separately. Reuse immutable dependencies and build artifacts identified by content and build inputs across projects. Add cache accounting and pruning. | The core runs without optional engines. Multiple projects reuse identical inputs. Pruning meets the configured disk budget, preserves artifacts in use and leaves enough information to rebuild evicted entries. |
| **P3 Reference releases** | Workload-specific milestones below | Ship the CPU gateway first on the hosted path, then a GPU provider. Pair the TypeScript path with a locked external CLI, then an agent harness. Develop the freestanding path against a bootable target. | Run the reference program from a fresh install and record compatibility, resource bounds, and regression measurements for that profile. |

### Next mergeable changes

The locked TCP application host serves a bounded CPU inference gateway with
the ONNX Runtime provider. Extend the ownership boundary to C header imports
and foreign completion. For guest languages, start a Cordis source transform
driver with a versioned contract and a fixture that emits ordinary K. Keep
release, benchmark and freestanding verification in their own tracks.
[Locked checks and builds](design/cordis_microkernel.md#locked-checks-and-builds) ·
[Hosted readiness](design/async_and_decorators.md#hosted-readiness) ·
[Cache usage and pruning](command_line.md#cache-usage-and-pruning)

| Priority | Change | Implementation sequence | Acceptance |
| --- | --- | --- | --- |
| **Implemented for statement and binding sites** | **P1.3e. Check control flow across suspension** | Structured polls resume within `if`, `while`, `loop`, and integer `for` bodies. Early returns and cancellation use the defers registered on the active path. | Both backends agree on completion and unwind order when a branch skips an await or a loop repeats one. Cancellation drains the active child before running that path's defers. |
| **Implemented for checked layouts** | **P1.3f. Extend suspension coverage** | Typed sites support staged values, local loans, and resumable sequence walks. Read-only references to whole arrays can observe append across suspension. Direct invocations and lexical task scopes can resize independent local arrays through writable inputs. Forwarded self-relative links retain the backing owner's lease. Copied structs and arrays can borrow from several independent owners. Direct and indirect holder views protect the reachable limited-array slots, including cycles, copied inputs and cancellation cleanup. Earlier arguments staged across an enclosing call borrow from the saved value. Shared leases cover independent root components, and legacy `reusable` pools retain elements and freelists across suspension. | Check evaluation order, bounds, sequence mutation and cancellation in both backends. Scoped writers exclude overlapping loans. Borrowed holders survive growable-array append and cancellation. Limited-array writes can modify independent slots and must reject reuse of a popped holder's slot while its loan lives. The initial liveness sample reduced the C frame from 2,620 bytes to 32 bytes. |
| **Implemented for fixed-commit native builds** | **P2.3c. Manage locked native package outputs** | Use the Git revision and native build inputs to identify shared artifacts. Count runtime and native entries under one budget. Retain Git checkouts and CMake dependency sources. | Two projects reuse the same native output. Pruning preserves active links and JIT libraries. Plain C and CMake builds recreate reclaimed outputs offline. |
| **Verified locally on macOS arm64 and Linux arm64 and x64** | **P0.1b. Reproduce the bundled release** | Verify native dependencies and record the toolchain for each profile. The Linux cloud profile delivers a prebuilt inference gateway with a locked CPU provider. | Two independent builds of each profile match by payload and archive digest. Programs from each isolated archive run with an empty `K_HOME` and no dependency fetches. Report bytes and timings by profile. |
| **Baseline verified on macOS arm64** | **Performance baseline for K and Rust** | The paired runner freezes workload inputs and expected results. Profile expression parsing and separate the compiler and C toolchain costs in cold builds, then repeat on Linux. | Equivalent outputs pass in both starting orders. The report records time, peak RSS, build cost, disk use and uncertainty, with a separate TinyCC series. |
| **First hosted reference workload** | **P3. CPU inference gateway** | The loopback gateway serves framed results with deadlines and bounded admission. The benchmark measures model sizes and allocator overlap. Linux startup can require a cgroup v2 memory boundary. The supervisor rolls back a failed rollout and retries once after a confirmed worker OOM. | A reference client observes the expected output from an isolated hosted release. Admission and memory stay bounded under load, and a cancelled client during a provider switch releases resources after drain. Save startup, throughput, p95/p99, and peak RSS. |
| **Synchronous forms implemented for checked signatures** | **P1.4. Add runtime-selected callables** | `fn&` borrows a named environment and checks read-only captures across alias writes and generic forwarding. `fn[N]` owns an inline environment and tracks read-only captures of known fixed storage. Generic `map`, `filter`, `fold` and composition fixtures exercise both forms. | Different capture layouts work under one signature. Negative tests cover escaping borrows, mismatched signatures, unstable capture replacement and insufficient storage. Compare the specialized and runtime-selected paths. |
| **Named lookups and scoped enumeration implemented** | **P1.5. Add typed decorators** | Typed `@` metadata can be read by declaration or enumerated in an explicit namespace or field owner. Synchronous wrappers use checked calls, and asynchronous wrappers use a single-use `await next()` with checked task stages. | Explicit and discovered Cordis registrations agree. Scoped enumeration is deterministic and preserves overload identity. Stacked wrappers preserve argument, return and cleanup order. |
| **One locked native transform implemented** | **P2.4. Optional Cordis source driver** | Transform explicitly supplied sources outside the core compiler. Extend the host to compose guest adapters and feed the same generated tree to editor analysis. | The fixture produces multiple K modules and a binary artifact. Offline check and build agree, diagnostics map to the original file, and lock or compilation failures preserve existing bundles. |
| **Fenced Python blocks implemented** | **P4.1. CPython guest adapter** | Lower typed Python inputs and results through the optional source driver. Pin the native runtime bridge in the graph and publish it with the bundle. | Check without running guest statements, run the relocated bundle, and verify UTF-8 and binary results, mapped syntax errors, exceptions, and overflow. |
| **Fenced TypeScript blocks implemented** | **P4.2. QuickJS guest adapter** | Use the locked TypeScript compiler to lower guest blocks and retain emitted JavaScript and source maps. Publish the separate QuickJS runtime bridge with the bundle. | Run a relocated bundle without Node.js, verify exact BigInt and binary conversions, map errors to the original source, and reject a changed compiler input. |
| **Local ESM runner implemented** | **P4.3. Cordis TypeScript project runner** | Select the frontend and engine from the same Cordis graph. Load project-relative modules and drive Promise completion or rejection. | Static and dynamic imports share identity, top-level `await` completes without Node.js, and dependency failures point to the original source. |

#### First implementation steps

1. Verify supervised worker recovery and provider rollback after a process
   exhausts its [Linux deployment memory budget](design/cordis_microkernel.md#linux-deployment-memory-budget).
2. Record cold and warm build cost, managed bytes and retained input bytes
   for the core release and optional native providers.

For each change, run the named fixture and its applicable negative tests,
then the baseline. Record commands, platform, skipped paths and produced
evidence with the change. Revisit the dependency order if a fixture
invalidates an ownership, ABI or storage assumption.

### Early target probes

Keep these dependency probes small enough to run while the core contracts
are being implemented.

- **C headers and targets.** Use an existing C library and header as input to
  a binding prototype. Cover scalar types, fixed-layout records, pointers and
  function declarations. Compare size, alignment, offsets and calls against
  the target C compiler. Diagnose unsupported declarations at the header
  location and attach ownership and callback rules explicitly at the K
  boundary. Verify the ABI on the development host and one second target.
- **Cloud and AI.** Call one CPU inference backend through C, with a pinned
  model and input. Record output tolerance, latency and peak memory. Use the
  same call in the gateway after the stream and replacement tests pass. Add
  DLPack and one GPU provider after the CPU gateway passes the overload and
  cancellation tests. Reuse the completion contract for device buffers.
- **TypeScript and embedded languages.** Run a multi-module fixture with
  top-level `await` and source maps in the optional Cordis runtime profile.
  Run one `using "python" { ... }` block and one `using "typescript" { ... }` block
  with typed scalars or bytes. Use Cordis to select and pin the adapters.
  Verify typed conversions, source locations and cancellation, with a
  separate process or Wasm boundary for untrusted code. After hosted service
  composition is available, compare one real locked CLI against Node.js
  before expanding to an existing agent harness.
- **Freestanding.** Link a RISC-V QEMU image using fixed startup storage, boot
  it and assert one serial output line. Check for host libc dependencies.
  Run this smoke test independently of hosted I/O and use it to validate
  target layout before implementing interrupts and drivers.
- **WebAssembly.** The WASI Preview 1 command target builds and runs checked
  K programs with 32-bit layout and address-stable data stacks. Plain C
  package sources compile and link with the selected WASI toolchain, using
  target- and toolchain-separated package caches. Exercise optional browser
  and component-model hosts next; worker threads, dynamic externs, and CMake
  and builtin native packages remain unsupported. Measure fixed-stack
  capacity and the cost of growth checks on workloads that exceed the
  introductory examples.
  [WebAssembly target](design/wasm.md#wasi-command-target)

### Verification commands

From the repository root on macOS or Linux, configure a Debug build and run
the baseline and documentation checks. The baseline runner includes the
scoped C fixture and the positive and negative language fixtures. CI uses
the appropriate executable path for each platform.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DK_BUILD_LSP_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug -R '^(lsp_|cordis_)' --output-on-failure
python3 test/run_tests.py --exe build/k --profile baseline --cc native --require-clang
python3 docs/site/build.py --out build/site --check
```

Use the release recipes to reproduce the macOS and Linux cloud profiles
with their pinned inputs. A new language feature should add
a running fixture and a failing fixture for each new lifetime rule. Cover
parser round trips, source diagnostics and native/JIT parity through the
normal entry points. Run the Linux sanitizer profile for changes that can
affect memory ownership or ABI layout.
[macOS release recipe](testing.md#rebuilding-core-and-bundled-releases) ·
[Linux cloud release recipe](testing.md#rebuilding-the-linux-cloud-release)

### Performance and disk budgets

- Record the initial download, bytes installed for the core, bytes installed
  per optional provider, dependency store, per-project output and build cache
  separately. Include the required C toolchain and SDK on the K side and the
  required Rust toolchain on the other side. List shared system prerequisites
  separately. Measure clean and warm builds for one project and for multiple
  projects sharing inputs. Use the measured baseline to set a size budget for
  each profile and check that concurrent builds and pruning preserve live
  artifacts.
- Measure cold startup, cold and warm builds, application throughput and
  tail latency, and peak RSS. Record the platform, pins, optimization
  flags, inputs, expected output and individual samples. Keep native
  runtime measurements and compile-plus-JIT measurements in separate series.
  Use `bench/run_bench.py paired` to save individual runs and alternate the
  candidate and reference on the same host when measuring a change.
  [Paired benchmark recipe](testing.md#paired-k-and-rust-benchmark)
- Evaluate the 1.10 native throughput target using the geometric mean of
  `Rust time / K time` over a predeclared workload set. Cover numeric
  kernels, parsing, data structures and end-to-end I/O. Require identical
  outputs and equivalent resource and safety contracts. Track idiomatic
  implementations and matched algorithms separately. Reserve one workload
  per family for validation after tuning. For each optimization, profile the
  cost, measure the change against the prior K build, then rerun the Rust
  comparison and held-out workloads. Report per-workload ratios, confidence
  intervals and regressions. Repeat on the pinned host with reversed run
  order. Record a result as confirmed when the repeated measurements support
  a speedup of at least 1.10 after accounting for uncertainty. The historical
  report documents its own toolchains, samples and selection method.
  [Benchmark report](../bench/results.md)

The first frozen comparison on Apple M6 measured matched-score ratios of
1.092 (95% interval 1.011–1.187) and 1.053 (0.994–1.121) in the two starting
orders. The expression workload includes generation, parsing and
evaluation. It lagged in both runs. Both toolchains kept the executable on
warm builds. K spent roughly 2.4–2.5 times as long on the cold build in the
dependency-free suite.
[Paired benchmark recipe](testing.md#paired-k-and-rust-benchmark)

A compiler trace attributed most of the macOS cold build to compiling the
common native runtime object. The macOS package now supplies that object
for matching Apple Clang configurations. In an alternating comparison
across summation, expression parsing, graph traversal and log scanning,
the median fell from 605 ms to 145 ms. The runtime objects matched
byte-for-byte across the local and packaged builds.
[Native build and runtime cache](command_line.md#3-building-an-executable)

## Milestones

The milestones below are planned. The [language reference](reference.md) and
[CLI reference](command_line.md) describe the features available today.

| Order | Runnable result | Critical dependency |
| --- | --- | --- |
| 0 | A versioned release can be rebuilt and measured from locked inputs | Today's compiler, toolchain, and release scripts |
| 1 | A returned closure completes or cancels a task and closes an owned C handle once | Checked capture layout, resource lifetime, and task frames |
| 2 | A hosted K service exercises bounded I/O and replaces a locked Cordis provider while calls are in flight | The shared lifecycle already runs in the LSP and a second host. Production I/O uses the resource rules from milestone 1. |
| 3 | A deployed inference gateway swaps a CPU or GPU provider during a live request | Cordis lifecycle and bounded, cancellable streams |
| 4 | An existing TypeScript CLI runs from its locked dependency tree without Node.js installed | An early embedded-engine prototype and the hosted services from milestone 2 |
| 5 | A freestanding image boots, handles an interrupt, and runs a driver | A QEMU smoke test from milestone 0 and explicit target layout and storage |

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

- A scoped file reader now uses `defer` and a compile-time operation
  borrowed by the caller's block. A C ABI fixture checks release on normal
  completion, early return and a recoverable error. This establishes the
  scoped contract for a handle before closures and tasks can own one across
  calls or suspension. [Scoped readers](stdlib.md#scoped-file-readers)
- Generalize the implemented fixed-size closure environments and checked
  captures to runtime selection of different capture layouts. Preserve the
  specialized trailing-block calls used by `map`, `filter`, and `fold`. Add
  checked effects such as purity once the call model is stable.
- Extend typed resource handles and scope cleanup to task frames. Implement
  `async fn`, `await`, and task scopes with cancellation, join, and cleanup.
  Add typed decorators with metadata and signature-preserving wrappers. Use
  familiar TypeScript and C# surface conventions where they fit these semantics.
- Exercise C layout and ownership against compiled headers. Add typed wrappers
  for callbacks, with a defined thread and re-entry policy. Exit when both
  execution paths agree on closure behavior and an async callback releases
  its resources exactly once on success, error, and cancellation. The checker
  must reject escaped borrows. [Functions](design/functions_and_typescript_runtime.md#first-class-function-values) ·
  [Async functions and decorators](design/async_and_decorators.md)

### 2 Cordis and bounded host I/O

- Extend the shared LSP and native host lifecycle to compiler and runtime
  extensions. Load native K and C components through a versioned ABI. Pin the
  component set for each compile and include extension versions, configuration,
  and tracked inputs in artifact cache keys.
- After locked diagnostics, exercise one optional compiler analysis with a
  versioned input format and declared file inputs. Check that updating an input
  invalidates dependent output and that failed activation leaves the build
  artifact intact. Keep type, ownership and bounds verification in the core.
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

### 4 Optional Cordis TypeScript and guest-language profiles

- The Cordis project runner selects a locked frontend and JavaScript
  engine and runs local ESM modules with top-level `await`. Add Node.js
  compatibility services under explicit profile capabilities as the
  reference fixtures exercise them.
- Extend the versioned source driver to compose guest adapters. It already
  takes extended source and feeds ordinary K to `k check` and `k build`.
  Separate CPython and QuickJS adapters support fenced blocks with typed
  scalar, text, and byte results. Verify a project with both languages and
  exercise isolated cancellation before adding handles and callbacks.
  Verify that a core install builds ordinary
  K with an empty extension selection and no guest dependencies.
  [Embedded languages design](design/embedded_languages.md)
- With the host services from milestone 2, support the module resolution and
  built-ins exercised by an existing TypeScript CLI with both ESM and CommonJS
  dependencies. Compare stdout, stderr, exit status, files, and errors against
  a pinned Node.js release with the same locked dependency tree.
- Expand coverage to an existing TypeScript agent harness with streaming,
  subprocesses, and cancellation. Exercise one bounded file access and process
  invocation under explicit Cordis capabilities, and verify that cancellation
  drains the subprocess and its streams. Add workers, test APIs, and Node-API
  addons as the compatibility fixtures require. Publish a matrix of the runtime
  APIs, config options, and workloads that have passed.
  [TypeScript runtime design](design/functions_and_typescript_runtime.md#runtime-architecture)

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
