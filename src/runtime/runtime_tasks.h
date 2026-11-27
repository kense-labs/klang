/* Single-thread task driver. Each task and its frame have caller-supplied,
   stable storage. A scope has a bounded intrusive queue and allocates nothing. */
#ifndef K_RUNTIME_TASKS_H
#define K_RUNTIME_TASKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    K_TASK_EMPTY = 0,
    K_TASK_CREATED,
    K_TASK_PENDING,
    K_TASK_DRAINING,
    K_TASK_SUCCEEDED,
    K_TASK_FAILED,
    K_TASK_CANCELLED
} k_task_state;

typedef enum {
    K_TASK_POLL_PENDING,
    K_TASK_POLL_READY,
    K_TASK_POLL_ERROR
} k_task_poll;

typedef enum {
    K_TASK_STEP_IDLE,
    K_TASK_STEP_PENDING,
    K_TASK_STEP_FINISHED,
    K_TASK_STEP_BUSY,
    K_TASK_STEP_WAITING
} k_task_step;

enum { K_TASK_WAKE_READY = 1, K_TASK_WAKE_CANCEL = 2 };

typedef struct {
    void *context;
    void (*signal)(void *context, uint32_t flags);
} k_task_waker;

/* These two helpers may be called from other threads. A waker stays valid
   through task finish, or until a host stops its external signal senders. */
static inline void k_task_wake(k_task_waker waker) {
    if (waker.signal) waker.signal(waker.context, K_TASK_WAKE_READY);
}

static inline void k_task_request_shutdown(k_task_waker waker) {
    if (waker.signal) waker.signal(waker.context, K_TASK_WAKE_CANCEL);
}

/* A host event latches signals until take consumes them. A blocking take
   waits for at least one flag. The task driver remains single-threaded. */
typedef struct {
    void *(*open)(void);
    void (*close)(void *event);
    void (*signal)(void *event, uint32_t flags);
    uint32_t (*take)(void *event, int block);
    uint32_t (*take_for)(void *event, uint32_t millis);
} k_task_wait_ops;

/* Callbacks run on the scope's driving thread and return to the driver.
   poll may start work. READY and ERROR both relinquish any foreign loans.
   After a cancellation request, request_cancel runs at most once. Subsequent
   drain calls can observe completion but cannot start more work. A true
   drain means no foreign operation can still access the frame or its resources.
   finish runs once, with started false if poll never ran, and quiesces its
   signal senders before returning. After pending or incomplete drain, arm
   atomically checks readiness and registers a waker. It returns true only
   when the task can park and the provider will signal its next progress.
   A waker with a null signal probes readiness without changing registration.
   Copy the waker before returning when registering it. */
typedef struct {
    k_task_poll (*poll)(void *frame);
    void (*request_cancel)(void *frame);
    bool (*drain)(void *frame);
    void (*finish)(void *frame, k_task_state terminal, bool started);
    bool (*arm)(void *frame, const k_task_waker *waker);
} k_task_ops;

/* Native operation transferred from C to K as an owned u64 token. The
   token is (uint64_t)(uintptr_t)descriptor. The descriptor and its frame
   stay live through finish, including cancellation before the first poll.
   A pending poll may retain the fixed result slot. READY, ERROR and a
   true drain relinquish that loan. */
#define K_FOREIGN_TASK_ABI_VERSION 1
typedef struct k_foreign_task {
    uint32_t abi_version;
    uint32_t struct_size;
    size_t result_size;
    void *frame;
    k_task_poll (*poll)(void *frame, void *result);
    void (*request_cancel)(void *frame);
    bool (*drain)(void *frame);
    void (*finish)(void *frame, k_task_state terminal, bool started);
} k_foreign_task;

/* Optional ABI v1 extension. Return &task.base with struct_size set to
   sizeof(k_foreign_task_ready). The v1 base layout is unchanged. */
typedef struct {
    k_foreign_task base;
    bool (*arm)(void *frame, const k_task_waker *waker);
} k_foreign_task_ready;

static inline const k_foreign_task *k_foreign_task_get(uint64_t token) {
    const k_foreign_task *op = (const k_foreign_task *)(uintptr_t)token;
    if (!op || op->abi_version != K_FOREIGN_TASK_ABI_VERSION || op->struct_size < sizeof(*op) ||
        !op->poll || !op->drain || !op->finish) return NULL;
    return op;
}

