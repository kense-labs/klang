/* K runtime — what runtime.h declares and leaves to the runtime: aborts,
   the data stack regions and the faults that reach them, slice pool
   freelists, byte search, text forms and printing. Follows runtime.h, in a
   standalone program's unit or in the runtime object (runtime.h has the
   two); this, runtime_threads.h and runtime_os.h are the only parts of
   either that include the platform's headers. */

static const char *k_errmsgs[] = {
    "limited array capacity exceeded",
    "slice bounds out of range",
    "relative reference offset overflow",
    "assert failed",
    "invalid capacity",
    "resize growth requires a fill value",
    "resize to a negative length",
    "pop on empty array",
    "thread_wait on an unknown thread id",
    "thread_wait on the current thread",
    "corrupt ADT tag",
    "serialization needs a little-endian host (not supported yet)",
    "invalid slice length",
    "slice not from this pool",
    "non-null relative reference encodes as null",
    "pop_n count negative or beyond the array's length",
    "queue limit of zero or less",
    "invalid array length",
    "slice not within this array",
    "slice pool span tree is corrupt",
};

/* Ends the process with `code`, its streams flushed: exit() and every
   runtime error but a contained worker's abort come here. The first thread
   to call it ends the process without the C library's teardown, which two
   threads running at once crash in on Windows, and which nothing of the
   program needs; any later caller waits for that. Under TinyCC the program
   runs inside the compiler, where exit() on the main thread returns to it
   instead. */
static K_NORETURN void k_end(int code);

#include <setjmp.h>
#include <stdarg.h>
/* No abort is raised in a signal handler, so the jump leaves the signal
   state alone. */
#ifdef _WIN32
#define k_setjmp(b)  setjmp(b)
#define k_longjmp    longjmp
#else
#define k_setjmp(b)  _setjmp(b)
#define k_longjmp    _longjmp
#endif

K_API K_NORETURN void k_panic(const char *msg) {
    fprintf(stderr, "klang runtime error: %s\n", msg);
    k_end(1);
}

/* Where an abort in a contained worker returns to (k_run_contained), and
   the message it leaves there for thread_wait; NULL on every other thread. */
typedef struct {
    jmp_buf jump;
    uint8_t *msg;
    int64_t len;
} k_contained;
static K_TLS k_contained *k_contain;

/* The end of a failed check or abort(msg) (§9.3), whose message is `len`
   bytes at `msg` and the location after it. A contained worker's keeps the
   message, without the "klang runtime error: " every printed one starts
   with, and ends that worker; anything else's prints it and ends the
   process. A NULL file is a check the runtime makes on its own, which has
   no K location to print. */
static K_NORETURN void k_fail(const void *msg, int64_t len, const char *file, int line) {
    static const char lead[] = "klang runtime error: ";
    int64_t pre = sizeof lead - 1;
    int64_t loc = file ? snprintf(NULL, 0, " (%s:%d)\n", file, line) : 1;
    uint8_t *text = (uint8_t *)malloc((size_t)(pre + len + loc + 1));
    if (!text) {
        if (k_contain) k_panic("out of memory recording an aborted thread");
        fputs(lead, stderr);
        fwrite(msg, 1, (size_t)len, stderr);
        if (file) fprintf(stderr, " (%s:%d)\n", file, line);
        else fputc('\n', stderr);
        k_end(1);
    }
    memcpy(text, lead, (size_t)pre);
    if (len) memcpy(text + pre, msg, (size_t)len);
    if (file) snprintf((char *)text + pre + len, (size_t)loc + 1, " (%s:%d)\n", file, line);
    else memcpy(text + pre + len, "\n", 2);
    if (k_contain) {
        memmove(text, text + pre, (size_t)(len + loc - 1));
        k_contain->msg = text;
        k_contain->len = len + loc - 1;
        k_longjmp(k_contain->jump, 1);
    }
    fwrite(text, 1, (size_t)(pre + len + loc), stderr);
    free(text);
    k_end(1);
}

/* k_fail with the message printf formats. */
static K_NORETURN void k_failf(const char *file, int line, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    k_fail(buf, n < 0 ? 0 : n < (int)sizeof buf ? n : (int)sizeof buf - 1, file, line);
}

K_API K_NORETURN void k_abort(int err, const char *file, int line) {
    k_fail(k_errmsgs[err], (int64_t)strlen(k_errmsgs[err]), file, line);
}

K_API K_NORETURN void k_abort_msg(const uint8_t *msg, int64_t len, const char *file,
                                     int line) {
    k_fail(msg, len, file, line);
}

K_API K_NORETURN void k_runtime_exit(int64_t code) {
    k_end((int)code);
}

K_API K_NORETURN void k_idxfail(int64_t i, int64_t n, const char *file, int line) {
    k_failf(file, line, "index %lld out of bounds (length %lld)", (long long)i, (long long)n);
}

K_API K_NORETURN void k_divfail(const char *file, int line) {
    k_failf(file, line, "division by zero");
}

K_API K_NORETURN void k_divovf(const char *file, int line) {
    k_failf(file, line, "integer overflow in division");
}

K_API K_NORETURN void k_ovf(int64_t a, const char *op, int64_t b, const char *type,
                               const char *file, int line) {
    k_failf(file, line, "integer overflow (debug): %lld %s %lld at %s", (long long)a, op,
             (long long)b, type);
}

K_API K_NORETURN void k_ovf_neg(int64_t a, const char *type, const char *file, int line) {
    k_failf(file, line, "integer overflow (debug): -(%lld) at %s", (long long)a, type);
}

/* The message of a failing `as` check, with the source value as text. */
static K_NORETURN void k_asfail(const char *why, const char *num, const char *type,
                                  const char *file, int line) {
    k_failf(file, line, "as conversion %s (debug): %s as %s", why, num, type);
}

K_API K_NORETURN void k_asfail_i(const char *why, int64_t v, const char *type,
                                    const char *file, int line) {
    char num[24];
    snprintf(num, sizeof(num), "%lld", (long long)v);
    k_asfail(why, num, type, file, line);
}

K_API K_NORETURN void k_asfail_u(const char *why, uint64_t v, const char *type,
                                    const char *file, int line) {
    char num[24];
    snprintf(num, sizeof(num), "%llu", (unsigned long long)v);
    k_asfail(why, num, type, file, line);
}

K_API K_NORETURN void k_asfail_f(const char *why, double d, int f32, const char *type,
                                    const char *file, int line) {
    uint8_t num[32];
    num[f32 ? k_fmt_f32(num, (float)d) : k_fmt_f64(num, d)] = 0;
    k_asfail(why, (const char *)num, type, file, line);
}

/* ---------------------------------------------------------------------------
   Data stack regions (runtime.h): the program's configuration, handed over
   by k_rt_start, and every region owned by the current thread program, so
   the fault handler can tell "commit more" from a genuine crash and
   guard-gap overruns abort with a message. K workers cannot access
   another thread's storage. Keeping this registry thread-local avoids both
   races with fault handlers and signal-unsafe locks when a different worker
   allocates or exits. */

/* The program's arguments, for stdlib/os.k (runtime_os.h). */
static int k_argc;
static char **k_argv;

/* What a new region reserves: the usable part, which halves whenever the
   platform refuses one (k_reserve_region), and the guard gap behind it;
   the usable part as configured, for the reports; and the page size, a
   whole number of which the usable part always is (k_rt_start). */
static size_t k_region_usable, k_region_gap, k_region_reserve, k_region_page;
/* The worker running on this thread, main's being -1 (runtime_threads.h). */
static K_TLS int64_t k_current_thread_id = -1;
/* The program's address space budget for regions and the most regions its
   main program and any one worker can hold, as k_rt_start was told, and
   the workers that leaves room for: what hardware_threads() reports at
   most. */
static uint64_t k_stack_budget;
static int64_t k_mainregions, k_workerregions;
static int64_t k_thread_cap = INT64_MAX;

/* A region's own sizes travel with it: those reserved before a refusal
   halved the size are larger than those after. */
typedef struct {
    uint8_t *base;
    size_t usable, size;    /* size: with the guard gap */
    k_stack *stack;       /* Stable descriptor of the owning data stack. */
    /* Windows: one past the highest page the fault handler has
       committed, so k_trim_memory releases only what was ever committed
       rather than walking the whole reservation. Pages below it can still
       be uncommitted: skipped by growth or decommitted by slice_pool's
       trim. Unused on POSIX, where the kernel supplies pages on touch. */
    uint8_t *committed;
} k_region;
static K_TLS k_region *k_regions;
static K_TLS volatile long k_nregions;
/* The registry's size: the thread program's region count, as the compiler
   worked it out, so a reservation past it is a compiler bug. */
static K_TLS long k_regions_cap;

/* Indexed stacks have a static count. Suspended tasks own their regions
   separately, so two tasks can use the same poll stack indices. */
typedef struct k_task_region {
    k_stack stack;  /* First member, so the descriptor identifies its owner. */
    k_region region;
    struct k_task_region *previous, *next;
} k_task_region;
static K_TLS k_task_region *k_task_regions;

static k_region *k_find_region(const void *at) {
    for (long i = 0; i < k_nregions; i++) {
        k_region *r = &k_regions[i];
        if ((uintptr_t)at - (uintptr_t)r->base < r->size) return r;
    }
    for (k_task_region *entry = k_task_regions; entry; entry = entry->next) {
        k_region *r = &entry->region;
        if ((uintptr_t)at - (uintptr_t)r->base < r->size) return r;
    }
    return NULL;
}

/* Text without stdio, which a fault handler may not call (nor malloc):
   decimal digits, a size in the largest unit that holds it exactly, and a
   string. Each returns the end of what it wrote. */
static char *k_dec(char *p, uint64_t v) {
    char d[24];
    int n = 0;
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) *p++ = d[--n];
    return p;
}
static char *k_size(char *p, uint64_t bytes) {
    static const char *const units[] = { " bytes", " KB", " MB", " GB", " TB" };
    int u = 0;
    while (u < 4 && bytes && bytes % 1024 == 0) {
        bytes /= 1024;
        u++;
    }
    p = k_dec(p, bytes);
    for (const char *s = units[u]; *s; s++) *p++ = *s;
    return p;
}
static char *k_text(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}

/* The report of a data stack overrunning its region into the guard gap,
   for one write from the fault handler: whose stacks, past how much, and
   what raises it. Fits K_OVERFLOW_TEXT. */
