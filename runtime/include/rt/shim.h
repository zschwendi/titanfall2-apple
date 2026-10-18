/* What a shim is written against (docs/RUNTIME.md section 3).
 *
 * A shim is one host C function that stands in for one imported Windows function.  You
 * write a typed function introduced by a one-line SHIM declaration; the generator
 * (runtime/tools/gen_shims.py) reads that line and writes the thunk that fetches the
 * arguments from the guest registers and stack, and the table row that registers the shim.
 * You never marshal by hand and never register anything.  See runtime/README.md.
 *
 *     SHIM(kernel32, BOOL, ReadFile, HANDLE hFile, LPVOID buf, DWORD len, LPDWORD got, LPOVERLAPPED ov)
 *     {
 *         ...                                  // `ctx` is in scope
 *         return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE);
 *     }
 *
 * Rules of the SHIM line: it is ONE line; a function without parameters is written
 * SHIM(lib, ret, Name); every parameter type and the return type must be in
 * runtime/tools/types.def (or be `T *` for a T that is); long, unsigned long, wchar_t and
 * size_t are forbidden.
 */
#ifndef RT_SHIM_H
#define RT_SHIM_H

#include <string.h>

#include "runtime.h"            /* Ctx, LD*, ST*: the lifter contract, never redeclared here */
#include "fpmode.h"             /* float mode at the boundary (section 3.6) */
#include "rt/wintypes.h"
#include "rt/win/error.h"
#include "rt/win/base.h"
#include "rt/win/teb.h"
#include "rt/log.h"
#include "rt/thread.h"
#include "rt/module.h"

/* ---- declaring a shim -------------------------------------------------------------------- */

/* The typed implementation.  The generated thunk shim_<lib>_<name> calls it. */
#define SHIM(lib, ret, name, ...) \
    ret impl_##lib##_##name(Ctx *ctx __attribute__((unused)) __VA_OPT__(,) __VA_ARGS__)

/* The same for a function the DLL also (or only) exports by ordinal `ord`.  The file is
 * named ord<number>_<name>.c. */
#define SHIM_ORD(lib, ord, ret, name, ...)  SHIM(lib, ret, name __VA_OPT__(,) __VA_ARGS__)

/* Varargs functions only: you fetch the arguments yourself with ARG_* and set the result
 * with RET_*.  The generated thunk still does everything else (float mode, trace, the
 * return address), so the body must leave ctx->r[4] alone. */
#define SHIM_RAW(lib, name)  void raw_##lib##_##name(Ctx *ctx __attribute__((unused)))

/* ---- raw access to arguments and results (generated thunks and SHIM_RAW bodies) ---------- */
/* n counts from 0.  Valid only while ctx->r[4] still has its value on entry, that is
 * before any GUEST_CALL.  Windows x64: arguments 0 to 3 are in rcx, rdx, r8, r9 or, for
 * float and double, in xmm0 to xmm3 chosen by position; argument n >= 4 is the 8 bytes at
 * rsp + 0x28 + 8*(n-4).  The upper bits of an argument narrower than 64 bits are
 * undefined: always cast to the declared width. */

TF_INLINE uint64_t ARG_U64(Ctx *ctx, unsigned n) {
    switch (n) {
    case 0: return ctx->r[1];
    case 1: return ctx->r[2];
    case 2: return ctx->r[8];
    case 3: return ctx->r[9];
    default: return LD64(ctx->r[4] + 0x28 + 8 * (uint64_t)(n - 4));
    }
}
TF_INLINE float ARG_F32(Ctx *ctx, unsigned n) {
    if (n < 4) return ctx->xmm[n].f[0];
    uint32_t v = LD32(ctx->r[4] + 0x28 + 8 * (uint64_t)(n - 4));
    float f; __builtin_memcpy(&f, &v, 4); return f;
}
TF_INLINE double ARG_F64(Ctx *ctx, unsigned n) {
    if (n < 4) return ctx->xmm[n].pd[0];
    uint64_t v = LD64(ctx->r[4] + 0x28 + 8 * (uint64_t)(n - 4));
    double d; __builtin_memcpy(&d, &v, 8); return d;
}

#define RET_U64(ctx, v)  ((ctx)->r[0] = (uint64_t)(v))
#define RET_U32(ctx, v)  ((ctx)->r[0] = (uint64_t)(uint32_t)(v))      /* upper half written as zero */
TF_INLINE void RET_F32(Ctx *ctx, float v)  { ctx->xmm[0].q[0] = 0; ctx->xmm[0].q[1] = 0; ctx->xmm[0].f[0] = v; }
TF_INLINE void RET_F64(Ctx *ctx, double v) { ctx->xmm[0].q[1] = 0; ctx->xmm[0].pd[0] = v; }

