/* Calling guest code from host code (docs/RUNTIME.md section 3.5).  Interface: rt/shim.h. */
#include "core.h"

static uint64_t return_stub;

/* The stub's address is only ever a return address on the guest stack: the lifted callee's
 * RET pops it and returns to rt_guest_call_ex through the host stack.  Calling it is a bug. */
static void return_to_host(Ctx *ctx) {
    (void)ctx;
    rt_fatal("the return-to-host stub was called; it is a return address, not a function");
}

void rt_guest_call_init(void) {
    RtModule *ntdll = rt_module_find("ntdll.dll");
    if (!ntdll) rt_fatal("the manifest has no ntdll.dll to hold the return-to-host stub");
    return_stub = rt_native_stub(ntdll, "rt_return_to_host", return_to_host);
}

uint64_t rt_return_to_host_address(void) { return return_stub; }

RtGuestRet rt_guest_call_ex(Ctx *ctx, GUESTFN fn, const RtArg *args, unsigned nargs) {
    static const uint8_t reg_of[4] = { 1, 2, 8, 9 };            /* rcx, rdx, r8, r9 */
    RtThread *t = RT_THREAD(ctx);
    char where[160];

    if (!t || t != rt_thread_self())
        rt_fatal("rt_guest_call: the ctx does not belong to the calling thread, or the calling thread is "
                 "not a guest thread (rt_thread_attach first)");
    if (nargs > RT_GUEST_CALL_MAX_ARGS) rt_fatal("rt_guest_call: %u arguments, the limit is %d", nargs, RT_GUEST_CALL_MAX_ARGS);

    /* A fresh frame below the caller's rsp: 16-byte aligned before the push, home space for
     * four arguments whether or not they exist, arguments 4 and up above it. */
    const uint64_t saved_rsp = ctx->r[4];
    unsigned slots = nargs < 4 ? 4 : nargs;
    uint64_t sp = (saved_rsp - 8 * (uint64_t)slots) & ~UINT64_C(15);
    if (sp < t->stack_limit + 0x1000 || sp > t->stack_base)
        rt_fatal("rt_guest_call: guest stack overflow (rsp %#llx, stack %#llx-%#llx)", (unsigned long long)saved_rsp,
                 (unsigned long long)t->stack_limit, (unsigned long long)t->stack_base);

    for (unsigned i = 0; i < nargs; i++) {
        uint64_t bits = args[i].v.u;
        if (args[i].cls == RT_AC_F32)      { uint32_t b; __builtin_memcpy(&b, &args[i].v.f, 4); bits = b; }
        else if (args[i].cls == RT_AC_F64) { __builtin_memcpy(&bits, &args[i].v.d, 8); }
        if (i < 4) {
            /* A float goes in the xmm register of its position.  It is put in the integer
             * register as well, which is what a caller of a varargs function must do. */
            if (args[i].cls == RT_AC_F32 || args[i].cls == RT_AC_F64) {
                ctx->xmm[i].q[0] = bits;
                ctx->xmm[i].q[1] = 0;
            }
            ctx->r[reg_of[i]] = bits;
        } else {
            ST64(sp + 8 * (uint64_t)i, bits);
        }
    }
    sp -= 8;
    ST64(sp, return_stub);
    ctx->r[4] = sp;

    const uint32_t saved_in_host = t->in_host;
    t->in_host = 0;
    tf_fp_enter_guest(ctx->mxcsr);              /* lifted code runs with the guest FPCR */
    rt_lookup(ctx, fn)(ctx);
    tf_fp_enter_host();
    t->in_host = saved_in_host;

    if (ctx->r[4] != sp + 8)
        rt_fatal("guest function %s, called from host code, returned with rsp = %#llx where %#llx was expected "
                 "(off by %lld).  A longjmp, an exception unwind or a stack switch passed through a guest "
                 "call made by the runtime; that is not supported (docs/RUNTIME.md section 9.3)",
                 rt_module_describe(fn, where, sizeof where), (unsigned long long)ctx->r[4],
                 (unsigned long long)(sp + 8), (long long)(ctx->r[4] - (sp + 8)));

    RtGuestRet r = { ctx->r[0], ctx->xmm[0] };
    ctx->r[4] = saved_rsp;
    return r;
}

uint64_t rt_guest_call(Ctx *ctx, GUESTFN fn, unsigned nargs, const uint64_t *args) {
    RtArg a[RT_GUEST_CALL_MAX_ARGS];
    if (nargs > RT_GUEST_CALL_MAX_ARGS) rt_fatal("rt_guest_call: %u arguments, the limit is %d", nargs, RT_GUEST_CALL_MAX_ARGS);
    for (unsigned i = 0; i < nargs; i++) a[i] = RT_ARG_U(args[i]);
    return rt_guest_call_ex(ctx, fn, a, nargs).rax;
}

double rt_guest_call_fp(Ctx *ctx, GUESTFN fn, const RtArg *args, unsigned nargs) {
    return rt_guest_call_ex(ctx, fn, args, nargs).xmm0.pd[0];
}
