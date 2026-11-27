# First-class async functions and decorators

K implements `async fn`, structured `await` sites, lexical task scopes,
and cleanup of frame-owned resources. The compiler checks ownership and
lowers tasks to C. The language design also includes typed, user-definable
`@` decorators.

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

### Sequential suspension

A call evaluates the arguments and constructs a task. The body starts when
`.join()` consumes the task and drives it to completion. A task has one
owner and can be moved through generic K code. In the cooperative example
below, the driver first runs through `await`, saves the frame, then resumes
it on the next poll.

```klang
import std;

async fn calculate(x: i64) -> i64 {
    let saved: i64 = x * 2;
    let delta: i64 = await yield_now(3);
    return saved + delta;
}

fn main() {
    let task = calculate(7);
    print(task.join());
}
```

At a suspension site, an async body can await a child. It takes
ownership of that child and saves its own live state while the child is
pending. An awaited value can be typed by inference or by a binding
annotation. A statement such as `await child();` waits for completion and
discards any result.

```klang
import std;

async fn child(x: i64) -> i64 {
    await yield_now();
    return x * 2;
}

async fn parent(x: i64) -> i64 {
    let task = child(x);
    let value: i64 = await task;
    let extra = await child(3);
    return value + extra;
}

fn main() {
    print(parent(7).join());
}
```

The implementation accepts top-level non-generic declarations with typed
parameters and one fixed-size result or no result. An `await` can appear
in a statement, a value binding, `return`, or `break`. Supported expressions
include unary and binary operations, casts, field reads, arguments to direct
calls, scalar assignment, and `if`, `match`, and `while` conditions. An
integer `for` can await either bound. It evaluates its bounds once and
saves its iteration state if the body can suspend. A `for` over a named
array with reference-free, fixed-size elements saves its counter and
reads the next element from the current extent when it resumes. This
includes runtime-sized arrays and named array fields. For an inline slice
range over local storage or a global fixed array, the bounds are evaluated
once. The loop saves the start and count, then reconstructs the view from
the same owner. A stored slice with proven stable owners can itself be
iterated, and a rebound slice is read on the next iteration.

A sequential array with variable-size elements saves a byte cursor and
the element index. Each iteration advances the cursor from the current
element's encoded size before entering the body. The implicit element
reference can stay live across suspension when its owner stays at a stable
address. Relative links within that owner remain usable. When a stored
slice changes its base, the next iteration walks the new sequence to the
saved index. Ordinary iteration only reads one element size per step.

Expression evaluation proceeds left to right. In
`combine(read_current(), await load(), read_current())`, the first read
completes before `load` starts. The second read runs after `load` finishes.
The frame stores already evaluated operands until the enclosing operation
can consume them. Short-circuit operators suspend only if the right operand
is evaluated. Partially evaluated operations can save fixed-size data and
the checked slice descriptors used to resolve indexed locations. If a
later argument suspends, a saved holder argument keeps its backing slots
borrowed. The loan starts from the frame's saved reference or copied
holder, so rebinding its original variable leaves the loan in place.

Reference-free struct, enum, and array literals can suspend in their
initializers. The compiler saves completed initializers through the last
suspending one, then constructs the aggregate. Fixed fills evaluate their
value once. New variable-length and growable arrays can allocate their
storage after the awaited initializer completes. Cancellation during
construction discards staged values before the assignment can publish them.

An index or assignment into named fixed storage or a slice with proven
owners can suspend while computing an index or evaluating the right-hand
side. The compiler checks and saves each completed index. It saves the
evaluated slice before the index runs, so rebinding the variable during
evaluation preserves the original destination. Compound assignment also
saves the old value before running the right-hand side.

Returning evaluates the result, then runs the defers registered on that
path. A nonlocal return from a callback invoked through a separate function
cannot cross a poll stage.

Each reachable site has a saved state for the values and defers needed
after it. The driver reuses the same storage for the saved state and child
of each site. When every path returns before a site, the checker skips
that continuation and its saved frame.

