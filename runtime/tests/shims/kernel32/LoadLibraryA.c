#include "rt/test.h"
#include "rt/module.h"

/* kernel32.dll is a native module, always loaded: each load of it adds one to its load count,
 * which the test takes back with load_count--. */
TEST(kernel32, LoadLibraryA)
{
    RtModule *k = rt_module_find("kernel32.dll");
    HMODULE base;
    int32_t n;

    T_REQUIRE(k != NULL);
    base = (HMODULE)(uintptr_t)k->base;

    T_CASE("a native module by its name, in any case, gives its base, and its load count is one higher each time");
    n = k->load_count;
    T_EQ(CALL(kernel32, LoadLibraryA, "kernel32.dll"), base);
    T_EQ(k->load_count, n + 1);
    T_EQ(CALL(kernel32, LoadLibraryA, "KERNEL32"), base);
    T_EQ(k->load_count, n + 2);
    k->load_count--;
    k->load_count--;

    T_CASE("a name no module has fails with ERROR_MOD_NOT_FOUND");
    T_EQ(CALL(kernel32, LoadLibraryA, "nosuch_titanfall.dll"), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);

    T_CASE("a byte of 0xE9 in the last path component fails with ERROR_MOD_NOT_FOUND, in a directory part it is ignored and the load succeeds");
    n = k->load_count;
    T_EQ(CALL(kernel32, LoadLibraryA, "caf\xE9.dll"), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    T_EQ(CALL(kernel32, LoadLibraryA, "caf\xE9\\kernel32.dll"), base);
    T_EQ(k->load_count, n + 1);
    k->load_count--;

    T_CASE("a NULL name fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(kernel32, LoadLibraryA, NULL), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, LoadLibraryA, "kernel32.dll"), base);
    T_EQ(T_LAST_ERROR(), 0x1234);
    k->load_count--;
}