#define K_OVERFLOW_TEXT 320
static size_t k_overflow_text(char *buf, const k_region *r) {
    char *p = k_text(buf, "klang runtime error: data stack overflow: a value on ");
    if (k_current_thread_id < 0) {
        p = k_text(p, "main's");
    } else {
        p = k_text(p, "worker ");
        p = k_dec(p, (uint64_t)k_current_thread_id);
        p = k_text(p, "'s");
    }
    p = k_text(p, " data stacks grew past the ");
    p = k_size(p, r->usable);
    p = k_text(p, " reserved for it");
    if (r->usable < k_region_reserve) {
        p = k_text(p, " (halved from ");
        p = k_size(p, k_region_reserve);
        p = k_text(p, " when the platform ran out of address space)");
    }
    p = k_text(p, "; --stack-reserve or -DK_STACK_RESERVE raises the reservation, "
                   "up to 256 TB\n");
    return (size_t)(p - buf);
}

K_API K_NORETURN void k_stack_overflow(const void *at) {
    const k_region *r = k_find_region(at);
    if (r) {
        char msg[K_OVERFLOW_TEXT];
        fwrite(msg, 1, k_overflow_text(msg, r), stderr);
        k_end(1);
    }
    k_panic("data stack overflow");
}

/* The calling thread program's registry, empty, as it starts. */
static void k_regions_begin(int64_t capacity) {
    k_regions_cap = (long)capacity;
    k_regions = (k_region *)calloc((size_t)(capacity > 0 ? capacity : 1), sizeof(k_region));
    if (!k_regions) k_panic("out of memory allocating the data stack registry");
    k_nregions = 0;
}

static void k_release_region(const k_region *r);
static void k_native_stack_free(void);

static K_TLS k_task_cleanup *k_task_cleanups;

K_API void k_task_cleanup_open(k_task_cleanup *entry, void (*release)(k_task_cleanup *)) {
    entry->release = release;
    entry->link = &k_task_cleanups;
    entry->next = k_task_cleanups;
    if (entry->next) entry->next->link = &entry->next;
    k_task_cleanups = entry;
}

K_API void k_task_cleanup_close(k_task_cleanup *entry) {
    *entry->link = entry->next;
    if (entry->next) entry->next->link = entry->link;
    entry->release(entry);
}

/* Unregister before unmapping, then drop the registry. */
K_API void k_release_regions(void) {
    while (k_task_cleanups) k_task_cleanup_close(k_task_cleanups);
    while (k_task_regions) {
        k_task_region *entry = k_task_regions;
        k_task_regions = entry->next;
        k_release_region(&entry->region);
        free(entry);
    }
    while (k_nregions) {
        long i = k_nregions - 1;
        k_region r = k_regions[i];
        k_nregions = i;
        k_regions[i].base = NULL;
        k_release_region(&r);
    }
    free(k_regions);
    k_regions = NULL;
    k_native_stack_free();
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <fcntl.h>

static size_t k_page_size = 0;
/* The vectored handler k_regions_init registered, for k_fault_uninstall. */
static void *k_fault_handle;
#define K_COMMIT_CHUNK (1u << 20)
/* What each thread program keeps of its native stack for reporting that
   stack's overflow (k_native_stack_init). */
#define K_STACK_GUARANTEE (64u << 10)

static LONG WINAPI k_fault_filter(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_STACK_OVERFLOW) {
        /* On the little stack the guarantee kept: WriteFile rather than
           stdio, which could want more. */
        static const char msg[] = "klang runtime error: native call stack overflow\n";
        DWORD written;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, sizeof(msg) - 1, &written, NULL);
        ExitProcess(1);
    }
    if (code != STATUS_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    uint8_t *hit = (uint8_t *)ep->ExceptionRecord->ExceptionInformation[1];
    k_region *r = k_find_region(hit);
    if (r) {
        /* Within the usable part: commit another chunk (clamped to the
           region) and resume, or where the system has no more memory to
           commit, say so. A read below the commit mark (a page that was
           skipped or decommitted by slice_pool's trim) commits only its
           page, so a stale view does not take back the pages around it;
           a write there still takes a chunk, as reuse writes on. Within
           the gap: a data stack overran. */
        if (hit < r->base + r->usable) {
            uint8_t *page = (uint8_t *)((size_t)hit & ~(k_page_size - 1));
            size_t n = page < r->committed && ep->ExceptionRecord->ExceptionInformation[0] == 0
                           ? k_page_size : K_COMMIT_CHUNK;
            if (page + n > r->base + r->usable)
                n = (size_t)(r->base + r->usable - page);
            if (VirtualAlloc(page, n, MEM_COMMIT, PAGE_READWRITE)) {
                if (page + n > r->committed) r->committed = page + n;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            static const char nomem[] =
                "klang runtime error: out of memory committing a data stack\n";
            DWORD written;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), nomem, sizeof(nomem) - 1, &written,
                      NULL);
            ExitProcess(1);
        }
        char msg[K_OVERFLOW_TEXT];
        DWORD written;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)k_overflow_text(msg, r),
                  &written, NULL);
        ExitProcess(1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* The unit memory is committed and protected in. */
static size_t k_os_page_size(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
}

/* Installed by main as the runtime starts, before any worker can. */
static void k_regions_init(void) {
    k_page_size = k_os_page_size();
    k_fault_handle = AddVectoredExceptionHandler(1, k_fault_filter);
    if (!k_fault_handle)
        k_panic("cannot install data stack fault handler");
}

/* Run by each thread program's thread as it starts. TinyCC's kernel32.def
   does not list SetThreadStackGuarantee, so a program it builds looks it up. */
static void k_native_stack_init(void) {
    ULONG room = K_STACK_GUARANTEE;
    #ifdef __TINYC__
        BOOL (WINAPI *guarantee)(PULONG) = (BOOL (WINAPI *)(PULONG))GetProcAddress(
            GetModuleHandleA("kernel32.dll"), "SetThreadStackGuarantee");
        if (guarantee) guarantee(&room);
    #else
        SetThreadStackGuarantee(&room);
    #endif
}

static void k_native_stack_free(void) {}

/* Address space for one region, or NULL where the platform has none left
   to give: nothing is committed until the fault handler is asked. */
static uint8_t *k_os_reserve(size_t usable, size_t size) {
    (void)usable;
    return (uint8_t *)VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE);
}

static void k_release_region(const k_region *r) {
    if (!VirtualFree(r->base, 0, MEM_RELEASE))
        k_panic("cannot release data stack address space");
}

/* Faults are handled as before k_regions_init (k_rt_stop). TinyCC's
   kernel32.def does not list RemoveVectoredExceptionHandler, so a program
   it builds looks it up, as k_native_stack_init does. */
static void k_fault_uninstall(void) {
    if (k_fault_handle) {
        #ifdef __TINYC__
            ULONG (WINAPI *remove)(PVOID) = (ULONG (WINAPI *)(PVOID))GetProcAddress(
                GetModuleHandleA("kernel32.dll"), "RemoveVectoredExceptionHandler");
            if (remove) remove(k_fault_handle);
        #else
            RemoveVectoredExceptionHandler(k_fault_handle);
        #endif
    }
    k_fault_handle = NULL;
}

/* Whether this is the first call of k_end; and where the later ones wait. */
static int k_end_first(void) {
    static volatile LONG ended;
    return !InterlockedExchange(&ended, 1);
}
static K_NORETURN void k_park(void) {
    for (;;) Sleep(INFINITE);
}

#else  /* posix */

#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

/* The calling thread's native stack, from K_NATIVE_SLOP below its lowest
   usable address up to its top: a fault in there is that stack overflowing,
   whether on the guard below it or at a limit the kernel would not grow it
   past. The slop covers a frame larger than the guard reaching over it.
   Empty where the platform cannot tell. */
#define K_NATIVE_SLOP (64u << 10)
static K_TLS uintptr_t k_native_lo, k_native_hi;
/* The alternate signal stack k_native_stack_init allocated, if it did. */
static K_TLS void *k_sigstack;
#define K_SIGSTACK_MIN (64u << 10)
/* What the fault handler replaced, for the faults that are not its own,
   and which of them it did (1: SIGSEGV, 2: SIGBUS), for k_fault_uninstall. */
static struct sigaction k_prev_segv, k_prev_bus;
static int k_fault_installed;

/* For k_trim_memory, which discards whole pages only. */
static size_t k_page_size = 0;

static void k_fault_handler(int sig, siginfo_t *info, void *ctx) {
    (void)ctx;
    uint8_t *hit = (uint8_t *)info->si_addr;
    k_region *r = k_find_region(hit);
    if (r) {
        char msg[K_OVERFLOW_TEXT];
        ssize_t w = write(2, msg, k_overflow_text(msg, r));
        (void)w;
        _exit(1);
    }
    if ((uintptr_t)hit - k_native_lo < k_native_hi - k_native_lo) {
        static const char msg[] = "klang runtime error: native call stack overflow\n";
        ssize_t w = write(2, msg, sizeof(msg) - 1);
        (void)w;
        _exit(1);
    }
    /* Not ours: the access faults again under what handled it before, the
       default action or a sanitizer's report. */
    sigaction(sig, sig == SIGSEGV ? &k_prev_segv : &k_prev_bus, NULL);
}

/* The unit memory is mapped and protected in. */
static size_t k_os_page_size(void) {
    long n = sysconf(_SC_PAGESIZE);
    return n > 0 ? (size_t)n : 4096;
}

static void k_regions_init(void) {
    k_page_size = k_os_page_size();
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = k_fault_handler;
    /* A native stack overflow leaves the handler no room on the thread's own. */
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &sa, &k_prev_segv))
        k_panic("cannot install data stack fault handler");
    k_fault_installed = 1;
    #ifdef SIGBUS
        if (sigaction(SIGBUS, &sa, &k_prev_bus))
            k_panic("cannot install data stack fault handler");
        k_fault_installed |= 2;
    #endif
}

#ifdef __linux__
/* pthread.h declares it only under _GNU_SOURCE, which would have to come
   before every system header a file includes. */
int pthread_getattr_np(pthread_t, pthread_attr_t *);
#endif

static size_t k_sigstack_size(void) {
    return SIGSTKSZ > K_SIGSTACK_MIN ? (size_t)SIGSTKSZ : (size_t)K_SIGSTACK_MIN;
}

/* Run by each thread program's thread as it starts: an alternate stack for
   the fault handler, unless the thread has one (ASan gives every thread its
   own), and the bounds of the thread's stack. */
