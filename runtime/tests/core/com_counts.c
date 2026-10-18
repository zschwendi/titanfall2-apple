/* The interface layer's call counts (rt/com.h): every call through a slot is counted per
 * (interface, method) whatever the method does, the first call of each is logged with its
 * caller, and the table is printed when the run ends, at a stop too.
 *
 * Expected values: the number of calls this test makes, and the layer's own format as
 * rt/com.h states it.
 */
#include "rt/test.h"
#include "rttest_com.h"
#include "rttest_com_calls.h"

static const GUID iid_counter = { 0x7e57c0de, 0x0003, 0x4000, { 0x80, 0x00, 0, 0, 0, 0, 0xc0, 0xde } };

TEST(core, com_counts)
{
    static char table[8192];
    void *got = NULL;

    T_CASE("nothing was called: no table");
    T_EQ(rt_com_counts_format(table, sizeof table), 0);
    T_EQ(table[0], 0);
    IRtTestShape *shape = rt_com_new(&rttest_shape_class);
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "GetTag"), 0);
    T_EQ(T_LOGGED(RT_CH_COM, "first call of"), false);

    T_CASE("every call through a slot is counted per interface and method");
    for (int i = 0; i < 3; i++) COM_CALL(IRtTestShape, GetTag, shape);                  /* a body */
    COM_CALL(IRtTestShape, Touch, shape, 1);                                            /* accept */
    COM_CALL(IRtTestShape, Touch, shape, 2);
    COM_CALL(IRtTestShape, QueryInterface, shape, &iid_counter, &got);                  /* the layer's own */
    IRtTestCounter *counter = got;
    COM_CALL(IRtTestCounter, Next, counter);
    COM_CALL(IRtTestCounter, AddRef, counter);
    COM_CALL(IRtTestShape, Release, shape);                                             /* the same method name, */
    COM_CALL(IRtTestCounter, Release, counter);                                         /* counted per interface */
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "GetTag"), 3);
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "Touch"), 2);
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "QueryInterface"), 1);
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "Release"), 1);
    T_EQ(rt_com_calls(&rt_iface_IRtTestCounter, "Release"), 1);
    T_EQ(rt_com_calls(&rt_iface_IRtTestCounter, "AddRef"), 1);
    T_EQ(rt_com_calls(&rt_iface_IRtTestCounter, "Next"), 1);
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "Mix"), 0);
    T_EQ(rt_com_calls(&rt_iface_IRtTestShape, "NoSuchMethod"), 0);
    T_EQ(rt_iface_IRtTestShape_counts[RT_COM_SLOT_IRtTestShape_GetTag].calls, 3);

    T_CASE("the first call of each method is logged once, with its caller");
    T_TRUE(T_LOGGED(RT_CH_COM, "first call of IRtTestShape::GetTag, from rttest.dll!rt_test_return on thread 0x1004"));
    T_TRUE(T_LOGGED(RT_CH_COM, "first call of IRtTestCounter::Release, from rttest.dll!rt_test_return"));
    T_EQ(T_LOG_COUNT(RT_CH_COM), 7);                                                    /* seven methods, ten calls */

    T_CASE("the table has one line per method called, in description order, with count, first caller and kind of body");
    size_t len = rt_com_counts_format(table, sizeof table);
    T_EQ(len, strlen(table));
    T_TRUE(strstr(table, "com: calls through interface slots: 10 calls of 7 methods (") == table);
    const char *rows[] = {
        "com:          1  IRtTestCounter::AddRef  first from rttest.dll!rt_test_return, thread 0x1004  [common]\n",
        "com:          1  IRtTestCounter::Release  first from rttest.dll!rt_test_return, thread 0x1004  [common]\n",
        "com:          1  IRtTestCounter::Next  first from rttest.dll!rt_test_return, thread 0x1004  [body]\n",
        "com:          1  IRtTestShape::QueryInterface  first from rttest.dll!rt_test_return, thread 0x1004  [common]\n",
        "com:          1  IRtTestShape::Release  first from rttest.dll!rt_test_return, thread 0x1004  [common]\n",
        "com:          3  IRtTestShape::GetTag  first from rttest.dll!rt_test_return, thread 0x1004  [body]\n",
        "com:          2  IRtTestShape::Touch  first from rttest.dll!rt_test_return, thread 0x1004  [accept]\n",
    };
    const char *at = table;
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        const char *hit = strstr(at, rows[i]);
        T_TRUE(hit != NULL);
        if (hit) at = hit + 1;                                                          /* in this order */
    }
    unsigned lines = 0;
    for (const char *p = table; *p; p++) lines += *p == '\n';
    T_EQ(lines, 8);                                                                     /* the heading and seven rows */
    /* A buffer that is too small is filled and terminated, never overrun. */
    char small[64];
    memset(small, 0x7F, sizeof small);
    T_EQ(rt_com_counts_format(small, 40), 39);
    T_EQ(small[39], 0);
    T_EQ(small[40], 0x7F);

    T_CASE("a stop prints the table after its report, the stopping call included");
    T_TRAPS(COM_CALL(IRtTestCounter, Shape, counter), "com: calls through interface slots: 11 calls of 8 methods (");
    T_TRAPS(COM_CALL(IRtTestCounter, Shape, counter),
            "com:          1  IRtTestCounter::Shape  first from rttest.dll!rt_test_return, thread 0x1004  [stop]\n");
    T_TRAPS(COM_CALL(IRtTestCounter, Shape, counter),
            "com:          3  IRtTestShape::GetTag  first from rttest.dll!rt_test_return, thread 0x1004  [body]\n");
    /* Any fatal report does, not only a method's stop. */
    T_TRAPS(rt_fatal("a fatal report of another kind"), "com:          2  IRtTestShape::Touch");
    T_TRAPS(rt_fatal("a fatal report of another kind"), "FATAL runtime: a fatal report of another kind");

    T_CASE("the table is printed once");
    T_TRAPS((rt_com_counts_print(), rt_com_counts_print(), rt_fatal("after two prints")), "com:          3  IRtTestShape::GetTag");
}
