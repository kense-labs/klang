# First-class async functions and decorators

Design proposal. K requires `async fn` and `await` as language constructs and
`@` decorators as typed, user-definable declarations. Both participate in
name resolution, type checking, formatting, diagnostics, and editor tooling.
The compiler lowers them to the same C backend used by ordinary K programs.

The syntax below shows the intended surface.

<!-- fragment -->
```klang
@http::get("/health")
@trace("health")
async fn health(ctx: RequestContext&) -> Response {
    let status = await check_store(ctx.store);
    return response(status);
}
```

## Async functions

`async fn f(...) -> R` declares the result of the async body. A call evaluates
its arguments in source order and constructs a task. The body starts when an
executor advances the task. `await task` advances the task and suspends the
enclosing async body when the task is pending. Completion delivers the
declared results to the awaiting body.
Implicit suspension through an ordinary function call is a compile error.

Each task has a concrete, statically sized frame type for its function and
specialization. The compiler knows its result types and reference roots. A task
can be bound to a variable, moved through a parameter or result, and composed
by generic code. The spelling for explicit task types is an open syntax choice.
Each task has one owner and one consumer. A lexical task scope can own several
tasks and join or cancel the tasks it owns before leaving the scope.
The [first-class functions proposal](functions_and_typescript_runtime.md#first-class-function-values)
extends these ownership rules to tasks created by closures.

The frame holds fixed-size values live across suspension. A reference or slice
held across suspension must point to storage outside the frame with a sufficient
lifetime. Growable scratch storage can live in a block that ends before the
next suspension, or in a caller-owned workspace borrowed by the task. The first
implementation covers fixed-size results and eligible borrowed results. Owned
variable-size results need an explicit destination that remains live through
completion.

Cancellation propagates from an owning task to its outstanding children. The
compiler emits cleanup for the active suspension point and executes pending
`defer` bodies once in reverse order. Pending cleanup bodies must complete
synchronously. The terminal transition records completion or cancellation
once. A never-started task has registered no body cleanup. The ownership draft
describes a way to track these obligations.
[Coroutine frames](coroutines.md#45-rules-at-a-suspension-point) ·
[Ownership and cancellation](ownership.md#53-design-b-own-frames-and-cancel)

The language defines task construction, suspension, result delivery, and
cleanup. The standard library provides a deterministic executor for tests,
an entry driver for `async fn main()`, and adapters for event loops and OS
I/O. The executor drives compiler-generated state transitions. Frame storage
is selected by the owner or executor. [Async state machines and executors](https://doc.rust-lang.org/stable/book/ch17-01-futures-and-syntax.html)

## Decorators

A package can define a decorator with typed parameters and supported
declaration targets in one of its namespaces. Other modules can import it and
apply it with `@name` or `@package::name(argument)`. The compiler resolves the decorator
at the declaration, checks its target and argument types, and evaluates its
configuration arguments at compile time.

Function decorators can attach typed metadata or wrap the function body.
Metadata consumers select declarations through compile-time reflection over
an explicit scope. A wrapper gets a statically bound call to the next layer
and is type-checked with the decorated signature. Existing reference
provenance, return placement, and external calling conventions apply to the
expanded body. The `next` call in this example forwards the decorated
function's arguments to the next layer.

<!-- fragment -->
```klang
decorator trace(label: const u8[:]) on async fn {
    wrap {
        trace_begin(label);
        defer trace_end(label);
        return await next();
    }
}
```

Struct, enum, and field decorators can attach typed metadata,
with declaration identity and layout preserved.

Configuration arguments are processed top to bottom. Wrappers compose from the
bottom decorator up, so in `@A @B fn f`, calling `f` enters A, then B, then
the body. This follows the composition order used by TypeScript.
[TypeScript decorators](https://www.typescriptlang.org/docs/handbook/release-notes/typescript-5-0.html#decorators)

An async function wrapper participates in the async body. Its prelude runs
when the task is first advanced, its continuation is awaited, and its pending
cleanup runs on completion or cancellation. Metadata consumers can identify
that the handler is asynchronous and dispatch it through the executor. In the
health endpoint example, the route is visible during compilation and tracing
covers the complete async operation.

## Implementation and acceptance

1. Parse and type-check `async fn` and `await`, then lower one suspension
   point to C and drive it with the deterministic executor. Test delayed
   execution, typed results, generic composition, and diagnostics that point
   to a captured reference and its suspension point.
2. Implement task ownership, cancellation, and cleanup across nested
   awaits. Verify that two tasks can overlap, that cancellation before and
   after suspension runs the expected cleanup, and that borrowed storage
   stays live until the correct task completes.
3. Implement `@` application, declarations for user-defined decorators,
   typed metadata, and static function wrapping. Verify import and
   resolution, generic signatures, stacking order, and diagnostics at both
   the decorator and the decorated declaration.
4. Run the decorated async handler end to end. Verify wrapper entry and
   exit order on normal completion and cancellation. Add the grammar,
   formatter, LSP navigation, and machine-readable diagnostics as each
   feature lands. Compare execution through TinyCC and native C on the
   deterministic executor.