static void k_native_stack_init(void) {
    stack_t ss;
    uintptr_t lo = 0, hi = 0;
    if (!sigaltstack(NULL, &ss) && (ss.ss_flags & SS_DISABLE)) {
        ss.ss_size = k_sigstack_size();
        ss.ss_sp = malloc(ss.ss_size);
        ss.ss_flags = 0;
        if (ss.ss_sp && !sigaltstack(&ss, NULL)) k_sigstack = ss.ss_sp;
        else free(ss.ss_sp);
    }
    #if defined(__APPLE__)
        hi = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
        lo = hi - pthread_get_stacksize_np(pthread_self());
    #elif defined(__linux__)
        pthread_attr_t attr;
        void *base;
        size_t size;
        if (!pthread_getattr_np(pthread_self(), &attr)) {
            if (!pthread_attr_getstack(&attr, &base, &size)) {
                lo = (uintptr_t)base;
                hi = lo + size;
            }
            pthread_attr_destroy(&attr);
        }
    #endif
    if (lo > K_NATIVE_SLOP) {
        k_native_lo = lo - K_NATIVE_SLOP;
        k_native_hi = hi;
    }
}

/* At a thread program's end, the alternate stack k_native_stack_init gave
   it. */
static void k_native_stack_free(void) {
    stack_t ss;
    if (!k_sigstack) return;
    memset(&ss, 0, sizeof(ss));
    ss.ss_flags = SS_DISABLE;
    ss.ss_size = k_sigstack_size();
    sigaltstack(&ss, NULL);
    free(k_sigstack);
    k_sigstack = NULL;
}

/* Address space for one region, or NULL where the platform has none left
   to give: its address space or an address-space limit exhausted, or
   strict overcommit accounting, which counts even a MAP_NORESERVE mapping.
   Commit-on-touch via overcommit; the gap at the end stays PROT_NONE. */
static uint8_t *k_os_reserve(size_t usable, size_t size) {
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS
                   #ifdef MAP_NORESERVE
                       | MAP_NORESERVE
                   #endif
                   , -1, 0);
    if (p == MAP_FAILED) return NULL;
    if (mprotect((uint8_t *)p + usable, size - usable, PROT_NONE)) {
        munmap(p, size);
        k_panic("cannot protect data stack guard gap");
    }
    /* Most stacks hold a few bytes, which a transparent huge page at the
       start would commit 2 MB for. */
    #ifdef MADV_NOHUGEPAGE
        madvise(p, usable < (2u << 20) ? usable : (2u << 20), MADV_NOHUGEPAGE);
    #endif
    return (uint8_t *)p;
}

static void k_release_region(const k_region *r) {
    if (munmap(r->base, r->size)) k_panic("cannot release data stack address space");
}

/* Faults are handled as before k_regions_init (k_rt_stop). */
static void k_fault_uninstall(void) {
    if (k_fault_installed & 1) sigaction(SIGSEGV, &k_prev_segv, NULL);
    #ifdef SIGBUS
        if (k_fault_installed & 2) sigaction(SIGBUS, &k_prev_bus, NULL);
    #endif
    k_fault_installed = 0;
}

/* Whether this is the first call of k_end; and where the later ones wait.
   TinyCC names the memory orders in its stdatomic.h alone. */
#ifndef __ATOMIC_SEQ_CST
#define __ATOMIC_SEQ_CST 5
#endif
static int k_end_first(void) {
    static int ended;
    return !__atomic_fetch_add(&ended, 1, __ATOMIC_SEQ_CST);
}
static K_NORETURN void k_park(void) {
    for (;;) pause();
}

#endif

static K_NORETURN void k_end(int code) {
#ifdef K_HOSTED_JIT
    k_jit_exit(code);
#else
    if (!k_end_first()) k_park();
    fflush(NULL);
    #ifdef __TINYC__
        if (k_current_thread_id < 0) exit(code);
    #endif
    _Exit(code);
#endif
}

/* The smallest a region gets before a refusal is fatal. */
#define K_REGION_MIN (1u << 20)

/* A region of the size new ones currently get. Where the platform refuses
   one, every region from then on is half as large, in whole pages, down to
   K_REGION_MIN: a smaller region is always safe, since the checks the
   compiler leaves out assume no more than K_STACK_RESERVE bytes in a
   stack, and the guard gap behind a smaller one aborts growth that much
   sooner. Workers racing through a refusal may each halve the size once
   more than needed, or restore a larger one for a moment; it only ever
   settles downward. */
static k_region k_make_region(k_stack *stack) {
    size_t usable = k_region_usable;
    uint8_t *p;
    for (;;) {
        p = k_os_reserve(usable, usable + k_region_gap);
        if (p) break;
        if (usable <= K_REGION_MIN) k_panic("cannot reserve data stack address space");
        usable /= 2;
        usable -= usable % k_region_page;
        k_region_usable = usable;
    }
    return (k_region){ p, usable, usable + k_region_gap, stack, p };
}

K_API uint8_t *k_reserve_region(k_stack *stack) {
    if (k_nregions >= k_regions_cap)
        k_panic("too many data stack regions");
    long i = k_nregions;
    k_regions[i] = k_make_region(stack);
    k_nregions = i + 1;  /* Publish only the initialized entry. */

    /* Region bases agree in their low 16 or more address bits (Windows
       reserves on 64 KB boundaries; mmap'd regions lie 2 GB + 1 MB apart),
       so the same offset in two stacks does too. A loop storing to several
       stacks in step then runs up to 2x slower (Zen 5, six f32 arrays),
       though sharing a page offset alone costs nothing: bases 4 KB apart
       run at full speed. A different 64-byte step per region within the
       smallest page breaks the match. */
    long colors = 4096 / 64;
    size_t pad = (size_t)(i % colors) * 64;
    return k_regions[i].base + pad;
}

K_API void k_task_stack_open(k_stack **slot) {
    if (*slot) k_panic("task data stack already open");
    k_task_region *entry = (k_task_region *)malloc(sizeof(k_task_region));
    if (!entry) k_panic("out of memory allocating task data stack");
    entry->region = k_make_region(&entry->stack);
    entry->stack.top = entry->region.base;
    entry->previous = NULL;
    entry->next = k_task_regions;
    if (k_task_regions) k_task_regions->previous = entry;
    k_task_regions = entry;
    *slot = &entry->stack;
}

K_API void k_task_stack_close(k_stack **slot) {
    k_task_region *entry = (k_task_region *)*slot;
    if (!entry) return;
    *slot = NULL;
    if (entry->previous) entry->previous->next = entry->next;
    else k_task_regions = entry->next;
    if (entry->next) entry->next->previous = entry->previous;
    k_release_region(&entry->region);
    free(entry);
}

/* Returns the pages [start, start + len) of one region to the OS, which
   reads them back as zeros; start and len are page-aligned. Windows
   decommits, and the access-fault handler commits the pages again on the
   next touch, anywhere in a region's usable part. Linux drops them from the
   resident set at once. macOS replaces them with fresh anonymous memory
   (k_trim_memory says why). k_trim_memory and slice_pool's trim
   (k_slice_pool_trim) share it. 0 means the OS refused. */
