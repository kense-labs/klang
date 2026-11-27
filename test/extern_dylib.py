"""Extern blocks bound from a shared library at run time (spec §7.10,
docs/design/dynamic_externs.md): a library built here is found by the
derived spelling of its name, relative to the program's resource directory
or on the loader's search path, and called through every parameter shape
an extern takes, under the JIT and in a built program alike. A library or
a symbol that is missing is a runtime error naming it, unless nothing
calls the declaration; a package whose extern is dynamic needs no native
part, and one with both kinds still links its own. The sidecar's dylibs
lists map a name to the spelling each platform opens, the program's over a
package's, and refuse a malformed entry or a name no block declares."""
import os
import shutil
import subprocess
from pathlib import Path

import toolchain as tc

LIB_SUFFIX = ".dll" if tc.IS_WINDOWS else ".dylib" if tc.IS_MACOS else ".so"
LIB_PREFIX = "" if tc.IS_WINDOWS else "lib"

LIBRARY_C = '''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "klang.h"
#include "runtime_tasks.h"
#ifdef _WIN32
#define DLT __declspec(dllexport)
#else
#define DLT
#endif

typedef struct { int32_t x, y; } Point;
typedef struct { int32_t *data; int64_t len; } sl_i32;
typedef struct { uint8_t *data; int64_t len; } sl_u8;

DLT int32_t dlt_add(int32_t a, int32_t b) { return a + b; }
DLT double dlt_scale(double x, float f) { return x * f; }
DLT Point dlt_swap(Point p) { Point r = { p.y, p.x }; return r; }
DLT void dlt_bump(Point *p) { p->x++; p->y++; }
DLT int64_t dlt_sum(sl_i32 xs) {
    int64_t s = 0;
    for (int64_t i = 0; i < xs.len; i++) s += xs.data[i];
    return s;
}
DLT void dlt_greet(k_rref out, sl_u8 name) {
    k_bld_append(out, "hello, ", 7);
    k_bld_append(out, name.data, name.len);
}
/* A handle: an object the library owns, crossing as a u64 (design §6). */
DLT uint64_t dlt_new(int32_t v) { int32_t *p = malloc(sizeof *p); *p = v; return (uint64_t)(uintptr_t)p; }
DLT int32_t dlt_get(uint64_t h) { return *(int32_t *)(uintptr_t)h; }
DLT void dlt_free(uint64_t h) { free((void *)(uintptr_t)h); }
/* The scoped resource travels as a nominal record in K and a C struct. */
typedef struct { uint64_t token; } CounterHandle;
static int32_t *live_counter;
static int32_t opened, closed;
DLT CounterHandle dlt_counter_open(int32_t value) {
    CounterHandle h = { 0 };
    if (value < 0 || live_counter) return h;
    live_counter = (int32_t *)malloc(sizeof *live_counter);
    if (live_counter) {
        *live_counter = value;
        h.token = (uint64_t)(uintptr_t)live_counter;
        opened++;
    }
    return h;
}
DLT int32_t dlt_counter_read(CounterHandle h) {
    return live_counter && h.token == (uint64_t)(uintptr_t)live_counter ? *live_counter : -1;
}
DLT void dlt_counter_close(CounterHandle h) {
    closed++;
    if (h.token == (uint64_t)(uintptr_t)live_counter) {
        free(live_counter);
        live_counter = NULL;
    }
}
DLT int32_t dlt_counter_opened(void) { return opened; }
DLT int32_t dlt_counter_closed(void) { return closed; }
DLT int32_t dlt_counter_size(void) { return (int32_t)sizeof(CounterHandle); }

/* Model a foreign operation that retains an owned counter until a separate
   completion callback. A K caller owns the control block through disposal. */
typedef struct { uint64_t token; } TaskHandle;
typedef struct {
    k_task_scope scope;
    k_task task;
    CounterHandle counter;
    bool foreign_pending;
    int32_t polls, cancellation_requests;
} CounterTask;

static k_task_poll poll_counter_task(void *arg) {
    CounterTask *frame = arg;
    if (++frame->polls == 1) frame->foreign_pending = true;
    return frame->foreign_pending ? K_TASK_POLL_PENDING : K_TASK_POLL_READY;
}

static void cancel_counter_task(void *arg) { ++((CounterTask *)arg)->cancellation_requests; }
static bool drain_counter_task(void *arg) { return !((CounterTask *)arg)->foreign_pending; }

static void finish_counter_task(void *arg, k_task_state state, bool started) {
    CounterTask *frame = arg;
    if (state != K_TASK_CANCELLED || started != (frame->polls != 0) || frame->foreign_pending) abort();
    dlt_counter_close(frame->counter);
}

static const k_task_ops counter_task_ops = {
    poll_counter_task, cancel_counter_task, drain_counter_task, finish_counter_task
};

DLT TaskHandle dlt_start_counter_task(CounterHandle counter) {
    CounterTask *frame = calloc(1, sizeof *frame);
    if (!frame) abort();
    frame->counter = counter;
    if (!k_task_scope_init(&frame->scope, 1) ||
        !k_task_init(&frame->task, frame, &counter_task_ops) ||
        !k_task_scope_submit(&frame->scope, &frame->task)) abort();
    TaskHandle handle = { (uint64_t)(uintptr_t)frame };
    return handle;
}

static CounterTask *counter_task(TaskHandle handle) { return (CounterTask *)(uintptr_t)handle.token; }

DLT int32_t dlt_step_counter_task(TaskHandle handle) {
    CounterTask *frame = counter_task(handle);
    k_task_scope_step(&frame->scope);
    return frame->task.state;
}

DLT void dlt_cancel_counter_task(TaskHandle handle) {
    k_task_scope_shutdown(&counter_task(handle)->scope);
}

DLT int32_t dlt_ack_counter_task(TaskHandle handle) {
    CounterTask *frame = counter_task(handle);
    if (!frame->foreign_pending || frame->task.state != K_TASK_DRAINING) abort();
    int32_t result = dlt_counter_read(frame->counter);
    frame->foreign_pending = false;
    return result;
}

DLT void dlt_dispose_counter_task(TaskHandle handle) {
    CounterTask *frame = counter_task(handle);
    if (frame->scope.count || frame->task.state != K_TASK_CANCELLED ||
        frame->polls > 1 || frame->cancellation_requests != (frame->polls == 1)) abort();
    free(frame);
}

/* Memory the library owns, read through ffi (stdlib/ffi.k). */
DLT uint64_t dlt_name(void) { return (uint64_t)(uintptr_t)"dltest"; }
DLT uint64_t dlt_null(void) { return 0; }
'''

