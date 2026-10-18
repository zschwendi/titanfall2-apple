/* What the tests of the placeholder user's stand-ins (8 on) share.  Included after
 * origin_names.h and rt/standin.h, once per test file.
 *
 * A test's runtime has no engine.dll: rttest.dll stands for it, the harness's return stub for
 * the stand-in's return address, and the test's guest function `kit_cb` for the engine's
 * callback of that request (rt_standin56_test_module, rt_standin56_test_return_rva,
 * rt_standin7_test_callback_rva: rt/standin.h).  kit_cb counts its calls: "never completes"
 * means the count stays 0. */
#ifndef ORIGIN_PLACEHOLDER_H
#define ORIGIN_PLACEHOLDER_H

static uint64_t op_seen_return;
GUEST_FN(op_where_from) { op_seen_return = LD64(ctx->r[4]); ctx->r[0] = 0; GUEST_RET(ctx); }

static unsigned op_cb_calls;
GUEST_FN(kit_cb) { op_cb_calls++; ctx->r[0] = 0; GUEST_RET(ctx); }

/* Name the stand-ins of the engine; the placeholder is NOT selected here.  The callback's address. */
static inline uint64_t op_stage(void) {
    RtModule *N = rt_module_find("rttest.dll");
    rt_test_invoke(T_GUEST_ADDR(op_where_from), 0, NULL, NULL);
    rt_standin56_test_module = "rttest.dll";
    rt_standin56_test_return_rva = (uint32_t)(op_seen_return - N->base);
    rt_standin7_test_callback_rva = (uint32_t)(T_GUEST_ADDR(kit_cb) - N->base);
    return T_GUEST_ADDR(kit_cb);
}

/* A call with `n` whole-register arguments (5 and up on the stack), as the engine makes it. */
static inline RtTestRet op_ask(uint64_t target, unsigned n, const uint64_t *v) {
    RtTestArg a[12];
    for (unsigned i = 0; i < n && i < 12; i++) a[i] = (RtTestArg){ RT_AC_U64, 8, { .u = v[i] } };
    const char *why = NULL;
    RtTestRet r = rt_test_invoke(target, n, a, &why);
    if (r.violations) rt_test_fail(__FILE__, __LINE__, "the stand-in broke the guest ABI: %s", why ? why : "?");
    return r;
}
#define OP_ASK(target, ...) op_ask((target), sizeof (uint64_t[]){ __VA_ARGS__ } / 8, (uint64_t[]){ __VA_ARGS__ })

/* Every import of originsdk.dll but `mine` halts by call and stops at the look-up by name: how many did. */
#define OP_OTHERS_HALT(ctx, mine, count) do { \
        RtModule *op_O = rt_module_find("originsdk.dll"), *op_A = rt_module_load_a((ctx), "t_lift_a.dll", 0); \
        T_REQUIRE(op_O != NULL && op_A != NULL); \
        char op_want[200]; \
        (count) = 0; \
        for (unsigned op_i = 0; op_i < ORIGIN_NAMES; op_i++) { \
            if (!strcmp(origin_names[op_i], (mine))) continue; \
            T_TRAPS(rt_test_invoke(LD64(op_A->base + ORIGIN_SLOT(op_i)), 0, NULL, NULL), origin_halt_text(op_i)); \
            snprintf(op_want, sizeof op_want, "FATAL unimplemented import GetProcAddress(originsdk.dll, %s)\n", origin_names[op_i]); \
            T_TRAPS(CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)op_O->base, origin_names[op_i]), op_want); \
            (count)++; \
        } \
    } while (0)

#endif