static int k_discard_pages(uintptr_t start, size_t len) {
#ifdef _WIN32
    return VirtualFree((void *)start, len, MEM_DECOMMIT) != 0;
#elif defined(__APPLE__)
    return mmap((void *)start, len, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED;
#else
    /* Linux walks only the page tables that were ever populated, so the
       untouched rest of a reservation costs it little. */
    return madvise((void *)start, len, MADV_DONTNEED) == 0;
#endif
}

/* Explicit rather than on every rewind: short-lived scratch can reuse its
   pages cheaply, while a long-running application can give them back after
   replacing a large document. Keep the virtual reservation and the partly
   used final page; limited-array capacity and live borrowed values are below
   the synchronized top and remain intact. Each worker has its own registry.

   Eager on purpose: Windows decommits, which returns the commit charge as
   well as the physical pages (DiscardVirtualMemory and MEM_RESET keep the
   charge), and Linux's MADV_DONTNEED drops the pages from RSS at once,
   where MADV_FREE leaves them counted until the kernel is short of memory.
   On macOS MADV_DONTNEED reclaims nothing reliably, so replace the unused
   suffix with fresh anonymous memory. MAP_FIXED replaces it atomically,
   without making the address range available to another thread. Trimming
   covers the full unused reservation; ordinary growth needs no tracking
   or signal handler, and neither does growth into the replacement mapping.

   Only generated code may call this, through os.trim_memory, whose callers
   publish their cached tops first (SFunction::readsalltops). C reached
   from K through another extern must not: tops cached by the K
   frames above it would still be stale, and live pages above them lost. */
static int k_trim_region(k_region *r) {
    uintptr_t page = (uintptr_t)k_page_size;
    uintptr_t base = (uintptr_t)r->base, top = (uintptr_t)r->stack->top;
    /* An invalid top could make trimming discard live data. */
    if (top < base || top - base > r->usable)
        k_panic("data stack top outside its region in trim_memory");
    uintptr_t start = (top + page - 1) / page * page;
    uintptr_t end = (base + r->usable) / page * page;
#ifdef _WIN32
    /* Nothing above the commit mark needs decommitting. */
    if ((uintptr_t)r->committed < end) end = (uintptr_t)r->committed;
#endif
    if (start >= end) return 1;
    if (!k_discard_pages(start, (size_t)(end - start))) return 0;
#ifdef _WIN32
    r->committed = (uint8_t *)start;
#endif
    return 1;
}

K_API uint8_t k_trim_memory(void) {
    int ok = 1;
    for (long i = 0; i < k_nregions; i++) ok &= k_trim_region(&k_regions[i]);
    for (k_task_region *entry = k_task_regions; entry; entry = entry->next)
        ok &= k_trim_region(&entry->region);
    return (uint8_t)ok;
}

/* ---------------------------------------------------------------------------
   Slice pools (§5.4, `reusable[]`). The freelist holds the free (index,
   count) spans, no two of them touching, in a B+ tree ordered by index:
   leaves hold spans, and every inner entry holds the first index and the
   largest count in the subtree under it, which is what lets first fit, the
   span at or before an index, and the last span each be found by one
   descent. Nodes are K_SPAN_NODE bytes, numbered from the freelist's base:
   node 0 is the header, and the others come off a chain of emptied nodes or
   past the last one, so the node count is the freelist's size, and the
   tree, in node numbers, is copied whole when a worker takes a copy of its
   pool. A freelist starts empty, without a header, until a span is freed.
   Every span comes from a slice of the pool, so spans stay inside the pool's
   length whatever the program frees: freeing a slice twice leaves
   overlapping spans that hand the same elements out twice, never anything
   outside the pool. */

#define K_SPAN_LEAF ((K_SPAN_NODE - 16) / 16)
#define K_SPAN_FAN ((K_SPAN_NODE - 16) / 24)
/* A full node splits into halves and an empty one leaves its parent, so
   every level a tree gains takes about K_SPAN_FAN / 2 times the insertions
   of the one before: no freelist gets this deep. */
#define K_SPAN_DEPTH 24

/* `link` chains emptied nodes. */
typedef struct { int64_t n, link, idx[K_SPAN_LEAF], cnt[K_SPAN_LEAF]; } k_spleaf;
typedef struct { int64_t n, link, key[K_SPAN_FAN], max[K_SPAN_FAN], child[K_SPAN_FAN]; } k_spinner;
typedef struct { int64_t root, height, chain; } k_sphead;

/* A freelist, and a path down it: the node and entry taken at each level,
   the leaf's last. */
typedef struct {
    uint8_t *base;
    int64_t *n;
    uint8_t **top;
    int64_t h, node[K_SPAN_DEPTH], pos[K_SPAN_DEPTH];
} k_sppath;

#define K_SPHEAD(p) ((k_sphead *)(p)->base)
#define K_SPLEAF(p, i) ((k_spleaf *)((p)->base + (i) * K_SPAN_NODE))
#define K_SPINNER(p, i) ((k_spinner *)((p)->base + (i) * K_SPAN_NODE))

static int64_t k_sp_newnode(k_sppath *p) {
    int64_t i = K_SPHEAD(p)->chain;
    if (i) {
        K_SPHEAD(p)->chain = K_SPLEAF(p, i)->link;
        return i;
    }
    *p->top += K_SPAN_NODE;
    return (*p->n)++;
}

static void k_sp_dropnode(k_sppath *p, int64_t i) {
    K_SPLEAF(p, i)->link = K_SPHEAD(p)->chain;
    K_SPHEAD(p)->chain = i;
}

/* Moves a node's entries from position s on up one, making room at s, or
   those after s down onto it. A loop over the arrays together rather than
   a memmove of each: a node holds a few entries, and the call costs more
   than moving them. */
static void k_sp_open(k_spleaf *lf, int64_t s) {
    int64_t j;
    for (j = lf->n; j > s; j--) {
        lf->idx[j] = lf->idx[j - 1];
        lf->cnt[j] = lf->cnt[j - 1];
    }
    lf->n++;
}

static void k_sp_close(k_spleaf *lf, int64_t s) {
    int64_t j;
    lf->n--;
    for (j = s; j < lf->n; j++) {
        lf->idx[j] = lf->idx[j + 1];
        lf->cnt[j] = lf->cnt[j + 1];
    }
}

static void k_sp_openin(k_spinner *in, int64_t s) {
    int64_t j;
    for (j = in->n; j > s; j--) {
        in->key[j] = in->key[j - 1];
        in->max[j] = in->max[j - 1];
        in->child[j] = in->child[j - 1];
    }
    in->n++;
}

static void k_sp_closein(k_spinner *in, int64_t s) {
    int64_t j;
    in->n--;
    for (j = s; j < in->n; j++) {
        in->key[j] = in->key[j + 1];
        in->max[j] = in->max[j + 1];
        in->child[j] = in->child[j + 1];
    }
}

/* The largest of the w counts at v, or `stop` as soon as one reaches it. */
static int64_t k_sp_most(const int64_t *v, int64_t w, int64_t stop) {
    int64_t m = 0, k;
    for (k = 0; k < w && m < stop; k++)
        if (v[k] > m) m = v[k];
    return m;
}

/* The largest count under node i, a leaf or an inner node, or `stop` as
   soon as one reaches it. */
static int64_t k_sp_max(k_sppath *p, int64_t i, int leaf, int64_t stop) {
    return leaf ? k_sp_most(K_SPLEAF(p, i)->cnt, K_SPLEAF(p, i)->n, stop)
                : k_sp_most(K_SPINNER(p, i)->max, K_SPINNER(p, i)->n, stop);
}

static int64_t k_sp_key(k_sppath *p, int64_t i, int leaf) {
    return leaf ? K_SPLEAF(p, i)->idx[0] : K_SPINNER(p, i)->key[0];
}

/* Brings the entries above level l of the path (the leaf being level h) up
   to date with the node below each, after one count in the node at level l
   went from `was` to `now` (0 for an entry added or removed), stopping at
   the first entry that is unchanged. A node's largest count is rescanned
   only where the count that held it shrank, and only until another as
   large turns up; a `was` of INT64_MAX rescans the node at level l whole,
   whatever changed in it. */
static void k_sp_fix(k_sppath *p, int64_t l, int64_t was, int64_t now) {
    for (; l > 0; l--) {
        k_spinner *in = K_SPINNER(p, p->node[l - 1]);
        int64_t k = p->pos[l - 1], c = p->node[l], m = in->max[k];
        int leaf = l == p->h;
        int64_t key = k_sp_key(p, c, leaf);
        int64_t nm = now >= m ? now : was < m ? m : k_sp_max(p, c, leaf, was);
        if (in->key[k] == key && nm == m) return;
        in->key[k] = key;
        in->max[k] = nm;
        was = m;
        now = nm;
    }
}

/* Descends to the leaf holding the last span that starts at or before idx,
   and returns its position there, or -1 where no span does. */
static int64_t k_sp_find(k_sppath *p, int64_t idx) {
    int64_t l, i = K_SPHEAD(p)->root, k;
    k_spleaf *lf;
    p->h = K_SPHEAD(p)->height;
    for (l = 0; l < p->h; l++) {
        k_spinner *in = K_SPINNER(p, i);
        for (k = 1; k < in->n && in->key[k] <= idx; k++) {}
        p->node[l] = i;
        p->pos[l] = k - 1;
        i = in->child[k - 1];
    }
    p->node[p->h] = i;
    lf = K_SPLEAF(p, i);
    for (k = 0; k < lf->n && lf->idx[k] <= idx; k++) {}
    p->pos[p->h] = k - 1;
    return k - 1;
}

/* A node full at the root splits under a new root of the two halves. */
static void k_sp_newroot(k_sppath *p, int64_t a, int64_t b, int leaf) {
    int64_t r = k_sp_newnode(p);
    k_spinner *rn = K_SPINNER(p, r);
    rn->n = 2;
    rn->key[0] = k_sp_key(p, a, leaf);
    rn->max[0] = k_sp_max(p, a, leaf, INT64_MAX);
    rn->child[0] = a;
    rn->key[1] = k_sp_key(p, b, leaf);
    rn->max[1] = k_sp_max(p, b, leaf, INT64_MAX);
    rn->child[1] = b;
    K_SPHEAD(p)->root = r;
    K_SPHEAD(p)->height++;
}

/* Puts the entry for node `child` at position k of the inner node at level
   l of the path, splitting full nodes on the way up. */
static void k_sp_addchild(k_sppath *p, int64_t l, int64_t k, int64_t child, int leaf) {
    for (;;) {
        k_spinner *in = K_SPINNER(p, p->node[l]), *o = in;
        int64_t key = k_sp_key(p, child, leaf), max = k_sp_max(p, child, leaf, INT64_MAX);
        int64_t s = k, nn = 0;
        if (in->n == K_SPAN_FAN) {
            int64_t half = K_SPAN_FAN / 2;
            k_spinner *r;
            nn = k_sp_newnode(p);
            r = K_SPINNER(p, nn);
            r->n = in->n - half;
            memcpy(r->key, in->key + half, (size_t)r->n * 8);
            memcpy(r->max, in->max + half, (size_t)r->n * 8);
            memcpy(r->child, in->child + half, (size_t)r->n * 8);
            in->n = half;
            if (k > half) {
                o = r;
                s = k - half;
            }
        }
        k_sp_openin(o, s);
        o->key[s] = key;
        o->max[s] = max;
        o->child[s] = child;
        if (!nn) {
            k_sp_fix(p, l, 0, max);
            return;
        }
        if (l == 0) {
            k_sp_newroot(p, p->node[0], nn, 0);
            return;
        }
        /* The node keeps its place above, and its new right half goes
           after it. */
        k_sp_fix(p, l, INT64_MAX, 0);
        child = nn;
        leaf = 0;
        k = p->pos[l - 1] + 1;
        l--;
    }
}

/* Puts span (idx, cnt) at position k of the path's leaf. */
static void k_sp_insert(k_sppath *p, int64_t k, int64_t idx, int64_t cnt) {
    k_spleaf *lf = K_SPLEAF(p, p->node[p->h]), *o = lf;
    int64_t s = k, nn = 0;
    if (lf->n == K_SPAN_LEAF) {
        int64_t half = K_SPAN_LEAF / 2;
        k_spleaf *r;
        nn = k_sp_newnode(p);
        r = K_SPLEAF(p, nn);
        r->n = lf->n - half;
        memcpy(r->idx, lf->idx + half, (size_t)r->n * 8);
        memcpy(r->cnt, lf->cnt + half, (size_t)r->n * 8);
        lf->n = half;
        if (k > half) {
            o = r;
            s = k - half;
        }
    }
    k_sp_open(o, s);
    o->idx[s] = idx;
    o->cnt[s] = cnt;
    if (!nn) {
        k_sp_fix(p, p->h, 0, cnt);
    } else if (p->h == 0) {
        k_sp_newroot(p, p->node[0], nn, 1);
    } else {
        k_sp_fix(p, p->h, INT64_MAX, 0);
        k_sp_addchild(p, p->h - 1, p->pos[p->h - 1] + 1, nn, 1);
    }
}

/* Removes the span at position k of the path's leaf. An emptied node
   leaves its parent, and a root left with one child hands the root down. */
static void k_sp_delete(k_sppath *p, int64_t k) {
    k_spleaf *lf = K_SPLEAF(p, p->node[p->h]);
    int64_t c = lf->cnt[k], l;
    k_sp_close(lf, k);
    if (lf->n > 0 || p->h == 0) {
        k_sp_fix(p, p->h, c, 0);
        return;
    }
    k_sp_dropnode(p, p->node[p->h]);
    for (l = p->h - 1;; l--) {
        k_spinner *in = K_SPINNER(p, p->node[l]);
        int64_t e = p->pos[l], m = in->max[e];
        k_sp_closein(in, e);
        if (in->n > 0) {
            k_sp_fix(p, l, m, 0);
            break;
        }
        /* Never the root: a root of one child is handed down below, so
           every root has two or more. */
        k_sp_dropnode(p, p->node[l]);
    }
    while (K_SPHEAD(p)->height > 0 && K_SPINNER(p, K_SPHEAD(p)->root)->n == 1) {
        int64_t r = K_SPHEAD(p)->root;
        K_SPHEAD(p)->root = K_SPINNER(p, r)->child[0];
        K_SPHEAD(p)->height--;
        k_sp_dropnode(p, r);
    }
}

/* Takes cnt elements off the front of the span at position k of the path's
   leaf, or the whole span where it holds no more. */
static void k_sp_take(k_sppath *p, int64_t k, int64_t cnt) {
    k_spleaf *lf = K_SPLEAF(p, p->node[p->h]);
    int64_t c = lf->cnt[k];
    if (c <= cnt) {
        k_sp_delete(p, k);
        return;
    }
    lf->idx[k] += cnt;
    lf->cnt[k] = c - cnt;
    k_sp_fix(p, p->h, c, c - cnt);
}

/* The front of the first span holding cnt, in index order, else the end of
   the array, where a free span that reaches the end starts the run. The
   caller grows the array to the returned index plus cnt. First fit rather
   than best: in index order it packs runs toward the start and leaves the
   end free. */
K_API int64_t k_spans_alloc(uint8_t *base, int64_t *n, uint8_t **top, int64_t len,
                              int64_t cnt) {
    k_sppath p;
    int64_t l, i, k = 0, j, m, c, now, idx, before[K_SPAN_DEPTH];
    k_spleaf *lf = NULL;
    if (cnt == 0 || *n == 0) return len;
    p.base = base;
    p.n = n;
    p.top = top;
    p.h = K_SPHEAD(&p)->height;
    i = K_SPHEAD(&p)->root;
    /* Down the first entry at each level whose largest count holds cnt,
       noting the largest count of the ones before it, all smaller than
       cnt, for bringing the entries up to date once the span is taken
       from. Where the root holds none, down to the last span. */
    for (l = 0; l < p.h; l++) {
        k_spinner *in = K_SPINNER(&p, i);
        for (k = 0, m = 0; k < in->n && in->max[k] < cnt; k++)
            if (in->max[k] > m) m = in->max[k];
        if (k == in->n) break;
        p.node[l] = i;
        p.pos[l] = k;
        before[l] = m;
        i = in->child[k];
    }
    if (l == p.h) {
        lf = K_SPLEAF(&p, i);
        for (k = 0, m = 0; k < lf->n && lf->cnt[k] < cnt; k++)
            if (lf->cnt[k] > m) m = lf->cnt[k];
        before[l] = m;
    }
    if (l < p.h || k == lf->n) {
        for (l = 0, i = K_SPHEAD(&p)->root; l < p.h; l++) {
            k_spinner *in = K_SPINNER(&p, i);
            p.node[l] = i;
            p.pos[l] = in->n - 1;
            i = in->child[in->n - 1];
        }
        p.node[p.h] = i;
        lf = K_SPLEAF(&p, i);
        k = lf->n - 1;
        if (k < 0 || lf->idx[k] + lf->cnt[k] != len) return len;
        idx = lf->idx[k];
        k_sp_take(&p, k, cnt);
        return idx;
    }
    p.node[p.h] = i;
    p.pos[p.h] = k;
    idx = lf->idx[k];
    c = lf->cnt[k];
    if (c > cnt) {
        lf->idx[k] = idx + cnt;
        lf->cnt[k] = now = c - cnt;
        j = k + 1;
    } else if (lf->n > 1 || p.h == 0) {
        k_sp_close(lf, k);
        now = 0;
        j = k;
    } else {
        k_sp_delete(&p, k);
        return idx;
    }
    /* As k_sp_fix, with what the descent noted: the count on the path at
       level l went from c to `now`, and the node's other entries are the
       ones before, smaller than cnt, and those from j on, which need
       scanning only where c was the node's largest. */
    for (l = p.h; l > 0; l--) {
        k_spinner *in = K_SPINNER(&p, p.node[l - 1]);
        int64_t e = p.pos[l - 1], mx = in->max[e], nm = mx;
        int64_t key = k_sp_key(&p, p.node[l], l == p.h);
        if (c == mx) {
            const int64_t *v = l == p.h ? lf->cnt : K_SPINNER(&p, p.node[l])->max;
            int64_t w = l == p.h ? lf->n : K_SPINNER(&p, p.node[l])->n;
            nm = before[l] > now ? before[l] : now;
            for (; j < w && nm < c; j++)
                if (v[j] > nm) nm = v[j];
        }
        if (in->key[e] == key && nm == mx) break;
        in->key[e] = key;
        in->max[e] = nm;
        c = mx;
        now = nm;
        j = e + 1;
    }
    return idx;
}

/* The spans around a run [idx, end): the path to the leaf holding the last
   span that starts at or before idx (its position k there, or -1), and the
   span after that one where it follows in the same leaf, or where it starts
   the next leaf and at end (nl and nk, reached by the path np). A next
   leaf's first index is the key of the entry that leads to it, so a path
   to it is only made where its first span touches the run. */
typedef struct {
    k_spleaf *lf, *nl;
    int64_t k, nk;
    k_sppath *np;
} k_spnear;

static void k_sp_near(k_sppath *p, k_sppath *q, int64_t idx, int64_t end, k_spnear *s) {
    int64_t l;
    s->k = k_sp_find(p, idx);
    s->lf = K_SPLEAF(p, p->node[p->h]);
    s->nl = NULL;
    s->nk = -1;
    s->np = p;
    if (s->k + 1 < s->lf->n) {
        s->nl = s->lf;
        s->nk = s->k + 1;
        return;
    }
    for (l = p->h - 1; l >= 0 && p->pos[l] + 1 >= K_SPINNER(p, p->node[l])->n; l--) {}
    if (l < 0 || K_SPINNER(p, p->node[l])->key[p->pos[l] + 1] != end) return;
    q->base = p->base;
    q->n = p->n;
    q->top = p->top;
    q->h = p->h;
    memcpy(q->node, p->node, (size_t)(l + 1) * 8);
    memcpy(q->pos, p->pos, (size_t)(l + 1) * 8);
    for (q->pos[l]++; l < q->h; l++) {
        q->node[l + 1] = K_SPINNER(q, q->node[l])->child[q->pos[l]];
        q->pos[l + 1] = 0;
    }
    s->np = q;
    s->nl = K_SPLEAF(q, q->node[q->h]);
    s->nk = 0;
}

/* [idx, idx + cnt) back on the freelist beside the spans k_sp_near found
   around it, merged with the ones it touches. */
static void k_sp_release(k_sppath *p, k_spnear *s, int64_t idx, int64_t cnt) {
    k_spleaf *lf = s->lf, *nl = s->nl;
    int64_t k = s->k, nk = s->nk;
    int prev = k >= 0 && lf->idx[k] + lf->cnt[k] == idx;
    int next = nl && idx + cnt == nl->idx[nk];
    if (prev) {
        int64_t c = lf->cnt[k], m = c + cnt + (next ? nl->cnt[nk] : 0);
        lf->cnt[k] = m;
        /* The merged count is larger than both it replaces, so one fix
           covers the span after leaving the leaf as well. */
        if (next && nl == lf) k_sp_close(lf, nk);
        k_sp_fix(p, p->h, c, m);
        if (next && nl != lf) k_sp_delete(s->np, nk);
    } else if (next) {
        int64_t c = nl->cnt[nk];
        nl->idx[nk] = idx;
        nl->cnt[nk] = c + cnt;
        k_sp_fix(s->np, s->np->h, c, c + cnt);
    } else {
        k_sp_insert(p, k + 1, idx, cnt);
    }
}

/* Merged with the spans it touches. */
K_API void k_spans_free(uint8_t *base, int64_t *n, uint8_t **top, int64_t idx, int64_t cnt) {
    k_sppath p, q;
    k_spnear s;
    if (cnt <= 0) return;
    p.base = base;
    p.n = n;
    p.top = top;
    if (*n == 0) {
        k_sphead *hd = (k_sphead *)base;
        *n = 1;
        *top = base + K_SPAN_NODE;
        hd->chain = 0;
        hd->height = 0;
        hd->root = k_sp_newnode(&p);
        K_SPLEAF(&p, hd->root)->n = 0;
    }
    k_sp_near(&p, &q, idx, idx + cnt, &s);
    k_sp_release(&p, &s, idx, cnt);
}

/* In place where a free span starts where the slice ends and holds the
   difference or reaches the end of the array; otherwise the slice's
   elements go back on the freelist and the run goes where alloc_slice
   places one of cnt elements. The span after the slice is the one after
   the last span before it, so one descent finds both what growth takes
   from and what the elements merge with. */
K_API int64_t k_spans_regrow(uint8_t *base, int64_t *n, uint8_t **top, int64_t len,
                               int64_t idx, int64_t ol, int64_t cnt) {
    k_sppath p, q;
    k_spnear s;
    int64_t end = idx + ol;
    if (*n == 0) {
        k_spans_free(base, n, top, idx, ol);
        return k_spans_alloc(base, n, top, len, cnt);
    }
    p.base = base;
    p.n = n;
    p.top = top;
    k_sp_near(&p, &q, idx, end, &s);
    if (s.nl && s.nl->idx[s.nk] == end &&
        (s.nl->cnt[s.nk] >= cnt - ol || end + s.nl->cnt[s.nk] == len)) {
        k_sp_take(s.np, s.nk, cnt - ol);
        return idx;
    }
    k_sp_release(&p, &s, idx, ol);
    return k_spans_alloc(base, n, top, len, cnt);
}

/* ---------------------------------------------------------------------------
   Byte search: std's find_any and find_pair (docs/stdlib.md). A set arrives
   as std's ByteSet, made by its byte_set, which k_byteset lays out as the
   K struct is (spec C.2: packed, in declaration order). `members` is
   the set. `kind` says how a block of bytes is tested for it: by comparing
   with 1 to 3 of `bytes` (kinds 1-3), or as the range bytes[0] .. bytes[0] +
   bytes[1] (kind 4), both complemented where `invert` is set; kind 0 is the
   empty set, or with `invert` every byte. Every set also has nibble tables,
   bit k of lo[j] standing for the byte k * 16 + j and bit k of hi[j] for the
   byte 128 + k * 16 + j, which are all that kind 5 has.

   Every x86-64 CPU has SSE2, so the comparisons test blocks of 16 bytes
   without any dispatch. The tables take SSSE3's pshufb, which the CPU is
   asked for as the runtime starts, and a search where either set is of kind
   5 tests both by their tables. Every AArch64 CPU has NEON, whose TBL does
   the same lookups, so there both tests run without dispatch. NEON has no
   movemask: a block's result is narrowed to 4 bits a byte (shrn), which
   gives one 64-bit word with a nibble for each byte, in order. Without
   SSSE3 a search by the tables, and on other targets (and big-endian
   AArch64, and MSVC for ARM64), under TinyCC (which has no intrinsics), or
   over fewer than 16 positions, every search, tests one byte at a time, a
   single byte by memchr. No load reaches past the range: its last part is
   tested as the range's last whole block, with the positions before it,
   already tested, shifted out of the result.

   The set is a ByteSet the program wrote, read here as a k_byteset: may_alias
   keeps gcc's type-based alias analysis from taking those for two objects,
   which at -O2 had it drop the program's stores to a set it then searched. */

#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
#define K_MAY_ALIAS __attribute__((may_alias))
#else
#define K_MAY_ALIAS
#endif
#pragma pack(push, 1)
typedef struct {
    uint8_t members[256];
    uint8_t kind, invert, bytes[3], lo[16], hi[16];
} K_MAY_ALIAS k_byteset;
#pragma pack(pop)

static int64_t k_scan_any_bytes(const uint8_t *p, int64_t n, const k_byteset *s) {
    if (s->kind == 1 && !s->invert) {
        const uint8_t *q = (const uint8_t *)memchr(p, s->bytes[0], (size_t)n);
        return q ? (int64_t)(q - p) : -1;
    }
    for (int64_t i = 0; i < n; i++)
        if (s->members[p[i]]) return i;
    return -1;
}

/* The first of the np positions i with p[i] in a and p[i + d] in b. */
static int64_t k_scan_pair_bytes(const uint8_t *p, int64_t np, const k_byteset *a, int64_t d,
                                  const k_byteset *b) {
    for (int64_t i = 0; i < np; i++)
        if (a->members[p[i]] & b->members[p[i + d]]) return i;
    return -1;
}

#if !defined(__TINYC__) && (defined(__x86_64__) || defined(_M_X64))
#define K_SCAN_SSE2 1
#include <emmintrin.h>
#include <tmmintrin.h>
#ifdef _WIN32
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#if defined(__GNUC__) || defined(__clang__)
/* clang and gcc compile pshufb only into a function that asks for it. */
#define K_SSSE3 __attribute__((target("ssse3")))
#define k_ctz32(x) __builtin_ctz(x)
#else
#define K_SSSE3
static int k_ctz32(unsigned x) {
    unsigned long i;
    _BitScanForward(&i, x);
    return (int)i;
}
#endif

static int k_cpu_ssse3;

static void k_scan_init(void) {
#ifdef _WIN32
    int r[4];
    __cpuid(r, 1);
    k_cpu_ssse3 = (r[2] >> 9) & 1;
#else
    unsigned a, b, c, d;
    k_cpu_ssse3 = __get_cpuid(1, &a, &b, &c, &d) && ((c >> 9) & 1);
#endif
}

/* A set's block test, held in registers: the bytes or range it compares
   with and all ones where the result is complemented, or its tables. */
typedef struct { __m128i a, b, c, flip; } k_bsm;

static K_INLINE void k_bsm_init(k_bsm *m, const k_byteset *s, int tables) {
    if (tables) {
        m->a = _mm_loadu_si128((const __m128i *)s->lo);
        m->b = _mm_loadu_si128((const __m128i *)s->hi);
        m->c = _mm_setr_epi8(1, 2, 4, 8, 16, 32, 64, -128, 1, 2, 4, 8, 16, 32, 64, -128);
        m->flip = _mm_setzero_si128();
    } else {
        m->a = _mm_set1_epi8((char)s->bytes[0]);
        m->b = _mm_set1_epi8((char)s->bytes[1]);
        m->c = _mm_set1_epi8((char)s->bytes[2]);
        m->flip = s->invert ? _mm_set1_epi8(-1) : _mm_setzero_si128();
    }
}

/* The members among the 16 bytes of x, as 0xff bytes, for a set of kind k
   from 1 to 4, a constant wherever this is inlined. A range holds x where
   x - lo, wrapping, is at most its width. */
static K_INLINE __m128i k_bsm_test(const k_bsm *m, int k, __m128i x) {
    __m128i r, t;
    if (k == 1) {
        r = _mm_cmpeq_epi8(x, m->a);
    } else if (k == 2) {
        r = _mm_or_si128(_mm_cmpeq_epi8(x, m->a), _mm_cmpeq_epi8(x, m->b));
    } else if (k == 3) {
        r = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(x, m->a), _mm_cmpeq_epi8(x, m->b)),
                         _mm_cmpeq_epi8(x, m->c));
    } else {
        t = _mm_sub_epi8(x, m->a);
        r = _mm_cmpeq_epi8(_mm_min_epu8(t, m->b), t);
    }
    return _mm_xor_si128(r, m->flip);
}