All suspending bodies use checked types and control-flow liveness. A local
remains live when a continuation, a later loop iteration, or a defer on the
current cancellation path can read it. The compiler releases dead scratch
storage after evaluating the await operand and before suspending.

A live local reference or slice keeps every proven owner alive. Small
fixed-size local owners stay in the task frame. Large fixed-size and
runtime-sized owners can use a task-owned data stack. Their addresses stay
stable between polls. Stable global storage and static literals can also
back a saved loan. A direct call that suspends in a later argument saves the
already evaluated reference or slice and checks its original owners. The
call consumes that saved argument even if the variable is rebound during
suspension.

The existing loan rules reject same-activation shrinks that could invalidate
a live loan. An async suspension also leases a proven global array owner
behind a live reference, slice, sampled iteration range or sequential
cursor. This covers grow-only and grow-shrink arrays, capacity-limited
arrays, and arrays embedded in records. The lease stays active while a
child runs or drains and through cancellation defers. Appending preserves
existing addresses. A shrink or overlapping replacement that encounters a
live lease reports a runtime error at the mutation site.

Legacy `reusable` and `reusable[]` pools also support these loans. A live
local pool keeps both its elements and its freelist in task-owned storage.
A saved pool reference carries both regions. Freeing a slot or run keeps
its elements valid, so an existing reference or slice can observe values
written by reuse after a task resumes.

A task-owned stack releases its region after the applicable defers. The
runtime includes parked stacks when trimming unused pages. The C backend
allows up to 4 KiB each for a task's initial state, copied K values at each
site, and its result.

### Borrowed task inputs

An async function can accept a reference, optional reference, or slice to
data with a stable element layout. It can follow self-relative links within
its backing owner when the linked data also has stable layout. A task with
borrowed inputs is constructed directly in
`await work(input)`, `work(input).join()`, or `task_scope(work(input)) { ... }`.
The compiler checks each argument's owner as the argument evaluates. It
rejects a temporary owner or a task value that could escape the checked
invocation.

An awaited child keeps its caller's borrowed storage live until the child
completes or finishes cancellation. A reference decoded from a self-relative
link inherits the original borrow, including when it points outside the
sampled slice and within the same owner. A child can forward that reference
to another checked task. A direct join of a borrowed global array also
leases the storage until it returns. A lexical task scope leases a proven
global or local owner from task construction through finish, including
cancellation cleanup. Each task's lease ends when the driver detaches it.
The scope removes its lease records before the owner's storage can go out
of scope. Writable inputs can update existing elements as tasks take their
turns.

A task can borrow a whole growable or capacity-limited array by read-only
reference when its elements have stable layout. The reference reads the
array's current length, and a full iteration can visit elements appended
between polls. A slice samples the base and range once, including when
its source reference is later rebound. A shared loan rejects shrinking
or replacing the array while the task can read it. A lexical task scope
holds that loan through cancellation cleanup.

A fixed-size struct or array passed by value can hold references and slices
into several independently owned regions. The task copies the reference
and slice slots when the argument evaluates. Rebinding a slot in the
original value leaves that copy intact, and writes to existing elements
remain visible through the copied views. Each possible backing owner is
checked separately and kept live through completion and cancellation.
An awaited child can receive the same copied views, and the caller can keep
a local value of this kind across suspension. A nested reference has the
same stable-layout requirement as a direct borrow. Whole-array resize
authority is passed through a top-level writable reference.

A task can also take a `const` reference to a fixed-size holder with
stable reference and slice slots. The holder can occupy an element or
field inside an owned local or global variable-size region. For local
storage, the compiler checks the owners of its contents after all
arguments have evaluated. An awaited child keeps that storage at a stable
address.
For a global holder, the compiler follows stores and calls throughout the
program. A read-only reference field can lead to another fixed-size holder.
The compiler follows these links to the backing regions, visiting each
owner once even when the links form a cycle. The task keeps every reached
holder and every backing owner valid through cancellation. While the loan
is active, the runtime rejects rebinding a slot, assigning a slice slot, or
replacing a value that contains those slots. Scalar fields and the elements
the slots point to can still be updated under their usual rules. Once the
task finishes or drains cancellation, the caller can rebind the slots.

