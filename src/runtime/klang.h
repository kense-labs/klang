/* klang.h -- for C compiled on its own, a package's or a program's sidecar
   C (docs/packages.md): the K values an extern fn hands it that C
   cannot spell from the layouts alone (spec §7.10, C.2). A `T[>..]&`
   parameter arrives as a k_rref, the resizable's header and its data
   stack. C may write its elements and append whole ones at the stack's
   top, moving the top and the header's count together, as k_bld_append
   does for the bytes of a `u8[>..]&`; nothing else of it is C's to touch.

   The compiler writes this header beside every native part it builds and
   puts it on that part's include path, `#include "klang.h"`, and builds
   the part again whenever the header changes. The generated program
   defines all of it itself (runtime.h, runtime_ext.h), and K_H with
   it, so an --include header may include this one too. */

#ifndef K_H
#define K_H

#include <stdint.h>
#include <string.h>

typedef struct {
    uint8_t *top;
} k_stack;

#pragma pack(push, 1)
typedef struct { uint8_t *base; int64_t len; } k_rhdr;
typedef struct { k_rhdr *hdr; k_stack *stk; } k_rref;
#pragma pack(pop)

/* Appends n bytes to the u8[>..] a builder reference points at: the
   resizable's elements top its stack, so the bytes go at the stack top and
   the header's count grows. */
static inline void k_bld_append(k_rref b, const void *p, int64_t n) {
    if (n <= 0) return;
    memcpy(b.stk->top, p, (size_t)n);
    b.stk->top += n;
    b.hdr->len += n;
}

#endif