/* The same by the tables: pshufb looks up lo[x & 15] where x < 128 and
   hi[x & 15] where not (it gives 0 for an index with its top bit set), and
   the bit for x's bits 4 to 6, which the entry has or not. */
static K_SSSE3 K_INLINE __m128i k_bsm_tables(const k_bsm *m, __m128i x) {
    __m128i lo = _mm_shuffle_epi8(m->a, x);
    __m128i hi = _mm_shuffle_epi8(m->b, _mm_xor_si128(x, _mm_set1_epi8((char)0x80)));
    __m128i bit = _mm_shuffle_epi8(m->c, _mm_and_si128(_mm_srli_epi16(x, 4), _mm_set1_epi8(7)));
    return _mm_cmpeq_epi8(_mm_and_si128(_mm_or_si128(lo, hi), bit), bit);
}

#define K_LOAD(q) _mm_loadu_si128((const __m128i *)(q))
#define K_AND(x, y) _mm_and_si128(x, y)

/* A block's members as a mask, bit i for byte i; K_BMASK_DROP removes the
   first k positions and K_BMASK_FIRST is the first that is left. */
typedef unsigned k_bmask;
#define K_BMASK(v) ((unsigned)_mm_movemask_epi8(v))
#define K_BMASK_DROP(m, k) ((m) >> (k))
#define K_BMASK_FIRST(m) k_ctz32(m)

