#include <stdio.h>

#include "rt/test.h"
#include "rt/module.h"
#include "rt/window.h"

/* RegisterClassExA.  The checks, the registration and the last error are the core's
 * (rt_wndclass_register, which sets the last error on failure); the shim passes the structure
 * on.  A case that registers a class uses names of its own.  The refusals at the end are
 * proofs with T_TRAPS and have no case of their own. */

#define PROC_GUEST  0x401000u   /* a guest code address: the core stores it and never calls it */

static HINSTANCE main_module(void)
{
    return (HINSTANCE)(uintptr_t)rt_module_exe()->base;
}

/* A valid structure for `name`: cbSize 80, a procedure, the main module, no menu, no extra
 * bytes. */
static WNDCLASSEXA good_class(const char *name)
{
    WNDCLASSEXA wc = { .cbSize = 80, .lpfnWndProc = PROC_GUEST, .hInstance = main_module(),
                       .lpszClassName = name };
    return wc;
}

/* Registers with the last error set to 0 first, so that a code left by the call is its own. */
static ATOM reg(const WNDCLASSEXA *wc)
{
    CALL(kernel32, SetLastError, 0);
    return CALL(user32, RegisterClassExA, wc);
}

static void fill(char *buf, unsigned len, char c)
{
    for (unsigned i = 0; i < len; i++) buf[i] = c;
    buf[len] = '\0';
}

/* Registers classes with names of their own until the core stops (more than 256 classes).
 * T_TRAPS runs it in a forked child, so the classes of this test are not changed. */
static void register_until_stop(void)
{
    char name[32];
    for (unsigned i = 0; i < 1000; i++) {
        WNDCLASSEXA wc;
        snprintf(name, sizeof name, "RtA_many_%u", i);
        wc = good_class(name);
        CALL(user32, RegisterClassExA, &wc);
    }
}

TEST(user32, RegisterClassExA)
{
    WNDCLASSEXA wc;
    ATOM a;
    char name[300];

    T_CASE("registers a class and returns its atom");
    wc = good_class("RtA_first");
    a = reg(&wc);
    T_TRUE(a >= 0xC000);

    T_CASE("a name registered again fails with ERROR_CLASS_ALREADY_EXISTS");
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);

    T_CASE("a name in other ASCII case is taken too");
    wc = good_class("RTA_FIRST");
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);

    T_CASE("a NULL hInstance is the main module");
    wc = good_class("RtA_main");                    /* registered with the executable's handle */
    T_TRUE(reg(&wc) >= 0xC000);
    wc.hInstance = NULL;
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);
    wc = good_class("RtA_null");                    /* registered with NULL */
    wc.hInstance = NULL;
    T_TRUE(reg(&wc) >= 0xC000);
    wc.hInstance = main_module();
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_CLASS_ALREADY_EXISTS);

    T_CASE("cbSize other than 80 fails with ERROR_INVALID_PARAMETER");
    wc = good_class("RtA_size");
    wc.cbSize = 72;
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    wc.cbSize = 88;
    T_EQ(reg(&wc), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("negative extra byte counts fail with ERROR_INVALID_PARAMETER");
    wc = good_class("RtA_extra");
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
    wc = good_class("RtA_quiet");
    a = CALL(user32, RegisterClassExA, &wc);
    T_TRUE(a >= 0xC000);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Refusals with no case of their own: the shim's own (a name of 256 characters, not
     * settled) and the core's stops (its comment's STOPS), each proved with T_TRAPS. */
    fill(name, 256, 'd');
    wc = good_class(name);
    T_TRAPS(CALL(user32, RegisterClassExA, &wc), "RegisterClassExA with a class name of 256 characters");

    wc = good_class((const char *)(uintptr_t)0xC001);      /* a class name that is an atom */
    T_TRAPS(CALL(user32, RegisterClassExA, &wc), "rt_wndclass_register: not implemented");

    wc = good_class("");
    T_TRAPS(CALL(user32, RegisterClassExA, &wc), "rt_wndclass_register: not implemented");

    wc = good_class("RtA_proc");
    wc.lpfnWndProc = 0;
    T_TRAPS(CALL(user32, RegisterClassExA, &wc), "rt_wndclass_register: not implemented");

    wc = good_class("RtA_menu");
    wc.lpszMenuName = "menu";
    T_TRAPS(CALL(user32, RegisterClassExA, &wc), "rt_wndclass_register: not implemented");

    wc = good_class("RtA_wnd41");
    wc.cbWndExtra = 41;
    T_TRAPS(CALL(user32, RegisterClassExA, &wc), "rt_wndclass_register: not implemented");
    wc = good_class("RtA_cls41");
    wc.cbClsExtra = 41;
    T_TRAPS(CALL(user32, RegisterClassExA, &wc), "rt_wndclass_register: not implemented");

    wc = good_class("RtA_inst");                    /* registered with the executable's handle */
    T_TRUE(reg(&wc) >= 0xC000);
    wc.hInstance = (HINSTANCE)(uintptr_t)0x1000;    /* another module's handle: same name */
    T_TRAPS(CALL(user32, RegisterClassExA, &wc), "rt_wndclass_register: not implemented");

    T_TRAPS(register_until_stop(), "rt_wndclass_register: not implemented");
}