static inline bool k_foreign_task_arm(const k_foreign_task *op, const k_task_waker *waker) {
    if (!op || op->struct_size < sizeof(k_foreign_task_ready)) return false;
    const k_foreign_task_ready *extension = (const k_foreign_task_ready *)op;
    return extension->arm && extension->arm(op->frame, waker);
}

typedef struct k_task_scope k_task_scope;
typedef struct k_task {
    const k_task_ops *ops;
    void *frame;
    k_task_scope *scope;
    struct k_task *next;
    k_task_state state;
    bool cancel_requested, parked;
} k_task;

struct k_task_scope {
    k_task *head, *tail;
    size_t count, capacity;
    bool closing, driving;
    const k_task_wait_ops *wait;
    void *event;
};

#if K_NEED_TASK_RESIZE_LOANS
/* A synchronous nested drive (an awaited child) keeps the authority of
   its caller. A peer polled after that caller returns starts a fresh chain. */
typedef struct k_task_drive {
    const k_task *task;
    const struct k_task_drive *parent;
} k_task_drive;
static K_TLS const k_task_drive *k_task_current_drive;

static inline bool k_task_in_drive(const k_task *task) {
    for (const k_task_drive *drive = k_task_current_drive; drive; drive = drive->parent)
        if (drive->task == task) return true;
    return false;
}
#endif

#if K_NEED_TASK_LOANS
#if K_NEED_TASK_HOLDER_GRAPHS
typedef struct k_task_holder_graph k_task_holder_graph;
typedef void (*k_task_holder_visit)(k_task_holder_graph *, const void *);
typedef struct {
    const void *start;
    size_t bytes;
    k_task_holder_visit visit;
} k_task_holder_range;

struct k_task_holder_graph {
    k_task_cleanup cleanup;
    k_task_holder_range *ranges;
    size_t count, capacity;
    size_t *seen;
};

static inline size_t k_task_holder_key(const k_task_holder_graph *graph, const void *start,
                                      size_t bytes, k_task_holder_visit visit) {
    uintptr_t address = (uintptr_t)start;
    size_t mask = 2 * graph->capacity - 1;
    size_t index = (size_t)((address >> 3) ^ (address >> 13)) & mask;
    while (graph->seen[index]) {
        const k_task_holder_range *range = &graph->ranges[graph->seen[index] - 1];
        if (range->start == start && range->bytes == bytes && range->visit == visit) break;
        index = (index + 1) & mask;
    }
    return index;
}

static inline void k_task_holder_add(k_task_holder_graph *graph, const void *start,
                                     size_t bytes, k_task_holder_visit visit) {
    if (!start || (!bytes && !visit)) return;
    size_t key = graph->capacity ? k_task_holder_key(graph, start, bytes, visit) : 0;
    if (graph->capacity && graph->seen[key]) return;
    if (graph->count == graph->capacity) {
        if (graph->capacity > (size_t)-1 / (4 * sizeof(k_task_holder_range)))
            k_panic("task holder graph is too large");
        size_t capacity = graph->capacity ? graph->capacity * 2 : 8;
        k_task_holder_range *ranges = (k_task_holder_range *)realloc(graph->ranges, capacity * sizeof(*ranges));
        size_t *seen = (size_t *)calloc(capacity * 2, sizeof(*seen));
        if (!ranges || !seen) k_panic("out of memory sampling a task holder graph");
        free(graph->seen);
        graph->ranges = ranges;
        graph->capacity = capacity;
        graph->seen = seen;
        for (size_t i = 0; i < graph->count; ++i) {
            k_task_holder_range range = ranges[i];
            seen[k_task_holder_key(graph, range.start, range.bytes, range.visit)] = i + 1;
        }
        key = k_task_holder_key(graph, start, bytes, visit);
    }
    graph->ranges[graph->count] = (k_task_holder_range){start, bytes, visit};
    graph->seen[key] = ++graph->count;
}

static int k_task_holder_range_order(const void *a, const void *b) {
    uintptr_t first = (uintptr_t)((const k_task_holder_range *)a)->start;
    uintptr_t second = (uintptr_t)((const k_task_holder_range *)b)->start;
    return (first > second) - (first < second);
}

