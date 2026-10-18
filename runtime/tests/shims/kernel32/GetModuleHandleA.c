#include "rt/test.h"
#include "rt/module.h"

/* GetModuleHandleA writes no memory, so its checks are the return value and last error.  The
 * NULL case takes the core's answer for NULL in the test process (the core is the oracle). */

TEST(kernel32, GetModuleHandleA)
{
    RtModule *k;
    RtModule *exe;

    k = rt_module_find("kernel32.dll");
    T_REQUIRE(k != NULL);
    exe = rt_module_handle_a(NULL);

    T_CASE("NULL gives the executable");
    T_EQ((uintptr_t)CALL(kernel32, GetModuleHandleA, NULL), exe ? (uintptr_t)exe->base : (uintptr_t)0);

    T_CASE("kernel32.dll, KERNEL32, kernel32 and a system path to kernel32.dll give kernel32's base");
    T_EQ((uintptr_t)CALL(kernel32, GetModuleHandleA, "kernel32.dll"), (uintptr_t)k->base);
    T_EQ((uintptr_t)CALL(kernel32, GetModuleHandleA, "KERNEL32"), (uintptr_t)k->base);
    T_EQ((uintptr_t)CALL(kernel32, GetModuleHandleA, "kernel32"), (uintptr_t)k->base);
    T_EQ((uintptr_t)CALL(kernel32, GetModuleHandleA, "C:\\Windows\\system32\\kernel32.dll"), (uintptr_t)k->base);

    T_CASE("a name no module has gives NULL and ERROR_MOD_NOT_FOUND");
    CALL(kernel32, SetLastError, 0);
    T_EQ((uintptr_t)CALL(kernel32, GetModuleHandleA, "rt_no_such_module.dll"), (uintptr_t)0);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    CALL(kernel32, SetLastError, 0);
    T_EQ((uintptr_t)CALL(kernel32, GetModuleHandleA, "rt_no_such_module"), (uintptr_t)0);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((uintptr_t)CALL(kernel32, GetModuleHandleA, "kernel32.dll"), (uintptr_t)k->base);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    if (exe) {
        T_EQ((uintptr_t)CALL(kernel32, GetModuleHandleA, NULL), (uintptr_t)exe->base);
        T_EQ(T_LAST_ERROR(), 0x1234);
    }

    /* The traps come after the checks of the last case. */
    T_TRAPS(CALL(kernel32, GetModuleHandleA, "k\xc3\xa9" "rnel32.dll"), "a character outside ASCII in the name");
}
