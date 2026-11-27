# Cordis microkernel for the compiler and runtimes

K is building Cordis-style composition as a core facility.
The compiler, the native K runtime, and the optional TypeScript runtime
use the same model for loading components, resolving services, and owning
registrations. A component can be added or removed while its host is alive.
The design adopts scoped contexts, service dependencies, and reversible
registrations from Cordis. [Cordis](https://github.com/cordiverse/cordis),
[Cordis primer](https://deepseek-harness.github.io/deepseek-harness/reference/cordis-primer)

## Native diagnostics in the language server

`k lsp --cordis-manifest file.json` enables one native diagnostics provider
for the server. Set the initial provider with `diagnostics`, which defaults
to `builtin`. Library paths are relative to the manifest or absolute.
Each provider version uses a distinct artifact path.

```json
{
  "version": 1,
  "diagnostics": "lint-v1",
  "providers": {
    "lint-v1": "providers/lint-v1.so",
    "lint-v2": "providers/lint-v2.so"
  }
}
```

The capability `experimental.klangCordisDiagnostics` has value `1` when
the server starts with a manifest. Send a request to
`klang/cordis/selectDiagnostics` with `{"provider":"lint-v2"}` to replace
the provider, or choose `builtin` to return to the built-in diagnostics.
The result reports the active provider ID. An enable failure returns an
error and keeps the previous provider. Open programs are reanalyzed after a
successful selection. Clients select by provider ID. The startup manifest
supplies the library paths. Start the server with a manifest and libraries
that the host trusts. Native code runs with the server's privileges.

Build a provider against `src/cordis/diagnostics.h` in the source tree
or `include/cordis/diagnostics.h` in a release. Export
`k_cordis_diagnostics_v1` with the descriptor and callbacks from that
header. `start` allocates instance state and returns zero on success.
`stop` releases the state once even if startup fails. The compiler lends
source text and diagnostic data to `inspect` for the duration of the call.
The sink copies supplemental diagnostics before it returns. The server
publishes them alongside the compiler diagnostics with source
`cordis/<provider-id>`. The worker serializes calls. Each analysis retains
the provider it started with until it finishes publishing.
[Lifecycle tests](../testing.md#running-the-suite)

## Resolving a provider graph

Resolve a trusted manifest and report the selected graph before loading
code with `k cordis resolve <service> --manifest cordis.json`. Use
`--provider <id>` to resolve a different selection for the root. The
language server also resolves diagnostics with
`k lsp --cordis-manifest cordis.json --cordis-graph`. A version 1 manifest
selects a diagnostics provider by ID and defaults to `builtin`. Version 2
declares the service supplied by each provider and its requirements.

```json
{
  "version": 2,
  "selections": { "diagnostics": "lint", "symbols": "index" },
  "grants": ["workspace.read"],
  "providers": {
    "lint": {
      "service": "diagnostics",
      "version": 1,
      "library": "providers/lint.so",
      "capabilities": ["workspace.read"],
      "requires": { "symbols": { "minVersion": 1, "maxVersion": 2 } },
      "inputs": { "rules": "lint-rules.json" }
    },
    "index": {
      "service": "symbols",
      "version": 2,
      "library": "providers/index.so"
    }
  }
}
```

Service versions are positive 32-bit integers with inclusive dependency
bounds. The service version and the native C ABI version are checked
independently. When several providers match a requirement, specify one in
`selections`. A graph uses one provider for each reachable service.
`builtin` names the linked diagnostics provider. The host registers it
with the running executable as its artifact. Each required capability
must appear in the manifest's `grants` array. Native providers still run
with the host's privileges.

Library and input paths are relative to the manifest or absolute. Keep the
files at those paths immutable while their selection is in use. The output
contains the `identity` and a versioned `graph` with sorted nodes and edges.
Each node records the service version, provider ID, artifact SHA-256, and
SHA-256 values for explicitly tracked inputs. The graph identity hashes
the canonical JSON of the `graph` field. Manifest formatting, absolute
locations, and unselected providers are outside that identity.

Resolution errors print an `error` object with `code`, `message`, `chain`,
and `candidates` and exit with status 1. A running server exposes its active
graph via `klang/cordis/graph` and advertises
`experimental.klangCordisGraph` version 1. Failed switches preserve the old
graph and put resolution details in the JSON-RPC error's `data` field.
The language server currently activates one diagnostics service. Inspect
multi-service graphs like the example above using the resolve-only command.

## Locked checks and builds

Save the graph document as a UTF-8 lock file and supply it with the
manifest. For a graph the compiler host can activate, the same lock works
with `k check` and `k build`.

```sh
k lsp --cordis-manifest cordis.json --cordis-graph > cordis.lock.tmp &&
    mv cordis.lock.tmp cordis.lock
k check --offline --cordis-manifest cordis.json --cordis-lock cordis.lock app.k
k build --offline --cordis-manifest cordis.json --cordis-lock cordis.lock app.k
```

Both flags are required. The compiler checks the lock's graph digest
against the manifest and selected files before activating a provider.
Regenerate the lock after an intentional change and review the selected
provider, grants, and digests. Whitespace may change in a saved lock,
while the order of its object fields follows the generated document.
`--offline` applies the compiler's normal local-only package policy.

An invocation pins the selected provider and gives it the same sources
and compiler diagnostics used by the language server. Findings carry
their provider ID as `source` in `--diagnostics json`. A provider error
fails the check or build. The compiler rechecks selected dynamic libraries
and tracked inputs after the provider returns, before writing generated
files. Lock and ABI errors include a `cordis` object with a `code`,
`message`, and dependency `chain` in the JSON diagnostics array.

A successful locked build embeds its graph document in the native artifact
as a null-terminated UTF-8 C array named `k_cordis_build_graph`. The graph
identity participates in the executable cache on the supported macOS
system Clang path. Changing only an unselected provider leaves the key
unchanged. Generated application C includes the graph, while the shared
runtime object and native package caches keep their existing keys.
Compiler activation currently covers the diagnostics service. Activation
of graphs with dependent services is a later milestone.

## Shared native service lifecycle

`src/cordis/service.h` holds the activation and retirement logic shared
by the LSP diagnostics adapter and a standalone native host. Each service
supplies its ABI version, entry symbol and required methods. A host can
register a linked entry or a library path for the same service. The host
stages and starts a candidate before publishing it. A request holds a
reference to the selected generation. Its last reference calls `stop`
before releasing the library, including for an old generation held across
replacement. The host serializes selection and request acquisition.

The standalone fixture uses a value service with a different C ABI from
diagnostics. It exercises linked-to-dynamic and dynamic-to-linked
replacement, rollback, missing entry points and an old request held
across replacement. A separate binary runs the same linked provider without
compiling or linking the dynamic loader. [Lifecycle tests](../testing.md#running-the-suite)

## Cancellable TCP receive

The optional TCP provider accepts ownership of a connected, blocking TCP
socket when `receive` succeeds. One operation reads a single byte on a
worker thread and publishes a fixed-size result. Cancellation interrupts
the receive with socket shutdown. The driver waits for completion before
joining the worker and closing the socket. A host can use
`src/cordis/foreign_task.h` to pin the selected provider generation
through the call that releases the operation.

Configure with `-DK_BUILD_TCP_READ_PROVIDER=ON` and build the
`k_cordis_tcp_read` target to obtain the shared library. Its entry point
is `k_cordis_tcp_read_v1`. Release packages include the C contract in
`include/cordis/tcp_read.h`. The optional module is built separately.

The loopback test transfers this operation to K, replaces the dynamic
provider with a linked provider while the receive is pending, and checks
that the old provider and socket remain live until cancellation drains.
[Native operation tasks](async_and_decorators.md#native-operation-tasks) ·
[Loopback test](../testing.md#running-the-suite)

## Bounded TCP streams

The same optional module exports `k_cordis_tcp_stream_v1`. Its C contract
is in `src/cordis/tcp_stream.h` and ships as `include/cordis/tcp_stream.h`
in the release. A host can register it as a linked or dynamic Cordis service
and adopt a connected, blocking socket with `open`. The host chooses a
capacity from 1 through 1,024 chunks, each holding up to 4 KiB. Allocation
and socket ownership transfer both happen at open, with transfer taking
effect on success.

Each stream has one reader and one producer task. The producer reserves a
ring slot before issuing a socket read to its worker. The reservation
counts against capacity until the worker finishes, and a partial
`take` keeps its slot until the consumer takes the remaining bytes.
When the ring is full, the producer submits no further reads. A single
worker handles repeated reads for that stream. The worker wakes the
task driver on completion. Freeing a slot in a full ring also wakes the
producer so it can reserve the next slot.

A K host can return the producer descriptor in an owned C handle and pass
it to `native_task<ReadResult>`. Include the producer in a `task_scope`.
The consumer calls `take` with its own byte slice. That call returns
`PENDING` when more work is needed or `DATA` with the number of bytes
copied. The consumer can use `scope.wait()` after `PENDING`. The
consumer receives buffered bytes in order before `EOF` or a socket
`ERROR`. [Hosted readiness](async_and_decorators.md#hosted-readiness)

Cancellation stops submissions and discards queued bytes. It revokes
unclaimed work or interrupts an in-flight receive, and drains before it
joins the worker and closes the socket. A retained reader observes
`CANCELLED` after cleanup. Releasing the reader during a receive also
requests cancellation, and the producer task still drives it to completion.
The host adapter holds independent generation references for the reader
and producer, so replacing the provider affects new streams while existing
streams keep their original callbacks until both owners finish.
[Loopback test](../testing.md#running-the-suite) ·
[Hosted I/O milestones](../roadmap.md#next-mergeable-changes)

## Locked application activation

The hosted K module `cordis` activates a `tcp.stream` provider and its
optional `tcp.policy` dependency from a version 2 manifest and a verified
lock. The application host is
separate from the TCP provider. Build both when using K's bundled
implementation of that service contract.

```sh
cmake -S . -B build -DK_BUILD_CORDIS_HOST=ON -DK_BUILD_TCP_READ_PROVIDER=ON
cmake --build build --target k_cordis_application k_cordis_tcp_read
k cordis resolve tcp.stream --manifest cordis.json > tcp.lock.tmp &&
    mv tcp.lock.tmp tcp.lock
k cordis resolve tcp.stream --manifest cordis.json --provider next > next.lock.tmp &&
    mv next.lock.tmp next.lock
```

Deploy the application host library in `lib/` under the program's resource
directory. Its name is `libk_cordis_application.dylib` on macOS,
`libk_cordis_application.so` on Linux, and `k_cordis_application.dll` on
Windows. Put the provider library at the manifest path. Its entry point and
ABI are the same as for other native TCP stream hosts.
[Dynamic library paths](dynamic_externs.md) · [TCP stream ABI](#bounded-tcp-streams)

The manifest can select one provider and register another for replacement.
Use the actual platform-specific filenames of the provider artifacts.

```json
{
  "version": 2,
  "selections": { "tcp.stream": "default" },
  "providers": {
    "default": { "service": "tcp.stream", "version": 1, "library": "providers/tcp-v1.so" },
    "next": { "service": "tcp.stream", "version": 2, "library": "providers/tcp-v2.so" }
  }
}
```

```klang
import cordis;

fn main() {
    let host = cordis::open("cordis.json", "tcp.lock");
    defer cordis::close(host);
    assert(cordis::valid(host) == 1);
    var id: u8[64] = [0; 64];
    let size = cordis::active_provider(host, id[..]);
    assert(size <= id.len);
    print(id[..size]);
}
```

The application drives its host on one thread. It can call
`cordis::select(host, "next", "next.lock")` to switch within the startup
catalog. The host checks the requested lock before loading code, starts
dependencies before consumers, and rechecks the selected inputs after
startup. Only a successful activation publishes the whole graph.
On failure, staged services stop in reverse order and the current graph
stays active. The selection returns
`1` for replacement, `0` for the same graph, and `-1` on failure. A failed
open, adopt or pump returns an owned invalid handle. Use `valid`, consume
it with the matching close or discard, and read `error_json` for the cause.
`error_json`, `active_graph`, `active_provider`, and `stream_provider`
return the required byte length and copy the entire result when it fits.

`adopt_tcp` takes an owned socket from a C binding and consumes it on either
outcome. A successful reader and its pump keep their own pins on the
selected generation. Pass a valid pump to `native_task<cordis::ReadResult>`
or use `discard_task` before scheduling it. New readers use the newly
selected provider. An old reader and pump can keep running after a
selection. Their pins keep both the retired stream provider and its policy
alive. When the last pin is released, the stream provider stops before its
policy. Keep provider binaries immutable at their deployed paths.

To attach a policy, add `"requires": {"tcp.policy": {"minVersion": 1,
"maxVersion": 1}}` to a stream provider and register a provider for the
required service version. Its C descriptor, `k_cordis_tcp_policy_v1`,
returns a maximum number of queue slots for new readers. The host checks the
limit before calling the TCP provider and closes the transferred socket if
the limit is exceeded. The error code is `stream_capacity_denied` and its
chain identifies both providers.

The generic resolver can inspect graphs for other service contracts. The
application host supports `tcp.stream`, `tcp.policy`, and `inference`
activation. Native providers run with the application's privileges. The core
release ships the C declarations under `include/cordis/`. The application
host and provider libraries are deployed separately.
[Application integration test](../testing.md#running-the-suite)

## Locked CPU inference

The optional ONNX Runtime provider implements `inference` through the
engine's C API. It accepts a self-contained ONNX graph with one fixed-shape,
contiguous float32 input and output and uses the CPU execution provider. Each
header-compatible runtime is supplied as a local library. The manifest lock
records the provider artifact, model, and runtime library digests.
[ONNX Runtime C API](https://onnxruntime.ai/docs/get-started/with-c.html)

Place the SDK headers in the chosen root's `include/` directory. The
integration test looks for the engine library under `lib/` or `bin/` in
the same root. The reference test uses ONNX Runtime 1.24.4.
[Microsoft ONNX Runtime package](https://www.nuget.org/packages/Microsoft.ML.OnnxRuntime/1.24.4)

```sh
cmake -S . -B build -DK_BUILD_CORDIS_HOST=ON -DK_BUILD_LSP_TESTS=ON \
    -DK_ONNXRUNTIME_ROOT=/path/to/onnxruntime
cmake --build build
ctest --test-dir build -R '^cordis_onnx_inference$' --output-on-failure
k cordis resolve inference --manifest providers.json
```

The provider entry in `providers.json` names the platform's built
`k_cordis_onnx` module and supplies these tracked inputs. The runtime
library must expose `OrtGetApiBase`.

```json
{
  "version": 2,
  "providers": {
    "cpu": {
      "service": "inference",
      "version": 1,
      "library": "providers/libk_cordis_onnx.so",
      "inputs": { "model": "models/classifier.onnx", "runtime": "engine/libonnxruntime.so" }
    }
  }
}
```

Save the resolved document as a lock and pass it with the manifest to
`cordis::open_inference`. A TCP graph can instead declare `inference` as
a dependency of its selected `tcp.stream` provider. Activation verifies
the file digests, loads the declared runtime by path, and initializes the
session from the verified model bytes before publishing the graph.

`cordis::inference_layout` gives the input and output shapes.
`cordis::infer` copies the input into an owned request. Claim its task
with `cordis::inference_task` and drive it with
`native_task<cordis::InferenceResult>`. A zero status indicates that the
owned request has a result. Copy it with `cordis::inference_output` and
release the request with `cordis::close_inference`. Cancellation requests
termination through the C API and drains the worker before releasing its
session. A new request uses the current provider while older requests and
tasks retain their original model generation.

The model file limit is 64 MiB. Each published tensor supports up to eight
axes and 1,048,576 elements. Each generation has one request slot, held
until both the request and its task are released. A second request reports
`inference_busy`.

`cordis::inference_workspace` samples the active CPU allocation arena,
including reserved bytes, bytes in use, and the session's peak bytes in use.
It also reports live retired generations and sums the reserved bytes of
those with telemetry. A retirement entry expires when its last request,
task or dependent service releases the generation. The last activation
sample sums the outgoing, staged and already-retired arenas immediately
before successful publication. A rejected replacement preserves this
sample and the active graph.

The inference ABI makes arena telemetry optional. The measurement flags
indicate which readings are available, including whether all arenas were
measured at publication. CPU providers built with ONNX Runtime 1.23 or
later use each session's allocator statistics. Process RSS also includes
the host, runtime, model parsing buffers, thread stacks and memory outside
the CPU arena. Measure RSS separately when establishing process budgets.
[ONNX Runtime CPU allocator](https://onnxruntime.ai/docs/get-started/with-c.html)

## Loopback inference gateway

`examples/inference_gateway.k` serves one request per TCP connection.
K owns request validation, the inference task scope, the provider transition
and response framing. The optional application host owns the sockets and
the bounded FIFO of accepted connections. It processes one active request
and binds IPv4 loopback. The optional ONNX Runtime provider remains
selectable through the locked `inference` service.

Put the application host shared library under `lib/` beside the K
source for `k run` or beside the executable for `k build`. Use the
inference manifest above to create its startup lock.

The Linux cloud archive includes `k-inference-gateway`, the application
host, the CPU provider and its engine. In your application manifest, point
the provider's `library` to the installed `lib/libk_cordis_onnx.so` and its
`runtime` input to the installed `lib/libonnxruntime.so`. Point `model` to
your model file, resolve the startup lock with the installed `k`, and run
`k-inference-gateway` with the manifest, lock and numeric options below.
[Cloud release build](../testing.md#rebuilding-the-linux-cloud-release)

```sh
k cordis resolve inference --manifest providers.json > initial.lock
k run --offline gateway.k -- providers.json initial.lock 0 8 2000 16384 256 0
```

The numeric options set the port, pending connection limit, request
deadline in milliseconds, tensor element limit, response frame element
limit and number of dequeued connections before exit. Port zero selects
an ephemeral port printed in the `ready` line. A connection count of zero
runs until stopped. The tensor limit applies to both model tensors. The
optional final arguments `MARKER PROVIDER LOCK` trigger one attempt to
activate a replacement at a request boundary or while inference is parked.
The already-running request keeps its original generation. A failed
activation preserves the current generation.

Append `--workspace` for JSON snapshots on standard output after startup,
after a replacement attempt, and before shutdown. Each snapshot has the
prefix `workspace` followed by its phase and one JSON object. The wire
protocol is independent of these diagnostics.

### Linux deployment memory budget

Place the gateway in a dedicated Linux cgroup v2 before execution. Set
`memory.max` to the deployment budget and `memory.swap.max` to zero.
Append `--memory-budget-mib MIB` to make the gateway verify those limits
before it loads the model or binds the listener. It accepts an effective
limit at or below the requested bytes and checks readable ancestors of the
process's actual cgroup. A missing or insufficient boundary fails startup.
The deployment supervisor owns the limits and group membership for the
life of the gateway.

For a Docker deployment, use the same value for `--memory` and
`--memory-swap`. Run the gateway and its loopback client in the same
network namespace. The cgroup charge includes the runtime and both model
generations during replacement. If reclaim fails under the hard limit,
Linux can terminate the process. A replacement rejected by Cordis still
preserves the current generation while the process is alive.
[Linux cgroup v2 memory controller](https://docs.kernel.org/admin-guide/cgroup-v2.html#memory) ·
[Docker memory constraints](https://docs.docker.com/engine/containers/resource_constraints/#limit-a-containers-access-to-memory)

### Supervised OOM rollback

The Linux cloud release also ships `k-inference-supervisor`. Provision an
empty worker cgroup with its own `memory.max`, `memory.swap.max=0`, and
`memory.oom.group=1`. Run the supervisor outside that group and delegate
process migration to it. The worker enters the group before executing the
gateway. Keep enough memory for the supervisor in the enclosing cgroup.

```sh
k-inference-supervisor --worker-cgroup /sys/fs/cgroup/cordis/worker \
  --memory-budget-mib 96 \
  --reload promote candidate candidate.lock --rollback-file candidate.rolled-back \
  -- providers.json stable.lock 8000 8 2000 16384 256 0
```

The supervisor requires a fixed nonzero port for recovery. Keep the stable
manifest, lock and their inputs available and unchanged for its lifetime.
On `SIGKILL` with a rise in the worker group's `oom_kill` counter, it retries
once with the stable lock. If a rollout was enabled, it writes and syncs the
rollback file before retrying. Later starts with that file present suppress
the same rollout even if its reload marker is still present. Use a distinct
rollback file for each rollout. Clients can reconnect to the same port after
the replacement worker reports `ready`. A second OOM exhausts the retry.

[Cgroup delegation](https://docs.kernel.org/admin-guide/cgroup-v2.html#delegation)

Each request starts with the ASCII bytes `KIG1`, a little-endian 32-bit
element count and that many IEEE 754 little-endian `f32` values. The
client sends one request and keeps its write half open until it receives
the terminal frame. The server sends frames with a one-byte tag and a
little-endian 32-bit payload byte count. Tag 1 carries a tensor chunk,
and terminal frames carry zero bytes.

| Tag | Meaning |
| --- | --- |
| 1 | Tensor data |
| 2 | Complete |
| 3 | Invalid request |
| 4 | Deadline expired |
| 5 | Admission queue is full |
| 6 | Inference failed |

The model produces its whole tensor before framing starts. The deadline is
monotonic and starts at socket acceptance. It applies while queued, during
I/O and while waiting for inference. An expired or disconnected request
cancels its outstanding inference and drains it before the active slot
is reused. Provider startup and foreign cancellation drain can outlast
the deadline. Deadline and overload notifications are best effort.
Clients consume the tensor only after a tag 2 terminal frame. The
element limit bounds application-owned tensor buffers. Model weights,
backend workspace and operating-system socket buffers contribute to
process and system memory independently.

`ctest --test-dir build -R '^cordis_inference_gateway$' --output-on-failure`
uses the ONNX SDK build above to exercise the wire protocol with both
execution backends.

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
| Optional TypeScript runtime profile | TypeScript transform, ECMAScript engine, module resolution and compatibility services | Node.js adapters and host integrations verified by versioned compatibility tests |
| Optional Cordis build driver | Source transforms that turn extended source into ordinary K, guest artifacts and source maps | Editor overlays and offline rebuilds from the same lock |
| Foreign-language host | Selected Python and TypeScript execution adapters | Additional languages with the same typed boundary through C ABI, isolated processes, or Wasm components |

Built-in providers exposed at these extension points use the same interfaces
as external components. The compiler owns the language grammar, type system,
reference checking, and IR invariants. Generated K source goes through the
normal checker. A build driver's source transform runs before the compiler
reads those modules. Guest syntax, engines and Node.js compatibility stay in
the project-selected components. The compiler holds a fixed set of components
for an entire compile or check. A long-lived LSP host can change the set
between requests.
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
2. Use the shared lifecycle from a K application with two scoped providers
   and typed consumers. Test missing dependencies, one failed replacement with
   rollback, removal during an active async callback, and release of all
   registrations and handles. Verify that in-flight calls never enter
   unloaded code.
3. Load a TypeScript component through the optional runtime and let it
   consume a K-provided service. Replace it while an async call is pending.
   Verify that the old call settles according to the service contract and
   new calls use the new implementation. Expose the active component and
   service graph in machine-readable diagnostics to make these states
   inspectable by an agent harness.
