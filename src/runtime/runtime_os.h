/* K runtime: the OS primitives behind stdlib/os.k (spec §7.10),
   and std's slice_pool's span tree, as runtime_ext.h declares them, which
   this follows. */

#include <time.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#if defined(__APPLE__) || defined(__linux__)
#include <sys/random.h>
#else
#include <unistd.h>
#endif
#endif

/* Paths are UTF-8 on every platform, and reach the C APIs NUL-terminated in
   a buffer of K_OS_PATH_MAX characters. A path that would not fit, that
   holds a NUL byte (where C would end it) or, on Windows, that is not UTF-8
   is refused: cut short or read another way, it could name another file. */
#define K_OS_PATH_MAX 4096

#ifdef _WIN32
/* Windows names files in UTF-16, which its wide APIs take as it is; the
   narrow ones read their bytes in the ANSI code page. */
typedef wchar_t k_os_char;
#define k_os_fopen(p, mode) _wfopen(p, L##mode)
/* Vista's; TinyCC's winnls.h predates it. */
#ifndef WC_ERR_INVALID_CHARS
#define WC_ERR_INVALID_CHARS 0x80
#endif
#else
typedef char k_os_char;
#define k_os_fopen(p, mode) fopen(p, mode)
#endif

/* The native form of a path, or of an environment variable's name, in buf
   (K_OS_PATH_MAX characters): its length, or -1 where it is refused. */
static int k_os_native(sl_u8 s, k_os_char *buf) {
    if (s.len >= K_OS_PATH_MAX || (s.len > 0 && memchr(s.data, 0, (size_t)s.len)))
        return -1;
    int n = (int)s.len;
#ifdef _WIN32
    /* A UTF-16 path has no more units than its UTF-8 form has bytes. */
    if (n > 0) {
        n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)s.data, n, buf,
                                K_OS_PATH_MAX - 1);
        if (n == 0) return -1;
    }
#else
    k_memcpy(buf, s.data, (size_t)n);
#endif
    buf[n] = 0;
    return n;
}

#ifdef _WIN32
/* Appends n UTF-16 units as UTF-8; where they are not valid UTF-16, appends
   nothing and returns 0. */
static int k_os_append_wide(k_rref out, const wchar_t *w, int n) {
    if (n <= 0) return 1;
    int k = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, n, NULL, 0, NULL, NULL);
    char *s = k > 0 ? (char *)malloc((size_t)k) : NULL;
    int ok = s && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, n, s, k, NULL, NULL) == k;
    if (ok) k_bld_append(out, s, k);
    free(s);
    return ok;
}

/* Moves `from` to `to`, replacing `to` if there is one, in one step.
   Another process, such as a virus scanner reading a file just written, can
   hold either open for a moment: while `to` is open anywhere it cannot be
   replaced (ERROR_ACCESS_DENIED), nor `from` moved while it is open without
   delete sharing (ERROR_SHARING_VIOLATION). Those failures are tried again
   for about a second, unless `to` is a directory or read-only, which no
   wait changes. */
static int k_os_replace(const wchar_t *from, const wchar_t *to) {
    for (DWORD wait = 0;; wait = wait ? wait * 2 : 1) {
        if (MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 1;
        DWORD e = GetLastError(), a = GetFileAttributesW(to);
        if ((e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION) || wait > 512 ||
            (a != INVALID_FILE_ATTRIBUTES && (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))))
            return 0;
        Sleep(wait);
    }
}
#else
/* An fsync that reaches the disk: on macOS a plain one stops at the drive's
   cache. */
static int k_os_fsync(int fd) {
    #ifdef F_FULLFSYNC
        if (fcntl(fd, F_FULLFSYNC) == 0) return 1;
    #endif
    return fsync(fd) == 0;
}

/* A rename is on the disk once the directory holding the new name is (what
   MOVEFILE_WRITE_THROUGH waits for on Windows). Best effort: the rename has
   happened either way. */
static void k_os_sync_dir(const char *path) {
    char dir[K_OS_PATH_MAX];
    const char *slash = strrchr(path, '/');
    size_t n = slash ? (size_t)(slash - path) : 0;
    if (slash && n == 0) n = 1;
    if (n) memcpy(dir, path, n);
    else dir[n++] = '.';
    dir[n] = 0;
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return;
    k_os_fsync(fd);
    close(fd);
}
#endif

/* What a native path names: 0 nothing that can be reached, 1 something
   other than a directory, 2 a directory. */
static int k_os_kind(const k_os_char *p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesW(p);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? 2 : 1;
#else
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 2 : 1;
#endif
}

/* Appends the whole file; on a failure, nothing. */
K_API uint8_t k_os_read_file(sl_u8 path, k_rref out) {
    k_os_char p[K_OS_PATH_MAX];
    if (k_os_native(path, p) < 0) return 0;
    FILE *f = k_os_fopen(p, "rb");
    if (!f) return 0;
    int64_t len = out.hdr->len;
    uint8_t *top = out.stk->top;
    uint8_t buf[65536];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, f);
        if (n == 0) break;
        k_bld_append(out, buf, (int64_t)n);
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        out.hdr->len = len;
        out.stk->top = top;
        return 0;
    }
    return 1;
}

