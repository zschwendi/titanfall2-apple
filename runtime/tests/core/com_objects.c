/* The interface layer (rt/com.h): objects, vtables, the three methods of IUnknown, and what a
 * call through a slot arrives at.  Everything goes through the test-only interfaces of
 * runtime/tests/com (IRtTestShape, which derives from IRtTestNamed, and IRtTestCounter) and
 * the class RtTestShape, one object with both faces (runtime/tests/abi/rttest_com.h).
 *
 * Where the expected values come from: the arithmetic of the test's own method bodies
 * (runtime/tests/com/bodies), the IIDs written in the two descriptions, and COM's rules for
 * IUnknown as Microsoft states them (learn.microsoft.com, "IUnknown::QueryInterface" and
 * "Using and Implementing IUnknown": from memory, not read for this test): a query for
 * IUnknown gives the same pointer through every interface of an object; AddRef and Release
 * return the new count; a successful QueryInterface adds a reference.
 */
#include "rt/test.h"
#include "rttest_com.h"
#include "rttest_com_calls.h"
#include "rt/nulldev.h"

static const GUID iid_unknown = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID iid_named   = { 0x7e57c0de, 0x0001, 0x4000, { 0x80, 0x00, 0, 0, 0, 0, 0xc0, 0xde } };
static const GUID iid_shape   = { 0x7e57c0de, 0x0002, 0x4000, { 0x80, 0x00, 0, 0, 0, 0, 0xc0, 0xde } };
static const GUID iid_counter = { 0x7e57c0de, 0x0003, 0x4000, { 0x80, 0x00, 0, 0, 0, 0, 0xc0, 0xde } };
static const GUID iid_nobody  = { 0x12345678, 0x9abc, 0xdef0, { 1, 2, 3, 4, 5, 6, 7, 8 } };

