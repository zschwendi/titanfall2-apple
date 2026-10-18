#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(hook_proc)
{
    GUEST_RET(ctx);
}

/* The argument checks of rt/window.h (rt_hook_set), in the order the core makes them.  The hook
 * procedure is never called: no input exists before Gate 4. */
TEST(user32, SetWindowsHookExA)
{
    HOOKPROC proc = (HOOKPROC)T_GUEST_ADDR(hook_proc);
    HINSTANCE mod = (HINSTANCE)0x140000000ull;
    DWORD tid = CALL(kernel32, GetCurrentThreadId);
    HHOOK ll, th, g;

    T_CASE("low-level hooks on no thread return handles, with or without a module");
    ll = CALL(user32, SetWindowsHookExA, WH_KEYBOARD_LL, proc, NULL, 0);
    T_TRUE(ll != NULL);
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_MOUSE_LL, proc, mod, 0) != NULL);

    T_CASE("keyboard and mouse hooks on the calling thread return handles, with or without a module");
    th = CALL(user32, SetWindowsHookExA, WH_MOUSE, proc, NULL, tid);
    T_TRUE(th != NULL);
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_KEYBOARD, proc, mod, tid) != NULL);

    T_CASE("keyboard and mouse hooks on no thread return handles when they have a module");
    g = CALL(user32, SetWindowsHookExA, WH_KEYBOARD, proc, mod, 0);
    T_TRUE(g != NULL);
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_MOUSE, proc, mod, 0) != NULL);

    T_CASE("hooks that succeed return different handles");
    T_TRUE(ll != th && ll != g && th != g);

    T_CASE("a filter type outside -1 to 14 fails with ERROR_INVALID_HOOK_FILTER");
    T_TRUE(CALL(user32, SetWindowsHookExA, 99, proc, mod, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_FILTER);
    T_TRUE(CALL(user32, SetWindowsHookExA, 15, proc, mod, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_FILTER);
    T_TRUE(CALL(user32, SetWindowsHookExA, -2, proc, mod, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_FILTER);

    T_CASE("a NULL procedure fails with ERROR_INVALID_FILTER_PROC whatever the type");
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_KEYBOARD, (HOOKPROC)0, NULL, tid) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FILTER_PROC);
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_KEYBOARD_LL, (HOOKPROC)0, NULL, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_FILTER_PROC);

    T_CASE("a low-level hook on a thread fails with ERROR_GLOBAL_ONLY_HOOK");
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_MOUSE_LL, proc, mod, tid) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_GLOBAL_ONLY_HOOK);
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_KEYBOARD_LL, proc, NULL, tid) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_GLOBAL_ONLY_HOOK);

    T_CASE("a keyboard or mouse hook on no thread without a module fails with ERROR_HOOK_NEEDS_HMOD");
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_KEYBOARD, proc, NULL, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_HOOK_NEEDS_HMOD);
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_MOUSE, proc, NULL, 0) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_HOOK_NEEDS_HMOD);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_TRUE(CALL(user32, SetWindowsHookExA, WH_KEYBOARD, proc, mod, 0) != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* A stop of the core, not built: a hook on another thread (RT-TRAPS). */
    T_TRAPS(CALL(user32, SetWindowsHookExA, WH_KEYBOARD, proc, NULL, tid + 4), "rt_hook_set: not implemented");
}