/* write_file and append_file. */
static uint8_t k_os_put_file(sl_u8 path, sl_u8 data, int append) {
    k_os_char p[K_OS_PATH_MAX];
    if (k_os_native(path, p) < 0) return 0;
    FILE *f = append ? k_os_fopen(p, "ab") : k_os_fopen(p, "wb");
    if (!f) return 0;
    size_t n = (size_t)(data.len < 0 ? 0 : data.len);
    int ok = fwrite(data.data, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    return ok ? 1 : 0;
}

K_API uint8_t k_os_write_file(sl_u8 path, sl_u8 data) { return k_os_put_file(path, data, 0); }

K_API uint8_t k_os_append_file(sl_u8 path, sl_u8 data) { return k_os_put_file(path, data, 1); }

K_API uint8_t k_os_file_exists(sl_u8 path) {
    k_os_char p[K_OS_PATH_MAX];
    return k_os_native(path, p) >= 0 && k_os_kind(p) == 1;
}

K_API uint8_t k_os_remove_file(sl_u8 path) {
    k_os_char p[K_OS_PATH_MAX];
    if (k_os_native(path, p) < 0) return 0;
#ifdef _WIN32
    return DeleteFileW(p) != 0;
#else
    /* Not remove, which takes an empty directory as well. */
    return unlink(p) == 0;
#endif
}

/* Replaces `to` if it exists, in one step: `to` names the old file or the
   moved one, never neither. */
K_API uint8_t k_os_rename_file(sl_u8 from, sl_u8 to) {
    k_os_char f[K_OS_PATH_MAX], t[K_OS_PATH_MAX];
    if (k_os_native(from, f) < 0 || k_os_native(to, t) < 0) return 0;
#ifdef _WIN32
    return k_os_replace(f, t);
#else
    if (rename(f, t) != 0) return 0;
    k_os_sync_dir(t);
    return 1;
#endif
}

/* Writes data to a new file beside path, flushes it to the disk and renames
   it over path, so that path holds either its old contents or all of the
   new ones whatever happens meanwhile; on a failure the new file is removed.
   Its name is path's with a random tag and ".tmp" added, created only if
   nothing has that name already: one that does belongs to someone else, and
   another tag is drawn. */
K_API uint8_t k_os_write_file_atomic(sl_u8 path, sl_u8 data) {
    k_os_char p[K_OS_PATH_MAX], t[K_OS_PATH_MAX + 21];
    int n = k_os_native(path, p);
    if (n <= 0) return 0;
    const uint8_t *d = data.data;
    int64_t left = data.len;
    for (int tries = 0; tries < 8; tries++) {
        uint64_t tag = k_os_random_u64();
        int k = n;
        memcpy(t, p, (size_t)n * sizeof *t);
        t[k++] = '.';
        for (int s = 60; s >= 0; s -= 4) t[k++] = "0123456789abcdef"[(tag >> s) & 15];
        t[k++] = '.';
        t[k++] = 't';
        t[k++] = 'm';
        t[k++] = 'p';
        t[k] = 0;
        int ok = 1;
#ifdef _WIN32
        HANDLE h = CreateFileW(t, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) continue;
            return 0;
        }
        while (ok && left > 0) {
            DWORD chunk = left > (1 << 30) ? (DWORD)1 << 30 : (DWORD)left, wrote = 0;
            ok = WriteFile(h, d, chunk, &wrote, NULL) && wrote == chunk;
            d += chunk;
            left -= chunk;
        }
        ok = ok && FlushFileBuffers(h);
        if (!CloseHandle(h)) ok = 0;
        ok = ok && k_os_replace(t, p);
        if (!ok) DeleteFileW(t);
#else
        int fd = open(t, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            return 0;
        }
        /* The permissions of the file it replaces, which write_file leaves
           as they are, rather than the umask's. */
        struct stat st;
        if (stat(p, &st) == 0) ok = fchmod(fd, st.st_mode & 0777) == 0;
        while (ok && left > 0) {
            ssize_t w = write(fd, d, left > (1 << 30) ? (size_t)1 << 30 : (size_t)left);
            if (w < 0 && errno == EINTR) continue;
            ok = w > 0;
            if (ok) {
                d += w;
                left -= w;
            }
        }
        ok = ok && k_os_fsync(fd);
        if (close(fd) != 0) ok = 0;
        ok = ok && rename(t, p) == 0;
        if (ok) k_os_sync_dir(p);
        else unlink(t);
#endif
        return ok ? 1 : 0;
    }
    return 0;
}

/* True where path is a directory afterwards, one that was there already
   included. */
K_API uint8_t k_os_make_dir(sl_u8 path) {
    k_os_char p[K_OS_PATH_MAX];
    if (k_os_native(path, p) < 0) return 0;
#ifdef _WIN32
    if (CreateDirectoryW(p, NULL)) return 1;
#else
    if (mkdir(p, 0777) == 0) return 1;
#endif
    return k_os_kind(p) == 2;
}

K_API uint8_t k_os_is_dir(sl_u8 path) {
    k_os_char p[K_OS_PATH_MAX];
    return k_os_native(path, p) >= 0 && k_os_kind(p) == 2;
}

/* An empty directory. */
K_API uint8_t k_os_remove_dir(sl_u8 path) {
    k_os_char p[K_OS_PATH_MAX];
    if (k_os_native(path, p) < 0) return 0;
#ifdef _WIN32
    return RemoveDirectoryW(p) != 0;
#else
    return rmdir(p) == 0;
#endif
}

/* The names a listing collects before sorting them: NUL-terminated, one
   after another. */
typedef struct {
    char *text;
    size_t len, cap, count;
} k_os_names;

/* A name with a newline in it cannot be a line of the listing, and is left
   out. */
static int k_os_names_add(k_os_names *ns, const char *name, size_t n) {
    if (memchr(name, '\n', n)) return 1;
    if (ns->len + n + 1 > ns->cap) {
        size_t cap = ns->cap ? ns->cap * 2 : 4096;
        while (cap < ns->len + n + 1) cap *= 2;
        char *text = (char *)realloc(ns->text, cap);
        if (!text) return 0;
        ns->text = text;
        ns->cap = cap;
    }
    memcpy(ns->text + ns->len, name, n);
    ns->text[ns->len + n] = 0;
    ns->len += n + 1;
    ns->count++;
    return 1;
}

static int k_os_name_order(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Appends the names in bytewise order, each followed by a newline. */
static int k_os_names_put(k_os_names *ns, k_rref out) {
    if (!ns->count) return 1;
    const char **v = (const char **)malloc(ns->count * sizeof *v);
    if (!v) return 0;
    size_t i = 0;
    for (size_t at = 0; at < ns->len; at += strlen(ns->text + at) + 1) v[i++] = ns->text + at;
    qsort(v, ns->count, sizeof *v, k_os_name_order);
    for (i = 0; i < ns->count; i++) {
        k_bld_append(out, v[i], (int64_t)strlen(v[i]));
        k_bld_append(out, "\n", 1);
    }
    free(v);
    return 1;
}

/* Appends the name of every entry of a directory but "." and "..", sorted
   so that a listing does not depend on the file system; on a failure
   appends nothing. */
K_API uint8_t k_os_list_dir(sl_u8 path, k_rref out) {
    k_os_char p[K_OS_PATH_MAX + 2];
    int n = k_os_native(path, p);
    if (n <= 0) return 0;
    k_os_names ns = { 0 };
    int ok = 1;
#ifdef _WIN32
    /* FindFirstFileW takes a pattern, which "*" after the directory makes
       every entry. */
    int dirlen = n;
    if (p[n - 1] != '/' && p[n - 1] != '\\' && p[n - 1] != ':') p[n++] = '\\';
    p[n++] = '*';
    p[n] = 0;
    WIN32_FIND_DATAW e;
    HANDLE h = FindFirstFileW(p, &e);
    if (h == INVALID_HANDLE_VALUE) {
        /* No entry at all, not even "." and "..": the empty root of a drive. */
        int empty = GetLastError() == ERROR_FILE_NOT_FOUND;
        p[dirlen] = 0;
        return empty && k_os_kind(p) == 2;
    }
    do {
        const wchar_t *w = e.cFileName;
        if (w[0] == '.' && (!w[1] || (w[1] == '.' && !w[2]))) continue;
        /* A name that is not valid UTF-16 has no UTF-8 spelling, and is left
           out. */
        char name[1024];
        int k = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, name, sizeof name,
                                    NULL, NULL);
        if (k > 1 && !k_os_names_add(&ns, name, (size_t)k - 1)) ok = 0;
    } while (ok && FindNextFileW(h, &e));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = 0;
    FindClose(h);
#else
    DIR *d = opendir(p);
    if (!d) return 0;
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(d);
        if (!e) {
            ok = errno == 0;
            break;
        }
        const char *w = e->d_name;
        if (w[0] == '.' && (!w[1] || (w[1] == '.' && !w[2]))) continue;
        if (!k_os_names_add(&ns, w, strlen(w))) {
            ok = 0;
            break;
        }
    }
    closedir(d);
