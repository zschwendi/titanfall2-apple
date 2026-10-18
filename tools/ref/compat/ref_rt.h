/* What the Gate 2 call tables (gate2/calls.c, calls_more.c, calls_wide.c) use of our runtime,
 * given again for real Windows, so that the Windows reference program compiles the very same
 * tables: there a "guest" function is simply a function of the real DLL and a guest address
 * is its address.  Every header under compat/rt/ includes this file; the build puts compat/
 * before runtime/include, whose rt/wintypes.h and rt/win/ headers (type and constant
 * definitions only) are used as they are.  The functions are in tools/ref/refcalls.c.
 *
 * Only what the tables use is here.  A table that starts to use more of the runtime stops
 * `make ref-build` with an undeclared name, which is the moment to decide what that name
 * means on Windows.
 */
#ifndef REF_RT_H
#define REF_RT_H

#include "ref_libc.h"
#include "rt/wintypes.h"

/* ---- registers ------------------------------------------------------------------------------- */

typedef union { uint64_t q[2]; float f[4]; double pd[2]; } Xmm;

typedef struct RtThread { uint32_t id; } RtThread;

/* In the runtime: the guest register file of a thread.  Here it exists for two uses: a table
 * passes c->ctx to every call (ignored: the call is made on the calling thread), and a
 * HOST_FN body reads its arguments from it and leaves its result in r[0]. */
typedef struct Ctx {
    uint64_t r[16];             /* in a HOST_FN: r[1], r[2], r[8], r[9] are the first four arguments */
    uint64_t stack_arg[4];      /* arguments 4 to 7 */
    RtThread thread;
} Ctx;
#define RT_THREAD(ctx) (&(ctx)->thread)

static inline uint64_t ARG_U64(Ctx *ctx, unsigned n) {
    static const uint8_t reg[4] = { 1, 2, 8, 9 };
    return n < 4 ? ctx->r[reg[n]] : ctx->stack_arg[(n - 4) & 3];
}

/* A function of ours that the DLLs call: Windows x64, integer and pointer arguments.  The
 * body is the same as in the driver; the function around it is called natively. */
void ref_host_ctx(Ctx *ctx, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);
#define HOST_FN(name) \
    static void name##_body(Ctx *ctx); \
    static uint64_t name(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7) { \
        Ctx cx_; \
        ref_host_ctx(&cx_, a0, a1, a2, a3); \
        cx_.stack_arg[0] = a4; cx_.stack_arg[1] = a5; cx_.stack_arg[2] = a6; cx_.stack_arg[3] = a7; \
        name##_body(&cx_); \
        return cx_.r[0]; \
    } \
    static void name##_body(Ctx *ctx)

/* ---- modules ---------------------------------------------------------------------------------- */

typedef struct RtModule {
    const char *name;
    uint64_t    base;           /* the HMODULE */
    uint32_t    size;           /* SizeOfImage, from the loaded header */
} RtModule;

/* "tier0.dll", "vstdlib.dll", "kernel32.dll", "ntdll.dll": the loaded module, or NULL. */
RtModule *rt_module_find(const char *name);
/* GetProcAddress by name (`ordinal` is not used by the tables); 0 when there is none. */
GUESTFN   rt_module_export(RtModule *m, const char *name, uint32_t ordinal);
/* In the runtime: makes a guest address for a host function.  Here the function is one.
 * The tables make their stubs in a function the runtime calls at start-up
 * (gate2_wide_before_seal); refcalls.c's child_rank must call each such function too. */
#define rt_native_stub(module, name, fn) ((void)(module), (uint64_t)(uintptr_t)(fn))

/* ---- calling a function of the DLLs ----------------------------------------------------------- */

enum { RT_AC_U64 = 0, RT_AC_F32 = 1, RT_AC_F64 = 2 };
typedef struct {
    uint8_t cls;
    union { uint64_t u; float f; double d; } v;
} RtArg;
#define RT_ARG_U(x)  ((RtArg){ RT_AC_U64, { .u = (uint64_t)(x) } })
#define RT_ARG_F(x)  ((RtArg){ RT_AC_F32, { .f = (x) } })
#define RT_ARG_D(x)  ((RtArg){ RT_AC_F64, { .d = (x) } })

/* One C call returns one register, so unlike the runtime's this has no rax: a table that
 * needs rax from a call with float arguments does not compile here, and is then given a
 * second entry point rather than a wrong zero. */
typedef struct { Xmm xmm0; } RtGuestRet;

#define RT_GUEST_CALL_MAX_ARGS 16
RtGuestRet rt_guest_call_ex(Ctx *ctx, GUESTFN fn, const RtArg *args, unsigned nargs);
uint64_t   rt_guest_call(Ctx *ctx, GUESTFN fn, unsigned nargs, const uint64_t *args);
double     rt_guest_call_fp(Ctx *ctx, GUESTFN fn, const RtArg *args, unsigned nargs);

#define GUEST_CALL(ctx, fn, ...) \
    rt_guest_call((ctx), (fn), RT_NARGS_(__VA_ARGS__), \
                  &((const uint64_t[]){ 0 __VA_OPT__(,) RT_MAP_U64_(__VA_ARGS__) })[1])

#define RT_NARGS_(...)  RT_NARGS__(__VA_ARGS__ __VA_OPT__(,) 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define RT_NARGS__(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, n, ...) n
#define RT_CAT_(a, b)   RT_CAT__(a, b)
#define RT_CAT__(a, b)  a##b
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

/* ---- what the tables ask the runtime about the machine and the process ------------------------ */

/* The id of the thread the rank runs on (in the runtime a constant; here GetCurrentThreadId). */
extern uint32_t ref_first_thread_id;
#define RT_FIRST_THREAD_ID ref_first_thread_id

typedef struct RtMachine {
    uint32_t os_major;          /* GetVersionExA */
    uint32_t cpu_count;         /* GetSystemInfo: dwNumberOfProcessors */
    uint64_t cpu_hz;            /* 0: this program does not know a nominal clock speed */
    uint64_t phys_total;        /* GlobalMemoryStatusEx: ullTotalPhys */
} RtMachine;
const RtMachine *rt_machine(void);

/* Threads of this process (a tool-help snapshot).  A thread that has just ended can still be
 * listed for a moment, so when the count differs from the one returned last, it is taken
 * again for up to a quarter of a second before the new number is believed. */
unsigned rt_thread_count(void);
BOOL     rt_thread_get_exit_code(HANDLE thread, DWORD *code);   /* GetExitCodeThread */
BOOL     rt_handle_close(HANDLE h);                             /* CloseHandle */
uint64_t rt_tls_get(Ctx *ctx, DWORD index);                     /* TlsGetValue */
LONG     rt_time_zone_bias_minutes(void);                       /* GetTimeZoneInformation: Bias (run_ref.sh sets TZ=UTC) */

#endif /* REF_RT_H */
