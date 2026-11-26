# First-class functions and a TypeScript runtime

Design proposal. First-class functions are a K language requirement. A
runtime hosted by K has a separate, long-term goal of replacing Node.js for
TypeScript projects. Compatibility milestones are based on running existing
projects against a pinned Node.js release.

## First-class function values

A function value has a checked parameter and result signature. It can be
bound, passed to another function, returned, stored in a struct or collection,
and selected and invoked at runtime. This applies to named functions and
closures. Existing generic function arguments and trailing blocks keep their
specialized calls and lexical `return` behavior.
[Current function values](../reference.md#function-values)

The example uses `impl fn` as a provisional spelling for a concrete closure
type whose capture layout is known to the compiler. The closure owns a copy
of `by`. `return` in the closure returns to its caller.

<!-- fragment -->
```klang
fn make_adder(by: i64) -> impl fn(i64) -> i64 {
    return fn(x: i64) -> i64 { return x + by; };
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
to remains live for all uses of the closure. Moving owned captures and
borrowing mutable captures need explicit capture rules.

Generic code can specialize for a concrete closure. A call site that chooses
at runtime between closures with different capture layouts needs an explicit
uniform callable type. Prototype a borrowed callable and an owned callable
with a declared storage bound. Both conversions check the call signature.
The borrowed form checks lifetime. The owned form checks that the capture
environment fits the destination's size and alignment, with any references
still subject to lifetime checks. These conversions do not allocate implicitly.

Use function composition, `map`, `filter`, `fold`, and callback registries to
check that generic and runtime calls compose with K's tagged enums and
exhaustive pattern matching. Extend the same ownership rules to async
closures. A task must own or validly borrow the data it uses across suspension
and release it on completion or cancellation. Add checked purity as a
follow-up so a higher-order function can constrain the effects of its
callback. [Async functions](async_and_decorators.md#async-functions),
[Effect checking research](llm_frontier.md#34-effects-pure-and-frames)

## Runtime architecture

**TypeScript frontend.** Emit JavaScript and source maps from the modules
loaded by the host, and report unsupported options explicitly. Type checking
can run as a separate step. Document the supported `tsconfig.json` options.
For constructs that need lowering, such as enums and decorators, use a pinned
TypeScript compiler to emit the JavaScript run by the reference Node.js
process. [TypeScript Compiler API](https://github.com/microsoft/TypeScript/wiki/Using-the-Compiler-API),
[Node.js TypeScript support](https://nodejs.org/api/typescript.html)

**ECMAScript engine and C interop.** Embed an established engine behind a
small C interface. Start with a QuickJS embedding experiment and choose a
preview engine based on conformance, startup time, disk size, and workload
benchmarks. The engine owns JavaScript objects and closures. K code exchanges
copied values or retained handles with explicit release, using K's own
memory and lifetime rules on its side of the interface. The initial C shim
drives both sides and queues callbacks that would re-enter K during a foreign
call. [QuickJS C API](https://bellard.org/quickjs/quickjs.html#QuickJS-C-API),
[K exports](../reference.md#export-fn)

**Host services.** K owns the module loader and the integration between the
engine and host I/O. Extensions use the shared
[Cordis microkernel](cordis_microkernel.md#extension-points). Keep Promise
jobs, Node's `nextTick` queue, and host events
in the order observed by the compatibility tests. Adapt one K task's completion
to a Promise whose result JavaScript can observe more than once. Keep cancellation
on the K side unless the JavaScript API specifies a cancellation mechanism.
[Node.js process API](https://nodejs.org/api/process.html#when-to-use-queuemicrotask-vs-processnexttick)

**Distribution.** A possible entry point is `k run app.ts` with an optional
runtime component. Keep the engine and TypeScript frontend out of the core K
toolchain installation. Share a bounded cache of emitted JavaScript and source
maps keyed by source, effective flags, and tool versions. Reuse the project's
installed dependency tree. Track core install size, runtime component size,
dependency storage, and cold and warm cache usage independently.

## Implementation and acceptance

1. **K closures.** Implement concrete closures and verify binding, passing,
   returning, and storing them. Diagnose attempts to return a closure that
   borrows storage in the returning function. Then test runtime selection
   across distinct capture layouts and an async closure's lifetime through
   suspension and cancellation. Compare execution through TinyCC and native C.
2. **TypeScript execution prototype.** In parallel with the language work,
   run a multi-file `.ts` project with ESM imports and top-level `await`.
   Confirm that a Node-free installation runs it and points failures back to
   its TypeScript lines. Check engine integration with a pinned subset of
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