#endif
    ok = ok && k_os_names_put(&ns, out);
    free(ns.text);
    return ok ? 1 : 0;
}

K_API void k_os_write_stdout(sl_u8 s) {
    if (s.len > 0) fwrite(s.data, 1, (size_t)s.len, stdout);
}

K_API void k_os_write_stderr(sl_u8 s) {
    if (s.len > 0) fwrite(s.data, 1, (size_t)s.len, stderr);
}

K_API void k_os_flush_stdout(void) { fflush(stdout); }

/* fgetc takes the stream's lock for every byte; read_line takes it once
   per line and reads the bytes without it. */
#ifdef _WIN32
#define k_os_lock_stdin() _lock_file(stdin)
#define k_os_unlock_stdin() _unlock_file(stdin)
#define k_os_getc_stdin() _getc_nolock(stdin)
#else
#define k_os_lock_stdin() flockfile(stdin)
#define k_os_unlock_stdin() funlockfile(stdin)
#define k_os_getc_stdin() getc_unlocked(stdin)
#endif

/* One line of stdin, without its newline or a carriage return before it;
   false at end of input. */
K_API uint8_t k_os_read_line(k_rref out) {
    k_os_lock_stdin();
    int c = k_os_getc_stdin();
    if (c == EOF) { k_os_unlock_stdin(); return 0; }
    int64_t start = out.hdr->len;
    while (c != EOF && c != '\n') {
        uint8_t b = (uint8_t)c;
        k_bld_append(out, &b, 1);
        c = k_os_getc_stdin();
    }
    k_os_unlock_stdin();
    if (c == '\n' && out.hdr->len > start && out.hdr->base[out.hdr->len - 1] == '\r') {
        out.hdr->len--;
        out.stk->top--;
    }
    return 1;
}

/* All of stdin. */
K_API void k_os_read_stdin(k_rref out) {
    uint8_t buf[65536];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, stdin);
        if (n == 0) break;
        k_bld_append(out, buf, (int64_t)n);
    }
}

/* Up to max bytes of stdin; false when none were left. */
K_API uint8_t k_os_read_stdin_max(k_rref out, int64_t max) {
    uint8_t buf[65536];
    int64_t got = 0;
    while (got < max) {
        size_t want = max - got < (int64_t)sizeof buf ? (size_t)(max - got) : sizeof buf;
        size_t n = fread(buf, 1, want, stdin);
        if (n == 0) break;
        k_bld_append(out, buf, (int64_t)n);
        got += (int64_t)n;
    }
    return got > 0;
}

K_API int64_t k_os_arg_count(void) { return k_argc; }

/* The application's resource directory (resource_dir(), below) in native
   form with its final separator, NUL-terminated in buf (K_OS_PATH_MAX
   characters): its length, or 0 where it is not known. JIT supplies its
   entry source directory; AOT discovers the running executable. */
static int k_os_resource_native(k_os_char *buf) {
#ifdef K_JIT_RESOURCE_DIR
    sl_u8 dir = { (uint8_t *)K_JIT_RESOURCE_DIR, (int64_t)strlen(K_JIT_RESOURCE_DIR) };
    int n = k_os_native(dir, buf);
    return n < 0 ? 0 : n;
#elif defined(_WIN32)
    DWORD n = GetModuleFileNameW(NULL, buf, K_OS_PATH_MAX);
    if (!n || n >= K_OS_PATH_MAX) return 0;
    while (n && buf[n - 1] != L'\\' && buf[n - 1] != L'/') --n;
    buf[n] = 0;
    return (int)n;
#else
    char *path = buf;
    size_t n;
    #ifdef __APPLE__
        uint32_t capacity = K_OS_PATH_MAX;
        if (_NSGetExecutablePath(path, &capacity) != 0) return 0;
        /* Resolve the loader's possible relative path and symlinks. */
        char *resolved = realpath(path, NULL);
        if (!resolved) return 0;
        n = strlen(resolved);
        if (n >= (size_t)K_OS_PATH_MAX) { free(resolved); return 0; }
        memcpy(path, resolved, n + 1);
        free(resolved);
    #elif defined(__linux__)
        ssize_t count = readlink("/proc/self/exe", path, (size_t)K_OS_PATH_MAX);
        if (count <= 0 || (size_t)count >= (size_t)K_OS_PATH_MAX) return 0;
        n = (size_t)count;
    #else
        return 0;
    #endif
    while (n && path[n - 1] != '/') --n;
    if (!n) return 0;
    #ifdef __APPLE__
        const char suffix[] = ".app/Contents/MacOS/";
        size_t suffixlen = sizeof suffix - 1;
        if (n >= suffixlen && !memcmp(path + n - suffixlen, suffix, suffixlen)) {
            n -= sizeof "MacOS/" - 1;
            if (n + sizeof "Resources/" - 1 >= (size_t)K_OS_PATH_MAX) return 0;
            memcpy(path + n, "Resources/", sizeof "Resources/" - 1);
            n += sizeof "Resources/" - 1;
        }
    #endif
    path[n] = 0;
    return (int)n;
#endif
}

/* Application resources, independent of cwd and argv[0]. Append only on
   success, including a final path separator. */
K_API uint8_t k_os_resource_dir(k_rref out) {
    k_os_char path[K_OS_PATH_MAX];
    int n = k_os_resource_native(path);
    if (!n) return 0;
#ifdef _WIN32
    return k_os_append_wide(out, path, n);
#else
    k_bld_append(out, path, (int64_t)n);
    return 1;
#endif
}

/* The shortest digits of a finite double (runtime_ext.h). */
K_API int64_t k_f64_digits(double x, sl_u8 dig, int64_t *point) {
    uint64_t b, frac;
    char buf[20];
    int k, n, bexp;
    if (x != x || isinf(x) || x == 0) return 0;
    memcpy(&b, &x, sizeof b);
    bexp = (int)(b >> 52) & 0x7FF;
    frac = b & (((uint64_t)1 << 52) - 1);
    n = k_float_digits(bexp ? frac | (uint64_t)1 << 52 : frac, (bexp ? bexp - 1 : 0) - 1074,
                        53, -1074, buf, &k);
    if (n > dig.len) n = (int)dig.len;
    memcpy(dig.data, buf, (size_t)n);
    *point = k;
    return n;
}