/* ---- last error (section 3.4) -------------------------------------------------------------- */
/* Lives in the guest thread block at the Windows offset, where inlined guest code looks.
 * Set it on failure and leave it alone on success, unless the Microsoft documentation of
 * the function says otherwise. */

TF_INLINE void  rt_set_last_error(Ctx *ctx, DWORD e) { ST32(ctx->gs_base + RT_TEB_LAST_ERROR, e); }
TF_INLINE DWORD rt_get_last_error(Ctx *ctx)          { return LD32(ctx->gs_base + RT_TEB_LAST_ERROR); }

/* return FAIL(ctx, ERROR_INVALID_HANDLE, FALSE); */
#define FAIL(ctx, err, retval)   (rt_set_last_error((ctx), (err)), (retval))

/* Which host call failed, for rt_win32_from_errno: the same errno means different Win32
 * errors in different places (ENOENT is ERROR_FILE_NOT_FOUND for a file and
 * ERROR_PATH_NOT_FOUND for a directory). */
typedef enum {
    RT_ERR_FILE = 1,      /* an operation on a file: open, read, write, seek, stat, unlink, rename */
    RT_ERR_DIR,           /* an operation on a directory: mkdir, rmdir, opendir, chdir */
    RT_ERR_MEMORY,        /* mmap, mprotect, allocation */
    RT_ERR_THREAD,        /* thread and synchronisation calls */
    RT_ERR_GENERIC        /* anything else */
} RtErrContext;

/* The Win32 error for a failed host call.  The ONLY place errno is mapped: a shim never
 * invents the mapping (`make shim-check` rejects errno constants in shim files).  An errno
 * with no mapping for the context is fatal, so that the table is extended deliberately
 * (runtime/core/error.c). */
DWORD rt_win32_from_errno(int e, RtErrContext what);

/* ---- calling guest code from host code (section 3.5) -------------------------------------- */

typedef struct {
    uint8_t cls;                                  /* RT_AC_F32, RT_AC_F64, or anything else for an integer */
    union { uint64_t u; float f; double d; } v;
} RtArg;
#define RT_ARG_U(x)  ((RtArg){ RT_AC_U64, { .u = (uint64_t)(x) } })
#define RT_ARG_F(x)  ((RtArg){ RT_AC_F32, { .f = (x) } })
#define RT_ARG_D(x)  ((RtArg){ RT_AC_F64, { .d = (x) } })

typedef struct { uint64_t rax; Xmm xmm0; } RtGuestRet;

#define RT_GUEST_CALL_MAX_ARGS 16

/* Call the guest function at `fn` on the calling thread's guest stack and return when it
 * returns.  Builds a fresh frame below the current guest rsp (16-byte aligned, 32 bytes of
 * home space, arguments 4 and up above it), loads the argument registers, pushes the
 * address of the return-to-host stub as the return address, switches to the guest float
 * mode, calls through the dispatch table, checks that rsp came back, restores the caller's
 * rsp and the host float mode.
 *
 * Rules:
 *  1. `ctx` must be the calling thread's own, and that thread must be a guest thread.
 *  2. Hold no runtime lock across the call except the loader lock: the guest may re-enter
 *     any shim.
 *  3. Afterwards every volatile guest register is garbage (so ARG_* is no longer valid);
 *     your C locals are intact.
 *  4. If the callee unwinds past the caller (an exception or longjmp through a callback)
 *     the rsp check is fatal.  That is plan A (section 9.3).
 */
RtGuestRet rt_guest_call_ex(Ctx *ctx, GUESTFN fn, const RtArg *args, unsigned nargs);

/* Integer and pointer arguments only; returns rax. */
uint64_t rt_guest_call(Ctx *ctx, GUESTFN fn, unsigned nargs, const uint64_t *args);

/* Mixed argument classes; returns xmm0 as a double.  For a callee that returns float or an
 * integer use rt_guest_call_ex. */
double rt_guest_call_fp(Ctx *ctx, GUESTFN fn, const RtArg *args, unsigned nargs);

/* GUEST_CALL(ctx, fn, a, b, ...): up to 12 integer or pointer arguments, each cast to
 * uint64_t; evaluates to rax.  For DllMain, thread start routines, window procedures,
 * enumeration callbacks, comparators and the like. */
#define GUEST_CALL(ctx, fn, ...) \
    rt_guest_call((ctx), (fn), RT_NARGS_(__VA_ARGS__), \
                  &((const uint64_t[]){ 0 __VA_OPT__(,) RT_MAP_U64_(__VA_ARGS__) })[1])

/* The guest address pushed as the return address by rt_guest_call.  It lies in ntdll.dll's
 * native module, so a guest stack walker sees a code address inside a module.  It is never
 * called. */
uint64_t rt_return_to_host_address(void);