# The block, with the library's spelling to fill in; the last member has
# no implementation, which an unused extern never needs.
BLOCK = '''
import ffi;
struct Point { x: i32, y: i32 }
extern "%s" {
    fn add(a: i32, b: i32) -> i32 = "dlt_add";
    fn scale(x: f64, f: f32) -> f64 = "dlt_scale";
    fn swap(p: Point) -> Point = "dlt_swap";
    fn bump(p: Point&) = "dlt_bump";
    fn sum(xs: const i32[:]) -> i64 = "dlt_sum";
    fn greet(out: u8[>..]&, name: const u8[:]) = "dlt_greet";
    fn new_counter(v: i32) -> u64 = "dlt_new";
    fn get(h: u64) -> i32 = "dlt_get";
    fn free_counter(h: u64) = "dlt_free";
    fn name() -> u64 = "dlt_name";
    fn null_ptr() -> u64 = "dlt_null";
    fn unused() = "dlt_unused";
}
'''

MAIN = '''
fn main() {
    print(add(40, 2));
    print(scale(1.5, 2.0));
    let p = swap(Point { 1, 2 });
    print(p.x, " ", p.y);
    var q = Point { 10, 20 };
    bump(q);
    print(q.x, " ", q.y);
    let xs: i32[4] = [1, 2, 3, 4];
    print(sum(xs));
    var s: u8[>..] = [];
    greet(s, "klang");
    print(s);
    let h = new_counter(7);
    print(get(h));
    free_counter(h);
    print(ffi::cstr(name()));
    var head: u8[>..] = [];
    print(ffi::bytes(name(), 2, head), " ", head);
    var none: u8[>..] = [];
    print(ffi::cstr(null_ptr(), none), " ", none.len, " ", ffi::bytes(null_ptr(), 3).len);
}
'''

EXPECTED = ["42", "3.0", "2 1", "11 21", "10", "hello, klang", "7", "dltest", "true dl",
            "false 0 0"]