/* ---- Dynamic externs: the libraries the program's extern blocks name ---- */

/* The spelling this platform opens for a library, UTF-8 in buf (K_OS_PATH_MAX
   bytes): the sidecar's for the platform, else the block's name. A last
   component without a dot takes the platform's library form (SDL2 is
   SDL2.dll, libSDL2.dylib, libSDL2.so); a spelling with a separator, unless
   absolute, is under the resource directory; a bare one goes to the loader's
   search as it is. 0 where it does not fit or the resource directory is not
   known. */
static int k_dl_spelling(const k_dllib *lib, char *buf) {
#ifdef _WIN32
    const char *s = lib->onwindows, *prefix = "", *suffix = ".dll";
#elif defined(__APPLE__)
    const char *s = lib->onmacos, *prefix = "lib", *suffix = ".dylib";
#else
    const char *s = lib->onlinux, *prefix = "lib", *suffix = ".so";
#endif
    if (!s) s = lib->name;
    size_t len = strlen(s), dir = 0;
    for (size_t i = 0; i < len; i++) {
        int sep = s[i] == '/';
#ifdef _WIN32
        sep = sep || s[i] == '\\';
#endif
        if (sep) dir = i + 1;
    }
    const char *leaf = s + dir;
    int bare = !strchr(leaf, '.');
    int absolute = s[0] == '/';
#ifdef _WIN32
    int drive = (s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z');
    absolute = absolute || s[0] == '\\' || (len >= 2 && drive && s[1] == ':');
#endif
    char root[K_OS_PATH_MAX];
    root[0] = 0;
    if (dir && !absolute) {
        k_os_char native[K_OS_PATH_MAX];
        int n = k_os_resource_native(native);
        if (!n) return 0;
#ifdef _WIN32
        n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native, n, root, K_OS_PATH_MAX - 1,
                                NULL, NULL);
        if (n <= 0) return 0;
        root[n] = 0;
#else
        memcpy(root, native, (size_t)n + 1);
#endif
    }
    int n = snprintf(buf, K_OS_PATH_MAX, "%s%.*s%s%s%s", root, (int)dir, s, bare ? prefix : "",
                     leaf, bare ? suffix : "");
    return n > 0 && n < K_OS_PATH_MAX ? n : 0;
}

K_API void k_dl_bind(const k_dllib *libs, int64_t nlibs, const k_dlbind *binds,
                       int64_t nbinds) {
    for (int64_t l = 0; l < nlibs; l++) {
        char spelling[K_OS_PATH_MAX], msg[K_OS_PATH_MAX + 512];
        if (!k_dl_spelling(&libs[l], spelling)) {
            snprintf(msg, sizeof msg, "cannot load %s: its path does not fit or is not known",
                     libs[l].name);
            k_panic(msg);
        }
#ifdef _WIN32
        sl_u8 utf8 = { (uint8_t *)spelling, (int64_t)strlen(spelling) };
        wchar_t wide[K_OS_PATH_MAX];
        HMODULE h = NULL;
        if (k_os_native(utf8, wide) >= 0) {
            /* A path, the resource directory's or an absolute one: the
               libraries it depends on are looked for beside it first, as
               a library shipped with its dependencies under lib/ needs.
               That search wants backslashes. A bare name keeps the
               loader's own search. */
            int path = 0;
            for (wchar_t *c = wide; *c; c++)
                if (*c == L'/' || *c == L'\\') { *c = L'\\'; path = 1; }
            h = path ? LoadLibraryExW(wide, NULL, LOAD_WITH_ALTERED_SEARCH_PATH) : LoadLibraryW(wide);
        }
        if (!h) {
            snprintf(msg, sizeof msg, "cannot load %s (%s): Windows error %u", libs[l].name,
                     spelling, (unsigned)GetLastError());
            k_panic(msg);
        }
#else
        void *h = dlopen(spelling, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            const char *why = dlerror();
            snprintf(msg, sizeof msg, "cannot load %s (%s): %s", libs[l].name, spelling,
                     why ? why : "unknown error");
            k_panic(msg);
        }
#endif
        for (int64_t b = 0; b < nbinds; b++) {
            if (binds[b].lib != l) continue;
#ifdef _WIN32
            void *f = (void *)GetProcAddress(h, binds[b].sym);
#else
            void *f = dlsym(h, binds[b].sym);
#endif
            if (!f) {
                snprintf(msg, sizeof msg, "%s (%s) has no symbol %s", libs[l].name, spelling,
                         binds[b].sym);
                k_panic(msg);
            }
            *binds[b].slot = f;
        }
    }
}

K_API void k_os_arg(int64_t i, k_rref out) {
    if (i < 0 || i >= k_argc) return;
    const char *a = k_argv[i];
#ifdef _WIN32
    /* main's argv is in the ANSI code page. The program gets it in UTF-8,
       the encoding of its paths, by way of UTF-16, which holds every
       character of the code page. */
    int n = MultiByteToWideChar(CP_ACP, 0, a, -1, NULL, 0);
    wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof *w) : NULL;
    int ok = w && MultiByteToWideChar(CP_ACP, 0, a, -1, w, n) == n &&
             k_os_append_wide(out, w, n - 1);
    free(w);
    if (ok) return;
#endif
    k_bld_append(out, a, (int64_t)strlen(a));
}

K_API uint8_t k_os_getenv(sl_u8 name, k_rref out) {
    k_os_char nm[K_OS_PATH_MAX];
    if (k_os_native(name, nm) < 0) return 0;
#ifdef _WIN32
    /* The environment holds UTF-16, which getenv would hand out in the ANSI
       code page. A value can change size between the calls. */
    wchar_t small[512], *v = small;
    DWORD cap = 512, len;
    for (;;) {
        SetLastError(0);
        len = GetEnvironmentVariableW(nm, v, cap);
        if (len < cap) break;
        if (v != small) free(v);
        cap = len;
        v = (wchar_t *)malloc(cap * sizeof *v);
        if (!v) return 0;
    }
    /* Zero is also the length of an empty value. */
    int ok = (len > 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND) &&
             k_os_append_wide(out, v, (int)len);
    if (v != small) free(v);
    return ok ? 1 : 0;
#else
    const char *v = getenv(nm);
    if (!v) return 0;
    k_bld_append(out, v, (int64_t)strlen(v));
    return 1;
#endif
}

/* Wall-clock time in nanoseconds since the Unix epoch. */
K_API int64_t k_os_time_ns(void) {
    /* Neither branch uses C11's timespec_get: the Microsoft C runtime tcc
       links against does not have it, and glibc hides it from a compiler
       announcing C99, which tcc also is. */
#ifdef _WIN32
    /* Windows counts 100 ns ticks from 1601; the offset to the Unix epoch is
       a constant. */
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (int64_t)(t - 116444736000000000ull) * 100;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
}

