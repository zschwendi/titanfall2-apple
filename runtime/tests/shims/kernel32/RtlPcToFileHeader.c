#include "rt/test.h"
#include "rt/module.h"

/* The module is found with rt_module_find, as the core's own tests find one.  The sentinel
 * in BaseOfImage must be overwritten on every call, found or not. */
TEST(kernel32, RtlPcToFileHeader)
{
    RtModule *k = rt_module_find("kernel32.dll");
    PVOID base, out;

    T_REQUIRE(k != NULL);

    T_CASE("the base address of kernel32 gives that base, stored and returned");
    out = (PVOID)(uintptr_t)0x5a5a;
    base = CALL(kernel32, RtlPcToFileHeader, (PVOID)(uintptr_t)k->base, &out);
    T_EQ((uintptr_t)base, (uintptr_t)k->base);
    T_EQ((uintptr_t)out, (uintptr_t)k->base);

    T_CASE("an address inside a module, not at its base, gives that module's base");
    out = (PVOID)(uintptr_t)0x5a5a;
    base = CALL(kernel32, RtlPcToFileHeader, (PVOID)(uintptr_t)(k->base + k->size - 1), &out);
    T_EQ((uintptr_t)base, (uintptr_t)k->base);
    T_EQ((uintptr_t)out, (uintptr_t)k->base);

    T_CASE("an address in no module gives NULL, stored and returned");
    out = (PVOID)(uintptr_t)0x5a5a;
    base = CALL(kernel32, RtlPcToFileHeader, (PVOID)(uintptr_t)0x10, &out);
    T_EQ((uintptr_t)base, 0);
    T_EQ((uintptr_t)out, 0);

    T_CASE("does not change last error, on success or on failure");
    CALL(kernel32, SetLastError, 0x1234);
    base = CALL(kernel32, RtlPcToFileHeader, (PVOID)(uintptr_t)k->base, &out);
    T_EQ((uintptr_t)base, (uintptr_t)k->base);
    T_EQ(T_LAST_ERROR(), 0x1234);
    base = CALL(kernel32, RtlPcToFileHeader, (PVOID)(uintptr_t)0x10, &out);
    T_EQ((uintptr_t)base, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: after the checks of the last case, no T_CASE of its own. */
    T_TRAPS(CALL(kernel32, RtlPcToFileHeader, (PVOID)(uintptr_t)k->base, NULL),
            "RtlPcToFileHeader with BaseOfImage NULL");
}