SCOPED = '''
import exception;

struct CounterHandle { token: u64 }
extern "lib/dltest" {
    fn open_counter(value: i32) -> CounterHandle = "dlt_counter_open";
    fn read_counter(handle: CounterHandle) -> i32 = "dlt_counter_read";
    fn close_counter(handle: CounterHandle) = "dlt_counter_close";
    fn opens() -> i32 = "dlt_counter_opened";
    fn closes() -> i32 = "dlt_counter_closed";
}

fn with_counter<Body>(value: i32) -> bool {
    let owned = open_counter(value);
    if owned.token == 0 { return false; }
    defer close_counter(owned);
    fn read() -> i32 { return read_counter(owned); }
    Body(read);
    return true;
}

fn early() -> i32 {
    with_counter(23) { read => return read(); };
    return -1;
}

fn fails() {
    with_counter(37) { read => exception::throw(read()); };
}

fn report(error: i32) { print(error, " ", opens(), " ", closes()); }

fn main() {
    let opened = with_counter(19) { read => print(read(), " ", opens(), " ", closes()); };
    print(opened, " ", opens(), " ", closes());
    print(early(), " ", opens(), " ", closes());
    exception::try<i32>(fails, report);
    let missing = with_counter(-1) { read => print(read()); };
    print(missing, " ", opens(), " ", closes());
}
'''

OWNED = '''
import exception;

struct CounterHandle { token: u64 }
extern "lib/dltest" {
    fn open_counter(value: i32) -> own CounterHandle = "dlt_counter_open";
    fn read_counter(borrow handle: CounterHandle) -> i32 = "dlt_counter_read";
    fn close_counter(handle: own CounterHandle) = "dlt_counter_close";
    fn opens() -> i32 = "dlt_counter_opened";
    fn closes() -> i32 = "dlt_counter_closed";
    fn counter_size() -> i32 = "dlt_counter_size";
}

fn make_counter(value: i32) {
    let acquired = open_counter(value);
    let handle = acquired;
    return fn[handle: own CounterHandle](mode: i32) -> i32 {
        defer close_counter(handle);
        let value = read_counter(handle);
        if mode == 1 { return value; }
        return value + 1;
    };
}

fn invoke<C>(call: C, mode: i32) -> i32 { call(mode) }

fn make_failing_counter(value: i32) {
    let handle = open_counter(value);
    return fn[handle: own CounterHandle]() {
        defer close_counter(handle);
        exception::throw(read_counter(handle));
    };
}

fn fails() { make_failing_counter(37)(); }
fn report(error: i32) { print(error, " ", opens(), " ", closes()); }

decorator TraceClose() on fn(own CounterHandle) -> i32 {
    wrap {
        defer print("wrapped cleanup ", closes());
        return next();
    }
}

@TraceClose
fn consume(handle: own CounterHandle) -> i32 {
    defer close_counter(handle);
    return read_counter(handle);
}

@TraceClose
fn consume_failing(handle: own CounterHandle) -> i32 {
    defer close_counter(handle);
    let value = read_counter(handle);
    if value != 0 { exception::throw(value); }
    return value;
}

fn wrapped_failure() { consume_failing(open_counter(47)); }

fn main() {
    let abi = [CounterHandle { token: 0 }];
    assert(bytes_of(abi).len == counter_size());
    let call = make_counter(19);
    print(opens(), " ", closes());
    print(invoke(call, 0), " ", opens(), " ", closes());
    print(make_counter(23)(1), " ", opens(), " ", closes());
    exception::try<i32>(fails, report);
    print(consume(open_counter(43)), " ", opens(), " ", closes());
    exception::try<i32>(wrapped_failure, report);
}
'''

TASKED = '''
struct CounterHandle { token: u64 }
struct TaskHandle { token: u64 }

extern "lib/dltest" {
    fn open_counter(value: i32) -> own CounterHandle = "dlt_counter_open";
    fn start_task(counter: own CounterHandle) -> own TaskHandle = "dlt_start_counter_task";
    fn step(borrow task: TaskHandle) -> i32 = "dlt_step_counter_task";
    fn request_cancel(borrow task: TaskHandle) = "dlt_cancel_counter_task";
    fn foreign_completion(borrow task: TaskHandle) -> i32 = "dlt_ack_counter_task";
    fn dispose(task: own TaskHandle) = "dlt_dispose_counter_task";
    fn closes() -> i32 = "dlt_counter_closed";
}

fn pending_work() {
    let task = start_task(open_counter(41));
    return fn[task: own TaskHandle]() {
        defer dispose(task);
        print(step(task), " ", closes());
        request_cancel(task);
        request_cancel(task);
        print(step(task), " ", step(task), " ", closes());
        print(foreign_completion(task), " ", closes());
        print(step(task), " ", step(task), " ", closes());
    };
}

fn main() {
    pending_work()();
    let unstarted = start_task(open_counter(17));
    defer dispose(unstarted);
    request_cancel(unstarted);
    print(step(unstarted), " ", closes());
}
'''