A growable array can append elements while a holder already in it is
borrowed. A limited array can retain typed references to popped elements.
When all loans into a limited array can be sampled, the runtime protects
the array header and the slots the task can actually reach. The compiler
emits a walker for the holder's layout. Starting from evaluated task
inputs, retained locals, or earlier arguments saved across `await`, the
runtime walks active enum variants, live elements of copied arrays, and
read-only links between holders.
For each root it visits each address and layout once, including in cycles,
then merges ranges for write checks. Direct references with no indirect
loan use a single range.
The caller and child can protect different slots in the same array.
`push`, `append`, growing with `resize`, and assignments can write disjoint
slots. They reject a write spanning a borrowed slot, even beyond the
current length or while a suspended task still needs the slot for
cancellation cleanup. The header loan prevents shrinking the array.
If a loan's extent cannot be sampled, the compiler protects the entire
reserved region for that owner.

A writable whole-array input can grow or shrink an independently owned local
array during a direct `await`, `.join()`, or `task_scope`. The invocation is
checked as a possible resize of that owner. The usual array-view rules apply
to references and slices the caller keeps. Other inputs to the same task and
inputs to peer tasks must borrow independent storage. Each writable input
has its own root in the task body, so a shrink also checks views created by
the task itself. Capacity-limited arrays preserve their type-safe reuse semantics.

In `task_scope`, the writable loan starts when the constructor finishes.
The calling block can access the array again after `join` or `cancel` on
every path to the access, or after scope exit drains the task. The
observation `finished` does not release the loan for the checker. This rule
also covers aliases and storage accessed through captured variables.
The runtime checks for overlapping active loans at writable loan acquisition.
A writable task can forward its input with direct `await` or `.join()`,
including when the child resizes it during cancellation cleanup. The
driver keeps the writable loan through cleanup and detaches the task only
after it finishes.

Leases are tracked per driving thread using task-frame or scope storage.
A growable region uses its data stack identity. Inline capacity-limited
arrays use the owner's byte extent so a replacement through an enclosing
record is checked too. A frame record can carry both. Copied replacements
check the lease after evaluating the incoming value. A fixed-layout
replacement with self-relative links uses its final address to encode
the links, so it checks the lease before construction. Its initializer
can drive tasks with independent storage. The checker rejects a task
that may use the region under construction, including through an alias.

A root struct's independently owned field keeps its own lease. Borrowing
a field leaves sibling regions available for mutation. A task also keeps
individual local fields live across suspension. Passing the whole owner
by reference keeps its descriptor and component addresses stable.

### Cleanup across suspension

A fixed-size `own` C handle acquired in the task body can move into the
suspended frame. Register `defer` before the suspension to release the
handle even if the task never resumes. The defer reserves its handle at the
point of declaration, so later code can borrow it but cannot release or
transfer it independently.

On completion, the resumed body runs its defers as it returns, including when
it returns a typed error value. On cancellation while suspended, the driver
drains the active child, then runs the defers registered at that site in
reverse order.

An `own` parameter moves its handle into the task at construction. When a
task takes owned parameters, its consecutive leading `defer` statements
must release all of them using its inputs and accessible globals. Cancelling
before the first poll runs those defers in reverse order. Once the task
starts, they register with the body's other defers. For a task with no owned
inputs, cancellation before the first poll leaves the body unexecuted.

A carried defer can use local scratch storage and release its resources.
The checker rejects external K buffer resizing and external reference
replacement in these callbacks because scope exit can run them after its
return value has already been evaluated.

### Lexical task scopes

`task_scope` takes ownership of a fixed list of tasks and runs a trailing
block with a controller for that scope. The driver starts each task when its
turn comes. The block can step the queue, cancel a task and wait for its
drain, or join a task and use its typed result.

```klang
import std;

async fn calculate(x: i64) -> i64 {
    await yield_now();
    return x * 2;
}

fn main() {
    task_scope(calculate(2), calculate(7)) { scope =>
        scope.step();
        scope.cancel(0);
        print(scope.join(1));
    }
}
```

