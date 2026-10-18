#include "rt/test.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_wndclass_register (ERROR_CLASS_ALREADY_EXISTS and the
 * negative extra size are what Wine's user32/tests/class.c asserts, review row V13).  The
 * class structure of the first case is the one vgui2's load passes (docs/handoff/
 * gate3-load.md): style 0xb, no menu, the executable's instance, a NULL icon. */
GUEST_FN(reg_proc) { ctx->r[0] = 0; GUEST_RET(ctx); }

TEST(user32, RegisterClassA)
{
    const HINSTANCE inst = (HINSTANCE)(uintptr_t)0x140000000ull;
    WNDCLASSA wc = { .style = 0xb, .lpfnWndProc = T_GUEST_ADDR(reg_proc), .cbClsExtra = 4, .cbWndExtra = 8,
                     .hInstance = inst, .hIcon = NULL, .hCursor = RT_HCURSOR_SYSTEM(IDC_ARROW),
                     .hbrBackground = (HBRUSH)(uintptr_t)6, .lpszMenuName = NULL, .lpszClassName = "ShimClassA" };

    T_CASE("a class gives an atom of 0xC000 or above and is stored under its name with its instance");
    CALL(kernel32, SetLastError, 0x1234);
    ATOM atom = CALL(user32, RegisterClassA, &wc);
    T_TRUE(atom >= 0xC000);
    T_EQ(rt_wndmsg_register(RT_TEXT_A("shimclassa")), atom);    /* the name's number, whatever its case */

    T_CASE("success does not change last error");
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("the same name twice fails with ERROR_CLASS_ALREADY_EXISTS");
    T_EQ(CALL(user32, RegisterClassA, &wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);
    /* The core finds the class under the same name and instance (another instance would stop). */
    WNDCLASSEXW same = { .cbSize = sizeof same, .lpfnWndProc = wc.lpfnWndProc, .hInstance = inst, .lpszClassName = u"SHIMCLASSA" };
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(rt_wndclass_register(&same, true), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);
    T_EQ(rt_wndclass_unregister(RT_TEXT_A("ShimClassA"), NULL), TRUE);
    T_EQ(CALL(user32, RegisterClassA, &wc), atom);

    T_CASE("a NULL structure, a NULL name and a negative extra size fail with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, RegisterClassA, NULL), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    WNDCLASSA bad = wc;
    bad.lpszClassName = NULL;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, RegisterClassA, &bad), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    bad = wc;
    bad.lpszClassName = "ShimClassB";
    bad.cbWndExtra = -1;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, RegisterClassA, &bad), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a name longer than 255 characters fails with ERROR_INVALID_PARAMETER");
    char longname[257];                                 /* 256 characters and the terminator */
    memset(longname, 'L', 256);
    longname[256] = 0;
    bad = wc;
    bad.lpszClassName = longname;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, RegisterClassA, &bad), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    /* RT-TRAPS: the core's stops, which the shim does not guard. */
    bad = wc;
    bad.lpszClassName = (LPCSTR)(uintptr_t)0xC123;
    T_TRAPS(CALL(user32, RegisterClassA, &bad), "rt_wndclass_register: not implemented: a class name that is an atom");
    bad = wc;
    bad.lpszClassName = "ShimClassB";
    bad.lpfnWndProc = 0;
    T_TRAPS(CALL(user32, RegisterClassA, &bad), "rt_wndclass_register: not implemented: a class with no window procedure");
    bad.lpfnWndProc = wc.lpfnWndProc;
    bad.lpszMenuName = "Menu";
    T_TRAPS(CALL(user32, RegisterClassA, &bad), "rt_wndclass_register: not implemented: a class with a menu name");
    bad.lpszMenuName = NULL;
    bad.cbClsExtra = 41;
    T_TRAPS(CALL(user32, RegisterClassA, &bad), "rt_wndclass_register: not implemented: more than 40 extra bytes");
    bad = wc;
    bad.lpszClassName = "";
    T_TRAPS(CALL(user32, RegisterClassA, &bad), "rt_wndclass_register: not implemented: an empty class name");
    bad = wc;                                           /* the name ShimClassA, registered under 0x140000000 */
    bad.hInstance = (HINSTANCE)(uintptr_t)0x180000000ull;
    T_TRAPS(CALL(user32, RegisterClassA, &bad), "rt_wndclass_register: not implemented: a class name that is registered with another hInstance");

    /* More than 256 classes: the table has 256 slots, ShimClassA takes one, so 255 more fill it. */
    bad = wc;
    char fill[] = "FillerAA";
    for (unsigned i = 0; i < 255; i++) {
        fill[6] = (char)('A' + i / 26);
        fill[7] = (char)('A' + i % 26);
        bad.lpszClassName = fill;
        ATOM filled = CALL(user32, RegisterClassA, &bad);
        T_TRUE(filled >= 0xC000);
    }
    bad.lpszClassName = "ShimClassZ";
    T_TRAPS(CALL(user32, RegisterClassA, &bad), "rt_wndclass_register: not implemented: more than 256 classes");
}
