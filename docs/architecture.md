# K architecture

The Cordis microkernel is the organizing principle for K. The compiler owns
language semantics and checks resource contracts. A shared component kernel
lets each project choose the services required by its target. The current
compiler already has a C backend, checked references, address-stable data
stacks, isolated workers, and two execution paths. The language server hosts
the first Cordis diagnostics service. An independent native host exercises
the same lifecycle with linked and dynamically loaded implementations of a
value service.
[Compiler implementation](implementation.md) ·
[Cordis diagnostics](design/cordis_microkernel.md#native-diagnostics-in-the-language-server)

## Domain boundaries in the code

The compiler's core domain is deciding whether a K program is valid and
what it means. The package resolver answers where the program's inputs come
from. The build and workspace have their own inputs and lifecycles. Keep
those vocabularies explicit at the points where they meet. This follows the
bounded contexts and context map of domain-driven design. [DDD Reference](https://www.domainlanguage.com/ddd/reference/)

| Context | Owns | Boundary |
| --- | --- | --- |
| Program loading | Import visibility, the source graph, and global initialization order | `ParseProgram` takes a root, standard-library search paths, executable location, package fetch mode, and an optional source overlay. It fills the caller's `Ast`. |
| Package resolution | Package source identity, checkout policy, sidecar configuration, and native declarations | `PackageRoot` resolves a local directory or cached Git checkout using the mode supplied by the request. Source inspection is shared by program loading and native builds. |
| Language semantics | Name resolution, types, lifetimes, and checked specializations | The compiler passes operate on one program's `Ast`. The AST and its derived nodes live for that analysis or build. |
| Build and execution | Generated C, toolchains, link inputs, caches, and process execution | The driver selects the build or JIT path. The native builder takes explicit options and materializes the artifacts for that path. |
| Editor workspace | Open buffers, analysis invalidation, cancellation, and published diagnostics | An editor buffer overlays disk text through `SourceProvider`. Each analysis uses offline package resolution and derives its indexes from the program that was checked. |
| Component lifecycle | Explicit selection, activation, replacement, and cleanup of a native provider | `cordis::Service` checks the provider ABI, starts a candidate before publication, and keeps the selected generation alive through each request. The service adapter owns the request data and ABI-specific checks. |

The CLI and the workspace both call the program loader. The loader asks
package resolution for roots and uses the parser to read source. It has no
dependency on building native artifacts. The CLI selects package fetch mode
per invocation and passes its offline choice to the native builder. The
workspace selects offline mode per analysis, including when embedded in a
process that also runs CLI commands. An import that needs an uncached package
reports a diagnostic at the import. These boundaries work with the current
single C++ translation unit for the compiler passes.

## Start with the resource

Every workload needs answers to the same questions. Who owns a resource, who
can use it, how long must it remain valid, and what happens at the limit? A
kernel driver asks these questions about a mapped page or device buffer. A
cloud request asks them about a socket, a deadline, and a queue slot. An AI
inference request asks them about a tensor and device work that may outlive
the request. K must express those answers across ordinary calls, closures,
`await`, foreign calls, and component replacement.

Keep the checks that the compiler can prove in K's type and reference
system. Give the host authority over conditions that depend on the running
system, such as granted access, quota consumption, device completion, and
available storage. A component may select how to fulfil a contract. Its
selection carries the same obligations at the call site.

## Kernel and component boundaries

| Layer | Owns | Extension surface |
| --- | --- | --- |
| Compiler core | K grammar, type and lifetime rules, checked program representation, and validation of generated source | Read-only diagnostics and analysis first. Code generation later takes versioned, validated inputs. |
| Target runtime | Data-stack storage, failure hooks, typed handles and task frames | Startup and storage supplied by the target. Hosted executors and I/O providers are selected components. |
| Cordis component kernel | Context tree, service resolution, registered effects, activation, cancellation and teardown | Versioned service contracts. Static registration for freestanding targets, dynamic loading for hosted targets. |
| Providers | Diagnostics, build integrations, I/O, tracing, language engines and device backends | Typed interfaces, explicit ownership and capabilities, and a versioned C ABI or isolated service protocol. |

Keep typed closures, `async fn`/`await`, and typed decorators in the language.
A closure's capture environment follows K's reference rules. An async body
has a task frame with explicit suspension and cleanup. Decorators attach
typed metadata or wrap declarations under the same checker. Executors,
metadata consumers, and the services they call are selectable. This lets a
freestanding program link its own scheduler while a hosted application uses
an event loop. [Functions](design/functions_and_typescript_runtime.md#first-class-function-values) ·
[Async functions and decorators](design/async_and_decorators.md)

## Composition and execution

1. **Resolve.** The project selects a target, components, and required
   service interfaces. Resolve dependencies within their context scopes and
   diagnose missing, incompatible, or ambiguous providers. Pin versions,
   content digests, and inputs in the lockfile. A missing provider is a
   configuration error with its dependency chain.
2. **Build.** Compile against the resolved graph. The cache identity covers
   sources, target, compiler and toolchain, enabled components, effective
   options, and tracked inputs. Bound cache size across workspaces and reuse
   results by content. Carry the resolved component graph with the artifact.
3. **Activate.** Check the component ABI and configuration before publishing
   services. The host grants resource access through typed handles. Trust
   determines where code runs. Native in-process components share host
   privileges. An isolated provider runs across a process or Wasm boundary
   with its own resource grants.
4. **Replace.** Prepare the new provider and affected dependents before
   exposing their registrations. Publish the new graph together. New calls
   use it while existing calls finish or cancel against the generation they
   started with. Handles, callbacks and tasks keep their generation live
   until cleanup completes. A failed activation leaves the previous graph
   serving requests. [Cordis microkernel design](design/cordis_microkernel.md)

For a compiler invocation, the resolved graph is stable for the entire
check or build. A language server can adopt a new graph between requests.
For a running service, a request's component references are fixed for the
duration of each retained operation, including foreign work that completes
after the request has been cancelled.

## Target configurations

| Target | First useful program | Runtime composition |
| --- | --- | --- |
| Freestanding | Boot a RISC-V QEMU image, write to its serial console, and handle a timer interrupt | Static components, fixed startup storage, optional page-backed storage, explicit layout and memory-order rules. |
| Hosted native | Serve an inference workload with bounded I/O and interchangeable CPU and GPU providers | Event loop, typed file and device handles, cancellation, backpressure, sandboxed extensions, and observability. |
| Hosted TypeScript | Run an existing TypeScript CLI against its locked dependencies | An optional Cordis profile selects the TypeScript transform, JavaScript engine, Node.js compatibility services and host capabilities. |

The core toolchain installs once. A project declares the providers it needs
and each target links or loads that selection. JavaScript engines, Python,
and GPU drivers have their own distribution and compatibility budgets.
Report core installation size, selected components, dependency storage, and
cache usage independently so projects can see what a choice costs.

## Foreign code

Use the C ABI as the direct integration boundary. Define layout, alignment,
ownership, and callback threads explicitly. Validate C bindings against
their headers. For asynchronous calls, retaining a K value requires an
explicit handle or storage whose lifetime covers the completion event.

An optional Cordis source extension owns the `using "language"` syntax.
A build driver resolves the extension and selected language adapters from
the project lock before reading extended source. The extension emits
ordinary K, guest module artifacts and a source map. The core compiler
checks the generated K through its existing entry point. Its lexer,
grammar and type system remain independent of the extension. The same
locked graph ties the generated K calls to the guest services used at run
time. Python may use an interpreter or an isolated worker. TypeScript can
reuse the transform and engine services of the optional runtime profile.
[Embedded languages](design/embedded_languages.md) ·
[TypeScript runtime](design/functions_and_typescript_runtime.md#runtime-architecture)

An inference request illustrates this contract. It owns a bounded queue slot
and a device buffer. A disconnect cancels the request and stops new work.
The device operation retains the buffer and its provider generation until
completion. Only then can the runtime recycle the buffer or release the
retired provider.

## Engineering priorities

The hosted application adapter activates a TCP provider and its optional
queue policy from a lock. It publishes them together and keeps their
previous generations alive until in-flight requests finish. Use this path
for the first inference service. Extend managed storage to locked native
outputs and verify their lifetime during pruning. Use a freestanding boot
fixture to exercise target layout and static composition alongside the
hosted work.
[Development roadmap](roadmap.md)
