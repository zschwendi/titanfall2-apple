/* Holds (rt/com.h, "references and holds"): references of the runtime's own objects on each
 * other, which keep an object alive and which the guest's AddRef and Release do not report.
 *
 * Where the expected values come from: the rule itself is Windows' behaviour as Wine's d3d11
 * conformance tests show it (docs/reviews/d3d-contract-check-2.md, F3: a texture's count is the
 * same before and after a view of it is created; a swap chain's buffer has count 1 after
 * GetBuffer, also with views on it); the numbers below are that rule applied to counts this
 * test makes itself (1 at creation, plus and minus one per call). */
#include "rt/test.h"
#include "rttest_com.h"
#include "rttest_com_calls.h"

TEST(core, com_holds)
{
    int destroyed = 0;
    IRtTestShape *shape = rt_com_new(&rttest_shape_class);
    ((RtTestShapeState *)rt_com_state_of(shape, &rttest_shape_class))->destroyed = &destroyed;

    T_CASE("a hold keeps the object alive and counts in rt_com_refs, and the guest's AddRef and Release do not report it");
    T_EQ(rt_com_hold(shape), 2);
    T_EQ(rt_com_hold(shape), 3);
    T_EQ(rt_com_refs(shape), 3);
    T_EQ(rt_com_public_refs(shape), 1);
    T_EQ(COM_CALL(IRtTestShape, AddRef, shape), 2);
    T_EQ(COM_CALL(IRtTestShape, Release, shape), 1);
    T_EQ(rt_com_refs(shape), 3);

    T_CASE("the application's last Release returns 0 and the held object lives on; AddRef finds it alive");
    T_EQ(COM_CALL(IRtTestShape, Release, shape), 0);
    T_EQ(destroyed, 0);
    T_TRUE(rt_com_class_of(shape) == &rttest_shape_class);
    T_EQ(rt_com_refs(shape), 2);
    T_EQ(rt_com_public_refs(shape), 0);
    T_EQ(COM_CALL(IRtTestShape, AddRef, shape), 1);
    T_EQ(COM_CALL(IRtTestShape, Release, shape), 0);

    T_CASE("a Release the application has no reference for is a stop, and a host release is fatal");
    T_TRAPS(COM_CALL(IRtTestShape, Release, shape), "to which the application holds no reference");
    T_TRAPS(rt_com_release(shape), "has no reference left but the runtime's own 2 hold(s)");
    T_EQ(rt_com_refs(shape), 2);

    T_CASE("the last unhold destroys the object once");
    T_EQ(rt_com_unhold(shape), 1);
    T_EQ(destroyed, 0);
    T_EQ(rt_com_unhold(shape), 0);
    T_EQ(destroyed, 1);
    T_TRUE(rt_com_class_of(shape) == NULL);

    T_CASE("an object whose holds end first dies at the application's last Release");
    IRtTestShape *second = rt_com_new(&rttest_shape_class);
    ((RtTestShapeState *)rt_com_state_of(second, &rttest_shape_class))->destroyed = &destroyed;
    rt_com_hold(second);
    T_EQ(rt_com_unhold(second), 1);
    T_EQ(destroyed, 1);
    T_EQ(COM_CALL(IRtTestShape, Release, second), 0);
    T_EQ(destroyed, 2);

    T_CASE("rt_com_unhold with no hold releases a plain reference and says so");
    IRtTestShape *third = rt_com_new(&rttest_shape_class);
    rt_com_addref(third);
    T_EQ(rt_com_unhold(third), 1);
    T_TRUE(T_LOGGED(RT_CH_COM, "has no hold; a plain reference is released instead"));
    T_EQ(rt_com_public_refs(third), 1);
}
