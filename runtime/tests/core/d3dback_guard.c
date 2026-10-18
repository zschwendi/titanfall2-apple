/* One thread in the immediate context at a time (rt/d3dback.h, rule 8; rt/nulldev.h, NdGuard;
 * runtime/com/guard.def).  Expected values: the rule as those three state it.  The guard knows a
 * thread by its guest thread id, so a second thread is stood in for by giving the test's one
 * thread another id for a call: what is tested is the guard's bookkeeping, not a race. */
#include <stdio.h>
#include <stdlib.h>

#include "rt/test.h"
#include "rt/nulldev.h"
#include "rt/thread.h"

static void guard_cases(bool counting)
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    rt_nulldev_create(&device, &context);
    NdGuard *g = &((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->guard;
    RtThread *t = RT_THREAD(T_CTX());
    const uint32_t me = t->id, other = t->id + 4;
    const RtComMethod *flush = &rt_iface_ID3D11DeviceContext.methods[RT_COM_SLOT_ID3D11DeviceContext_Flush];
    uint64_t collisions = 99;

    T_CASE("one thread: every context call is counted for it, and nobody is inside afterwards");
    T_EQ(rt_nulldev_guard_stats(device, &collisions), 0);
    COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 3, 0, 0);
    COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 3, 0, 0);
    COM_CALL(ID3D11DeviceContext, Flush, context);
    T_EQ(rt_nulldev_guard_stats(device, &collisions), 1);
    T_EQ(collisions, 0);
    T_EQ(g->inside, 0);
    T_EQ(g->threads[0].id, me);
    T_EQ(g->threads[0].calls, 3);

    T_CASE("a second thread that calls while nobody is inside is a second thread and no collision");
    t->id = other;
    COM_CALL(ID3D11DeviceContext, Flush, context);
    t->id = me;
    T_EQ(rt_nulldev_guard_stats(device, &collisions), 2);
    T_EQ(collisions, 0);
    T_EQ(g->threads[1].id, other);
    T_EQ(g->threads[1].calls, 1);
    T_TRUE(T_LOGGED(RT_CH_COM, "the 2nd thread to call it"));

    T_CASE("a call made from inside a call, on the same thread, nests");
    RtShimFrame frame = { &flush->entry, 0, t->shim };  /* what the thunk of Flush would have pushed */
    t->shim = &frame;
    void *a = rt_nulldev_guard_enter(T_CTX(), context, flush);
    void *b = rt_nulldev_guard_enter(T_CTX(), context, flush);
    T_TRUE(a != NULL && a == b);
    T_EQ(g->depth, 2);
    T_EQ(g->inside, me);
    rt_nulldev_guard_leave(b);
    T_EQ(g->inside, me);
    rt_nulldev_guard_leave(a);
    T_EQ(g->inside, 0);

    T_CASE("a second thread that enters while one is inside");
    a = rt_nulldev_guard_enter(T_CTX(), context, flush);
    t->id = other;
    if (!counting) {
        char want[200];
        snprintf(want, sizeof want, "a second thread in the immediate context: thread 0x%x enters ID3D11DeviceContext::DrawIndexed "
                 "while thread 0x%x is inside ID3D11DeviceContext::Flush", (unsigned)other, (unsigned)me);
        T_TRAPS(COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 3, 0, 0), want);
    } else {
        COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 3, 0, 0);      /* counted, and the call goes on */
        COM_CALL(ID3D11DeviceContext, DrawIndexed, context, 3, 0, 0);
    }
    t->id = me;
    rt_nulldev_guard_stats(device, &collisions);
    T_EQ(collisions, counting ? 2 : 0);
    if (counting) T_TRUE(T_LOGGED(RT_CH_COM, "a second thread in the immediate context (counted, RT_D3D_GUARD=count)"));
    T_EQ(g->inside, me);                                /* the first thread is still inside, undisturbed */
    T_EQ(g->depth, 1);
    rt_nulldev_guard_leave(a);
    T_EQ(g->inside, 0);

    T_CASE("a call that was left without returning does not hold the guard for ever");
    a = rt_nulldev_guard_enter(T_CTX(), context, flush);
    T_EQ(g->inside, me);
    RtShimFrame later = { &flush->entry, 0, frame.prev };       /* `frame` is gone from the chain, as after an unwind */
    t->shim = &later;
    b = rt_nulldev_guard_enter(T_CTX(), context, flush);
    T_EQ(g->depth, 1);
    T_TRUE(T_LOGGED(RT_CH_COM, "without returning (an unwind); the guard of the context starts again"));
    rt_nulldev_guard_leave(b);
    T_EQ(g->inside, 0);
    t->shim = frame.prev;
    (void)a;
}

TEST_SETUP(core, d3dback_guard) { unsetenv("RT_D3D_GUARD"); }
TEST(core, d3dback_guard) { guard_cases(false); }

TEST_SETUP(core, d3dback_guard_count) { setenv("RT_D3D_GUARD", "count", 1); }
TEST(core, d3dback_guard_count) { guard_cases(true); }