/* Merge adjacent and overlapping ranges once. Writes can then find the
   first possible conflict with a binary search. */
static inline void k_task_holder_compact(k_task_holder_graph *graph) {
    if (graph->count > 1)
        qsort(graph->ranges, graph->count, sizeof(*graph->ranges), k_task_holder_range_order);
    size_t count = 0;
    for (size_t i = 0; i < graph->count; ++i) {
        k_task_holder_range range = graph->ranges[i];
        if (!range.bytes) continue;
        if (count) {
            k_task_holder_range *previous = &graph->ranges[count - 1];
            size_t offset = (uintptr_t)range.start - (uintptr_t)previous->start;
            if (offset <= previous->bytes) {
                if (range.bytes > previous->bytes - offset) previous->bytes = offset + range.bytes;
                continue;
            }
        }
        graph->ranges[count++] = range;
    }
    graph->count = count;
}
#endif

/* A growable region has a stable stack identity. Inline array storage can
   also be overwritten through an enclosing aggregate, so lease its byte
   extent. The owning worker drives all tasks that can access these bytes. */
typedef struct k_task_loan {
    struct k_task_loan *next, **link;
    const k_stack *owner;
    uintptr_t start;
    size_t bytes;
    const k_task *task;
    bool resize;
#if K_NEED_TASK_HOLDER_GRAPHS
    k_task_holder_graph *graph;
#endif
} k_task_loan;

static K_TLS k_task_loan *k_task_loans;

static inline void k_task_loan_open(k_task_loan *loan, const k_stack *owner,
                                   const void *start, size_t bytes, const k_task *task, bool resize) {
    loan->owner = owner;
    loan->start = (uintptr_t)start;
    loan->bytes = bytes;
    loan->task = task;
    loan->resize = resize;
#if K_NEED_TASK_HOLDER_GRAPHS
    loan->graph = NULL;
#endif
    loan->link = &k_task_loans;
    loan->next = k_task_loans;
    if (loan->next) loan->next->link = &loan->next;
    k_task_loans = loan;
}

#if K_NEED_TASK_HOLDER_GRAPHS
static inline void k_task_holder_release(k_task_cleanup *entry) {
    k_task_holder_graph *graph = (k_task_holder_graph *)entry;
    free(graph->ranges);
    free(graph->seen);
    free(graph);
}

static inline void k_task_holder_open(k_task_loan *loan, const k_task *task, const void *root,
                                     size_t bytes, k_task_holder_visit visit) {
    k_task_loan_open(loan, NULL, NULL, 0, task, false);
    if (!root) return;
    k_task_holder_graph *graph = (k_task_holder_graph *)calloc(1, sizeof(*graph));
    if (!graph) k_panic("out of memory opening a task holder graph");
    k_task_cleanup_open(&graph->cleanup, k_task_holder_release);
    k_task_holder_add(graph, root, bytes, visit);
    for (size_t i = 0; i < graph->count; ++i) {
        k_task_holder_range range = graph->ranges[i];
        if (range.visit) range.visit(graph, range.start);
    }
    free(graph->seen);
    graph->seen = NULL;
    k_task_holder_compact(graph);
    loan->graph = graph;
}
#endif

static inline void k_task_loans_close(k_task_loan *loans, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        k_task_loan *loan = &loans[i];
        *loan->link = loan->next;
        if (loan->next) loan->next->link = loan->link;
#if K_NEED_TASK_HOLDER_GRAPHS
        if (loan->graph) k_task_cleanup_close(&loan->graph->cleanup);
#endif
    }
}

static inline bool k_task_ranges_overlap(uintptr_t a, size_t n, uintptr_t b, size_t m) {
    return n && m && (a <= b ? b - a < n : a - b < m);
}

#if K_NEED_TASK_HOLDER_GRAPHS
static inline bool k_task_holder_overlaps(const k_task_holder_graph *graph, uintptr_t address, size_t bytes) {
    size_t first = 0, last = graph->count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        k_task_holder_range range = graph->ranges[middle];
        uintptr_t start = (uintptr_t)range.start;
        if (start <= address && address - start >= range.bytes) first = middle + 1;
        else last = middle;
    }
    if (first == graph->count) return false;
    k_task_holder_range range = graph->ranges[first];
    return k_task_ranges_overlap(address, bytes, (uintptr_t)range.start, range.bytes);
}
#endif

