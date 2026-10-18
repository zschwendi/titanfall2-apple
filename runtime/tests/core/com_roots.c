/* The root interfaces (rt/com.h; runtime/com/gen_desc.py, `root`): an interface with no base has
 * no IUnknown, no IID and its own slots from 0.  The test set has one root, IRtTestRoot (two
 * methods, a body for the first, a stop for the second), and IRtTestLeaf, which derives from it
 * (its chain ends with the root, whose IID is none).  An object of class rttest_root_class has
 * that one face.  Its methods are called by the guest's slots; it ends from C, by rt_com_release,
 * which runs the class's destroy: nothing in the guest can release it.
 */
#include <string.h>

#include "rt/test.h"
#include "rttest_com.h"
#include "rttest_com_calls.h"

TEST(core, com_roots)
{
    static const GUID zero;
    int destroyed = 0;

    T_CASE("a root's description: no IID, an empty chain; a derived one's chain ends with the root");
    T_EQ(memcmp(&rt_iface_IRtTestRoot.iid, &zero, sizeof zero), 0);
    T_EQ(rt_iface_IRtTestRoot.nchain, 0);
    T_EQ(rt_iface_IRtTestRoot.nmethods, 2);
    T_EQ(strcmp(rt_iface_IRtTestRoot_methods[0].declared_by, "IRtTestRoot"), 0);
    T_EQ(rt_iface_IRtTestLeaf.nchain, 1);
    T_EQ(strcmp(rt_iface_IRtTestLeaf.chain[0].name, "IRtTestRoot"), 0);
    T_EQ(memcmp(&rt_iface_IRtTestLeaf.chain[0].iid, &zero, sizeof zero), 0);
    T_EQ(rt_iface_IRtTestLeaf.chain[0].nslots, 2);
    T_EQ(strcmp(rt_iface_IRtTestLeaf_methods[0].declared_by, "IRtTestRoot"), 0);
    T_EQ(strcmp(rt_iface_IRtTestLeaf_methods[2].declared_by, "IRtTestLeaf"), 0);

    T_CASE("the guest's call of slot 0 reaches the body");
    IRtTestRoot *obj = rt_com_new(&rttest_root_class);
    RtTestRootState *st = rt_com_state(T_CTX(), obj, &rttest_root_class);
    st->value = 42;
    st->destroyed = &destroyed;
    T_EQ(rt_com_refs(obj), 1);
    T_EQ(COM_CALL(IRtTestRoot, Value, obj), 42);

    T_CASE("a method with no body is the named stop");
    T_TRAPS(COM_CALL(IRtTestRoot, Stop, obj, 3), "FATAL unimplemented interface method IRtTestRoot::Stop");

    T_CASE("ending the object from C runs the destroy function, once, and the object is dead");
    T_EQ(destroyed, 0);
    T_EQ(rt_com_release(obj), 0);
    T_EQ(destroyed, 1);
    T_TRAPS(COM_CALL(IRtTestRoot, Value, obj), "which was destroyed");
}