#elif !defined(__TINYC__) && defined(__aarch64__) && !defined(__ARM_BIG_ENDIAN) && \
    (defined(__GNUC__) || defined(__clang__))
#define K_SCAN_NEON 1
#include <arm_neon.h>
#define K_SSSE3

static void k_scan_init(void) {}

typedef struct { uint8x16_t a, b, c, flip; } k_bsm;

static const uint8_t k_bsm_bits[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};

static K_INLINE void k_bsm_init(k_bsm *m, const k_byteset *s, int tables) {
    if (tables) {
        m->a = vld1q_u8(s->lo);
        m->b = vld1q_u8(s->hi);
        m->c = vld1q_u8(k_bsm_bits);
        m->flip = vdupq_n_u8(0);
    } else {
        m->a = vdupq_n_u8(s->bytes[0]);
        m->b = vdupq_n_u8(s->bytes[1]);
        m->c = vdupq_n_u8(s->bytes[2]);
        m->flip = vdupq_n_u8(s->invert ? 0xff : 0);
    }
}

/* As the SSE2 k_bsm_test: the members among the 16 bytes of x, as 0xff
   bytes, for a set of kind k from 1 to 4. */
static K_INLINE uint8x16_t k_bsm_test(const k_bsm *m, int k, uint8x16_t x) {
    uint8x16_t r;
    if (k == 1) {
        r = vceqq_u8(x, m->a);
    } else if (k == 2) {
        r = vorrq_u8(vceqq_u8(x, m->a), vceqq_u8(x, m->b));
    } else if (k == 3) {
        r = vorrq_u8(vorrq_u8(vceqq_u8(x, m->a), vceqq_u8(x, m->b)), vceqq_u8(x, m->c));
    } else {
        r = vcleq_u8(vsubq_u8(x, m->a), m->b);
    }
    return veorq_u8(r, m->flip);
}

/* The same by the tables. TBL gives 0 for an index of 16 or more, so
   masking x with 0x8f looks up lo[x & 15] where x < 128 and gives 0 where
   not, and the same of x ^ 0x80 looks up hi[x & 15] where x >= 128. */
static K_INLINE uint8x16_t k_bsm_tables(const k_bsm *m, uint8x16_t x) {
    uint8x16_t low4 = vdupq_n_u8(0x8f);
    uint8x16_t lo = vqtbl1q_u8(m->a, vandq_u8(x, low4));
    uint8x16_t hi = vqtbl1q_u8(m->b, vandq_u8(veorq_u8(x, vdupq_n_u8(0x80)), low4));
    uint8x16_t bit = vqtbl1q_u8(m->c, vshrq_n_u8(x, 4));
    return vceqq_u8(vandq_u8(vorrq_u8(lo, hi), bit), bit);
}

#define K_LOAD(q) vld1q_u8(q)
#define K_AND(x, y) vandq_u8(x, y)

/* A block's members as a mask, the nibble i (4 bits) for byte i: shifting
   each 16-bit lane right by 4 and keeping its low byte (shrn) takes the high
   nibble of the lane's first byte and the low nibble of its second. */
typedef uint64_t k_bmask;
static K_INLINE uint64_t k_bmask_neon(uint8x16_t v) {
    return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(v), 4)), 0);
}
#define K_BMASK(v) k_bmask_neon(v)
#define K_BMASK_DROP(m, k) ((m) >> (4 * (k)))
#define K_BMASK_FIRST(m) (__builtin_ctzll(m) >> 2)

#else
static void k_scan_init(void) {}
#endif

#if defined(K_SCAN_SSE2) || defined(K_SCAN_NEON)
/* The block loop of a search over n >= 16 positions for the bytes TEST
   finds in a block. */
