/* Classes and registered messages (rt/window.h, core/win_class.c).  Expected values: the
 * comments of rt_wndclass_register, rt_wndclass_unregister and rt_wndmsg_register; the error
 * codes of a wrong cbSize, negative extra sizes, a second registration and the two
 * unregistration failures are the ones Wine's user32/tests/class.c asserts (test_instances),
 * as docs/reviews/contracts-check.md row V13 records. */
#include <stdio.h>
#include <string.h>

#include "rt/test.h"
#include "../../core/win_priv.h"
#include "rt/module.h"

#define STOP_REG "rt_wndclass_register: not implemented: "
#define STOP_MSG "rt_wndmsg_register: not implemented: "

static const WNDPROC WPROC = (WNDPROC)0x140001000ull;
static const HINSTANCE INST = (HINSTANCE)0x140000000ull;

static WNDCLASSEXW wide(const WCHAR *name) {
    return (WNDCLASSEXW){ .cbSize = sizeof(WNDCLASSEXW), .lpfnWndProc = WPROC, .hInstance = INST, .lpszClassName = name };
}

static ATOM reg_a(const char *name) {
    WNDCLASSEXA wc = { .cbSize = sizeof wc, .lpfnWndProc = WPROC, .hInstance = INST, .lpszClassName = name };
    return rt_wndclass_register((const WNDCLASSEXW *)&wc, false);
}

static RtWndClass *find_w(const WCHAR *name) {
    RtClassKey key;
    if (rt_win_text_key(RT_TEXT_W(name), &key) != RT_KEY_NAME) return NULL;
    rt_win_lock();
    RtWndClass *c = rt_win_class_lookup(&key);
    rt_win_unlock();
    return c;
}

