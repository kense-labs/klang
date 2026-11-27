#include "../../src/runtime/runtime_tasks.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { \
    if (!(expr)) { fprintf(stderr, "line %d: %s\n", __LINE__, #expr); exit(1); } \
} while (0)

typedef struct {
    k_task task;
    k_task_scope *scope;
    k_task *active;
    k_task *submit_on_poll;
    int value;
    int result;
    int polls, cancels, drains, finishes, releases;
    int *resource, *foreign_loan;
    bool failed, finished_started, cancel_in_poll;
    k_task_state terminal;
} Frame;

static k_task_poll poll_frame(void *arg) {
    Frame *f = arg;
    CHECK(k_task_scope_step(f->scope) == K_TASK_STEP_BUSY);
    if (++f->polls == 1) {
        f->resource = malloc(sizeof *f->resource);
        CHECK(f->resource);
        *f->resource = f->value;
        f->foreign_loan = f->resource;
        if (f->submit_on_poll) CHECK(k_task_scope_submit(f->scope, f->submit_on_poll));
        if (f->cancel_in_poll) k_task_cancel(f->active);
    }
    if (f->foreign_loan) return K_TASK_POLL_PENDING;
    f->result = *f->resource;
    return f->failed ? K_TASK_POLL_ERROR : K_TASK_POLL_READY;
}

static void cancel_frame(void *arg) {
    Frame *f = arg;
    CHECK(++f->cancels == 1);
    CHECK(f->active->state == K_TASK_DRAINING);
    CHECK(k_task_scope_step(f->scope) == K_TASK_STEP_BUSY);
}

static bool drain_frame(void *arg) {
    Frame *f = arg;
    ++f->drains;
    return f->foreign_loan == NULL;
}

static void finish_frame(void *arg, k_task_state terminal, bool started) {
    Frame *f = arg;
    CHECK(!f->foreign_loan);
    CHECK(k_task_scope_step(f->scope) == K_TASK_STEP_BUSY);
    CHECK(++f->finishes == 1);
    CHECK(started == (f->polls != 0));
    f->terminal = terminal;
    f->finished_started = started;
    if (f->resource) {
        ++f->releases;
        free(f->resource);
        f->resource = NULL;
    }
}

static const k_task_ops ops = { poll_frame, cancel_frame, drain_frame, finish_frame };

static void prepare(Frame *f, k_task_scope *scope, int value) {
    f->scope = scope;
    f->active = &f->task;
    f->value = value;
    CHECK(k_task_init(&f->task, f, &ops));
}

/* The foreign operation touches the underlying resource one last time
   before acknowledging that its loan has ended. */
static void complete_foreign_work(Frame *f) {
    CHECK(f->foreign_loan && *f->foreign_loan == f->value);
    f->foreign_loan = NULL;
}

static void completion_and_failure(void) {
    k_task_scope scope = { 0 }, another = { 0 };
    Frame a = { 0 }, b = { 0 }, rejected = { 0 };
    CHECK(k_task_scope_init(&scope, 2));
    CHECK(k_task_scope_init(&another, 1));
    prepare(&a, &scope, 11);
    prepare(&b, &scope, 12);
    prepare(&rejected, &scope, 13);
    a.submit_on_poll = &b.task;
    b.failed = true;
    CHECK(k_task_scope_submit(&scope, &a.task));
    CHECK(!k_task_scope_submit(&another, &a.task));
    CHECK(!k_task_scope_init(&scope, 8));
    CHECK(!k_task_init(&a.task, &a, &ops));
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && a.polls == 1);
    CHECK(scope.count == 2 && !k_task_scope_submit(&scope, &rejected.task));
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && b.polls == 1 && a.polls == 1);
    complete_foreign_work(&a);
    complete_foreign_work(&b);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_FINISHED && scope.count == 1);
    CHECK(a.result == 11 && a.terminal == K_TASK_SUCCEEDED && a.releases == 1 && !a.task.scope);
    k_task_cancel(&a.task);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_FINISHED && scope.count == 0);
    CHECK(b.result == 12 && b.terminal == K_TASK_FAILED && b.releases == 1 && !b.task.scope);
    CHECK(!a.cancels && !b.cancels && !a.drains && !b.drains);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_IDLE && a.finishes == 1 && b.finishes == 1);
    CHECK(k_task_scope_init(&scope, 3));
    CHECK(k_task_init(&a.task, &a, &ops));
}

