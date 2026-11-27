#define K_NEED_TASK_WAIT 1
#include "../../src/runtime/runtime.h"
#include "../../src/runtime/runtime_impl.h"
#include "../../src/runtime/runtime_threads.h"
#include "../../src/runtime/runtime_tasks.h"

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); exit(1); } \
} while (0)

typedef struct {
    k_task_waker waker;
    int polls, cancels, drains, finishes;
    bool ready, drain_ready, complete_before_arm;
    k_task_state terminal;
} Frame;

static k_task_poll poll_frame(void *opaque) {
    Frame *frame = opaque;
    ++frame->polls;
    if (frame->ready) return K_TASK_POLL_READY;
    if (frame->complete_before_arm) frame->ready = true;
    return K_TASK_POLL_PENDING;
}

static void cancel_frame(void *opaque) {
    Frame *frame = opaque;
    CHECK(++frame->cancels == 1);
}

static bool drain_frame(void *opaque) {
    Frame *frame = opaque;
    ++frame->drains;
    return frame->drain_ready;
}

static void finish_frame(void *opaque, k_task_state terminal, bool started) {
    Frame *frame = opaque;
    CHECK(started);
    CHECK(++frame->finishes == 1);
    frame->terminal = terminal;
}

static bool arm_frame(void *opaque, const k_task_waker *waker) {
    Frame *frame = opaque;
    if (frame->cancels ? frame->drain_ready : frame->ready) return false;
    if (waker->signal) frame->waker = *waker;
    return true;
}

static const k_task_ops ops = { poll_frame, cancel_frame, drain_frame, finish_frame, arm_frame };

static void (*before_wait)(void *);
static void *boundary;
static int waits;

static uint32_t take_at_boundary(void *event, int block) {
    if (block) {
        ++waits;
        CHECK(before_wait);
        void (*action)(void *) = before_wait;
        before_wait = NULL;
        action(boundary);
    }
    return k_task_event_take(event, block);
}

static const k_task_wait_ops observed_wait = {
    k_task_event_open, k_task_event_close, k_task_event_signal, take_at_boundary, NULL
};

static void prepare(k_task_scope *scope, k_task *task, Frame *frame) {
    waits = 0;
    CHECK(k_task_scope_init(scope, 1));
    CHECK(k_task_scope_use_waiter(scope, &observed_wait));
    CHECK(k_task_init(task, frame, &ops));
    CHECK(k_task_scope_submit(scope, task));
}

static void complete_read(void *opaque) {
    Frame *frame = opaque;
    frame->ready = true;
    k_task_wake(frame->waker);
    k_task_wake(frame->waker);
}

static void completion_races_sleep(void) {
    k_task_scope scope = { 0 };
    k_task task = { 0 };
    Frame frame = { 0 };
    prepare(&scope, &task, &frame);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING);
    CHECK(frame.waker.signal && task.parked);
    for (int i = 0; i < 64; ++i) CHECK(k_task_scope_step(&scope) == K_TASK_STEP_WAITING);
    CHECK(frame.polls == 1 && waits == 0);
    before_wait = complete_read;
    boundary = &frame;
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_FINISHED);
    CHECK(waits == 1 && frame.polls == 2 && frame.finishes == 1 && frame.terminal == K_TASK_SUCCEEDED);
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_IDLE);
    CHECK(k_task_scope_release(&scope));
}

static void completion_races_arm(void) {
    k_task_scope scope = { 0 };
    k_task task = { 0 };
    Frame frame = { .complete_before_arm = true };
    prepare(&scope, &task, &frame);
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_PENDING);
    CHECK(!task.parked && !scope.event);
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_FINISHED);
    CHECK(waits == 0 && frame.polls == 2 && frame.finishes == 1);
    CHECK(k_task_scope_release(&scope));
}

static void timed_wait_leaves_parked_task_pending(void) {
    k_task_scope scope = { 0 };
    k_task task = { 0 };
    Frame frame = { 0 };
    CHECK(k_task_scope_init(&scope, 1));
    k_task_scope_hosted(&scope);
    CHECK(k_task_init(&task, &frame, &ops) && k_task_scope_submit(&scope, &task));
    CHECK(k_task_scope_wait_for(&scope, 0) == K_TASK_STEP_PENDING);
    CHECK(k_task_scope_wait_for(&scope, 5) == K_TASK_STEP_WAITING);
    CHECK(frame.polls == 1 && task.parked);
    complete_read(&frame);
    CHECK(k_task_scope_wait_for(&scope, 100) == K_TASK_STEP_FINISHED);
    CHECK(frame.polls == 2 && frame.finishes == 1);
    CHECK(k_task_scope_release(&scope));
}