/* A monotonic clock in nanoseconds, for measuring intervals. */
K_API int64_t k_os_clock_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (int64_t)((double)c.QuadPart * (1000000000.0 / (double)f.QuadPart));
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
}

K_API void k_os_sleep_ms(int64_t ms) {
    if (ms <= 0) return;
#ifdef _WIN32
    /* Sleep takes 32 bits, the largest of them meaning INFINITE: a longer
       sleep goes in parts. */
    while (ms > 0) {
        DWORD part = ms > 0x7fffffff ? 0x7fffffff : (DWORD)ms;
        Sleep(part);
        ms -= part;
    }
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000);
    nanosleep(&ts, NULL);
#endif
}

/* Cryptographically secure random bytes from the OS into p[0..n): 1, or 0
   if it gives none. Windows: ProcessPrng (bcryptprimitives.dll, Windows 10
   and later, what Go and Rust use), else RtlGenRandom (advapi32.dll's
   SystemFunction036), each looked up when first needed, so that a program
   links no library beyond the runtime's (TinyCC's .def files list neither).
   Elsewhere getentropy(3), 256 bytes a call, else /dev/urandom. */
#ifdef _WIN32
typedef BOOL (WINAPI *k_os_process_prng)(PBYTE, SIZE_T);
typedef BOOLEAN (WINAPI *k_os_rtl_gen_random)(PVOID, ULONG);

/* A function of a system DLL: from the copy already loaded, else from one
   loaded out of System32 only, as Go does. bcryptprimitives.dll is not a
   KnownDLL, so a plain LoadLibraryW would look in the application's
   directory first, and a DLL placed there would supply the program's keys. */
static FARPROC k_os_system_proc(const wchar_t *dll, const char *name) {
    HMODULE h = GetModuleHandleW(dll);
    if (!h) h = LoadLibraryExW(dll, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return h ? GetProcAddress(h, name) : NULL;
}

static int k_os_entropy(uint8_t *p, size_t n) {
    /* The function pointers themselves are published, with full barriers,
       so a thread that sees one sees it whole (ARM64 too). Workers racing
       the first call each look it up and find the same function. */
    static PVOID volatile prng_fn, rtl_fn;
    k_os_process_prng prng = (k_os_process_prng)InterlockedCompareExchangePointer(&prng_fn, NULL, NULL);
    k_os_rtl_gen_random rtl = (k_os_rtl_gen_random)InterlockedCompareExchangePointer(&rtl_fn, NULL, NULL);
    if (!prng && !rtl) {
        prng = (k_os_process_prng)k_os_system_proc(L"bcryptprimitives.dll", "ProcessPrng");
        if (prng) InterlockedExchangePointer(&prng_fn, (PVOID)prng);
        else {
            rtl = (k_os_rtl_gen_random)k_os_system_proc(L"advapi32.dll", "SystemFunction036");
            if (rtl) InterlockedExchangePointer(&rtl_fn, (PVOID)rtl);
        }
    }
    /* ProcessPrng always returns TRUE. */
    if (prng) return prng(p, n) ? 1 : 0;
    if (!rtl) return 0;
    while (n > 0) {
        ULONG part = n > 0x40000000u ? 0x40000000u : (ULONG)n;
        if (!rtl(p, part)) return 0;
        p += part;
        n -= part;
    }
    return 1;
}
#else
static int k_os_entropy(uint8_t *p, size_t n) {
    size_t done = 0;
    while (done < n) {
        size_t part = n - done > 256 ? 256 : n - done;
        if (getentropy(p + done, part) != 0) break;
        done += part;
    }
    if (done == n) return 1;
    /* No getentropy (ENOSYS: a kernel before Linux 3.17): the device. */
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return 0;
    while (done < n) {
        size_t got = fread(p + done, 1, n - done, f);
        if (got == 0) break;
        done += got;
    }
    fclose(f);
    return done == n;
}
#endif

K_API uint8_t k_os_random_bytes(sl_u8 out) {
    if (out.len <= 0) return 1;
    return (uint8_t)k_os_entropy(out.data, (size_t)out.len);
}

/* Entropy for seeding a PRNG: the OS's, as random_bytes takes it; if it
   gives none, a splitmix64 mix of the clocks, the process id and an
   address, which is not cryptographic. */
static uint64_t k_os_mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15u;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9u;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebu;
    return x ^ (x >> 31);
}

K_API uint64_t k_os_random_u64(void) {
    uint64_t v = 0;
    if (k_os_entropy((uint8_t *)&v, sizeof v)) return v;
    /* Which call this is, counted atomically: workers calling at once each
       get a count of their own. */
#ifdef _WIN32
    static volatile LONG64 counter;
    uint64_t count = (uint64_t)InterlockedIncrement64(&counter);
#else
    static uint64_t counter;
    uint64_t count = __atomic_add_fetch(&counter, 1, __ATOMIC_SEQ_CST);
#endif
    uint64_t seed = (uint64_t)k_os_time_ns();
    seed = k_os_mix64(seed ^ (uint64_t)k_os_clock_ns());
#ifdef _WIN32
    seed = k_os_mix64(seed ^ (uint64_t)GetCurrentProcessId());
#endif
    seed = k_os_mix64(seed ^ (uint64_t)(uintptr_t)&counter);
    return k_os_mix64(seed ^ count);
}

/* ---------------------------------------------------------------------------
   std's slice_pool (stdlib/slice_pool_internal.k) keeps the span tree
   of runtime_impl.h's k_spans_* in nodes of its own and a `tree` field for
   the header, both of which the program can write. These are those
   functions, taking the nodes as its externs pass them and the header
   apart from them, as `hd`, and with everything read from the tree checked
   before it is used: a node number must be one of the nodes past node 0
   (K_SPBADNUM), an entry count must fit the node it is read from as a leaf
   or inner node, a height must fit a path, and an entry moved into or out
   of a node must be one it has room for or holds. A node reached at two
   levels of a path, or one the chain hands out while it is in the tree,
   keeps every access inside the nodes: each count these write fits a leaf,
   and the counts that index an inner node's children are checked where
   they are read. Sums of stored indices and counts wrap. A check that fails
   aborts (K_E_SPANTREE); a wrong tree that passes them hands out wrong
   spans, which slice_pool checks against its length. New nodes start
   zeroed, as a pushed `node { .. }` would. reusable[]'s freelist is out of
   the program's reach, so it keeps the unchecked functions, and this copy
   goes when reusable[] does. It is here, not beside them, because the
   externs take a k_rref, which runtime_ext.h defines. */

typedef struct {
    k_sphead *hd;
    uint8_t *base;
    int64_t *n;
    uint8_t **top;
    const k_loc *at;    /* The call that a corrupt tree is reported at. */
    int64_t h, node[K_SPAN_DEPTH], pos[K_SPAN_DEPTH];
} k_spcpath;

