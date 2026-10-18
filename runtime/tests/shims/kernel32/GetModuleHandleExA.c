#include "rt/test.h"
#include "rt/module.h"

/* kernel32.dll is a native module, always loaded, found by name.  Its load count is never
 * changed by the test except where a case below raises it (flags without UNCHANGED_REFCOUNT),
 * and each raise is put back at once: the test is single-threaded.  The executable is the
 * native module rt_module_exe(). */
#define SENTINEL ((HMODULE)(uintptr_t)0x5a5a5a5a5a5a5a5aull)

TEST(kernel32, GetModuleHandleExA)
{
    RtModule *k, *exe;
    HMODULE base, h;
    int32_t n;

    k = rt_module_find("kernel32.dll");
    T_REQUIRE(k != NULL);
    base = (HMODULE)(uintptr_t)k->base;

    T_CASE("by name, with UNCHANGED_REFCOUNT, gives the module and leaves its load count alone");
    n = k->load_count;
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, "kernel32.dll", &h), TRUE);
    T_EQ((uint64_t)(uintptr_t)h, (uint64_t)(uintptr_t)base);
    T_EQ(k->load_count, n);

    T_CASE("by name with flags 0 raises the load count by one");
    n = k->load_count;
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, 0, "kernel32.dll", &h), TRUE);
    T_EQ((uint64_t)(uintptr_t)h, (uint64_t)(uintptr_t)base);
    T_EQ(k->load_count, n + 1);
    k->load_count--;                                /* put it back */

    T_CASE("a name without the extension, or in capitals, is found");
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, "kernel32", &h), TRUE);
    T_EQ((uint64_t)(uintptr_t)h, (uint64_t)(uintptr_t)base);
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, "KERNEL32.DLL", &h), TRUE);
    T_EQ((uint64_t)(uintptr_t)h, (uint64_t)(uintptr_t)base);

    T_CASE("the NULL name gives the executable");
    exe = rt_module_exe();
    T_REQUIRE(exe != NULL);
    n = exe->load_count;
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, NULL, &h), TRUE);
    T_EQ((uint64_t)(uintptr_t)h, (uint64_t)(uintptr_t)exe->base);
    T_EQ(exe->load_count, n);

    T_CASE("by address, the module whose range holds the address is found");
    n = k->load_count;
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              (LPCVOID)(uintptr_t)k->base, &h), TRUE);
    T_EQ((uint64_t)(uintptr_t)h, (uint64_t)(uintptr_t)base);
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              (LPCVOID)(uintptr_t)(k->base + 0x10), &h), TRUE);      /* inside the module, not its base */
    T_EQ((uint64_t)(uintptr_t)h, (uint64_t)(uintptr_t)base);
    T_EQ(k->load_count, n);
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
              (LPCVOID)(uintptr_t)k->base, &h), TRUE);
    T_EQ((uint64_t)(uintptr_t)h, (uint64_t)(uintptr_t)base);
    T_EQ(k->load_count, n + 1);
    k->load_count--;                                /* put it back */

    T_CASE("a NULL address with the address flag fails with ERROR_MOD_NOT_FOUND");
    CALL(kernel32, SetLastError, 0);
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, NULL, &h), FALSE);
    T_EQ((uint64_t)(uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);

    T_CASE("a name no module has fails with ERROR_MOD_NOT_FOUND and sets *phModule to NULL");
    n = k->load_count;
    CALL(kernel32, SetLastError, 0);
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, 0, "nosuch.dll", &h), FALSE);
    T_EQ((uint64_t)(uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    T_EQ(k->load_count, n);

    T_CASE("PIN with UNCHANGED_REFCOUNT, a flag bit of 8 and phModule NULL fail with ERROR_INVALID_PARAMETER");
    n = k->load_count;
    CALL(kernel32, SetLastError, 0);
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              "kernel32.dll", &h), FALSE);
    T_EQ((uint64_t)(uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, 0x8, "kernel32.dll", &h), FALSE);
    T_EQ((uint64_t)(uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetModuleHandleExA, 0, "kernel32.dll", NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(k->load_count, n);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    h = SENTINEL;
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, "kernel32.dll", &h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, GetModuleHandleExA, GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              (LPCVOID)(uintptr_t)k->base, &h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, GetModuleHandleExA, 0, "kernel32.dll", &h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    k->load_count--;                                /* put it back */
}
