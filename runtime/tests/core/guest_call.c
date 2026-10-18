/* Calling guest code from host code (docs/RUNTIME.md section 3.5): rt_guest_call,
 * GUEST_CALL, rt_guest_call_fp, the rsp check, and a shim re-entered from a callback.
 */
#include "rt/test.h"
#include "rttest_calls.h"

static uint64_t seen_ret, seen_rsp, seen_fpcr, seen_in_host;

/* ULONGLONG weighted_sum(a, b, c, d, e, f): a guest function with six integer arguments. */
GUEST_FN(weighted_sum)
{
    uint64_t rsp = ctx->r[4];
    uint64_t a = ctx->r[1], b = ctx->r[2], c = ctx->r[8], d = ctx->r[9];
    uint64_t e = LD64(rsp + 0x28), f = LD64(rsp + 0x30);
    seen_ret = LD64(rsp);
    seen_rsp = rsp;
    seen_fpcr = tf_fpcr_read();
    seen_in_host = RT_THREAD(ctx)->in_host;
    for (int i = 0; i < 4; i++) ST64(rsp + 8 + 8 * (uint64_t)i, 0x5555555555555555);   /* home space is the callee's */
    ctx->r[1] = ctx->r[2] = ctx->r[8] = ctx->r[9] = ctx->r[10] = ctx->r[11] = 0xBADBADBADBAD;   /* so are these */
    ctx->r[0] = a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f;
    GUEST_RET(ctx);
}

/* double fp_callee(LONG n, double x, float y, LONG k, double z) */
GUEST_FN(fp_callee)
{
    double z;
    uint64_t zbits = LD64(ctx->r[4] + 0x28);
    memcpy(&z, &zbits, 8);
    ctx->xmm[0].pd[0] = (double)(int32_t)ctx->r[1] + ctx->xmm[1].pd[0] + (double)ctx->xmm[2].f[0]
                      + (double)(int32_t)ctx->r[9] + z;
    GUEST_RET(ctx);
}

TEST(rttest, CallBack)
{
    T_CASE("calls a guest function with six integer arguments through GUEST_CALL and returns its result");
    T_EQ(CALL(rttest, CallBack, T_GUEST_ADDR(weighted_sum), 1, 2, 3, 4, 5, 6), 1 + 4 + 9 + 16 + 25 + 36);
    /* What the callee saw: a real return address on the guest stack, the Windows stack
     * alignment at entry, the guest float mode, and the thread marked as in guest code. */
    T_EQ(seen_ret, rt_return_to_host_address());
    T_EQ(seen_rsp & 15, 8);
    T_EQ(seen_fpcr, tf_fpcr_for_mxcsr(T_CTX()->mxcsr));
    T_EQ(seen_in_host, 0);
    char where[64];
    T_TRUE(!strcmp(rt_module_describe(seen_ret, where, sizeof where), "ntdll.dll!rt_return_to_host"));
}

TEST(rttest, CallBackFp)
{
    T_CASE("calls a guest function with mixed integer, float and double arguments and gets a double back");
    T_EQ(CALL(rttest, CallBackFp, T_GUEST_ADDR(fp_callee), -2, 0.5, 0.25f, 8.0), -2.0 + 0.5 + 0.25 + 1000.0 + 8.0);
}

TEST(core, guest_call_from_host_code)
{
    /* The driver's use (appendix A): host code that is not inside a shim calls a guest function. */
    Ctx *ctx = T_CTX();
    const uint64_t rsp = ctx->r[4];
    const uint64_t args[6] = { 6, 5, 4, 3, 2, 1 };
    T_EQ(rt_guest_call(ctx, T_GUEST_ADDR(weighted_sum), 6, args), 6 + 10 + 12 + 12 + 10 + 6);
    T_EQ(ctx->r[4], rsp);                           /* the caller's rsp is restored */
    T_TRUE(seen_rsp < rsp);                         /* the frame was built below it */
    T_EQ(tf_fpcr_read(), TF_FPCR_HOST);             /* and host code is back in the host float mode */
    T_EQ(GUEST_CALL(ctx, T_GUEST_ADDR(weighted_sum), 1, 1, 1, 1, 1, 1), 21);
    T_EQ(GUEST_CALL(ctx, T_GUEST_ADDR(weighted_sum), 2, 0, 0, 0, 0, 0), 2);

    const RtArg fp[5] = { RT_ARG_U(1), RT_ARG_D(2.0), RT_ARG_F(3.0f), RT_ARG_U(4), RT_ARG_D(5.0) };
    T_EQ(rt_guest_call_fp(ctx, T_GUEST_ADDR(fp_callee), fp, 5), 15.0);
    RtGuestRet r = rt_guest_call_ex(ctx, T_GUEST_ADDR(fp_callee), fp, 5);
    T_EQ(r.xmm0.pd[0], 15.0);
}