static void cancellation_and_shutdown(void) {
    k_task_scope scope = { 0 };
    Frame a = { 0 }, b = { 0 }, never = { 0 }, rejected = { 0 };
    CHECK(k_task_scope_init(&scope, 3));
    prepare(&a, &scope, 23);
    prepare(&b, &scope, 29);
    prepare(&never, &scope, 31);
    prepare(&rejected, &scope, 37);
    a.cancel_in_poll = true;
    CHECK(k_task_scope_submit(&scope, &a.task) && k_task_scope_submit(&scope, &b.task));
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && a.polls == 1);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && b.polls == 1);
    k_task_cancel(&never.task);
    CHECK(k_task_scope_submit(&scope, &never.task));
    k_task_scope_shutdown(&scope);
    k_task_scope_shutdown(&scope);
    CHECK(!k_task_scope_submit(&scope, &rejected.task));

    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && a.cancels == 1);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && b.cancels == 1);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_FINISHED && scope.count == 2);
    CHECK(never.terminal == K_TASK_CANCELLED && never.finishes == 1 && !never.finished_started);
    CHECK(!never.polls && !never.cancels && !never.drains && !never.releases);
    CHECK(!k_task_scope_submit(&scope, &rejected.task));
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && a.drains == 2 && !a.finishes);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && b.drains == 2 && !b.finishes);

    complete_foreign_work(&a);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_FINISHED && scope.count == 1);
    CHECK(a.finishes == 1 && a.releases == 1 && a.polls == 1 && a.terminal == K_TASK_CANCELLED);
    CHECK(b.foreign_loan && *b.foreign_loan == 29 && !b.finishes);
    complete_foreign_work(&b);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_FINISHED && scope.count == 0);
    CHECK(b.finishes == 1 && b.releases == 1 && b.polls == 1 && b.terminal == K_TASK_CANCELLED);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_IDLE);
}

typedef struct {
    k_task_child child;
    Frame foreign;
    int polls, finishes;
} Parent;

static k_task_poll poll_parent(void *arg) {
    Parent *p = arg;
    if (++p->polls == 1) {
        p->foreign.scope = &p->child.scope;
        p->foreign.active = &p->child.task;
        p->foreign.value = 43;
        CHECK(k_task_child_init(&p->child, &p->foreign, &ops));
    }
    return k_task_child_poll(&p->child);
}

static void cancel_parent(void *arg) {
    Parent *p = arg;
    k_task_child_cancel(&p->child);
}

static bool drain_parent(void *arg) {
    Parent *p = arg;
    return k_task_child_drain(&p->child);
}

static void finish_parent(void *arg, k_task_state terminal, bool started) {
    Parent *p = arg;
    CHECK(terminal == K_TASK_CANCELLED && started);
    CHECK(p->child.task.state == K_TASK_CANCELLED && !p->child.task.scope);
    CHECK(p->foreign.releases == 1 && ++p->finishes == 1);
}

static void nested_cancellation_and_peer_progress(void) {
    const k_task_ops parent_ops = { poll_parent, cancel_parent, drain_parent, finish_parent };
    k_task_scope scope = { 0 };
    k_task parent = { 0 };
    Parent frame = { 0 };
    Frame peer = { 0 };
    CHECK(k_task_scope_init(&scope, 2));
    CHECK(k_task_init(&parent, &frame, &parent_ops));
    prepare(&peer, &scope, 47);
    CHECK(k_task_scope_submit(&scope, &parent) && k_task_scope_submit(&scope, &peer.task));
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && frame.foreign.polls == 1);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && peer.polls == 1);
    k_task_cancel(&parent);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_PENDING && frame.foreign.cancels == 1);
    CHECK(frame.foreign.foreign_loan && frame.foreign.finishes == 0 && frame.finishes == 0);
    complete_foreign_work(&peer);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_FINISHED && peer.terminal == K_TASK_SUCCEEDED);
    complete_foreign_work(&frame.foreign);
    CHECK(k_task_scope_step(&scope) == K_TASK_STEP_FINISHED && parent.state == K_TASK_CANCELLED);
    CHECK(frame.polls == 1 && frame.foreign.polls == 1 && frame.finishes == 1 && !scope.count);
}

int main(void) {
    completion_and_failure();
    cancellation_and_shutdown();
    nested_cancellation_and_peer_progress();
    puts("task scope lifecycle passed");
    return 0;
}
