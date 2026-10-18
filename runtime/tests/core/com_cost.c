/* What a call through a slot of the interface layer costs (rt/com.h, COST).
 *
 * Three guest targets are called with rt_guest_call in a loop, on the test's one thread:
 *     a guest function that only returns          the cost of rt_guest_call itself
 *     a shim (rttest.dll!Negative)                 the dispatch lookup and a shim thunk
 *     a method (IRtTestShape::GetTag)              the same plus the call count and the body's
 *                                                  rt_com_state
 * The times are printed on the `com` channel (make rt-test SHIM=core/com_cost RT_TEST_ARGS=-v)
 * and nothing is asserted about them: the machine runs other builds.  What is checked is
 * that every call arrived and was counted.
 */
#include <time.h>

#include "rt/test.h"
#include "rttest_calls.h"
#include "rttest_com.h"
#include "rttest_com_calls.h"

GUEST_FN(only_returns) { ctx->r[0] = 7; GUEST_RET(ctx); }

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

TEST(core, com_cost)
{
    enum { N = 200000 };
    Ctx *ctx = T_CTX();
    IRtTestShape *shape = rt_com_new(&rttest_shape_class);
    T_EQ(COM_CALL(IRtTestShape, SetTag, shape, 7), S_OK);
    const uint64_t self = (uint64_t)(uintptr_t)shape;
    const uint64_t shim = rt_test_import_slot(RT_ID_rttest_Negative), zero[2] = { 0, 0 };
    uint64_t sum[3] = { 0, 0, 0 };
    double t[4];

    T_CASE("N calls of each target arrive");
    t[0] = now_ns();
    for (int i = 0; i < N; i++) sum[0] += rt_guest_call(ctx, T_GUEST_ADDR(only_returns), 1, &self);
    t[1] = now_ns();
    for (int i = 0; i < N; i++) sum[1] += rt_guest_call(ctx, shim, 2, zero) + 7;
    t[2] = now_ns();
    /* As lifted code calls a method: the target is read from the object every time. */
    for (int i = 0; i < N; i++) sum[2] += rt_guest_call(ctx, LD64(LD64(self) + 8 * RT_COM_SLOT_IRtTestShape_GetTag), 1, &self);
    t[3] = now_ns();
    T_EQ(sum[0], 7ull * N);
    T_EQ((uint32_t)sum[1], (uint32_t)(7ull * N));          /* Negative(0, 0) is 0 in eax; the upper half of rax is not defined */
    T_EQ(sum[2], 7ull * N);
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "GetTag"), N);
    rt_log(RT_CH_COM, RT_LL_INFO, "cost per call over %d calls: guest function %.1f ns, shim %.1f ns, method through a slot %.1f ns",
           N, (t[1] - t[0]) / N, (t[2] - t[1]) / N, (t[3] - t[2]) / N);
    T_EQ(COM_CALL(IRtTestShape, Release, shape), 0);
}
