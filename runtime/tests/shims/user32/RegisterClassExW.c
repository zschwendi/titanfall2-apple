#include <stdio.h>

#include "rt/test.h"
#include "rt/module.h"
#include "rt/window.h"

/* RegisterClassExW.  The checks, the registration and the last error are the core's
 * (rt_wndclass_register, which sets the last error on failure); the shim passes the structure
 * on.  A case that registers a class uses names of its own.  The refusals at the end are
 * proofs with T_TRAPS and have no case of their own.  Names are built from ASCII: the test
 * uses no wchar_t. */

#define PROC_GUEST  0x401000u   /* a guest code address: the core stores it and never calls it */

static HINSTANCE main_module(void)
{
    return (HINSTANCE)(uintptr_t)rt_module_exe()->base;
}

/* A valid structure for `name`: cbSize 80, a procedure, the main module, no menu, no extra
 * bytes. */
static WNDCLASSEXW good_class(const WCHAR *name)
{
    WNDCLASSEXW wc = { .cbSize = 80, .lpfnWndProc = PROC_GUEST, .hInstance = main_module(),
                       .lpszClassName = name };
    return wc;
}

/* The same for the A structure, for the case of a class the A function registers. */
static WNDCLASSEXA good_class_a(const char *name)
{
    WNDCLASSEXA wc = { .cbSize = 80, .lpfnWndProc = PROC_GUEST, .hInstance = main_module(),
                       .lpszClassName = name };
    return wc;
}

/* Registers with the last error set to 0 first, so that a code left by the call is its own. */
static ATOM reg(const WNDCLASSEXW *wc)
{
    CALL(kernel32, SetLastError, 0);
    return CALL(user32, RegisterClassExW, wc);
}

static void wide(WCHAR *d, const char *s)
{
    do { *d++ = (WCHAR)(unsigned char)*s; } while (*s++);
}

static void fill(WCHAR *buf, unsigned len, char c)
{
    for (unsigned i = 0; i < len; i++) buf[i] = (WCHAR)(unsigned char)c;
    buf[len] = 0;
}

/* Registers classes with names of their own until the core stops (more than 256 classes).
 * T_TRAPS runs it in a forked child, so the classes of this test are not changed. */
static void register_until_stop(void)
{
    char tmp[32];
    WCHAR name[32];
    for (unsigned i = 0; i < 1000; i++) {
        WNDCLASSEXW wc;
        snprintf(tmp, sizeof tmp, "RtW_many_%u", i);
        wide(name, tmp);
        wc = good_class(name);
        CALL(user32, RegisterClassExW, &wc);
    }
}

TEST(user32, RegisterClassExW)
{
    WNDCLASSEXW wc;
    WNDCLASSEXA ca;
    WCHAR name[300];
    WCHAR menu[8];
    ATOM a;

    T_CASE("registers a class and returns its atom");
    wide(name, "RtW_first");
    wc = good_class(name);
    a = reg(&wc);
    T_TRUE(a >= 0xC000);

    T_CASE("a name registered again fails with ERROR_CLASS_ALREADY_EXISTS");
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);

    T_CASE("a name in other ASCII case is taken too");
    wide(name, "RTW_FIRST");
    wc = good_class(name);
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);

    T_CASE("an A class is found again under the same name in other case");
    ca = good_class_a("RtW_shared");
    T_TRUE(CALL(user32, RegisterClassExA, &ca) >= 0xC000);
    wide(name, "RTW_SHARED");
    wc = good_class(name);
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);

    T_CASE("a NULL hInstance is the main module");
    wide(name, "RtW_main");                         /* registered with the executable's handle */
    wc = good_class(name);
    T_TRUE(reg(&wc) >= 0xC000);
    wc.hInstance = NULL;
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);
    wide(name, "RtW_null");                         /* registered with NULL */
    wc = good_class(name);
    wc.hInstance = NULL;
    T_TRUE(reg(&wc) >= 0xC000);
    wc.hInstance = main_module();
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);

    T_CASE("cbSize other than 80 fails with ERROR_INVALID_PARAMETER");
    wide(name, "RtW_size");
    wc = good_class(name);
    wc.cbSize = 72;
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    wc.cbSize = 88;
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("negative extra byte counts fail with ERROR_INVALID_PARAMETER");
    wide(name, "RtW_extra");
    wc = good_class(name);
    wc.cbWndExtra = -1;
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    wc.cbWndExtra = 0;
    wc.cbClsExtra = -1;
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a NULL structure or class name fails with ERROR_INVALID_PARAMETER");
    T_EQ(reg(NULL), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    wc = good_class(NULL);
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a class name of 255 characters registers");
    fill(name, 255, 'b');
    wc = good_class(name);
    T_TRUE(reg(&wc) >= 0xC000);

    T_CASE("a class name of 257 characters fails with ERROR_INVALID_PARAMETER");
    fill(name, 257, 'c');
    wc = good_class(name);
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a successful registration leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    wide(name, "RtW_quiet");
    wc = good_class(name);
    a = CALL(user32, RegisterClassExW, &wc);
    T_TRUE(a >= 0xC000);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Refusals with no case of their own: the shim's own (a name of 256 characters, not
     * settled) and the core's stops (its comment's STOPS), each proved with T_TRAPS. */
    fill(name, 256, 'd');
    wc = good_class(name);
    T_TRAPS(CALL(user32, RegisterClassExW, &wc), "RegisterClassExW with a class name of 256 characters");

    wc = good_class((const WCHAR *)(uintptr_t)0xC001);     /* a class name that is an atom */
    T_TRAPS(CALL(user32, RegisterClassExW, &wc), "rt_wndclass_register: not implemented");

    wide(name, "");
    wc = good_class(name);
    T_TRAPS(CALL(user32, RegisterClassExW, &wc), "rt_wndclass_register: not implemented");

    wide(name, "RtW_proc");
    wc = good_class(name);
    wc.lpfnWndProc = 0;
    T_TRAPS(CALL(user32, RegisterClassExW, &wc), "rt_wndclass_register: not implemented");

    wide(name, "RtW_menu");
    wide(menu, "menu");
    wc = good_class(name);
    wc.lpszMenuName = menu;
    T_TRAPS(CALL(user32, RegisterClassExW, &wc), "rt_wndclass_register: not implemented");

    wide(name, "RtW_wnd41");
    wc = good_class(name);
    wc.cbWndExtra = 41;
    T_TRAPS(CALL(user32, RegisterClassExW, &wc), "rt_wndclass_register: not implemented");
    wide(name, "RtW_cls41");
    wc = good_class(name);
    wc.cbClsExtra = 41;
    T_TRAPS(CALL(user32, RegisterClassExW, &wc), "rt_wndclass_register: not implemented");

    wide(name, "RtW_inst");                         /* registered with the executable's handle */
    wc = good_class(name);
    T_TRUE(reg(&wc) >= 0xC000);
    wc.hInstance = (HINSTANCE)(uintptr_t)0x1000;    /* another module's handle: same name */
    T_TRAPS(CALL(user32, RegisterClassExW, &wc), "rt_wndclass_register: not implemented");

    T_TRAPS(register_until_stop(), "rt_wndclass_register: not implemented");
}