static inline bool k_task_loan_conflicts(const k_stack *owner, const void *start, size_t bytes,
                                        bool reserve) {
    uintptr_t address = (uintptr_t)start;
    for (const k_task_loan *loan = k_task_loans; loan; loan = loan->next) {
        /* A scope input is protected from construction through finish.
           Detachment follows finish, and the scope owns the token itself. */
        if (loan->task && !loan->task->scope && loan->task->state != K_TASK_EMPTY) continue;
        bool overlaps = k_task_ranges_overlap(address, bytes, loan->start, loan->bytes);
#if K_NEED_TASK_HOLDER_GRAPHS
        if (!overlaps && bytes && loan->graph)
            overlaps = k_task_holder_overlaps(loan->graph, address, bytes);
#endif
        if (!(owner && loan->owner == owner) && !overlaps) continue;
        if (!reserve && loan->resize) {
#if K_NEED_TASK_RESIZE_LOANS
            if (k_task_in_drive(loan->task)) continue;
#endif
        }
        return true;
    }
    return false;
}

static inline void k_task_check_shrink(const k_stack *owner, const void *start, size_t bytes,
                                       const char *file, int line) {
    static const uint8_t message[] = "cannot shrink an array borrowed by a task";
    if (k_task_loan_conflicts(owner, start, bytes, false))
        k_abort_msg(message, sizeof message - 1, file, line);
}

#if K_NEED_TASK_HOLDER_LOANS
static inline void k_task_check_holder_write(const void *start, size_t bytes,
                                             const char *file, int line) {
    static const uint8_t message[] = "cannot replace reference slots borrowed by a task";
    if (k_task_loan_conflicts(NULL, start, bytes, false))
        k_abort_msg(message, sizeof message - 1, file, line);
}
#endif

#if K_NEED_TASK_RESIZE_LOANS
static inline void k_task_check_resize_loan(const k_stack *owner, const void *start, size_t bytes,
                                           const char *file, int line) {
    static const uint8_t message[] = "cannot exclusively borrow an array already borrowed by a task";
    if (k_task_loan_conflicts(owner, start, bytes, true))
        k_abort_msg(message, sizeof message - 1, file, line);
}
#endif
#endif

static inline bool k_task_terminal(k_task_state state) {
    return state == K_TASK_SUCCEEDED || state == K_TASK_FAILED || state == K_TASK_CANCELLED;
}

/* Initialize zeroed storage, or reuse a detached terminal task. The task,
   frame, scope and operations table must stay valid until the step detaches
   the task. finish releases payloads and leaves that storage to its caller. */
static inline bool k_task_init(k_task *task, void *frame, const k_task_ops *ops) {
    if (task->scope || (task->state != K_TASK_EMPTY && !k_task_terminal(task->state)) ||
        !ops || !ops->poll || !ops->drain) return false;
    task->ops = ops;
    task->frame = frame;
    task->next = NULL;
    task->state = K_TASK_CREATED;
    task->cancel_requested = false;
    task->parked = false;
    return true;
}

/* Initialize zeroed or fully drained scope storage. */
static inline bool k_task_scope_init(k_task_scope *scope, size_t capacity) {
    if (scope->count || scope->driving || scope->event) return false;
    scope->head = scope->tail = NULL;
    scope->capacity = capacity;
    scope->closing = false;
    scope->wait = NULL;
    return true;
}

static inline bool k_task_scope_use_waiter(k_task_scope *scope, const k_task_wait_ops *wait) {
    if (scope->wait || scope->closing || scope->driving ||
        !wait || !wait->open || !wait->close || !wait->signal || !wait->take) return false;
    scope->wait = wait;
    return true;
}

static inline void k_task_scope_hosted(k_task_scope *scope) {
#if K_NEED_TASK_WAIT
    static const k_task_wait_ops host = {
        k_task_event_open, k_task_event_close, k_task_event_signal, k_task_event_take, k_task_event_take_for
    };
    k_task_scope_use_waiter(scope, &host);
#else
    (void)scope;
#endif
}