ASYNC_DECORATORS = '''
struct CounterHandle { token: u64 }

extern "lib/dltest" {
    fn open_counter(value: i32) -> own CounterHandle = "dlt_counter_open";
    fn read_counter(borrow handle: CounterHandle) -> i32 = "dlt_counter_read";
    fn close_counter(handle: own CounterHandle) = "dlt_counter_close";
    fn opens() -> i32 = "dlt_counter_opened";
    fn closes() -> i32 = "dlt_counter_closed";
}

decorator Before() on async fn {
    wrap {
        defer print("outer ", closes());
        await yield_now();
        return await next();
    }
}

decorator After() on async fn {
    wrap {
        defer print("inner ", closes());
        let result = await next();
        await yield_now();
        return result;
    }
}

decorator Skip() on async fn {
    wrap {
        defer print("skip ", closes());
        return 99;
    }
}

@Before
@After
async fn work(handle: own CounterHandle) -> i32 {
    defer {
        print("release ", read_counter(handle));
        close_counter(handle);
    }
    print("started ", read_counter(handle));
    await yield_now();
    return read_counter(handle);
}

@Skip
async fn skipped(handle: own CounterHandle) {
    defer {
        print("release ", read_counter(handle));
        close_counter(handle);
    }
    print("unexpected original body");
    return read_counter(handle);
}

async fn parent() -> i32 {
    return await work(open_counter(5));
}

fn main() {
    task_scope(work(open_counter(1))) { scope => scope.cancel(0); }
    assert(opens() == 1 && closes() == 1);
    task_scope(work(open_counter(2))) { scope =>
        assert(scope.step());
        scope.cancel(0);
    }
    assert(opens() == 2 && closes() == 2);
    task_scope(work(open_counter(3))) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    assert(opens() == 3 && closes() == 3);
    task_scope(work(open_counter(4))) { scope =>
        assert(scope.step());
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    assert(opens() == 4 && closes() == 4);
    print("joined ", parent().join());
    assert(opens() == 5 && closes() == 5);
    print("skipped ", skipped(open_counter(6)).join());
    assert(opens() == 6 && closes() == 6);
}
'''

ASYNC_OWNED = '''
struct CounterHandle { token: u64 }
enum Outcome {
    Ok { value: i32 },
    Failed { code: i32 },
}

extern "lib/dltest" {
    fn open_counter(value: i32) -> own CounterHandle = "dlt_counter_open";
    fn read_counter(borrow handle: CounterHandle) -> i32 = "dlt_counter_read";
    fn close_counter(handle: own CounterHandle) = "dlt_counter_close";
    fn opens() -> i32 = "dlt_counter_opened";
    fn closes() -> i32 = "dlt_counter_closed";
}

async fn offset(value: i32) -> i32 {
    defer { print("child cleanup"); }
    await yield_now();
    return value + 1;
}

async fn measure(value: i32, fail: bool) -> Outcome {
    var state: i32 = 1;
    defer { print("outer ", state, " ", closes()); }
    let handle: own CounterHandle = open_counter(value);
    defer {
        print("inner ", read_counter(handle));
        close_counter(handle);
    }
    let sum = await offset(read_counter(handle));
    state = 2;
    if fail { return Outcome.Failed { read_counter(handle) }; }
    return Outcome.Ok { sum };
}

fn report(result: Outcome) {
    match result {
        Ok item => print("ok ", item.value, " ", opens(), " ", closes()),
        Failed failure => print("failed ", failure.code, " ", opens(), " ", closes()),
    }
}

fn leave() {
    task_scope(measure(11, false)) { scope =>
        assert(scope.step());
        return;
    }
}

async fn cooperative(value: i32) -> i32 {
    let handle: own CounterHandle = open_counter(value);
    defer close_counter(handle);
    await yield_now();
    return read_counter(handle);
}

async fn consume(handle: own CounterHandle) -> i32 {
    defer {
        print("input cleanup ", read_counter(handle));
        close_counter(handle);
    }
    print("input started");
    await yield_now();
    defer { print("resume cleanup"); }
    let result = await offset(read_counter(handle));
    return result;
}

async fn selective(handle: own CounterHandle, stop: i32) -> i32 {
    defer close_counter(handle);
    if stop == 1 { return read_counter(handle); }
    await yield_now();
    if stop == 2 { return read_counter(handle) + 1; }
    let result = await offset(read_counter(handle));
    return result;
}

async fn repeat_owned(handle: own CounterHandle) -> i32 {
    defer {
        print("structured owner ", read_counter(handle));
        close_counter(handle);
    }
    var total: i32 = 0;
    for index in 2 {
        defer { print("structured iteration ", index, " ", total); }
        let value = await offset(read_counter(handle) + index as i32);
        total += value;
    }
    return total;
}

fn main() {
    report(measure(8, false).join());
    report(measure(9, true).join());
    task_scope(measure(70, false)) { scope => scope.cancel(0); }
    print("unstarted ", opens(), " ", closes());
    task_scope(measure(10, false)) { scope =>
        assert(scope.step());
        print("pending ", opens(), " ", closes());
        scope.cancel(0);
    }
    print("cancelled ", opens(), " ", closes());
    leave();
    print("shutdown ", opens(), " ", closes());
    print("cooperative ", cooperative(13).join(), " ", opens(), " ", closes());
    task_scope(consume(open_counter(21))) { scope =>
        print("input constructed ", opens(), " ", closes());
        scope.cancel(0);
    }
    print("input cancelled ", opens(), " ", closes());
    task_scope(consume(open_counter(23))) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("input drained ", opens(), " ", closes());
    print("input joined ", consume(open_counter(29)).join(), " ", opens(), " ", closes());
    print("before suspension ", selective(open_counter(31), 1).join(), " ", opens(), " ", closes());
    print("after suspension ", selective(open_counter(32), 2).join(), " ", opens(), " ", closes());
    task_scope(repeat_owned(open_counter(40))) { scope => scope.cancel(0); }
    task_scope(repeat_owned(open_counter(41))) { scope =>
        assert(scope.step());
        assert(scope.step());
        scope.cancel(0);
    }
    print("structured drained ", opens(), " ", closes());
    print("structured joined ", repeat_owned(open_counter(42)).join(), " ", opens(), " ", closes());
}
'''