#define K_SPCHEAD(p) ((p)->hd)
#define K_SPSUM(a, b) ((int64_t)((uint64_t)(a) + (uint64_t)(b)))

/* Macros rather than functions, which TinyCC would call at every level. */
#define K_SPBADNUM(p, i) ((uint64_t)(i) - 1 >= (uint64_t)*(p)->n - 1)
#define K_SPBADIN(p, i) (K_SPBADNUM(p, i) || (uint64_t)K_SPINNER(p, i)->n - 1 >= K_SPAN_FAN)
#define K_SPBADLEAF(p, i) (K_SPBADNUM(p, i) || (uint64_t)K_SPLEAF(p, i)->n > K_SPAN_LEAF)
#define K_SPBADH(h) ((uint64_t)(h) >= K_SPAN_DEPTH)

static K_NORETURN void k_spc_corrupt(const k_spcpath *p) {
    k_abort(K_E_SPANTREE, p->at->file, p->at->line);
}

#define K_SPSTART(p, tree, nodes, loc) \
    ((p)->hd = (k_sphead *)(tree), (p)->base = (nodes).hdr->base, (p)->n = &(nodes).hdr->len, \
     (p)->top = &(nodes).stk->top, (p)->at = (loc))

static int64_t k_spc_newnode(k_spcpath *p) {
    int64_t i = K_SPCHEAD(p)->chain;
    if (i) {
        if (K_SPBADNUM(p, i)) k_spc_corrupt(p);
        K_SPCHEAD(p)->chain = K_SPLEAF(p, i)->link;
        return i;
    }
    i = (*p->n)++;
    memset(K_SPLEAF(p, i), 0, K_SPAN_NODE);
    *p->top += K_SPAN_NODE;
    return i;
}

static void k_spc_dropnode(k_spcpath *p, int64_t i) {
    K_SPLEAF(p, i)->link = K_SPCHEAD(p)->chain;
    K_SPCHEAD(p)->chain = i;
}

static void k_spc_open(const k_spcpath *p, k_spleaf *lf, int64_t s) {
    int64_t j;
    if ((uint64_t)lf->n >= K_SPAN_LEAF || (uint64_t)s > (uint64_t)lf->n) k_spc_corrupt(p);
    for (j = lf->n; j > s; j--) {
        lf->idx[j] = lf->idx[j - 1];
        lf->cnt[j] = lf->cnt[j - 1];
    }
    lf->n++;
}

static void k_spc_close(const k_spcpath *p, k_spleaf *lf, int64_t s) {
    int64_t j;
    if ((uint64_t)s >= (uint64_t)lf->n) k_spc_corrupt(p);
    lf->n--;
    for (j = s; j < lf->n; j++) {
        lf->idx[j] = lf->idx[j + 1];
        lf->cnt[j] = lf->cnt[j + 1];
    }
}

static void k_spc_openin(const k_spcpath *p, k_spinner *in, int64_t s) {
    int64_t j;
    if ((uint64_t)in->n >= K_SPAN_FAN || (uint64_t)s > (uint64_t)in->n) k_spc_corrupt(p);
    for (j = in->n; j > s; j--) {
        in->key[j] = in->key[j - 1];
        in->max[j] = in->max[j - 1];
        in->child[j] = in->child[j - 1];
    }
    in->n++;
}

static void k_spc_closein(const k_spcpath *p, k_spinner *in, int64_t s) {
    int64_t j;
    if (in->n > K_SPAN_FAN || (uint64_t)s >= (uint64_t)in->n) k_spc_corrupt(p);
    in->n--;
    for (j = s; j < in->n; j++) {
        in->key[j] = in->key[j + 1];
        in->max[j] = in->max[j + 1];
        in->child[j] = in->child[j + 1];
    }
}

static int64_t k_spc_max(k_spcpath *p, int64_t i, int leaf, int64_t stop) {
    return leaf ? k_sp_most(K_SPLEAF(p, i)->cnt, K_SPLEAF(p, i)->n, stop)
                : k_sp_most(K_SPINNER(p, i)->max, K_SPINNER(p, i)->n, stop);
}

static int64_t k_spc_key(k_spcpath *p, int64_t i, int leaf) {
    return leaf ? K_SPLEAF(p, i)->idx[0] : K_SPINNER(p, i)->key[0];
}

static void k_spc_fix(k_spcpath *p, int64_t l, int64_t was, int64_t now) {
    for (; l > 0; l--) {
        k_spinner *in = K_SPINNER(p, p->node[l - 1]);
        int64_t k = p->pos[l - 1], c = p->node[l], m = in->max[k];
        int leaf = l == p->h;
        int64_t key = k_spc_key(p, c, leaf);
        int64_t nm = now >= m ? now : was < m ? m : k_spc_max(p, c, leaf, was);
        if (in->key[k] == key && nm == m) return;
        in->key[k] = key;
        in->max[k] = nm;
        was = m;
        now = nm;
    }
}

static int64_t k_spc_find(k_spcpath *p, int64_t idx) {
    int64_t l, i = K_SPCHEAD(p)->root, k;
    k_spleaf *lf;
    p->h = K_SPCHEAD(p)->height;
    if (K_SPBADH(p->h)) k_spc_corrupt(p);
    for (l = 0; l < p->h; l++) {
        k_spinner *in;
        if (K_SPBADIN(p, i)) k_spc_corrupt(p);
        in = K_SPINNER(p, i);
        for (k = 1; k < in->n && in->key[k] <= idx; k++) {}
        p->node[l] = i;
        p->pos[l] = k - 1;
        i = in->child[k - 1];
    }
    p->node[p->h] = i;
    if (K_SPBADLEAF(p, i)) k_spc_corrupt(p);
    lf = K_SPLEAF(p, i);
    for (k = 0; k < lf->n && lf->idx[k] <= idx; k++) {}
    p->pos[p->h] = k - 1;
    return k - 1;
}

static void k_spc_newroot(k_spcpath *p, int64_t a, int64_t b, int leaf) {
    int64_t r = k_spc_newnode(p);
    k_spinner *rn = K_SPINNER(p, r);
    rn->n = 2;
    rn->key[0] = k_spc_key(p, a, leaf);
    rn->max[0] = k_spc_max(p, a, leaf, INT64_MAX);
    rn->child[0] = a;
    rn->key[1] = k_spc_key(p, b, leaf);
    rn->max[1] = k_spc_max(p, b, leaf, INT64_MAX);
    rn->child[1] = b;
    K_SPCHEAD(p)->root = r;
    K_SPCHEAD(p)->height++;
}

