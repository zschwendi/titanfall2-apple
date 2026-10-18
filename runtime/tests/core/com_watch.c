/* A driver's watch on one method of the interface layer (rt/com.h, rt_com_watch): its function
 * runs in that method's thunk for every call, with the number of the call, before the body;
 * no other method reaches it; the method's own answer is untouched; one watch per process.
 *
 * Expected values: the calls this test makes, and rt/com.h's statement of the watch.
 */
#include "rt/test.h"
#include "rttest_com.h"
#include "rttest_com_calls.h"

static uint64_t seen[8];
static unsigned nseen;
static const RtComMethod *seen_method;
static uint64_t counted_at_watch;

static void on_touch(Ctx *ctx, const RtComMethod *m, uint64_t nth) {
    (void)ctx;
    if (nseen < 8) seen[nseen] = nth;
    nseen++;
    seen_method = m;
    counted_at_watch = rt_com_calls(&rt_iface_IRtTestShape, "Touch");      /* counted before the watch runs */
}

static void other(Ctx *ctx, const RtComMethod *m, uint64_t nth) { (void)ctx; (void)m; (void)nth; }

TEST(core, com_watch)
{
    IRtTestShape *shape = rt_com_new(&rttest_shape_class);

    T_CASE("with no watch nothing runs");
    COM_CALL(IRtTestShape, Touch, shape, 1);
    T_EQ(nseen, 0);

    T_CASE("the watch runs at every call of its method, with the call's number, after the count");
    rt_com_watch(&rt_iface_IRtTestShape, "Touch", on_touch);
    T_EQ(COM_CALL(IRtTestShape, Touch, shape, 2), 0);                      /* accept: S_OK, as without a watch */
    T_EQ(nseen, 1);
    T_EQ(seen[0], 2);                                                      /* the second call of the process */
    T_EQ(counted_at_watch, 2);
    T_TRUE(seen_method && !strcmp(seen_method->name, "Touch") && seen_method->iface == &rt_iface_IRtTestShape);
    T_EQ(COM_CALL(IRtTestShape, Touch, shape, 3), 0);
    T_EQ(nseen, 2);
    T_EQ(seen[1], 3);

    T_CASE("no other method reaches it");
    const UINT tag = COM_CALL(IRtTestShape, GetTag, shape);
    T_EQ(COM_CALL(IRtTestShape, GetTag, shape), tag);                      /* a body's answer is its own */
    COM_CALL(IRtTestShape, AddRef, shape);
    COM_CALL(IRtTestShape, Release, shape);
    T_EQ(nseen, 2);
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "Touch"), 3);
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "GetTag"), 2);

    T_CASE("one watch per process; a method the interface lacks is fatal");
    T_TRAPS(rt_com_watch(&rt_iface_IRtTestShape, "GetTag", other), "a method is watched already");
    T_TRAPS(rt_com_watch(&rt_iface_IRtTestShape, "NoSuchMethod", other), "has no method NoSuchMethod");
    COM_CALL(IRtTestShape, Touch, shape, 4);                               /* and the first watch is still the one */
    T_EQ(nseen, 3);
    T_EQ(seen[2], 4);
    COM_CALL(IRtTestShape, Release, shape);
}