/* May also be acquired on the driving thread before handing a shutdown
   signal to an external owner. Ordinary cooperative tasks allocate no event. */
static inline k_task_waker k_task_scope_waker(k_task_scope *scope) {
    k_task_waker waker = { NULL, NULL };
    if (scope->wait) {
        if (!scope->event) scope->event = scope->wait->open();
        if (scope->event) {
            waker.context = scope->event;
            waker.signal = scope->wait->signal;
        } else {
            scope->wait = NULL;
        }
    }
    return waker;
}

static inline bool k_task_scope_release(k_task_scope *scope) {
    if (scope->count || scope->driving) return false;
    if (scope->event) scope->wait->close(scope->event);
    scope->wait = NULL;
    scope->event = NULL;
    return true;
}

static inline void k_task_scope_append(k_task_scope *scope, k_task *task) {
    task->next = NULL;
    if (scope->tail) scope->tail->next = task;
    else scope->head = task;
    scope->tail = task;
}

static inline bool k_task_scope_submit(k_task_scope *scope, k_task *task) {
    if (scope->closing || scope->count == scope->capacity || task->scope ||
        task->state != K_TASK_CREATED) return false;
    task->scope = scope;
    k_task_scope_append(scope, task);
    ++scope->count;
    return true;
}

/* Requests are idempotent. Work already inside a poll runs to its boundary.
   A terminal result from that poll settles it. A pending result enters the
   cancellation path on the next step and never runs poll again. */
static inline void k_task_cancel(k_task *task) {
    if (task->state != K_TASK_EMPTY && !k_task_terminal(task->state) && !task->cancel_requested) {
        task->cancel_requested = true;
        task->parked = false;
    }
}

/* Close submissions and request cancellation of all outstanding tasks. Drive
   steps until the scope has no tasks before releasing its storage. */
static inline void k_task_scope_shutdown(k_task_scope *scope) {
    scope->closing = true;
    for (k_task *task = scope->head; task; task = task->next) k_task_cancel(task);
}

static inline k_task_state k_task_advance(k_task *task) {
    if (task->cancel_requested) {
        if (task->state == K_TASK_CREATED) return K_TASK_CANCELLED;
        if (task->state != K_TASK_DRAINING) {
            task->state = K_TASK_DRAINING;
            if (task->ops->request_cancel) task->ops->request_cancel(task->frame);
        }
        return task->ops->drain(task->frame) ? K_TASK_CANCELLED : K_TASK_DRAINING;
    }

    // Publish the active state before poll, which may submit or cancel tasks.
    task->state = K_TASK_PENDING;
    switch (task->ops->poll(task->frame)) {
        case K_TASK_POLL_PENDING: return K_TASK_PENDING;
        case K_TASK_POLL_READY: return K_TASK_SUCCEEDED;
        case K_TASK_POLL_ERROR: return K_TASK_FAILED;
    }
    // An invalid provider result cannot certify that its loans have ended.
    task->cancel_requested = true;
    return K_TASK_PENDING;
}

static inline void k_task_scope_signals(k_task_scope *scope, uint32_t flags) {
    if (flags & K_TASK_WAKE_CANCEL) k_task_scope_shutdown(scope);
    if (flags & K_TASK_WAKE_READY)
        for (k_task *task = scope->head; task; task = task->next) task->parked = false;
}

static inline k_task_step k_task_scope_run_ready(k_task_scope *scope) {
    k_task *task = scope->head;
    if (!task) return K_TASK_STEP_IDLE;
    for (size_t remaining = scope->count; task->parked && remaining > 1; --remaining) {
        scope->head = task->next;
        k_task_scope_append(scope, task);
        task = scope->head;
    }
    if (task->parked) return K_TASK_STEP_WAITING;
    scope->driving = true;
#if K_NEED_TASK_RESIZE_LOANS
    k_task_drive drive = { task, k_task_current_drive };
    k_task_current_drive = &drive;
#endif
    bool started = task->state != K_TASK_CREATED || !task->cancel_requested;
    task->state = k_task_advance(task);
    bool terminal = k_task_terminal(task->state);
    if (terminal && task->ops->finish) task->ops->finish(task->frame, task->state, started);
    if (!terminal && scope->wait && task->ops->arm &&
        (!task->cancel_requested || task->state == K_TASK_DRAINING)) {
        k_task_waker probe = { NULL, NULL };
        if (scope->event || task->ops->arm(task->frame, &probe)) {
            k_task_waker waker = k_task_scope_waker(scope);
            if (waker.signal) task->parked = task->ops->arm(task->frame, &waker);
        }
    }

#if K_NEED_TASK_RESIZE_LOANS
    k_task_current_drive = drive.parent;
#endif
    scope->head = task->next;
    if (!scope->head) scope->tail = NULL;
    if (terminal) {
        task->scope = NULL;
        task->next = NULL;
        --scope->count;
    } else {
        k_task_scope_append(scope, task);
    }
    scope->driving = false;
    return terminal ? K_TASK_STEP_FINISHED : K_TASK_STEP_PENDING;
}