static void k_spc_addchild(k_spcpath *p, int64_t l, int64_t k, int64_t child, int leaf) {
    for (;;) {
        k_spinner *in = K_SPINNER(p, p->node[l]), *o = in;
        int64_t key = k_spc_key(p, child, leaf), max = k_spc_max(p, child, leaf, INT64_MAX);
        int64_t s = k, nn = 0;
        if (in->n == K_SPAN_FAN) {
            int64_t half = K_SPAN_FAN / 2;
            k_spinner *r;
            nn = k_spc_newnode(p);
            if (nn == p->node[l]) k_spc_corrupt(p);
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
        k_spc_openin(p, o, s);
        o->key[s] = key;
        o->max[s] = max;
        o->child[s] = child;
        if (!nn) {
            k_spc_fix(p, l, 0, max);
            return;
        }
        if (l == 0) {
            k_spc_newroot(p, p->node[0], nn, 0);
            return;
        }
        k_spc_fix(p, l, INT64_MAX, 0);
        child = nn;
        leaf = 0;
        k = p->pos[l - 1] + 1;
        l--;
    }
}

static void k_spc_insert(k_spcpath *p, int64_t k, int64_t idx, int64_t cnt) {
    k_spleaf *lf = K_SPLEAF(p, p->node[p->h]), *o = lf;
    int64_t s = k, nn = 0;
    if (lf->n == K_SPAN_LEAF) {
        int64_t half = K_SPAN_LEAF / 2;
        k_spleaf *r;
        nn = k_spc_newnode(p);
        if (nn == p->node[p->h]) k_spc_corrupt(p);
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
    k_spc_open(p, o, s);
    o->idx[s] = idx;
    o->cnt[s] = cnt;
    if (!nn) {
        k_spc_fix(p, p->h, 0, cnt);
    } else if (p->h == 0) {
        k_spc_newroot(p, p->node[0], nn, 1);
    } else {
        k_spc_fix(p, p->h, INT64_MAX, 0);
        k_spc_addchild(p, p->h - 1, p->pos[p->h - 1] + 1, nn, 1);
    }
}

static void k_spc_delete(k_spcpath *p, int64_t k) {
    k_spleaf *lf = K_SPLEAF(p, p->node[p->h]);
    int64_t c = lf->cnt[k], l;
    k_spc_close(p, lf, k);
    if (lf->n > 0 || p->h == 0) {
        k_spc_fix(p, p->h, c, 0);
        return;
    }
    k_spc_dropnode(p, p->node[p->h]);
    for (l = p->h - 1;; l--) {
        k_spinner *in = K_SPINNER(p, p->node[l]);
        int64_t e = p->pos[l], m = in->max[e];
        k_spc_closein(p, in, e);
        if (in->n > 0) {
            k_spc_fix(p, l, m, 0);
            break;
        }
        /* A root of one child, which the functions never leave. */
        if (l == 0) k_spc_corrupt(p);
        k_spc_dropnode(p, p->node[l]);
    }
    while (K_SPCHEAD(p)->height > 0) {
        int64_t r = K_SPCHEAD(p)->root;
        if (K_SPBADNUM(p, r)) k_spc_corrupt(p);
        if (K_SPINNER(p, r)->n != 1) break;
        K_SPCHEAD(p)->root = K_SPINNER(p, r)->child[0];
        K_SPCHEAD(p)->height--;
        k_spc_dropnode(p, r);
    }
}

static void k_spc_take(k_spcpath *p, int64_t k, int64_t cnt) {
    k_spleaf *lf = K_SPLEAF(p, p->node[p->h]);
    int64_t c = lf->cnt[k];
    if (c <= cnt) {
        k_spc_delete(p, k);
        return;
    }
    lf->idx[k] = K_SPSUM(lf->idx[k], cnt);
    lf->cnt[k] = c - cnt;
    k_spc_fix(p, p->h, c, c - cnt);
}

K_API int64_t k_slice_pool_place(k_rref nodes, void *tree, int64_t len, int64_t cnt,
                                   const k_loc *at) {
    k_spcpath p;
    int64_t l, i, k = 0, j, m, c, now, idx, before[K_SPAN_DEPTH];
    k_spleaf *lf = NULL;
    if (cnt == 0 || nodes.hdr->len == 0) return len;
    K_SPSTART(&p, tree, nodes, at);
    p.h = K_SPCHEAD(&p)->height;
    if (K_SPBADH(p.h)) k_spc_corrupt(&p);
    i = K_SPCHEAD(&p)->root;
    for (l = 0; l < p.h; l++) {
        k_spinner *in;
        if (K_SPBADIN(&p, i)) k_spc_corrupt(&p);
        in = K_SPINNER(&p, i);
        for (k = 0, m = 0; k < in->n && in->max[k] < cnt; k++)
            if (in->max[k] > m) m = in->max[k];
        if (k == in->n) break;
        p.node[l] = i;
        p.pos[l] = k;
        before[l] = m;
        i = in->child[k];
    }
    if (l == p.h) {
        if (K_SPBADLEAF(&p, i)) k_spc_corrupt(&p);
        lf = K_SPLEAF(&p, i);
        for (k = 0, m = 0; k < lf->n && lf->cnt[k] < cnt; k++)
            if (lf->cnt[k] > m) m = lf->cnt[k];
        before[l] = m;
    }
    if (l < p.h || k == lf->n) {
        for (l = 0, i = K_SPCHEAD(&p)->root; l < p.h; l++) {
            k_spinner *in;
            if (K_SPBADIN(&p, i)) k_spc_corrupt(&p);
            in = K_SPINNER(&p, i);
            p.node[l] = i;
            p.pos[l] = in->n - 1;
            i = in->child[in->n - 1];
        }
        p.node[p.h] = i;
        if (K_SPBADLEAF(&p, i)) k_spc_corrupt(&p);
        lf = K_SPLEAF(&p, i);
        k = lf->n - 1;
        if (k < 0 || K_SPSUM(lf->idx[k], lf->cnt[k]) != len) return len;
        idx = lf->idx[k];
        k_spc_take(&p, k, cnt);
        return idx;
    }
    p.node[p.h] = i;
    p.pos[p.h] = k;
    idx = lf->idx[k];
    c = lf->cnt[k];
    if (c > cnt) {
        lf->idx[k] = K_SPSUM(idx, cnt);
        lf->cnt[k] = now = c - cnt;
        j = k + 1;
    } else if (lf->n > 1 || p.h == 0) {
        k_spc_close(&p, lf, k);
        now = 0;
        j = k;
    } else {
        k_spc_delete(&p, k);
        return idx;
    }
    for (l = p.h; l > 0; l--) {
        k_spinner *in = K_SPINNER(&p, p.node[l - 1]);
        int64_t e = p.pos[l - 1], mx = in->max[e], nm = mx;
        int64_t key = k_spc_key(&p, p.node[l], l == p.h);
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

typedef struct {
    k_spleaf *lf, *nl;
    int64_t k, nk;
    k_spcpath *np;
} k_spcnear;

static void k_spc_near(k_spcpath *p, k_spcpath *q, int64_t idx, int64_t end, k_spcnear *s) {
    int64_t l;
    s->k = k_spc_find(p, idx);
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
    q->hd = p->hd;
    q->base = p->base;
    q->n = p->n;
    q->top = p->top;
    q->at = p->at;
    q->h = p->h;
    memcpy(q->node, p->node, (size_t)(l + 1) * 8);
    memcpy(q->pos, p->pos, (size_t)(l + 1) * 8);
    for (q->pos[l]++; l < q->h; l++) {
        int64_t c = K_SPINNER(q, q->node[l])->child[q->pos[l]];
        if (l + 1 < q->h ? K_SPBADIN(q, c) : K_SPBADLEAF(q, c)) k_spc_corrupt(q);
        q->node[l + 1] = c;
        q->pos[l + 1] = 0;
    }
    s->np = q;
    s->nl = K_SPLEAF(q, q->node[q->h]);
    s->nk = 0;
}

static void k_spc_release(k_spcpath *p, k_spcnear *s, int64_t idx, int64_t cnt) {
    k_spleaf *lf = s->lf, *nl = s->nl;
    int64_t k = s->k, nk = s->nk;
    int prev = k >= 0 && K_SPSUM(lf->idx[k], lf->cnt[k]) == idx;
    int next = nl && idx + cnt == nl->idx[nk];
    if (prev) {
        int64_t c = lf->cnt[k], m = K_SPSUM(K_SPSUM(c, cnt), next ? nl->cnt[nk] : 0);
        lf->cnt[k] = m;
        if (next && nl == lf) k_spc_close(p, lf, nk);
        k_spc_fix(p, p->h, c, m);
        if (next && nl != lf) k_spc_delete(s->np, nk);
    } else if (next) {
        int64_t c = nl->cnt[nk];
        nl->idx[nk] = idx;
        nl->cnt[nk] = K_SPSUM(c, cnt);
        k_spc_fix(s->np, s->np->h, c, nl->cnt[nk]);
    } else {
        k_spc_insert(p, k + 1, idx, cnt);
    }
}

K_API void k_slice_pool_free(k_rref nodes, void *tree, int64_t idx, int64_t cnt,
                               const k_loc *at) {
    k_spcpath p, q;
    k_spcnear s;
    if (cnt <= 0) return;
    K_SPSTART(&p, tree, nodes, at);
    if (*p.n == 0) {
        /* Node 0 stays zero, the chain's end. */
        memset(p.base, 0, K_SPAN_NODE);
        *p.n = 1;
        *p.top = p.base + K_SPAN_NODE;
        p.hd->chain = 0;
        p.hd->height = 0;
        p.hd->root = k_spc_newnode(&p);
    }
    k_spc_near(&p, &q, idx, idx + cnt, &s);
    k_spc_release(&p, &s, idx, cnt);
}

K_API int64_t k_slice_pool_regrow(k_rref nodes, void *tree, int64_t len, int64_t idx,
                                    int64_t ol, int64_t cnt, const k_loc *at) {
    k_spcpath p, q;
    k_spcnear s;
    int64_t end = idx + ol;
    if (nodes.hdr->len == 0) {
        k_slice_pool_free(nodes, tree, idx, ol, at);
        return k_slice_pool_place(nodes, tree, len, cnt, at);
    }
    K_SPSTART(&p, tree, nodes, at);
    k_spc_near(&p, &q, idx, end, &s);
    if (s.nl && s.nl->idx[s.nk] == end &&
        (s.nl->cnt[s.nk] >= cnt - ol || K_SPSUM(end, s.nl->cnt[s.nk]) == len)) {
        k_spc_take(s.np, s.nk, cnt - ol);
        return idx;
    }
    k_spc_release(&p, &s, idx, ol);
    return k_slice_pool_place(nodes, tree, len, cnt, at);
}

/* slice_pool's trim: returns the complete pages inside every free span to
   the OS (k_discard_pages), which reads them back as zeros. `items` is the
   pool's array viewed as bytes and `elem` its element size, so the span
   (idx, cnt) covers bytes [idx * elem, (idx + cnt) * elem). Free spans never
   touch, since a free merges with its neighbours, so each span is whole: a
   page is kept only where it also holds a live element, at either end.

   The walk visits each node once, so a tree whose children lead back to
   nodes already seen is corrupt, as is a span outside the array; both abort
   before anything is discarded past them. Pages are discarded below the
   array's stack top only, so k_trim_memory's commit mark is not moved.
   0 means an OS request failed. */
K_API uint8_t k_slice_pool_trim(k_rref nodes, void *tree, sl_u8 items, int64_t elem,
                                  const k_loc *at) {
    k_spcpath p;
    uintptr_t page = (uintptr_t)k_page_size;
    int64_t count, visits = 0, l = 0;
    int ok = 1;
    if (nodes.hdr->len == 0 || items.len <= 0 || elem <= 0 || page == 0) return 1;
    count = items.len / elem;
    K_SPSTART(&p, tree, nodes, at);
    p.h = K_SPCHEAD(&p)->height;
    if (K_SPBADH(p.h)) k_spc_corrupt(&p);
    p.node[0] = K_SPCHEAD(&p)->root;
    p.pos[0] = 0;
    if (p.h ? K_SPBADIN(&p, p.node[0]) : K_SPBADLEAF(&p, p.node[0])) k_spc_corrupt(&p);
    while (l >= 0) {
        if (l == p.h) {
            k_spleaf *lf = K_SPLEAF(&p, p.node[l]);
            if (++visits >= *p.n) k_spc_corrupt(&p);
            for (int64_t k = 0; k < lf->n; k++) {
                int64_t idx = lf->idx[k], cnt = lf->cnt[k];
                if (idx < 0 || cnt <= 0 || idx > count || cnt > count - idx) k_spc_corrupt(&p);
                uintptr_t lo = (uintptr_t)items.data + (uintptr_t)(idx * elem);
                uintptr_t hi = (uintptr_t)items.data + (uintptr_t)((idx + cnt) * elem);
                uintptr_t start = (lo + page - 1) / page * page;
                uintptr_t end = hi / page * page;
                if (start < end && !k_discard_pages(start, (size_t)(end - start))) ok = 0;
            }
            l--;
            continue;
        }
        k_spinner *in = K_SPINNER(&p, p.node[l]);
        if (p.pos[l] == 0 && ++visits >= *p.n) k_spc_corrupt(&p);
        if (p.pos[l] >= in->n) {
            l--;
            continue;
        }
        int64_t c = in->child[p.pos[l]++];
        if (l + 1 < p.h ? K_SPBADIN(&p, c) : K_SPBADLEAF(&p, c)) k_spc_corrupt(&p);
        l++;
        p.node[l] = c;
        p.pos[l] = 0;
    }
    return (uint8_t)ok;
}
