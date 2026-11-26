# K architecture

The Cordis microkernel is the organizing principle for K. The compiler owns
language semantics and checks resource contracts. A shared component kernel
lets each project choose the services required by its target. The current
compiler already has a C backend, checked references, address-stable data
stacks, isolated workers, and two execution paths. The component architecture
below is the design those mechanisms will support. [Compiler implementation](implementation.md)

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
| Hosted TypeScript | Run an existing TypeScript CLI against its locked dependencies | TypeScript lowering, a JavaScript engine, source maps, and tested Node.js APIs and module resolution. |

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

A proposed K `using` block adds a source-level expression for guest code
with an explicit language, typed inputs, and a checked result. The selected
Cordis adapter owns guest parsing and execution. Python can use an embedded
interpreter with trusted code or an isolated worker. TypeScript can reuse the
optional runtime. Other languages can supply an adapter through a supported
ABI, an isolated process, or a Wasm component. The initial source syntax uses
an explicit fence to preserve guest text and source locations independently
of the guest grammar. [Embedded languages](design/embedded_languages.md) ·
[TypeScript runtime](design/functions_and_typescript_runtime.md#runtime-architecture)

An inference request illustrates this contract. It owns a bounded queue slot
and a device buffer. A disconnect cancels the request and stops new work.
The device operation retains the buffer and its provider generation until
completion. Only then can the runtime recycle the buffer or release the
retired provider.

## Engineering priorities

Start by exercising a returned closure, asynchronous cancellation, and an
owned native handle in one program. In parallel, make the first Cordis
registry load one diagnostics provider and make one freestanding image boot.
Those three tests expose the memory, ABI, and target assumptions before the
project commits to a wider set of providers. [Development roadmap](roadmap.md)
