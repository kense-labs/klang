# First-class functions and a TypeScript runtime

K implements concrete closures. This design extends the callable model and
specifies a hosted runtime with a long-term goal of replacing Node.js for
TypeScript projects. Compatibility milestones are based on running existing
projects against a pinned Node.js release.

## First-class function values

A function value has a checked parameter and result signature. It can be
bound, passed to another function, returned, stored in a struct or collection,
and selected and invoked at runtime. This applies to named functions and
closures. Existing generic function arguments and trailing blocks keep their
specialized calls and lexical `return` behavior.
[Current function values](../reference.md#function-values)

The compiler infers the concrete return type of `make_adder`. The closure
owns a copy of `by`. `return` in the closure returns to its caller.

<!-- fragment -->
```klang
fn make_adder(by: i64) {
    return fn[by: i64](x: i64) -> i64 { return x + by; };
}

fn main() {
    let add_three = make_adder(3);
    print(add_three(4));
}
```

The compiler tracks the closure's environment like any other value. Copyable
captures can be owned by value. Captures that borrow data obey K's reference
provenance rules, including when a closure is returned or placed in a task.
A borrowed capture can escape its defining scope only when the data it refers
to remains live for all uses of the closure. Moving an owned resource into
an environment needs a checked transfer of its cleanup obligation.
[Concrete closures](../reference.md#concrete-closures)

Generic code can specialize for a concrete closure. For runtime selection,
`fn&` gives different closure layouts a common checked signature.
`borrow_callable` stores an invocation pointer and an environment pointer.
Named K functions need only the invocation pointer. The compiler checks
the borrow with ordinary reference roots and adapts each target's native
calling convention without heap allocation. For read-only reference
captures, it uses the store records and parameter origins to check the
backing storage. This covers a generic caller that replaces the environment
through an alias, including replacement during argument evaluation.
[Borrowed callables](../reference.md#borrowed-callables)

The owned form uses `fn[N]` with an explicit inline byte capacity.
`own_callable` checks the concrete capture size using the same packed
layout as code generation. The target trampoline restores those bytes in
typed storage at invocation, where the target's alignment applies.
Copies preserve the capture bytes. Read-only captures of known fixed
storage carry their ordinary borrow roots through the callable value.
[Owned callables](../reference.md#owned-callables)

Use function composition, `map`, `filter`, `fold`, and callback registries to
check that generic and runtime calls compose with K's tagged enums and
exhaustive pattern matching. Extend the same ownership rules to async
closures. A task must own or validly borrow the data it uses across suspension
and release it on completion or cancellation. Add checked purity as a
follow-up so a higher-order function can constrain the effects of its
callback. [Async functions](async_and_decorators.md#async-functions),
[Effect checking research](llm_frontier.md#34-effects-pure-and-frames)

## Runtime architecture

The Node.js compatibility runtime is an optional Cordis application
profile. Its runner resolves the transform, engine, module loader and host
services from the project lock. The K compiler continues to process K
source through its normal CLI and accepts no new language mode for this
profile. A native K application can share the selected services through
versioned interfaces.

**TypeScript frontend.** Emit JavaScript and source maps from the modules
loaded by the host, and report unsupported options explicitly. Type checking
can run as a separate step. Document the supported `tsconfig.json` options.
The fenced-block adapter and project runner share a pinned TypeScript
compiler running in QuickJS. The project runner preserves ESM imports and
lowers syntax such as `enum`. [TypeScript Compiler API](https://github.com/microsoft/TypeScript/wiki/Using-the-Compiler-API),
[Node.js TypeScript support](https://nodejs.org/api/typescript.html)

**ECMAScript engine and C interop.** Embed an established engine behind a
small C interface. The optional fenced-block adapter exercises QuickJS with
typed value copies and maps guest exceptions to the original source. Evaluate
the project runtime engine by conformance, startup time, disk size, and
workload benchmarks. The engine owns JavaScript objects and closures. K code exchanges
copied values or retained handles with explicit release, using K's own
memory and lifetime rules on its side of the interface. The initial C shim
drives both sides and queues callbacks that would re-enter K during a foreign
call. [Fenced TypeScript adapter](embedded_languages.md#optional-typescript-adapter),
[QuickJS C API](https://bellard.org/quickjs/quickjs.html#QuickJS-C-API),
[K exports](../reference.md#export-fn)

**Host services.** The selected runtime profile owns the module loader, the
Node.js API behavior, and the integration between its engine and Cordis I/O
services. File, network and subprocess access follows the project's granted
capabilities. Keep Promise
jobs, Node's `nextTick` queue, and host events
in the order observed by the compatibility tests. Adapt one K task's completion
to a Promise whose result JavaScript can observe more than once. Keep cancellation
on the K side unless the JavaScript API specifies a cancellation mechanism.
[Node.js process API](https://nodejs.org/api/process.html#when-to-use-queuemicrotask-vs-processnexttick)

**Distribution.** Deliver the runner, engine, transform and Node.js adapters
through optional Cordis packages. The first executable entry point can be
the installed profile runner. A later launcher can delegate by profile
selection outside the K compiler. Share a bounded cache of emitted
JavaScript and source maps keyed by source, effective flags, service
versions and locked inputs. Reuse the project's installed dependency tree.
Track core install size, runtime component size, dependency storage, and
cold and warm cache usage independently.

## Optional project runner

Build and install the `cordis-typescript` component alongside an ordinary K
compiler. The runtime and frontend are dynamically selected Cordis services.

```sh
cmake -S . -B build-ts -DK_BUILD_CORDIS_TYPESCRIPT=ON
cmake --build build-ts --target cordis-typescript
cmake --install build-ts --component cordis-typescript --prefix ./cordis-tools
```

Place `cordis.json` beside `cordis-tools` and use the installed library
filenames for your host.

```json
{
  "version": 2,
  "selections": { "typescript-project": "quickjs", "typescript-frontend": "tsc" },
  "providers": {
    "quickjs": {
      "service": "typescript-project",
      "version": 1,
      "library": "cordis-tools/lib/cordis/libk_cordis_typescript_project.so",
      "requires": { "typescript-frontend": { "minVersion": 1, "maxVersion": 1 } }
    },
    "tsc": {
      "service": "typescript-frontend",
      "version": 1,
      "library": "cordis-tools/lib/cordis/libk_cordis_typescript_frontend.so",
      "inputs": { "compiler": "cordis-tools/share/cordis/typescript/typescript.js" }
    }
  }
}
```

```sh
k cordis resolve typescript-project --manifest cordis.json > cordis.lock
./cordis-tools/bin/k-cordis-ts --manifest cordis.json --lock cordis.lock --root ./app main.ts
```

The profile loads relative `.ts`, `.mts`, `.js` and `.mjs` modules below
the chosen project root. The resolver tries a `.ts` or `.mts` sibling for
a missing `.js` or `.mjs` file. Static and dynamic imports share module
identity. The runner locks the services and compiler input, and captures
each project module on first read for the duration of the invocation.

The frontend uses ES2022 output with ESM and syntactic diagnostics.
The runtime drains Promise jobs and reports unhandled rejections and
top-level `await` that remains pending after all jobs have drained. It maps
stack lines through each module's source map. `console.log` and `console.error`
write the string representation of each argument through the runner. This
preview covers local ESM and Promise jobs. Node.js built-ins, npm package
resolution, timer events and `tsconfig.json` options are future milestones.

## Implementation and acceptance

1. **K closures.** Implement concrete closures and verify binding, passing,
   returning, and storing them. Diagnose attempts to return a closure that
   borrows storage in the returning function. Then test runtime selection
   across distinct capture layouts and an async closure's lifetime through
   suspension and cancellation. Compare execution through TinyCC and native C.
2. **TypeScript execution prototype.** The optional runner executes a
   multi-file `.ts` project with ESM imports and top-level `await` on a
   Node-free installation, and points failures back to its TypeScript lines.
   The ordinary K install has no guest engine dependency. Check engine
   integration with a pinned subset of
   [Test262](https://github.com/tc39/test262).
3. **First Node.js replacement target.** Run an existing TypeScript CLI
   that reads and writes JSON and imports a pinned npm dependency graph
   with both ESM and CommonJS. Cover package `exports` and `imports`,
   `node:fs`, `node:path`, `process`, `Buffer`, and timers as exercised by the
   fixtures. Accept the release when the same project and dependency tree
   produce matching output, files, exit status, and relevant errors under
   the K runtime and the reference Node.js release. Use a prepared locked
   dependency tree for this milestone. [Node.js packages](https://nodejs.org/api/packages.html)
4. **Broader project compatibility.** Add HTTP and streams with backpressure,
   subprocesses, workers, test APIs, and package scripts in the order required
   by real project fixtures. Include a TypeScript agent harness that streams
   responses, launches a tool subprocess, and cancels in-flight work. Define
   the supported Node-API versions and test addons against them. Record V8- and
   Node-specific addons in the compatibility matrix. Compare observable
   behavior, including callback order and error codes. [Node-API](https://nodejs.org/api/n-api.html)