#define K_ANY_LOOP(TEST)                                                              \
    int64_t i = 0;                                                                     \
    k_bmask k;                                                                        \
    for (; i + 16 <= n; i += 16) {                                                     \
        k = K_BMASK(TEST(K_LOAD(p + i)));                                            \
        if (k) return i + K_BMASK_FIRST(k);                                           \
    }                                                                                  \
    if (i == n) return -1;                                                             \
    k = K_BMASK_DROP(K_BMASK(TEST(K_LOAD(p + n - 16))), 16 - (n - i));              \
    return k ? i + K_BMASK_FIRST(k) : -1;

/* The same over np >= 16 positions for two sets, d bytes apart. */
#define K_PAIR_LOOP(TESTA, TESTB)                                                     \
    int64_t i = 0;                                                                     \
    k_bmask k;                                                                        \
    for (; i + 16 <= np; i += 16) {                                                    \
        k = K_BMASK(K_AND(TESTA(K_LOAD(p + i)), TESTB(K_LOAD(p + i + d))));        \
        if (k) return i + K_BMASK_FIRST(k);                                           \
    }                                                                                  \
    if (i == np) return -1;                                                            \
    k = K_BMASK_DROP(K_BMASK(K_AND(TESTA(K_LOAD(p + np - 16)),                     \
                                      TESTB(K_LOAD(p + np - 16 + d)))),               \
                      16 - (np - i));                                                  \
    return k ? i + K_BMASK_FIRST(k) : -1;

#define K_TEST_S(x) k_bsm_test(&ms, ks, x)
#define K_TEST_A(x) k_bsm_test(&ma, ka, x)
#define K_TEST_B(x) k_bsm_test(&mb, kb, x)
#define K_TABLES_S(x) k_bsm_tables(&ms, x)
#define K_TABLES_A(x) k_bsm_tables(&ma, x)
#define K_TABLES_B(x) k_bsm_tables(&mb, x)

static K_INLINE int64_t k_scan_any_k(const uint8_t *p, int64_t n, const k_byteset *s, int ks) {
    k_bsm ms;
    k_bsm_init(&ms, s, 0);
    K_ANY_LOOP(K_TEST_S)
}

static K_SSSE3 int64_t k_scan_any_tables(const uint8_t *p, int64_t n, const k_byteset *s) {
    k_bsm ms;
    k_bsm_init(&ms, s, 1);
    K_ANY_LOOP(K_TABLES_S)
}

static K_INLINE int64_t k_scan_pair_k(const uint8_t *p, int64_t np, const k_byteset *a,
                                        int64_t d, const k_byteset *b, int ka, int kb) {
    k_bsm ma, mb;
    k_bsm_init(&ma, a, 0);
    k_bsm_init(&mb, b, 0);
    K_PAIR_LOOP(K_TEST_A, K_TEST_B)
}

static K_SSSE3 int64_t k_scan_pair_tables(const uint8_t *p, int64_t np, const k_byteset *a,
                                            int64_t d, const k_byteset *b) {
    k_bsm ma, mb;
    k_bsm_init(&ma, a, 1);
    k_bsm_init(&mb, b, 1);
    K_PAIR_LOOP(K_TABLES_A, K_TABLES_B)
}

/* A loop of its own for each kind, and for each pair of kinds, from 1 to 4. */
static int64_t k_scan_any_blocks(const uint8_t *p, int64_t n, const k_byteset *s) {
    switch (s->kind) {
        case 1: return k_scan_any_k(p, n, s, 1);
        case 2: return k_scan_any_k(p, n, s, 2);
        case 3: return k_scan_any_k(p, n, s, 3);
        default: return k_scan_any_k(p, n, s, 4);
    }
}

#define K_PAIR_CASE(ka, kb) \
    case (ka) * 4 + (kb): return k_scan_pair_k(p, np, a, d, b, ka, kb);

static int64_t k_scan_pair_blocks(const uint8_t *p, int64_t np, const k_byteset *a, int64_t d,
                                   const k_byteset *b) {
    switch (a->kind * 4 + b->kind) {
        K_PAIR_CASE(1, 1) K_PAIR_CASE(1, 2) K_PAIR_CASE(1, 3) K_PAIR_CASE(1, 4)
        K_PAIR_CASE(2, 1) K_PAIR_CASE(2, 2) K_PAIR_CASE(2, 3) K_PAIR_CASE(2, 4)
        K_PAIR_CASE(3, 1) K_PAIR_CASE(3, 2) K_PAIR_CASE(3, 3) K_PAIR_CASE(3, 4)
        K_PAIR_CASE(4, 1) K_PAIR_CASE(4, 2) K_PAIR_CASE(4, 3)
        default: return k_scan_pair_k(p, np, a, d, b, 4, 4);
    }
}
#endif

K_API int64_t k_scan_any(const uint8_t *p, int64_t n, const void *set) {
    const k_byteset *s = (const k_byteset *)set;
    if (n <= 0) return -1;
    if (s->kind == 0) return s->invert ? 0 : -1;
#ifdef K_SCAN_SSE2
    if (n >= 16 && s->kind != 5) return k_scan_any_blocks(p, n, s);
    if (n >= 16 && k_cpu_ssse3) return k_scan_any_tables(p, n, s);
#elif defined(K_SCAN_NEON)
    if (n >= 16) return s->kind != 5 ? k_scan_any_blocks(p, n, s) : k_scan_any_tables(p, n, s);
#endif
    return k_scan_any_bytes(p, n, s);
}

K_API int64_t k_scan_pair(const uint8_t *p, int64_t n, const void *a, int64_t d,
                            const void *b) {
    const k_byteset *sa = (const k_byteset *)a, *sb = (const k_byteset *)b;
    if (d < 0 || d >= n) return -1;
    int64_t np = n - d;     /* the positions both bytes fit at */
    /* A set of every byte leaves a search for the other. */
    if (sa->kind == 0) return sa->invert ? k_scan_any(p + d, np, sb) : -1;
    if (sb->kind == 0) return sb->invert ? k_scan_any(p, np, sa) : -1;
#ifdef K_SCAN_SSE2
    if (np >= 16 && sa->kind != 5 && sb->kind != 5) return k_scan_pair_blocks(p, np, sa, d, sb);
    if (np >= 16 && k_cpu_ssse3) return k_scan_pair_tables(p, np, sa, d, sb);
#elif defined(K_SCAN_NEON)
    if (np >= 16) {
        if (sa->kind != 5 && sb->kind != 5) return k_scan_pair_blocks(p, np, sa, d, sb);
        return k_scan_pair_tables(p, np, sa, d, sb);
    }
#endif
    return k_scan_pair_bytes(p, np, sa, d, sb);
}

K_API void k_rt_start(int argc, char **argv, uint64_t reserve, uint64_t gap,
                        uint64_t budget, int64_t mainregions, int64_t workerregions) {
    /* A host's k_init of a second program (an image it reloaded, say)
       over the regions of the first, which share this runtime: starting
       would forget them rather than release them. */
    if (k_regions)
        k_panic("a program is already started in this process: call its k_exit first");
    k_argc = argc;
    k_argv = argv;
    /* A region's usable part is whole pages: the guard gap behind it has to
       start on a page to be protected, and on Windows a stack would run on
       unseen through the rest of a part page committed at its end, past the
       K_STACK_RESERVE bytes the compiler's checks go by. */
    k_region_page = k_os_page_size();
    reserve -= reserve % k_region_page;
    if (!reserve) k_panic("the data stack reservation (K_STACK_RESERVE) is less than a page");
    k_region_usable = k_region_reserve = (size_t)reserve;
    k_region_gap = (size_t)gap;
    k_stack_budget = budget;
    k_mainregions = mainregions;
    k_workerregions = workerregions;
    /* The workers the budget holds beside the main program, each at the
       most regions one can take: what hardware_threads() reports at most.
       At least one, so a program sized by it still runs; the regions the
       platform then refuses are retried smaller (k_reserve_region). */
    if (workerregions > 0) {
        uint64_t per = reserve + gap;
        uint64_t total = per ? budget / per : 0;
        int64_t spare = total > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)total;
        spare -= mainregions;
        k_thread_cap = spare > workerregions ? spare / workerregions : 1;
    }
    // Unbuffered stdout: output is never lost to an abort or a killed run,
    // and interleaves correctly with stderr diagnostics. Revisit if print
    // throughput ever matters.
    setvbuf(stdout, NULL, _IONBF, 0);
    #ifdef _WIN32
        // The bytes a program writes are the bytes that come out, '\n' one
        // byte as on every other platform, rather than a "\r\n" the CRT's
        // text mode makes of it byte by byte; and the bytes it reads are
        // the bytes that came in, where text mode would make "\r\n" '\n'
        // and stop at the first Ctrl-Z.
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stderr), _O_BINARY);
    #endif
    k_regions_init();
    k_regions_begin(mainregions);
    k_native_stack_init();
    k_scan_init();
}

/* The inverse, on main's thread: its regions and registry, then the fault
   handling and the arguments. k_rt_stop (runtime_threads.h) adds the
   thread programs' part; the JIT host calls this directly, since a program
   it runs may return from main with workers still running (jit_runtime.c). */
static void k_rt_release(void) {
    k_release_regions();
    k_fault_uninstall();
    k_argc = 0;
    k_argv = NULL;
}

/* ---------------------------------------------------------------------------
   Text forms (§3.7). */

/* Integers are written by a digit loop rather than printf, whose locale
   handling and format parsing cost several times the conversion itself:
   the digit count first, then the digits from the right, two per division
   by 100. */
static const char k_digit_pairs[201] =
    "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
    "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

static int k_fmt_digits(uint64_t v) {
    int n = 1;
    for (;;) {
        if (v < 10) return n;
        if (v < 100) return n + 1;
        if (v < 1000) return n + 2;
        if (v < 10000) return n + 3;
        v /= 10000;
        n += 4;
    }
}

K_API int64_t k_fmt_u64(uint8_t *dst, uint64_t v) {
    int n = k_fmt_digits(v);
    uint8_t *p = dst + n;
    while (v >= 100) {
        unsigned r = (unsigned)(v % 100);
        v /= 100;
        p -= 2;
        p[0] = (uint8_t)k_digit_pairs[2 * r];
        p[1] = (uint8_t)k_digit_pairs[2 * r + 1];
    }
    if (v >= 10) {
        p[-2] = (uint8_t)k_digit_pairs[2 * v];
        p[-1] = (uint8_t)k_digit_pairs[2 * v + 1];
    } else {
        p[-1] = (uint8_t)('0' + v);
    }
    return n;
}