def check_extern_dylib(exe, cc, base, *, jit, runtime, extra=()):
    """Raises RuntimeError on a failure. `cc` None skips the built form."""
    base = (Path(base) / "extern-dylib").resolve()
    libdir = base / "lib"
    libdir.mkdir(parents=True, exist_ok=True)
    if not cc:
        return  # The library has to be built by something.
    tc.write_text(base / "dltest.c", LIBRARY_C)
    lib = libdir / (LIB_PREFIX + "dltest" + LIB_SUFFIX)
    ok, log = cc.compile_shared(base / "dltest.c", lib, opt=1, extra=extra,
                                includes=[tc.REPO_ROOT / "src" / "runtime"])
    if not ok:
        raise RuntimeError(f"building the test library:\n{log}")

    def run(argv, cwd=base, env=None):
        full = dict(os.environ)
        full.update(env or {})
        r = subprocess.run([str(a) for a in argv], cwd=cwd, env=full, capture_output=True,
                           text=True, errors="replace", timeout=600)
        if tc.sanitizer_failure(r.stderr):
            raise RuntimeError(r.stdout + r.stderr)
        return r.returncode, r.stdout, r.stderr

    def build(source, level=0):
        """The C `k -o` writes, compiled with the runtime object: the
        executable, beside the source as `k build` would put it, so
        the two forms share the resource directory the library is under."""
        cfile = source.with_suffix(f".{level}.c")
        code, out, err = run([exe, f"-O{level}", "-o", cfile, source])
        if code:
            raise RuntimeError(f"{source.name} -O{level}:\n{out}{err}")
        binary = source.with_name(f"{source.stem}-{level}{tc.EXE_SUFFIX}")
        ok, log = cc.compile(cfile, binary, opt=level if not extra else 1, extra=extra,
                             strict_decls=True, runtime=runtime)
        if not ok:
            raise RuntimeError(f"{source.name} -O{level}:\n{log}")
        return binary, cfile.read_text(encoding="utf-8")

    def both(source, env=None):
        """Runs the program under the JIT and built, at -O0 and -O2, each
        yielding (how, code, out, err)."""
        for level in (0, 2):
            if jit:
                yield (f"jit -O{level}", *run([exe, f"-O{level}", "--jit", source], env=env))
            binary, _ = build(source, level)
            yield (f"built -O{level}", *run([binary], env=env))

    def expect_output(source, env=None, lines=EXPECTED):
        for how, code, out, err in both(source, env):
            if code != 0 or out.split("\n")[:-1] != lines:
                raise RuntimeError(f"{source.name} ({how}) exit {code}:\n{out}{err}\n"
                                   f"expected {lines}")

    def expect_failure(source, *texts):
        for how, code, out, err in both(source):
            if code == 0 or any(t not in err for t in texts):
                raise RuntimeError(f"{source.name} ({how}) exit {code}; expected a runtime "
                                   f"error naming {texts}:\n{out}{err}")

    # A spelling with a separator is relative to the resource directory: the
    # source's under the JIT, the executable's built, both `base` here.
    relative = base / "relative.k"
    tc.write_text(relative, BLOCK % "lib/dltest" + MAIN)
    expect_output(relative)
    scoped = base / "scoped-counter.k"
    tc.write_text(scoped, SCOPED)
    scoped_expected = ["19 1 0", "true 1 1", "23 2 2", "37 3 3", "false 3 3"]
    expect_output(scoped, lines=scoped_expected)
    owned = base / "owned-counter.k"
    tc.write_text(owned, OWNED)
    owned_expected = ["1 0", "20 1 1", "23 2 2", "37 3 3",
                      "wrapped cleanup 4", "43 4 4", "wrapped cleanup 5", "47 5 5"]
    expect_output(owned, lines=owned_expected)
    tasked = base / "counter-task.k"
    tc.write_text(tasked, TASKED)
    tasked_expected = ["2 0", "3 3 0", "41 0", "6 6 1", "6 2"]
    expect_output(tasked, lines=tasked_expected)
    async_owned = base / "async-owned-counter.k"
    tc.write_text(async_owned, ASYNC_OWNED)
    async_owned_expected = [
        "child cleanup", "inner 8", "outer 2 1", "ok 9 1 1",
        "child cleanup", "inner 9", "outer 2 2", "failed 9 2 2", "unstarted 2 2",
        "pending 3 2", "child cleanup", "inner 10", "outer 1 3", "cancelled 3 3",
        "child cleanup", "inner 11", "outer 1 4", "shutdown 4 4", "cooperative 13 5 5",
        "input constructed 6 5", "input cleanup 21", "input cancelled 6 6",
        "input started", "child cleanup", "resume cleanup", "input cleanup 23", "input drained 7 7",
        "input started", "child cleanup", "resume cleanup", "input cleanup 29", "input joined 30 8 8",
        "before suspension 31 9 9", "after suspension 33 10 10",
        "structured owner 40", "child cleanup", "structured iteration 0 42",
        "child cleanup", "structured iteration 1 42", "structured owner 41", "structured drained 12 12",
        "child cleanup", "structured iteration 0 43", "child cleanup", "structured iteration 1 87",
        "structured owner 42", "structured joined 87 13 13",
    ]
    expect_output(async_owned, lines=async_owned_expected)
    async_decorators = base / "async-decorated-counter.k"
    tc.write_text(async_decorators, ASYNC_DECORATORS)
    async_decorators_expected = [
        "release 1", "outer 1", "release 2",
        "started 3", "release 3", "inner 3", "outer 3",
        "started 4", "release 4", "inner 4", "outer 4",
        "started 5", "release 5", "inner 5", "outer 5", "joined 5",
        "skip 5", "release 6", "skipped 99",
    ]
    expect_output(async_decorators, lines=async_decorators_expected)
    if not extra:
        for source, expected in ((scoped, scoped_expected), (owned, owned_expected),
                                 (tasked, tasked_expected), (async_owned, async_owned_expected),
                                 (async_decorators, async_decorators_expected)):
            app = base / (source.stem + tc.EXE_SUFFIX)
            code, out, err = run([exe, "build", "--cc", cc.cc, "-o", app, source])
            if code:
                raise RuntimeError(f"{source.name} via k build, exit {code}:\n{out}{err}")
            commands = [[app]]
            if jit:
                commands.append([exe, "run", source])
            for command in commands:
                code, out, err = run(command)
                if code or out.splitlines() != expected:
                    raise RuntimeError(f"{source.name} via {command}, exit {code}:\n{out}{err}")
    # The generated C is the same from one compile to the next, binds only
    # the externs a call reaches, and takes no prototype from the library.
    _, c1 = build(relative, 1)
    _, c2 = build(relative, 1)
    if c1 != c2:
        raise RuntimeError("the generated C differs between two compiles")
    if "k_dl_bind(k_dllibs, 1, k_dlbinds, 11);" not in c1 or "dlt_unused" in c1:
        raise RuntimeError(f"the generated C binds other than the 11 called members:\n{c1}")
    # A bare name is the loader's to find, by its platform spelling.
    bare = base / "bare.k"
    tc.write_text(bare, BLOCK % "dltest" + MAIN)
    pathvar = "PATH" if tc.IS_WINDOWS else "DYLD_LIBRARY_PATH" if tc.IS_MACOS else "LD_LIBRARY_PATH"
    expect_output(bare, {pathvar: str(libdir) + os.pathsep + os.environ.get(pathvar, "")})
    # The feature's point: a JIT run binds the library with no C toolchain
    # in sight, where a native part would need one.
    if jit:
        code, out, err = run([exe, "--jit", relative], env={"K_CC": str(base / "no-cc")})
        if code != 0 or out.split("\n")[:-1] != EXPECTED:
            raise RuntimeError(f"relative.k (jit, no toolchain) exit {code}:\n{out}{err}")
    # A library that is not there, or a symbol the library lacks, is a
    # runtime error naming the spelling tried; declared but never called,
    # neither is looked for.
    nolib = base / "nolib.k"
    tc.write_text(nolib, BLOCK % "lib/nosuch" + MAIN)
    expect_failure(nolib, f"cannot load lib/nosuch (", f"lib/{LIB_PREFIX}nosuch{LIB_SUFFIX}")
    nosym = base / "nosym.k"
    tc.write_text(nosym, BLOCK % "lib/dltest" + 'extern "lib/dltest" { fn dlt_missing(); }\n'
                  'fn main() { dlt_missing(); }\n')
    expect_failure(nosym, f"lib/dltest (", f"{LIB_PREFIX}dltest{LIB_SUFFIX}) has no symbol dlt_missing")
    unused = base / "unused.k"
    tc.write_text(unused, BLOCK % "lib/nosuch" + 'extern "lib/dltest" { fn dlt_missing(); }\n'
                  'fn main() { print("unused ok"); }\n')
    for how, code, out, err in both(unused):
        if code != 0 or out.strip() != "unused ok":
            raise RuntimeError(f"unused.k ({how}) exit {code}:\n{out}{err}")
    # The sidecar's dylibs maps a name a block uses to the spelling to open,
    # on every platform or on one, where a dotted spelling is literal; the
    # program's sidecar maps a name for every package it uses, a package's
    # maps its own as defaults, and the program's entry wins for a platform
    # it says anything about.
    platform = "_windows" if tc.IS_WINDOWS else "_macos" if tc.IS_MACOS else "_linux"
    other = "_linux" if tc.IS_WINDOWS else "_windows"
    over = base / "over.k"
    tc.write_text(over, BLOCK % "mapped" + MAIN)
    tc.write_text(base / "over.build", 'dylibs = ["mapped=lib/dltest"];\n')
    expect_output(over)
    _, c = build(over, 1)
    if '{ "mapped", "lib/dltest", "lib/dltest", "lib/dltest" }' not in c:
        raise RuntimeError(f"the generated C lacks the sidecar's spelling:\n{c}")
    literal = base / "literal.k"
    tc.write_text(literal, BLOCK % "mapped" + MAIN)
    tc.write_text(base / "literal.build",
                  f'dylibs{platform} = ["mapped=lib/{lib.name}"];\n'
                  f'dylibs{other} = ["mapped=lib/nosuch"];\n')
    expect_output(literal)
    pkg = base / "pkgs" / "mapped"
    pkg.mkdir(parents=True, exist_ok=True)
    tc.write_text(pkg / "mapped.k", 'namespace mapped;\n'
                  'extern "mappedlib" { fn add(a: i32, b: i32) -> i32 = "dlt_add"; }\n')
    tc.write_text(pkg / "mapped.build", 'dylibs = ["mappedlib=lib/dltest"];\n')
    use = 'import mapped from "./pkgs/mapped";\nfn main() { print(mapped::add(2, 3)); }\n'
    default = base / "pkgdefault.k"
    tc.write_text(default, use)
    expect_output(default, lines=["5"])
    merged = base / "pkgmerged.k"
    tc.write_text(merged, use)
    tc.write_text(base / "pkgmerged.build", f'dylibs{other} = ["mappedlib=lib/nosuch"];\n')
    expect_output(merged, lines=["5"])
    overridden = base / "pkgover.k"
    tc.write_text(overridden, use)
    tc.write_text(base / "pkgover.build", f'dylibs{platform} = ["mappedlib=lib/nosuch"];\n')
    expect_failure(overridden, "cannot load mappedlib (", f"lib/{LIB_PREFIX}nosuch{LIB_SUFFIX}")
    # An entry that is not name=spelling, a name mapped twice, and a name no
    # block declares, the program's over every package and a package's over
    # its own, are errors at the sidecar.
    typo = base / "pkgs" / "typo"
    typo.mkdir(parents=True, exist_ok=True)
    tc.write_text(typo / "typo.k", 'namespace typo;\nfn nothing() {}\n')
    tc.write_text(typo / "typo.build", 'dylibs = ["gone=lib/dltest"];\n')
    bad = {
        "nosep": ('dylibs = ["mapped"];\n', BLOCK % "mapped" + MAIN,
                  'nosep.build: dylibs entry "mapped" is written name=spelling'),
        "twice": (f'dylibs{platform} = ["mapped=a", "mapped=b"];\n', BLOCK % "mapped" + MAIN,
                  f"twice.build: dylibs{platform} maps mapped twice"),
        "unnamed": ('dylibs = ["mapped=lib/dltest", "other=lib/dltest"];\n', BLOCK % "mapped" + MAIN,
                    "unnamed.build: dylibs maps other, which no extern block in the program names"),
        "pkgtypo": ("", 'import typo from "./pkgs/typo";\nfn main() { typo::nothing(); }\n',
                    "typo.build: dylibs maps gone, which no extern block of the package names"),
    }
    for name, (sidecar, source, message) in bad.items():
        tc.write_text(base / f"{name}.k", source)
        if sidecar:
            tc.write_text(base / f"{name}.build", sidecar)
        code, out, err = run([exe, "check", base / f"{name}.k"])
        if code == 0 or message not in err:
            raise RuntimeError(f"{name}.k: expected the error {message!r}, got exit {code}:"
                               f"\n{out}{err}")
    # A package's mapping may be for a module of it the program does not
    # import: the package's blocks are those of all its files.
    optional = base / "pkgs" / "optional"
    optional.mkdir(parents=True, exist_ok=True)
    tc.write_text(optional / "optional.k", 'namespace optional;\nfn pure() -> i32 { 42 }\n')
    tc.write_text(optional / "extra.k",
                  'namespace optional;\nextern "extra" { fn foreign() -> i32; }\n')
    tc.write_text(optional / "optional.build", 'dylibs = ["extra=lib/dltest"];\n')
    unimported = base / "pkgunimported.k"
    tc.write_text(unimported, 'import optional from "./pkgs/optional";\n'
                              'fn main() { print(optional::pure()); }\n')
    expect_output(unimported, lines=["42"])
    # A package with a native part may declare both kinds: a program calling
    # its static extern builds and loads the part, one calling only its
    # dynamic extern does not need it, with no toolchain at all.
    if jit:
        pkg = base / "pkgs" / "mixed"
        (pkg / "src").mkdir(parents=True, exist_ok=True)
        tc.write_text(pkg / "mixed.k",
                      'namespace mixed;\n'
                      'extern fn twice(x: i32) -> i32 = "mixed_twice";\n'
                      'extern "lib/dltest" { fn add(a: i32, b: i32) -> i32 = "dlt_add"; }\n')
        tc.write_text(pkg / "mixed.build", 'c_sources = ["src/mixed.c"];\n')
        tc.write_text(pkg / "src" / "mixed.c",
                      '#include <stdint.h>\nint32_t mixed_twice(int32_t x) { return x * 2; }\n')
        home = base / "home"
        shutil.rmtree(home, ignore_errors=True)  # An earlier run's builds.
        dyn = base / "mixed_dyn.k"
        tc.write_text(dyn, 'import mixed from "./pkgs/mixed";\nfn main() { print(mixed::add(2, 3)); }\n')
        code, out, err = run([exe, "run", dyn], env={"K_HOME": str(home),
                                                     "K_CC": str(base / "no-cc")})
        if code != 0 or out.strip() != "5":
            raise RuntimeError(f"mixed_dyn.k (k run) exit {code}:\n{out}{err}")
        if list(home.glob("build/pkg/**/*.c*")) + list(home.glob("build/pkg/**/*.o*")):
            raise RuntimeError("a program calling only a dynamic extern built the native part")
        if tc.IS_WINDOWS and cc.style != "msvc":
            return  # k run builds the part with the toolchain it finds, not this one.
        full = base / "mixed_both.k"
        tc.write_text(full, 'import mixed from "./pkgs/mixed";\n'
                            'fn main() { print(mixed::add(2, 3)); print(mixed::twice(4)); }\n')
        code, out, err = run([exe, "run", full], env={"K_HOME": str(home)})
        if code != 0 or out.split() != ["5", "8"]:
            raise RuntimeError(f"mixed_both.k (k run) exit {code}:\n{out}{err}")