/* One nonblocking round-robin step. A parked task waits for a signal.
   Return BUSY for re-entry from a callback into this scope. */
static inline k_task_step k_task_scope_step(k_task_scope *scope) {
    if (scope->driving) return K_TASK_STEP_BUSY;
    if (scope->event) k_task_scope_signals(scope, scope->wait->take(scope->event, 0));
    return k_task_scope_run_ready(scope);
}

/* When no task can run, take the latched event and check the queue again.
   Signals racing the transition to waiting remain latched. */
static inline k_task_step k_task_scope_wait_step(k_task_scope *scope) {
    k_task_step step = k_task_scope_step(scope);
    while (step == K_TASK_STEP_WAITING) {
        k_task_scope_signals(scope, scope->wait->take(scope->event, 1));
        step = k_task_scope_run_ready(scope);
    }
    return step;
}

/* Drive one task, allowing the host to park until a wake or the time budget.
   The caller can recheck its own deadline after a timeout or spurious wake. */
static inline k_task_step k_task_scope_wait_for(k_task_scope *scope, int64_t millis) {
    k_task_step step = k_task_scope_step(scope);
    if (step != K_TASK_STEP_WAITING || millis <= 0 || !scope->wait->take_for) return step;
    uint32_t timeout = millis > INT32_MAX ? INT32_MAX : (uint32_t)millis;
    uint32_t flags = scope->wait->take_for(scope->event, timeout);
    if (!flags) return step;
    k_task_scope_signals(scope, flags);
    return k_task_scope_run_ready(scope);
}

/* An embedded child has one stable record. Its parent advances it at most
   once per poll, and waits for terminal drain before dropping its storage. */
typedef struct {
    k_task task;
    k_task_scope scope;
} k_task_child;

static inline bool k_task_child_init(k_task_child *child, void *frame, const k_task_ops *ops) {
    return k_task_scope_init(&child->scope, 1) && k_task_init(&child->task, frame, ops) &&
           k_task_scope_submit(&child->scope, &child->task);
}

static inline k_task_poll k_task_child_poll(k_task_child *child) {
    if (!k_task_terminal(child->task.state)) k_task_scope_step(&child->scope);
    switch (child->task.state) {
        case K_TASK_SUCCEEDED: return K_TASK_POLL_READY;
        case K_TASK_FAILED: case K_TASK_CANCELLED: case K_TASK_EMPTY: return K_TASK_POLL_ERROR;
        default: return K_TASK_POLL_PENDING;
    }
}

static inline void k_task_child_cancel(k_task_child *child) {
    k_task_cancel(&child->task);
}

static inline bool k_task_child_drain(k_task_child *child) {
    k_task_child_cancel(child);
    return k_task_child_poll(child) != K_TASK_POLL_PENDING;
}

static inline bool k_task_child_arm(k_task_child *child, const k_task_waker *waker) {
    k_task *task = &child->task;
    return task->scope && task->ops->arm && task->ops->arm(task->frame, waker);
}

static inline bool k_task_drain_local(void *frame) {
    (void)frame;
    return true;
}

static inline bool k_task_join_local(void *frame, const k_task_ops *ops) {
    k_task_child root = {0};
    if (!k_task_child_init(&root, frame, ops)) return false;
    k_task_scope_hosted(&root.scope);
    while (root.scope.count) k_task_scope_wait_step(&root.scope);
    k_task_scope_release(&root.scope);
    return root.task.state == K_TASK_SUCCEEDED;
}

#endif