/* ---- the thunk's bookkeeping (generated code only) ---------------------------------------- */

typedef struct RtShimFrame {
    const RtShimEntry  *shim;
    uint64_t            entry_rsp;      /* guest rsp on entry: the address of the return address */
    struct RtShimFrame *prev;           /* the shim this one was (indirectly) called from, or NULL */
} RtShimFrame;

void rt_trace_call(Ctx *ctx, const RtShimFrame *f);                 /* runtime/core/trace.c */
void rt_trace_ret(Ctx *ctx, const RtShimFrame *f);
_Noreturn void rt_shim_bad_rsp(Ctx *ctx, const RtShimFrame *f);     /* runtime/core/trap.c */

TF_INLINE void rt_shim_enter(Ctx *ctx, const RtShimEntry *e, RtShimFrame *f) {
    tf_fp_enter_host();                           /* host code runs with the host FPCR */
    RtThread *t = RT_THREAD(ctx);
    f->shim = e;
    f->entry_rsp = ctx->r[4];
    f->prev = t->shim;
    t->shim = f;
    t->in_host = 1;
    if (__builtin_expect(RT_LOG_ON(RT_CH_TRACE, RT_LL_DEBUG), 0)) rt_trace_call(ctx, f);
}
TF_INLINE void rt_shim_leave(Ctx *ctx, RtShimFrame *f) {
    RtThread *t = RT_THREAD(ctx);
    if (__builtin_expect(ctx->r[4] != f->entry_rsp, 0)) rt_shim_bad_rsp(ctx, f);
    if (__builtin_expect(RT_LOG_ON(RT_CH_TRACE, RT_LL_DEBUG), 0)) rt_trace_ret(ctx, f);
    t->shim = f->prev;
    t->in_host = 0;
    ctx->r[4] += 8;                               /* pop the return address: the guest `ret` */
    tf_fp_enter_guest(ctx->mxcsr);                /* lifted code runs with the guest FPCR */
}
#define RT_SHIM_ENTER(ctx, id)  RtShimFrame rt_frame_; rt_shim_enter((ctx), (id), &rt_frame_)
#define RT_SHIM_LEAVE(ctx)      rt_shim_leave((ctx), &rt_frame_)

/* The row of the innermost shim the thread is in, or NULL. */
TF_INLINE const RtShimEntry *rt_current_shim(Ctx *ctx) {
    RtShimFrame *f = RT_THREAD(ctx)->shim;
    return f ? f->shim : NULL;
}

/* ---- macro plumbing ------------------------------------------------------------------------- */

/* Number of arguments, 0 to 12. */
#define RT_NARGS_(...)  RT_NARGS__(__VA_ARGS__ __VA_OPT__(,) 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define RT_NARGS__(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, n, ...) n
#define RT_CAT_(a, b)   RT_CAT__(a, b)
#define RT_CAT__(a, b)  a##b
/* (uint64_t)(a), (uint64_t)(b), ... */
#define RT_U64_(x)      (uint64_t)(x)
#define RT_MAP_U64_(...)       RT_CAT_(RT_MAP_U64_, RT_NARGS_(__VA_ARGS__))(__VA_ARGS__)
#define RT_MAP_U64_0()
#define RT_MAP_U64_1(a)        RT_U64_(a)
#define RT_MAP_U64_2(a, ...)   RT_U64_(a), RT_MAP_U64_1(__VA_ARGS__)
#define RT_MAP_U64_3(a, ...)   RT_U64_(a), RT_MAP_U64_2(__VA_ARGS__)
#define RT_MAP_U64_4(a, ...)   RT_U64_(a), RT_MAP_U64_3(__VA_ARGS__)
#define RT_MAP_U64_5(a, ...)   RT_U64_(a), RT_MAP_U64_4(__VA_ARGS__)
#define RT_MAP_U64_6(a, ...)   RT_U64_(a), RT_MAP_U64_5(__VA_ARGS__)
#define RT_MAP_U64_7(a, ...)   RT_U64_(a), RT_MAP_U64_6(__VA_ARGS__)
#define RT_MAP_U64_8(a, ...)   RT_U64_(a), RT_MAP_U64_7(__VA_ARGS__)
#define RT_MAP_U64_9(a, ...)   RT_U64_(a), RT_MAP_U64_8(__VA_ARGS__)
#define RT_MAP_U64_10(a, ...)  RT_U64_(a), RT_MAP_U64_9(__VA_ARGS__)
#define RT_MAP_U64_11(a, ...)  RT_U64_(a), RT_MAP_U64_10(__VA_ARGS__)
#define RT_MAP_U64_12(a, ...)  RT_U64_(a), RT_MAP_U64_11(__VA_ARGS__)

#endif /* RT_SHIM_H */