/* ---- the rsp check --------------------------------------------------------------------------- */

GUEST_FN(returns_without_popping) { (void)ctx; }
GUEST_FN(pops_sixteen)            { ctx->r[4] += 16; }

TEST(core, guest_call_checks_rsp)
{
    /* A callee that does not come back with the frame it was given is what a longjmp or an
     * unwind through a callback looks like: fatal, with the callee named (plan A, 9.3). */
    T_TRAPS(GUEST_CALL(T_CTX(), T_GUEST_ADDR(returns_without_popping)),
            "guest function rttest.dll!returns_without_popping, called from host code, returned with rsp");
    T_TRAPS(GUEST_CALL(T_CTX(), T_GUEST_ADDR(pops_sixteen)), "(off by 8)");
    T_TRAPS(CALL(rttest, CallBack, T_GUEST_ADDR(returns_without_popping), 0, 0, 0, 0, 0, 0), "(off by -8)");
}

TEST(core, guest_call_misuse)
{
    /* A guest address with no function behind it is a dispatch miss like any other. */
    T_TRAPS(GUEST_CALL(T_CTX(), 0x1234), "FATAL dispatch miss: call to guest address 0x1234");
    /* The return-to-host stub is a return address, not a function. */
    T_TRAPS(GUEST_CALL(T_CTX(), rt_return_to_host_address()), "the return-to-host stub was called");
    /* A ctx that is not the calling thread's. */
    Ctx other = *T_CTX();
    other.host = NULL;
    T_TRAPS(GUEST_CALL(&other, T_GUEST_ADDR(weighted_sum)), "does not belong to the calling thread");
    uint64_t many[17] = { 0 };
    T_TRAPS(rt_guest_call(T_CTX(), T_GUEST_ADDR(weighted_sum), 17, many), "17 arguments");
}

/* ---- re-entering a shim from a callback ---------------------------------------------------- */

static int   nested_ok;
static DWORD poked;

/* A guest function that does what lifted code does for `call [rip+slot]`: push the return
 * address and go through the dispatch table to a shim (rttest!Poke(&poked, 1234)). */
GUEST_FN(calls_a_shim)
{
    const uint64_t entry = ctx->r[4];
    ctx->r[4] -= 0x28;                              /* home space; rsp is 16-byte aligned again */
    ctx->r[1] = (uint64_t)(uintptr_t)&poked;        /* rcx */
    ctx->r[2] = 0xFEEDFACE00000000 | 1234;          /* rdx, with garbage above the DWORD */
    ctx->r[4] -= 8;
    ST64(ctx->r[4], T_GUEST_ADDR(calls_a_shim) + 1);
    rt_dispatch(ctx, rt_test_import_slot(RT_ID_rttest_Poke));
    nested_ok = ctx->r[4] == entry - 0x28
             && RT_THREAD(ctx)->in_host == 0                            /* back in guest code */
             && tf_fpcr_read() == tf_fpcr_for_mxcsr(ctx->mxcsr)         /* in the guest float mode */
             && RT_THREAD(ctx)->shim != NULL                            /* still inside the outer shim */
             && !strcmp(RT_THREAD(ctx)->shim->shim->name, "CallBack");
    ctx->r[4] += 0x28;
    ctx->r[0] = 77;
    GUEST_RET(ctx);
}

TEST(core, shim_reentered_from_callback)
{
    T_EQ(CALL(rttest, CallBack, T_GUEST_ADDR(calls_a_shim), 0, 0, 0, 0, 0, 0), 77);
    T_EQ(poked, 1234);
    T_TRUE(nested_ok);
    T_TRUE(RT_THREAD(T_CTX())->shim == NULL);
}