K_API int64_t k_fmt_i64(uint8_t *dst, int64_t v) {
    if (v >= 0) return k_fmt_u64(dst, (uint64_t)v);
    /* The magnitude in unsigned arithmetic, so i64.min needs no case. */
    dst[0] = '-';
    return 1 + k_fmt_u64(dst + 1, 0 - (uint64_t)v);
}

/* A float's text is the fewest significant digits that read back as the
   same value of its own type and, of those, the ones nearest it, a tie going
   to the even digit (as Python's repr and Ryu choose). They are found in
   integers, by Burger and Dybvig's free-format algorithm, not through the C
   library: msvcrt, which tcc uses on Windows, rounds a printf tie away from
   zero, and below a power of two the gap to the next float is half the gap
   above, so the shortest text can lie above the value while the correctly
   rounded spelling of that length, below it, does not read back.

   The numbers are naturals in 32-bit limbs, least significant first. The
   denominator s stays below 2^1079 (ten times 2^1075, for the smallest
   doubles), and the others below eleven times s. */
typedef struct { int n; uint32_t d[36]; } k_big;

/* a = v << sh, for v > 0. */
static void k_big_set(k_big *a, uint64_t v, int sh) {
    int w = sh >> 5, b = sh & 31, i;
    uint64_t lo = v << b, hi = b ? v >> (64 - b) : 0;
    for (i = 0; i < w; i++) a->d[i] = 0;
    a->d[w] = (uint32_t)lo;
    a->d[w + 1] = (uint32_t)(lo >> 32);
    a->d[w + 2] = (uint32_t)hi;
    a->n = w + 3;
    while (!a->d[a->n - 1]) a->n--;
}

static void k_big_mul(k_big *a, uint32_t m) {
    uint64_t c = 0;
    for (int i = 0; i < a->n; i++) {
        c += (uint64_t)a->d[i] * m;
        a->d[i] = (uint32_t)c;
        c >>= 32;
    }
    if (c) a->d[a->n++] = (uint32_t)c;
}

static void k_big_mul_pow10(k_big *a, int k) {
    for (; k >= 9; k -= 9) k_big_mul(a, 1000000000u);
    for (; k > 0; k--) k_big_mul(a, 10);
}

static int k_big_cmp(const k_big *a, const k_big *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

/* t = a + b */
static void k_big_add(k_big *t, const k_big *a, const k_big *b) {
    int n = a->n > b->n ? a->n : b->n;
    uint64_t c = 0;
    for (int i = 0; i < n; i++) {
        c += (uint64_t)(i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);
        t->d[i] = (uint32_t)c;
        c >>= 32;
    }
    t->n = n;
    if (c) t->d[t->n++] = (uint32_t)c;
}

/* a -= b, for a >= b. */
static void k_big_sub(k_big *a, const k_big *b) {
    uint64_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t x = (uint64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
        a->d[i] = (uint32_t)x;
        borrow = x >> 63;
    }
    while (a->n && !a->d[a->n - 1]) a->n--;
}

/* The shortest digits of f * 2^e (f > 0) as a float of `bits` significant
   bits and least exponent emin, into dig; returns their count and sets *k
   so that the value they spell is 0.d1d2... * 10^*k. */
static int k_float_digits(uint64_t f, int e, int bits, int emin, char *dig, int *k) {
    k_big r, s, mp, mlo, t;
    /* Reading rounds a tie to the even significand, so the interval that
       reads back as f * 2^e includes its ends when f is even. */
    int even = !(f & 1);
    /* Below a power of two, the gap to the next float down is half the gap up. */
    int asym = f == (uint64_t)1 << (bits - 1) && e > emin;
    int up = e > 0 ? e : 0, lg = e - 1, n = 0, c;
    /* The value is r / s, and what reads back as it reaches from (r - *mm) / s
       to (r + mp) / s: half the gap to each neighbour. */
    k_big *mm = asym ? &mlo : &mp;
    k_big_set(&r, f, up + 1 + asym);
    k_big_set(&s, 1, up - e + 1 + asym);
    k_big_set(&mp, 1, up + asym);
    k_big_set(&mlo, 1, up);
    /* Scale to 10^*k, the least power of ten above the interval: with
       lg = floor(log2 value), ceil(lg log10 2) is it or one short, and
       78913 / 2^18 is log10 2 closely enough to compute that for |lg| < 1650. */
    for (uint64_t g = f; g; g >>= 1) lg++;
    *k = lg > 0 ? ((lg * 78913) >> 18) + 1 : -((-lg * 78913) >> 18);
    if (*k >= 0) k_big_mul_pow10(&s, *k);
    else {
        k_big_mul_pow10(&r, -*k);
        k_big_mul_pow10(&mp, -*k);
        if (asym) k_big_mul_pow10(&mlo, -*k);
    }
    k_big_add(&t, &r, &mp);
    c = k_big_cmp(&t, &s);
    if (even ? c >= 0 : c > 0) {
        k_big_mul(&s, 10);
        ++*k;
    }
    /* Each digit d leaves the remainder r: stop where the digits so far
       followed by d (r within *mm) or by d + 1 (r + mp past s) read back. */
    for (;;) {
        int d = 0, low, high;
        k_big_mul(&r, 10);
        k_big_mul(&mp, 10);
        if (asym) k_big_mul(&mlo, 10);
        while (k_big_cmp(&r, &s) >= 0) {
            k_big_sub(&r, &s);
            d++;
        }
        c = k_big_cmp(&r, mm);
        low = even ? c <= 0 : c < 0;
        k_big_add(&t, &r, &mp);
        c = k_big_cmp(&t, &s);
        high = even ? c >= 0 : c > 0;
        if (!low && !high) {
            dig[n++] = (char)('0' + d);
            continue;
        }
        if (low && high) {
            /* Both read back: the nearer, d + 1 when r is past half of s,
               and of two as near, the even one. */
            k_big_add(&t, &r, &r);
            c = k_big_cmp(&t, &s);
            high = c > 0 || (c == 0 && (d & 1));
        }
        dig[n++] = (char)('0' + d + high);
        return n;
    }
}

/* The text of a finite float from its fields: the sign, the significand
   without its implicit bit and the biased exponent, for a type of `bits`
   significant bits and least exponent emin. The digits are laid out as C's
   %g lays them out at a precision of max(15, digits): in exponent form, with
   at least two exponent digits, below 1e-4 or from that power of ten up. */
static int64_t k_fmt_float(uint8_t *dst, int neg, uint64_t frac, int bexp, int bits,
                            int emin) {
    char dig[20], *p = (char *)dst;
    int k, n, x, i;
    if (neg) *p++ = '-';
    if (!frac && !bexp) {
        memcpy(p, "0.0", 3);
        return p + 3 - (char *)dst;
    }
    n = k_float_digits(bexp ? frac | (uint64_t)1 << (bits - 1) : frac,
                        (bexp ? bexp - 1 : 0) + emin, bits, emin, dig, &k);
    x = k - 1;
    if (x < -4 || x >= (n > 15 ? n : 15)) {
        *p++ = dig[0];
        if (n > 1) *p++ = '.';
        for (i = 1; i < n; i++) *p++ = dig[i];
        *p++ = 'e';
        *p++ = x < 0 ? '-' : '+';
        if (x < 0) x = -x;
        if (x >= 100) *p++ = (char)('0' + x / 100);
        *p++ = (char)('0' + x / 10 % 10);
        *p++ = (char)('0' + x % 10);
    } else if (x < 0) {
        *p++ = '0';
        *p++ = '.';
        for (i = -1; i > x; i--) *p++ = '0';
        for (i = 0; i < n; i++) *p++ = dig[i];
    } else {
        for (i = 0; i < n || i <= x; i++) {
            if (i == x + 1) *p++ = '.';
            *p++ = i < n ? dig[i] : '0';
        }
        /* A whole number still reads as a float: 1.0, not 1. */
        if (n <= x + 1) {
            *p++ = '.';
            *p++ = '0';
        }
    }
    return p - (char *)dst;
}

K_API int64_t k_fmt_f64(uint8_t *dst, double v) {
    uint64_t b;
    /* C libraries disagree here (msvcrt, which tcc uses on Windows, writes
       1.#INF and -1.#IND; others give a NaN's sign bit, which depends on
       the CPU that made it), so these are spelled by the runtime. */
    if (v != v) { memcpy(dst, "nan", 3); return 3; }
    if (isinf(v)) {
        if (v > 0) { memcpy(dst, "inf", 3); return 3; }
        memcpy(dst, "-inf", 4);
        return 4;
    }
    memcpy(&b, &v, sizeof b);
    return k_fmt_float(dst, (int)(b >> 63), b & (((uint64_t)1 << 52) - 1),
                        (int)(b >> 52) & 0x7FF, 53, -1074);
}

/* The digits are the f32's own, laid out as an f64's are, so 0.1 as an f32
   prints as 0.1, not as the digits of the f64 it widens to. */
K_API int64_t k_fmt_f32(uint8_t *dst, float v) {
    double d = v;
    uint32_t b;
    if (d != d || isinf(d)) return k_fmt_f64(dst, d);
    memcpy(&b, &v, sizeof b);
    return k_fmt_float(dst, (int)(b >> 31), b & 0x7FFFFF, (int)(b >> 23) & 0xFF, 24, -149);
}

K_API int64_t k_fmt_bool(uint8_t *dst, int64_t v) {
    memcpy(dst, v ? "true" : "false", v ? 4 : 5);
    return v ? 4 : 5;
}

K_API int64_t k_fmt_quoted(uint8_t *dst, const uint8_t *s, int64_t n) {
    uint8_t *d = dst;
    *d++ = '"';
    for (int64_t i = 0; i < n; i++) {
        uint8_t c = s[i];
        switch (c) {
            case '"': *d++ = '\\'; *d++ = '"'; break;
            case '\\': *d++ = '\\'; *d++ = '\\'; break;
            case '\n': *d++ = '\\'; *d++ = 'n'; break;
            case '\r': *d++ = '\\'; *d++ = 'r'; break;
            case '\t': *d++ = '\\'; *d++ = 't'; break;
            default: *d++ = c; break;
        }
    }
    *d++ = '"';
    return (int64_t)(d - dst);
}

/* stdout is unbuffered (k_rt_start): each print is this one write. */
K_API void k_out_bytes(const uint8_t *p, int64_t len) { fwrite(p, 1, (size_t)len, stdout); }
