#include "rt/test.h"
#include "rt/module.h"

/* kernel32.dll is a native module, always loaded, found by name.  Its load count is
 * raised by the test as a LoadLibrary would, and each FreeLibrary here undoes one raise. */
TEST(kernel32, FreeLibrary)
{
    RtModule *k;
    HMODULE base;
    int32_t n;

    k = rt_module_find("kernel32.dll");
    T_REQUIRE(k != NULL);
    base = (HMODULE)(uintptr_t)k->base;

    T_CASE("frees a loaded module, so its load count goes down by one and TRUE is returned");
    n = k->load_count;
    k->load_count++;
    T_EQ(CALL(kernel32, FreeLibrary, base), TRUE);
    T_EQ(k->load_count, n);

    T_CASE("does not change last error on success");
    n = k->load_count;
    k->load_count++;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FreeLibrary, base), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(k->load_count, n);

    T_CASE("a handle that is no module fails with ERROR_MOD_NOT_FOUND");
    n = k->load_count;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, FreeLibrary, (HMODULE)(uintptr_t)4), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, FreeLibrary, (HMODULE)(uintptr_t)(k->base + 0x10)), FALSE);   /* inside the module, not its base */
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    T_EQ(k->load_count, n);

    T_TRAPS(CALL(kernel32, FreeLibrary, NULL), "FreeLibrary(NULL)");
}