TEST(core, win_class)
{
    T_CASE("a registration gives an atom from 0xC000 up and stores what it was given");
    WNDCLASSEXW wc = { .cbSize = sizeof wc, .style = 0xb, .lpfnWndProc = WPROC, .cbClsExtra = 8, .cbWndExtra = 40,
                       .hInstance = INST, .hIcon = (HICON)0x27F00, .hCursor = (HCURSOR)0x17F00,
                       .hbrBackground = (HBRUSH)6, .lpszClassName = u"First", .hIconSm = (HICON)0x27F01 };
    rt_thread_set_last_error(0xDEAD);
    ATOM first = rt_wndclass_register(&wc, true);
    T_TRUE(first >= 0xC000);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    RtWndClass *c = find_w(u"FIRST");
    T_REQUIRE(c != NULL);
    T_TRUE(c->used && c->unicode && c->atom == first && c->style == 0xb && c->proc == WPROC);
    T_TRUE(c->cls_extra == 8 && c->wnd_extra == 40 && c->instance == INST && c->windows == 0);
    T_TRUE(c->icon == (HICON)0x27F00 && c->icon_sm == (HICON)0x27F01 && c->cursor == (HCURSOR)0x17F00);
    T_TRUE(c->background == (HBRUSH)6 && c->name[0] == 'F' && c->name[5] == 0 && c->extra[0] == 0 && c->extra[39] == 0);
    wc.lpszClassName = u"Second";
    wc.hIcon = NULL;                                 /* a class with no icon is legal */
    ATOM second = rt_wndclass_register(&wc, true);
    T_TRUE(second >= 0xC000 && second != first);
    T_TRUE(find_w(u"Second") != NULL && find_w(u"Second")->icon == NULL);

    T_CASE("the same name with the same hInstance exists already, whatever its case or character type");
    wc.lpszClassName = u"First";
    T_EQ(rt_wndclass_register(&wc, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);
    rt_thread_set_last_error(0xDEAD);
    wc.lpszClassName = u"fIRST";
    T_EQ(rt_wndclass_register(&wc, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(reg_a("first"), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);

    /* Wine user32/tests/class.c, test_instances: "null handle means the same thing as main
     * module": with a main-module class of that name NULL fails with 1410, and a class
     * registered with NULL is found under the main module (review D6, contracts-check-2.md). */
    T_CASE("hInstance NULL is the main module");
    const HINSTANCE exe = (HINSTANCE)(uintptr_t)rt_module_exe()->base;
    T_REQUIRE(exe != NULL && exe != INST);
    WNDCLASSEXW mm = wide(u"MainMod");
    mm.hInstance = exe;
    T_TRUE(rt_wndclass_register(&mm, true) >= 0xC000);
    mm.hInstance = NULL;
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_register(&mm, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);
    mm = wide(u"NullMod");
    mm.hInstance = NULL;
    T_TRUE(rt_wndclass_register(&mm, true) >= 0xC000);
    T_TRUE(find_w(u"NullMod") != NULL && find_w(u"NullMod")->instance == exe);
    mm.hInstance = exe;
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_register(&mm, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);

    T_CASE("a narrow name is code page 1252");
    ATOM cafe = reg_a("Caf\xE9\x80");                 /* e acute, the euro sign */
    T_TRUE(cafe >= 0xC000);
    c = find_w(u"café€");
    T_TRUE(c != NULL && c->atom == cafe && !c->unicode);
    T_TRUE(find_w(u"CAFÉ€") == NULL);      /* only ASCII letters are folded */

    T_CASE("what Windows refuses: ERROR_INVALID_PARAMETER");
    WNDCLASSEXW bad = wide(u"Bad");
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_register(NULL, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    static const UINT sizes[] = { 0, 72, 79, 81 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        bad = wide(u"Bad");
        bad.cbSize = sizes[i];
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_wndclass_register(&bad, true), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    }
    bad = wide(NULL);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_register(&bad, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    bad = wide(u"Bad");
    bad.cbClsExtra = -1;
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_register(&bad, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    bad = wide(u"Bad");
    bad.cbWndExtra = -1;
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_register(&bad, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    static WCHAR long_w[257];
    static char long_a[257];
    for (unsigned i = 0; i < 256; i++) { long_w[i] = 'x'; long_a[i] = 'y'; }
    bad = wide(long_w);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_register(&bad, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(reg_a(long_a), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    long_w[255] = 0;                                 /* 255 characters is the longest name */
    bad = wide(long_w);
    T_TRUE(rt_wndclass_register(&bad, true) >= 0xC000);
    T_TRUE(find_w(u"Bad") == NULL);                  /* nothing of the refused calls was stored */

    T_CASE("what this runtime does not build stops");
    bad = wide((const WCHAR *)(uintptr_t)0xC001);
    T_TRAPS(rt_wndclass_register(&bad, true), STOP_REG "a class name that is an atom (0xc001)");
    bad = wide(u"");
    T_TRAPS(rt_wndclass_register(&bad, true), STOP_REG "an empty class name");
    bad = wide(u"Bad");
    bad.lpfnWndProc = 0;
    T_TRAPS(rt_wndclass_register(&bad, true), STOP_REG "a class with no window procedure");
    bad = wide(u"Bad");
    bad.lpszMenuName = u"Menu";
    T_TRAPS(rt_wndclass_register(&bad, true), STOP_REG "a class with a menu name");
    bad = wide(u"Bad");
    bad.cbWndExtra = 41;
    T_TRAPS(rt_wndclass_register(&bad, true), STOP_REG "more than 40 extra bytes (cbClsExtra 0, cbWndExtra 41)");
    bad = wide(u"Bad");
    bad.cbClsExtra = 1000;
    T_TRAPS(rt_wndclass_register(&bad, true), STOP_REG "more than 40 extra bytes (cbClsExtra 1000, cbWndExtra 0)");
    bad = wide(u"first");
    bad.hInstance = (HINSTANCE)0x180000000ull;
    T_TRAPS(rt_wndclass_register(&bad, true), STOP_REG "a class name that is registered with another hInstance");
    bad.hInstance = NULL;                       /* the executable, which is not INST: another module */
    T_TRAPS(rt_wndclass_register(&bad, true), STOP_REG "a class name that is registered with another hInstance");
}

TEST(core, win_class_unregister)
{
    WNDCLASSEXW wc = wide(u"One");
    ATOM one = rt_wndclass_register(&wc, true);
    wc = wide(u"Two");
    ATOM two = rt_wndclass_register(&wc, true);
    wc = wide(u"Three");
    ATOM three = rt_wndclass_register(&wc, true);
    T_REQUIRE(one && two && three);

    T_CASE("by name, by name in other case and by atom; the instance is not compared");
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_unregister(RT_TEXT_W(u"One"), INST), TRUE);
    T_EQ(rt_wndclass_unregister(RT_TEXT_A("tWO"), NULL), TRUE);
    T_EQ(rt_wndclass_unregister(RT_TEXT_W((const WCHAR *)(uintptr_t)three), (HINSTANCE)0x77), TRUE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_TRUE(find_w(u"One") == NULL && find_w(u"Two") == NULL && find_w(u"Three") == NULL);

    T_CASE("a class that is not there: ERROR_CLASS_DOES_NOT_EXIST");
    T_EQ(rt_wndclass_unregister(RT_TEXT_W(u"One"), INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_unregister(RT_TEXT_A((const char *)(uintptr_t)three), INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_unregister(RT_TEXT_A(NULL), INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_unregister(RT_TEXT_A(""), INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_unregister(RT_TEXT_W((const WCHAR *)(uintptr_t)0x8002), INST), FALSE);   /* an atom no class has */
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_DOES_NOT_EXIST);

    T_CASE("a name keeps its atom when it is registered again");
    wc = wide(u"TWO");
    T_EQ(rt_wndclass_register(&wc, true), two);
    wc = wide(u"One");
    T_EQ(rt_wndclass_register(&wc, true), one);

    T_CASE("a class with a window cannot go: ERROR_CLASS_HAS_WINDOWS");
    RtWndClass *c = find_w(u"One");
    T_REQUIRE(c != NULL);
    RtWinNew a = { .cls = c, .style = WS_POPUP, .rect = { 0, 0, 10, 10 }, .thread_id = 4 };
    HWND h = rt_win_window_new(&a);
    T_REQUIRE(h != NULL);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_wndclass_unregister(RT_TEXT_W(u"One"), INST), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_HAS_WINDOWS);
    T_TRUE(find_w(u"One") == c);
    T_EQ(rt_win_window_drop(h), TRUE);
    T_EQ(rt_wndclass_unregister(RT_TEXT_W(u"One"), INST), TRUE);
}

TEST(core, win_class_message)
{
    T_CASE("a message number is 0xC000 to 0xFFFF and the same for the same string");
    rt_thread_set_last_error(0xDEAD);
    UINT m = rt_wndmsg_register(RT_TEXT_A("ShutdownValvePlatform"));
    T_TRUE(m >= 0xC000 && m <= 0xFFFF);
    T_EQ(rt_wndmsg_register(RT_TEXT_A("ShutdownValvePlatform")), m);
    T_EQ(rt_wndmsg_register(RT_TEXT_A("SHUTDOWNVALVEPLATFORM")), m);
    T_EQ(rt_wndmsg_register(RT_TEXT_W(u"shutdownvalveplatform")), m);
    UINT other = rt_wndmsg_register(RT_TEXT_A("ShutdownValvePlatform2"));
    T_TRUE(other >= 0xC000 && other <= 0xFFFF && other != m);
    T_EQ(T_LAST_ERROR(), 0xDEAD);

    T_CASE("classes and messages share one series of numbers");
    WNDCLASSEXW wc = wide(u"ShutdownValvePlatform");
    T_EQ(rt_wndclass_register(&wc, true), m);
    wc = wide(u"OnlyAClass");
    ATOM cls = rt_wndclass_register(&wc, true);
    T_TRUE(cls >= 0xC000 && cls != m && cls != other);
    T_EQ(rt_wndmsg_register(RT_TEXT_A("onlyaclass")), cls);
    T_EQ(rt_wndclass_unregister(RT_TEXT_W(u"OnlyAClass"), INST), TRUE);
    T_EQ(rt_wndmsg_register(RT_TEXT_A("OnlyAClass")), cls);       /* a message name outlives the class */

    T_CASE("what this runtime does not build stops");
    static char long_a[257];
    memset(long_a, 'z', 256);
    T_TRAPS(rt_wndmsg_register(RT_TEXT_A(NULL)), STOP_MSG "a NULL message name");
    T_TRAPS(rt_wndmsg_register(RT_TEXT_A("")), STOP_MSG "an empty message name");
    T_TRAPS(rt_wndmsg_register(RT_TEXT_W(u"")), STOP_MSG "an empty message name");
    T_TRAPS(rt_wndmsg_register(RT_TEXT_A(long_a)), STOP_MSG "a message name longer than 255 characters");
    T_TRAPS(rt_wndmsg_register(RT_TEXT_A((const char *)(uintptr_t)0xC000)), STOP_MSG "a message name that is an atom (0xc000)");
    long_a[255] = 0;
    T_TRUE(rt_wndmsg_register(RT_TEXT_A(long_a)) >= 0xC000);
}

/* The two limits, each in a process of its own (a TEST is one). */
TEST(core, win_class_limits)
{
    T_CASE("256 classes can be registered and the 257th stops");
    WCHAR name[16] = u"Class000";
    for (unsigned i = 0; i < RT_WIN_MAX_CLASSES; i++) {
        name[5] = (WCHAR)('0' + i / 100); name[6] = (WCHAR)('0' + i / 10 % 10); name[7] = (WCHAR)('0' + i % 10);
        WNDCLASSEXW wc = wide(name);
        if (rt_wndclass_register(&wc, true) != 0xC000 + i) { T_EQ(i, (unsigned)-1); break; }
    }
    WNDCLASSEXW over = wide(u"OneTooMany");
    T_TRAPS(rt_wndclass_register(&over, true), STOP_REG "more than 256 classes");
    T_EQ(rt_wndclass_unregister(RT_TEXT_W(u"Class007"), INST), TRUE);       /* a freed slot is used again */
    T_EQ(rt_wndclass_register(&over, true), 0xC000 + RT_WIN_MAX_CLASSES);

    T_CASE("the names 0xC000 to 0xFFFF can all be given out and the next one stops");
    UINT last = 0;
    for (unsigned i = RT_WIN_MAX_CLASSES + 1; i < 0x4000; i++) {
        char text[16];
        snprintf(text, sizeof text, "%u.msg", i);
        last = rt_wndmsg_register(RT_TEXT_A(text));
        if (last != 0xC000 + i) { T_EQ(last, 0xC000 + i); break; }
    }
    T_EQ(last, 0xFFFF);
    T_EQ(rt_wndmsg_register(RT_TEXT_A("9000.msg")), 0xC000 + 9000);           /* a known name still answers */
    T_TRAPS(rt_wndmsg_register(RT_TEXT_A("one name too many")), STOP_MSG "the table of class and message names is full");
    WNDCLASSEXW wc = wide(u"a class too many");
    T_EQ(rt_wndclass_unregister(RT_TEXT_W(u"Class008"), INST), TRUE);
    T_TRAPS(rt_wndclass_register(&wc, true), STOP_REG "the table of class and message names is full");
}