static void cooperative_peer_progresses(void) {
    k_task_scope scope = { 0 };
    k_task slow = { 0 }, peer = { 0 };
    Frame pending = { 0 }, cooperative = { .complete_before_arm = true };
    k_task_ops cooperative_ops = ops;
    cooperative_ops.arm = NULL;
    waits = 0;
    CHECK(k_task_scope_init(&scope, 2));
    CHECK(k_task_scope_use_waiter(&scope, &observed_wait));
    CHECK(k_task_init(&slow, &pending, &ops) && k_task_scope_submit(&scope, &slow));
    CHECK(k_task_init(&peer, &cooperative, &cooperative_ops) && k_task_scope_submit(&scope, &peer));
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_PENDING && slow.parked);
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_PENDING && cooperative.polls == 1);
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_FINISHED && cooperative.finishes == 1);
    CHECK(pending.polls == 1 && waits == 0);
    complete_read(&pending);
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_FINISHED && pending.finishes == 1);
    CHECK(k_task_scope_release(&scope));
}

typedef struct {
    void *entered;
    k_task_waker waker;
} Cancel;

#ifdef _WIN32
static DWORD WINAPI send_cancel(LPVOID opaque)
#else
static void *send_cancel(void *opaque)
#endif
{
    Cancel *request = opaque;
    CHECK(k_task_event_take(request->entered, 1) == K_TASK_WAKE_READY);
    k_task_request_shutdown(request->waker);
    return 0;
}

static void announce_wait(void *event) {
    k_task_event_signal(event, K_TASK_WAKE_READY);
}

static void complete_drain(void *opaque) {
    Frame *frame = opaque;
    frame->drain_ready = true;
    k_task_wake(frame->waker);
}

static void external_cancel_and_drain(void) {
    k_task_scope scope = { 0 };
    k_task task = { 0 };
    Frame frame = { 0 };
    prepare(&scope, &task, &frame);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && task.parked);
    Cancel request = { k_task_event_open(), k_task_scope_waker(&scope) };
    CHECK(request.entered && request.waker.signal);
#ifdef _WIN32
    HANDLE worker = CreateThread(NULL, 0, send_cancel, &request, 0, NULL);
    CHECK(worker != NULL);
#else
    pthread_t worker;
    CHECK(!pthread_create(&worker, NULL, send_cancel, &request));
#endif
    before_wait = announce_wait;
    boundary = request.entered;
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_PENDING);
#ifdef _WIN32
    CHECK(WaitForSingleObject(worker, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(worker);
#else
    CHECK(!pthread_join(worker, NULL));
#endif
    k_task_event_close(request.entered);
    CHECK(scope.closing && task.parked && frame.polls == 1 && frame.cancels == 1 && frame.drains == 1);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_WAITING && frame.drains == 1 && !frame.finishes);
    before_wait = complete_drain;
    boundary = &frame;
    CHECK(k_task_scope_wait_step(&scope) == K_TASK_STEP_FINISHED);
    CHECK(waits == 2 && frame.cancels == 1 && frame.drains == 2 &&
          frame.finishes == 1 && frame.terminal == K_TASK_CANCELLED);
    CHECK(k_task_scope_release(&scope));
}

static k_task_poll foreign_poll(void *opaque, void *result) {
    (void)opaque;
    *(int *)result = 17;
    return K_TASK_POLL_READY;
}

static void legacy_prefix(void) {
    size_t size = sizeof(k_foreign_task);
    k_foreign_task descriptor = { K_FOREIGN_TASK_ABI_VERSION, (uint32_t)size, sizeof(int), NULL,
                                  foreign_poll, NULL, drain_frame, finish_frame };
    void *old = malloc(size);
    CHECK(old);
    memcpy(old, &descriptor, size);
    const k_foreign_task *op = k_foreign_task_get((uint64_t)(uintptr_t)old);
    k_task_waker probe = { 0 };
    CHECK(op && !k_foreign_task_arm(op, &probe));
    int result = 0;
    CHECK(op->poll(op->frame, &result) == K_TASK_POLL_READY && result == 17);
    free(old);
}

int main(void) {
    completion_races_sleep();
    completion_races_arm();
    timed_wait_leaves_parked_task_pending();
    cooperative_peer_progresses();
    external_cancel_and_drain();
    legacy_prefix();
    puts("task readiness and external shutdown passed");
    return 0;
}