`step()` advances at most one runnable task without blocking and returns
whether the scope still holds work. `wait()` advances one runnable task,
waiting for notification first if every task is parked. An empty scope
returns false immediately from either operation.
`wait_for(milliseconds)` also advances at most one task. With a hosted
waiter it may park for up to the supplied `i64` number of milliseconds
when all tasks are waiting. A zero or negative duration checks without
parking. It can return while work remains after a timeout or wake, so code
with a deadline checks its own monotonic clock. `finished(index)` checks
whether the task at a zero-based constant index has reached a terminal
state. It leaves the slot available for `join` or `cancel`.
`join(index)` and `cancel(index)` use zero-based constant indices. Either
operation consumes that slot. Other tasks can run while an operation waits
for its target. The block can return a result, and the controller can be
used only in the block that owns it. Block exit cancels and drains all
outstanding tasks, including on an early return from the enclosing function.
Joins, cancellations, and scope exit use the same wait path while draining.

### Task model

`async fn f(...) -> R` declares the result of the async body. A call evaluates
its arguments in source order and constructs a task. The body starts when an
executor advances the task. `await task` advances the task and suspends the
enclosing async body when the task is pending. Completion delivers the
declared results to the awaiting body.
Implicit suspension through an ordinary function call is a compile error.

Each task has a concrete, statically sized frame type for its function and
specialization. The compiler knows its result types and reference roots.
Tasks with owned or copied inputs can be bound to a variable, moved through
a parameter or result, and composed by generic code. Borrowed inputs use
the checked invocation above. Explicit task type syntax remains open.
Each task has one owner and one consumer. A lexical task scope can own several
tasks and join or cancel the tasks it owns before leaving the scope.
The [first-class functions proposal](functions_and_typescript_runtime.md#first-class-function-values)
extends these ownership rules to tasks created by closures.

The frame holds fixed-size values live across suspension. Saved references,
slices, and copied values that hold them can point into pinned locals,
task-owned data stacks, or caller storage kept live by the invocation. The current implementation returns
fixed-size values without references. Owned variable-size results need an
explicit destination that remains live through completion.

Cancellation propagates from an owning task to its outstanding children. The
compiler emits cleanup for the active suspension point and executes pending
`defer` bodies once in reverse order. Pending cleanup bodies must complete
synchronously. The terminal transition records completion or cancellation
once. A never-started task can release owned inputs through the leading
defers established by construction. The ownership draft describes additional
frame ownership rules.
[Coroutine frames](coroutines.md#45-rules-at-a-suspension-point) ·
[Ownership and cancellation](ownership.md#53-design-b-own-frames-and-cancel)

The language defines task construction, suspension, result delivery, and
cleanup. The standard library provides a deterministic executor for tests,
an entry driver for `async fn main()`, and adapters for event loops and OS
I/O. The executor drives compiler-generated state transitions. Frame storage
is selected by the owner or executor. [Async state machines and executors](https://doc.rust-lang.org/stable/book/ch17-01-futures-and-syntax.html)

### Native operation tasks

`native_task<T>(handle)` consumes an `own` C handle record with a single
`u64` field and produces a task that can be awaited, joined, or moved
into a lexical task scope. C supplies the handle as
`(uint64_t)(uintptr_t)descriptor` using the versioned `k_foreign_task`
descriptor. The descriptor and its frame stay alive through `finish`,
including when the task is cancelled before its first poll.

`T` is a fixed-size C-compatible value with a maximum size of 4 KiB.
The native provider supplies its matching C layout and declares its size
in the descriptor. A poll can borrow the task's result slot while pending.
A ready poll writes the result, and a terminal poll or successful drain
ends the borrow. Keep recoverable errors in the result value so K callers
can branch on them. The driver's error state handles failures to drive the
operation.

The optional Cordis TCP provider delivers a byte, EOF, or a socket error
through this boundary. Its native task pins the component that created it
until `finish` returns. [Cancellable TCP receive](cordis_microkernel.md#cancellable-tcp-receive)

### Deterministic native driver

`src/runtime/runtime_tasks.h` provides the native task driver. A release
installs it at `include/klang/runtime_tasks.h`. Each task has stable storage
provided by its caller. A scope borrows its task records until they reach a
terminal state. Its bounded queue advances one task at a time and rotates
pending tasks without allocation. A provider may submit a task from a
callback. A callback that tries to step the same scope receives `BUSY`.

The driver calls `poll` for ordinary progress. A terminal poll result
certifies that foreign borrows have ended. A cancellation request takes
effect between polls. For a pending task, the driver sends a single
`request_cancel` notification and then calls `drain` until the provider
acknowledges completion. The task keeps its queue slot and all frame
resources during that interval. The `finish` callback runs once on the
terminal transition. Its `started` argument lets the frame distinguish
registered body cleanup from resources already owned at construction.
Shutdown closes submissions. The caller keeps driving pending cancellations
until the scope is empty.

### Hosted readiness

The hosted driver allocates a wait event when it first registers a
readiness-aware task. A provider can return the optional
`k_foreign_task_ready` extension of the existing foreign task descriptor.
After an incomplete poll or drain, its `arm` callback checks readiness and
copies the supplied waker under the same synchronization used to publish
completion. If the operation already made progress, the callback returns
false and the task stays runnable. A null signal in the supplied waker probes
readiness before the driver allocates an event.

`k_task_wake` can run on a provider worker thread. The event latches
the signal until the scope consumes it, including when a signal arrives
after the runnable queue was checked and before the host sleeps. The
driving thread alone executes polls and drain callbacks. A notification
allows parked tasks in the scope to recheck readiness. An awaiting parent
forwards its waker to its child. Cooperative tasks and providers with the
original descriptor continue to run through the nonblocking polling path.

A native host may acquire a waker on the driving thread and give it to
another owner to request shutdown via `k_task_request_shutdown`. The
driving thread applies that request between callbacks and keeps waiting
for any pending drain. Providers quiesce their signal senders before
returning from `finish`. The host releases the event after the scope is
empty and external owners have stopped signaling. The raw C driver also
supports caller-supplied event operations or manual stepping.

The C, K interop, and Cordis fixtures use this driver. A compiled parent
stores its child frame and advances it once per poll. Parent cancellation
requests child cancellation, and parent drain waits until the child is
terminal. The native lifecycle test holds a foreign loan during child drain
and advances another task in the parent's scope. The compiler generates a
terminal callback for active defers. The K/C interop fixture checks a
handle's survival across suspension and exactly-once release on completion
and cancellation.
[Task lifecycle tests](../testing.md#configurations-and-backends)

## Decorators

A metadata declaration can specify an exact handler contract with
`decorator Route(provider: const u8[:], capacity: u32) on async fn(i64) -> i64;`.
K resolves `@Route(...)` at the function declaration and folds its
configuration arguments into a typed record. Code can read the record
with `decorator_metadata<Route>(handler)`. The Cordis stream fixture passes
this record through the ordinary provider selection and reader setup,
verifying the same activation, response and retirement as explicit
configuration. [Typed metadata decorators](../reference.md#typed-metadata-decorators)

A package can define a decorator with typed parameters and supported
declaration targets in one of its namespaces. Other modules can import it and
apply it with `@name` or `@package::name(argument)`. The compiler resolves the decorator
at the declaration, checks its target and argument types, and evaluates its
configuration arguments at compile time.

Synchronous wrappers use a statically bound `next()` call. The compiler
forwards the specialization's concrete parameters, generic bindings and
function values through the ordinary call checker. It gets inferred
results from the original body before checking wrapper returns.
Reference and ownership checks apply throughout. The C handle fixture
observes a single release before wrapper cleanup on normal and exceptional
exits. [Synchronous decorator wrappers](../reference.md#synchronous-decorator-wrappers)

Asynchronous wrappers compile to checked task stages. Each layer runs as
the scheduler polls it and can transfer its original inputs to a child with
`await next()`. The compiler checks that the continuation is consumed at
most once per execution path. Before the child starts, the layer retains
the original input cleanup. Once the child starts, it owns cancellation
and cleanup for those inputs. A parent waits for child drain before running
its active defers. [Asynchronous decorator wrappers](../reference.md#asynchronous-decorator-wrappers)

Configuration arguments are processed top to bottom. Wrappers compose from the
bottom decorator up. Execution enters the first wrapper, followed by
successive wrappers as each continuation runs. This follows the composition
order used by TypeScript.
[TypeScript decorators](https://www.typescriptlang.org/docs/handbook/release-notes/typescript-5-0.html#decorators)

Structs, enums, and their named fields carry metadata on the nominal
declaration. Type aliases and generic instantiations resolve to that
declaration, while an enum payload field also has a variant identity.
Consumers can query the constant record without constructing a value.
The compiler keeps the records out of the annotated value's layout.
The `decorated` expansion source gives libraries the declaration and
typed metadata within an explicit namespace or field owner. Function
bindings retain overload identity. Cordis discovers a handler's
registration record through this path and uses it in the normal
provider selection and reader setup.
[Enumerating decorated declarations](../reference.md#enumerating-decorated-declarations)

## Implementation and acceptance

1. Native and TinyCC tests cover delayed execution, typed results, several
   sequential awaits, child frame reuse, and diagnostics for references that
   would cross a suspension site.
2. Task scope tests check cancellation before the first poll and at each
   sequential site. The C ownership and Cordis TCP fixtures verify that
   cleanup runs once and that a pending foreign operation drains first.
   Early-return tests check the result and cleanup order at multiple stages,
   including a return while a lexical task scope owns a pending child.
   Structured task tests check branch bypass, payloads in suspended `match`
   arms, direct `return await` and `break await`, fixed array iteration,
   and cancellation on different iterations. Liveness tests cover dead
   scratch storage, values used through a loop back edge, and locals read
   by deferred functions during cancellation. In one arm that computes an
   await operand from an `i64[256]` and a `u8[512]`, the full generated C
   task frame measured by Clang `sizeof` on macOS arm64 fell from 2,620
   bytes to 32 bytes after both arrays became dead at suspension.
   Expression tests check argument order across multiple suspension sites,
   by-value aggregate copies, compound assignment, short-circuiting, bounds
   evaluated once, and cancellation before any later operand runs.
   Construction and location tests check nested array and enum literals,
   deferred allocation, cancellation before publication, and multi-level
   indices whose source variables change while the task is suspended.
   Iteration tests cover growth between polls, per-task array ownership,
   inline slice ranges whose bounds are sampled once, and trimming while
   multiple tasks keep arrays live.
   Loan tests cover slices returned by calls, alternative owners,
   rebinding an indexed receiver, cancellation, and shrink diagnostics.
   Sequential walk tests cover variable-size strings, fixed-field mutation,
   growth, self-relative links, and cancellation after a slice rebinds.
3. Metadata declarations, `@` application and typed retrieval cover
   namespace imports, overload selection and signature diagnostics.
   Synchronous wrappers exercise stacked execution, generic and inferred
   signatures, multiple results, reference returns, and ownership
   transfer through normal and exceptional exits.
4. Asynchronous wrappers exercise inferred completion types, task loans,
   and cancellation both before and after starting the continuation.
   The C handle fixture checks exactly-once release for these cancellation
   phases and for a wrapper that returns early. The Cordis stream fixture
   cancels a decorated handler during a pending native TCP read and checks
   socket closure after drain. Both fixtures run through TinyCC and native C.
5. Declaration metadata tests cover aliases, generic specializations,
   root structs and separate enum variant fields. The native and TinyCC
   tests compare physical layout with equivalent undecorated records.
   The language server resolves a field selector to its declaration.
6. Scoped enumeration tests cover overload identity, imported scopes,
   generic types, field accessors in initializers and empty match
   fragments. The async fixture awaits selected handlers, and the
   Cordis fixture compares discovery against explicit registration.