TEST(core, com_objects)
{
    char where[160];
    int destroyed = 0;

    T_CASE("an object begins with the address of its interface's vtable, and every slot is a stub of the owner");
    IRtTestShape *shape = rt_com_new(&rttest_shape_class);
    RtTestShapeState *st = rt_com_state(T_CTX(), shape, &rttest_shape_class);
    st->destroyed = &destroyed;
    T_REQUIRE(rt_iface_IRtTestShape.vtable != 0);
    T_EQ(LD64((uint64_t)(uintptr_t)shape), rt_iface_IRtTestShape.vtable);
    T_TRUE(rt_module_from_address(rt_iface_IRtTestShape.vtable) == rt_module_find("rttest.dll"));
    T_EQ(rt_iface_IRtTestShape.vtable - rt_module_find("rttest.dll")->base >= RT_NATIVE_VTABLES, 1);
    T_EQ(rt_iface_IRtTestShape.nmethods, 10);
    for (unsigned s = 0; s < rt_iface_IRtTestShape.nmethods; s++) {
        uint64_t stub = LD64(rt_iface_IRtTestShape.vtable + 8 * s);
        T_TRUE(rt_disp_find(stub) == rt_iface_IRtTestShape_methods[s].entry.thunk);
    }
    T_EQ(strcmp(rt_module_describe(LD64(rt_iface_IRtTestShape.vtable + 8 * RT_COM_SLOT_IRtTestShape_Mix), where, sizeof where),
                "rttest.dll!IRtTestShape::Mix"), 0);
    /* The runtime's own set is registered the same way, in d3d11.dll. */
    T_EQ(strcmp(rt_module_describe(LD64(rt_iface_ID3D11Device.vtable + 8 * RT_COM_SLOT_ID3D11Device_CreateBuffer), where, sizeof where),
                "d3d11.dll!ID3D11Device::CreateBuffer"), 0);
    T_TRUE(rt_com_class_of(shape) == &rttest_shape_class);
    T_TRUE(rt_com_class_of(&destroyed) == NULL);
    T_EQ(rt_com_refs(shape), 1);

    T_CASE("a call through a slot reaches the method's body with its arguments");
    T_EQ(COM_CALL(IRtTestShape, SetTag, shape, 7), S_OK);          /* the body is the base's (IRtTestNamed) */
    T_EQ(st->tag, 7);
    T_EQ(COM_CALL(IRtTestShape, GetTag, shape), 7);
    UINT out = 0;
    /* this + 7: integers, a float and a double by position, three stack arguments.  The body
     * returns the upper half of c and stores a + b + (low c) + d + e + strlen(text) + tag. */
    T_EQ(COM_CALL(IRtTestShape, Mix, shape, 1000, 20.0f, 0x0000002A00000300ull, 4.0, 5, "four", &out), 0x2A);
    T_EQ(out, 1000 + 20 + 0x300 + 4 + 5 + 4 + 7);
    T_EQ(COM_CALL(IRtTestShape, Scale, shape, 1.5f), 10.5f);

    T_CASE("a method with no body is a stop that names itself and prints its arguments by the description");
    IRtTestCounter *lone = rt_com_new(&rttest_counter_class);
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "FATAL unimplemented interface method IRtTestShape::NotBuilt");
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "rttest.dll, vtable slot 9 of IRtTestShape, declared by IRtTestShape (rttest)");
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "it has no body: runtime/tests/com/bodies/IRtTestShape/NotBuilt.c does not exist");
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "RTTEST_KIND kind = 0000002a (42)");
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "-> 12345678 def09abc 04030201 08070605");                         /* the 32 bytes behind `desc` */
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "-> {7e57c0de-0003-4000-8000-00000000c0de} IRtTestCounter");
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "of class RtTestCounter (its IRtTestCounter), 1 reference");
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "FLOAT ratio = 1.5 ");
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "\"a label\"");
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "of class RtTestShape (its IRtTestShape), 1 reference");            /* `This` */
    T_TRAPS(COM_CALL(IRtTestShape, NotBuilt, shape, 42, (const RTTEST_DESC *)&iid_nobody, &iid_counter, lone, 1.5f, "a label"),
            "in shim rttest.dll!IRtTestShape::NotBuilt, called from rttest.dll!rt_test_return");
    T_TRAPS(COM_CALL(IRtTestCounter, Shape, lone), "FATAL unimplemented interface method IRtTestCounter::Shape");
    /* A stop a body makes itself reads like a shim's. */
    T_TRAPS(COM_CALL(IRtTestShape, SetTag, shape, 0x1000), "unimplemented case in rttest.dll!IRtTestShape::SetTag: SetTag with tag 0x1000");

    T_CASE("a method marked accept returns success and does nothing");
    T_EQ(COM_CALL(IRtTestShape, Touch, shape, 0xFFFFFFFFu), S_OK);
    COM_CALL(IRtTestShape, Poke, shape, &out);
    T_EQ(out, 1000 + 20 + 0x300 + 4 + 5 + 4 + 7);
    T_EQ(st->tag, 7);
    T_EQ(rt_iface_IRtTestShape_methods[RT_COM_SLOT_IRtTestShape_Touch].body, RT_COM_BODY_ACCEPT);
    T_EQ(rt_iface_IRtTestShape_methods[RT_COM_SLOT_IRtTestShape_NotBuilt].body, RT_COM_BODY_STOP);
    T_EQ(rt_iface_IRtTestShape_methods[RT_COM_SLOT_IRtTestShape_GetTag].body, RT_COM_BODY_FILE);
    T_EQ(rt_iface_IRtTestShape_methods[RT_COM_SLOT_IRtTestShape_Release].body, RT_COM_BODY_UNKNOWN);

    T_CASE("QueryInterface answers the interfaces the class presents, and their bases");
    void *got = NULL;
    T_EQ(COM_CALL(IRtTestShape, QueryInterface, shape, &iid_shape, &got), S_OK);
    T_TRUE(got == shape);
    T_EQ(rt_com_refs(shape), 2);
    got = NULL;
    T_EQ(COM_CALL(IRtTestShape, QueryInterface, shape, &iid_named, &got), S_OK);       /* a base of the first face */
    T_TRUE(got == shape);
    got = NULL;
    T_EQ(COM_CALL(IRtTestShape, QueryInterface, shape, &iid_counter, &got), S_OK);     /* the second face */
    IRtTestCounter *counter = got;
    T_REQUIRE(counter != NULL);
    T_TRUE((void *)counter != (void *)shape);
    T_EQ(LD64((uint64_t)(uintptr_t)counter), rt_iface_IRtTestCounter.vtable);
    T_TRUE(rt_com_face(shape, &rt_iface_IRtTestCounter) == counter);
    T_TRUE(rt_com_face(counter, &rt_iface_IRtTestShape) == shape);
    T_TRUE(rt_com_face(lone, &rt_iface_IRtTestShape) == NULL);
    T_EQ(rt_com_refs(counter), 4);                                                      /* one count for the object */
    T_EQ(COM_CALL(IRtTestCounter, Next, counter), 1);                                   /* the same state through the other face */
    T_EQ(COM_CALL(IRtTestCounter, Next, counter), 2);
    T_EQ(st->next, 2);
    /* IUnknown: the same pointer whichever face is asked. */
    void *unk_a = NULL, *unk_b = NULL;
    T_EQ(COM_CALL(IRtTestShape, QueryInterface, shape, &iid_unknown, &unk_a), S_OK);
    T_EQ(COM_CALL(IRtTestCounter, QueryInterface, counter, &iid_unknown, &unk_b), S_OK);
    T_TRUE(unk_a == shape && unk_b == shape);
    got = NULL;
    T_EQ(COM_CALL(IRtTestCounter, QueryInterface, counter, &iid_shape, &got), S_OK);   /* and back */
    T_TRUE(got == shape);
    T_EQ(rt_com_refs(shape), 7);

    T_CASE("QueryInterface for an IID the class does not present is a stop that prints the IID");
    T_TRAPS(COM_CALL(IRtTestShape, QueryInterface, shape, &iid_nobody, &got),
            "QueryInterface for {12345678-9abc-def0-0102-030405060708} (an interface the runtime does not describe)");
    T_TRAPS(COM_CALL(IRtTestShape, QueryInterface, shape, &iid_nobody, &got),
            "(an interface the runtime does not describe): object #1 of class RtTestShape presents only IRtTestShape, IRtTestCounter (and bases)");
    T_TRAPS(COM_CALL(IRtTestCounter, QueryInterface, lone, &iid_shape, &got),
            "QueryInterface for {7e57c0de-0002-4000-8000-00000000c0de} (IRtTestShape)");
    T_TRAPS(COM_CALL(IRtTestCounter, QueryInterface, lone, &rt_iface_ID3D11Device.iid, &got),
            "(ID3D11Device): object #2 of class RtTestCounter presents only IRtTestCounter (and bases)");
    T_TRAPS(COM_CALL(IRtTestShape, QueryInterface, shape, &iid_shape, NULL), "QueryInterface with riid");
    T_EQ(rt_com_refs(shape), 7);

    T_CASE("an IID the class's `refuses` names is answered E_NOINTERFACE with a NULL pointer and no reference");
    got = shape;
    T_EQ(COM_CALL(IRtTestShape, QueryInterface, shape, &rttest_iid_refused, &got), E_NOINTERFACE);
    T_TRUE(got == NULL);
    got = shape;
    T_EQ(COM_CALL(IRtTestCounter, QueryInterface, counter, &rttest_iid_refused, &got), E_NOINTERFACE);
    T_TRUE(got == NULL);
    T_EQ(rt_com_refs(shape), 7);
    st->tag = 0;                /* the refusal is made from the object's state: without it the IID is a stop again */
    T_TRAPS(COM_CALL(IRtTestShape, QueryInterface, shape, &rttest_iid_refused, &got),
            "QueryInterface for {7e57c0de-00fe-4000-8000-00000000c0de} (an interface the runtime does not describe)");
    st->tag = 7;

    T_CASE("AddRef and Release return the new count; the last Release destroys the object once");
    T_EQ(COM_CALL(IRtTestShape, AddRef, shape), 8);
    T_EQ(COM_CALL(IRtTestCounter, AddRef, counter), 9);
    T_EQ(rt_com_addref(shape), 10);                 /* the host-side pair */
    T_EQ(rt_com_release(counter), 9);
    for (ULONG want = 8; want >= 1; want--) T_EQ(COM_CALL(IRtTestShape, Release, shape), want);
    T_EQ(destroyed, 0);
    T_TRUE(rt_com_class_of(shape) == &rttest_shape_class);
    T_EQ(COM_CALL(IRtTestCounter, Release, counter), 0);
    T_EQ(destroyed, 1);

    T_CASE("a destroyed object is dead: every later call through one of its faces stops");
    T_TRUE(rt_com_class_of(shape) == NULL);
    T_TRAPS(COM_CALL(IRtTestShape, GetTag, shape), "is object #1 of class RtTestShape, which was destroyed");
    T_TRAPS(COM_CALL(IRtTestCounter, Next, counter), "is object #1 of class RtTestShape, which was destroyed");
    T_TRAPS(COM_CALL(IRtTestShape, AddRef, shape), "which was destroyed");
    T_TRAPS(COM_CALL(IRtTestShape, Release, shape), "which was destroyed");
    T_TRAPS(COM_CALL(IRtTestShape, Touch, shape, 0), "which was destroyed");            /* accept checks `this` too */
    T_TRAPS(COM_CALL(IRtTestShape, QueryInterface, shape, &iid_shape, &got), "which was destroyed");
    T_TRAPS(rt_com_refs(shape), "object #1 of class RtTestShape was destroyed");
    /* A new object never reuses the block: the dead one stays dead. */
    IRtTestShape *second = rt_com_new(&rttest_shape_class);
    T_TRUE(second != shape);
    T_EQ(rt_com_serial(second), 3);
    T_TRUE(rt_com_class_of(shape) == NULL);
    T_EQ(COM_CALL(IRtTestShape, GetTag, second), 0);                                    /* state starts zeroed */

    T_CASE("a method refuses a `this` of another class, and one that is no object");
    /* GetTag's body asks for the state of a RtTestShape; `lone` is a RtTestCounter. */
    T_TRAPS(rt_com_state(T_CTX(), lone, &rttest_shape_class), "is object #2 of class RtTestCounter, where an object of class RtTestShape is expected");
    uint64_t fake[4] = { rt_iface_IRtTestShape.vtable, 0, 0, 0 };                        /* a vtable address, but not an object of ours */
    T_TRAPS(COM_CALL(IRtTestShape, GetTag, (IRtTestShape *)fake), "is not an object of the interface layer");
    T_TRAPS(COM_CALL(IRtTestShape, Release, (IRtTestShape *)fake), "is not an object of the interface layer");
    T_EQ(COM_CALL(IRtTestCounter, Release, lone), 0);
    T_EQ(COM_CALL(IRtTestShape, Release, second), 0);
    T_EQ(destroyed, 1);                                                                  /* second's state had no counter set */
}
