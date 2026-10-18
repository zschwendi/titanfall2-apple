#include "rt/test.h"
#include "rt/window.h"

GUEST_FN(hook_proc)
{
    GUEST_RET(ctx);
}

/* The handles come from SetWindowsHookExA, a low-level keyboard hook on no thread (no module
 * needed).  The hook procedure is never called: no input exists before Gate 4. */
TEST(user32, UnhookWindowsHookEx)
{
    HHOOK h, h2;

    h = CALL(user32, SetWindowsHookExA, WH_KEYBOARD_LL, T_GUEST_ADDR(hook_proc), NULL, 0);
    T_REQUIRE(h != NULL);

    T_CASE("removes a hook and returns TRUE");
    T_EQ(CALL(user32, UnhookWindowsHookEx, h), TRUE);

    T_CASE("a second unhook of the same handle fails with ERROR_INVALID_HOOK_HANDLE");
    T_EQ(CALL(user32, UnhookWindowsHookEx, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HOOK_HANDLE);

    T_CASE("success does not change last error");
    h2 = CALL(user32, SetWindowsHookExA, WH_KEYBOARD_LL, T_GUEST_ADDR(hook_proc), NULL, 0);
    T_REQUIRE(h2 != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, UnhookWindowsHookEx, h2), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
