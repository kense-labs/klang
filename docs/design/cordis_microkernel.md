# Cordis microkernel for the compiler and runtimes

Design proposal. K will ship Cordis-style composition as a core facility.
The compiler, the native K runtime, and the optional TypeScript runtime
use the same model for loading components, resolving services, and owning
registrations. A component can be added or removed while its host is alive.
The design adopts scoped contexts, service dependencies, and reversible
registrations from Cordis. [Cordis](https://github.com/cordiverse/cordis),
[Cordis primer](https://deepseek-harness.github.io/deepseek-harness/reference/cordis-primer)

## User-selected components

The microkernel is K's primary architectural principle. The host owns the
language semantics, safety checks, and component lifecycle. Projects choose
the providers they need for compiler tooling, I/O, foreign execution, and
device services. A versioned manifest describes requirements. A lockfile
records the selected component versions and content digests. Diagnostics
identify unsatisfied requirements without installing providers implicitly.
The same service contracts support static composition where dynamic loading
is unavailable and dynamic replacement in long-lived hosts. A build exposes
the resolved component graph and the capabilities granted by its host.
[K architecture](../architecture.md)

## Contexts and services

The kernel owns the context tree, service registry, dependency graph, typed
event dispatch, and component lifecycle. Each component instance gets a
child context for its configuration, services, tasks, and registrations.
A workspace or application can create its own scope and use a different
provider for the same service. Consumers use a versioned service interface.
They declare required and optional dependencies by service ID and version
range. A scope can explicitly override its parent's selected provider.
The resolver reports ambiguity when several providers are eligible at the
same scope and the project has selected none. Optional dependencies can be
absent. A component waits until its required providers are ready. A required
provider change restarts the affected dependents. If a required provider
disappears, those dependents return to waiting. Diagnose incompatible versions
or dependency cycles with the chain of components involved.

Event declarations define the payload, result, and dispatch order. A scope
owns every listener and any closure it captures. Unloading a scope removes
its services and listeners, cancels tasks it owns, and releases registered
resources. Typed closures implement callbacks, `async fn` implements startup
and shutdown, and decorators can declare service and event metadata. Use
explicit registration in the first prototype and verify that a decorated
registration produces the same metadata and lifecycle.
[First-class functions](functions_and_typescript_runtime.md#first-class-function-values),
[Async functions and decorators](async_and_decorators.md)

## Extension points

| Host | Initial extension points | Further extension points |
| --- | --- | --- |
| `k check`, `k build`, and `k lsp` | Diagnostics and read-only analysis of a checked compilation snapshot | Linters, code actions, declaration-driven generation, and backends with versioned input contracts |
| Native K runtime | Application services, event handlers, and configuration providers | I/O adapters, tracing exporters, and application-level middleware |
| TypeScript runtime | Application services and host integrations | Module loaders and host I/O adapters that pass the relevant Node.js compatibility tests |
| Foreign-language host | Selected Python and TypeScript execution adapters | Additional languages with the same typed boundary through C ABI, isolated processes, or Wasm components |

Built-in providers exposed at these extension points use the same interfaces
as external components. The compiler owns the language grammar, type system,
reference checking, and IR invariants. Generated K source goes through the
normal checker. The compiler holds a fixed set of components for an entire
compile or check. A long-lived LSP host can change the set between requests.
Artifact-producing extensions declare their inputs. Cache entries include
the enabled extension versions, content hashes, effective configuration,
and tracked input hashes. An extension that can read untracked inputs
disables cache reuse for the stages it affects.

## Dynamic loading and replacement

A manifest declares the component ID, host and ABI compatibility, entry
point, provided and required services, and configuration schema. The host
selects enabled components from explicit configuration. Start with compiled
K or C shared libraries using a versioned C ABI, with opaque handles,
explicit ownership, and fixed-layout data at the boundary. The existing K
shared-library build and `export fn` are the starting point for the adapter.
The bootstrap loader can live in C or C++ without a JavaScript engine.
TypeScript components can later use the same registry through a bridge once
the optional TypeScript runtime is active. [K shared library builds](../command_line.md#3-building-an-executable),
[TypeScript runtime architecture](functions_and_typescript_runtime.md#runtime-architecture)
[Embedded language adapters](embedded_languages.md)

For a replacement, prepare the new component and its affected dependents in
a staging context. Load a new native binary under a distinct artifact path
so the platform loader can keep both versions live during cutover. Publish
their registrations together at a host safe point
after successful startup. On failure, dispose the staged scopes and leave
the active configuration in service. At cutover, new calls see the new
providers. Stop routing new work to the retired scopes, drain or cancel
in-flight work, and tear down dependents before their providers. Release a
native library only after its last outstanding handle, callback, and task is
gone. A provider that needs exclusive access to a resource must declare when
it can acquire that resource during cutover. External writes and other
irreversible effects remain subject to the service's own contract.

Service scope controls lookup and lifecycle. In-process native code shares
the host's privileges. A host that runs untrusted components needs a separate
execution boundary and an explicit service protocol. Distribute the core
registry with `k` and share it across hosts without requiring the optional
TypeScript runtime or downloading a copy for each project.

## Implementation and acceptance

1. Load a native diagnostics component from a manifest into `k lsp`. Check
   that it observes the same checked program as `k check`. Replace it between
   requests without restarting the server, and verify that the two versions
   report independently and the retired listener is released.
2. Run the same lifecycle in a K application with two scoped providers and
   typed consumers. Test missing dependencies, one failed replacement with
   rollback, removal during an active async callback, and release of all
   registrations and handles. Verify that in-flight calls never enter
   unloaded code.
3. Load a TypeScript component through the optional runtime and let it
   consume a K-provided service. Replace it while an async call is pending.
   Verify that the old call settles according to the service contract and
   new calls use the new implementation. Expose the active component and
   service graph in machine-readable diagnostics to make these states
   inspectable by an agent harness.
