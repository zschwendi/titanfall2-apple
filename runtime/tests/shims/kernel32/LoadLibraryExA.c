#include "rt/test.h"
#include "rt/module.h"

/* kernel32.dll is a native module, always loaded: each load of it adds one to its load count,
 * which the test takes back with load_count--.  The stops are proved last, after the checks
 * of the last case. */
TEST(kernel32, LoadLibraryExA)
{
    RtModule *k = rt_module_find("kernel32.dll");
    HMODULE base;
    int32_t n;

    T_REQUIRE(k != NULL);
    base = (HMODULE)(uintptr_t)k->base;

    T_CASE("a native module by its name, in any case, gives its base, and its load count is one higher each time");
    n = k->load_count;
    T_EQ(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, 0), base);
    T_EQ(k->load_count, n + 1);
    T_EQ(CALL(kernel32, LoadLibraryExA, "KERNEL32", NULL, 0), base);
    T_EQ(k->load_count, n + 2);
    k->load_count--;
    k->load_count--;

    T_CASE("a name no module has fails with ERROR_MOD_NOT_FOUND");
    T_EQ(CALL(kernel32, LoadLibraryExA, "nosuch_titanfall.dll", NULL, 0), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);

    T_CASE("a byte of 0xE9 in the last path component fails with ERROR_MOD_NOT_FOUND, in a directory part it is ignored and the load succeeds");
    n = k->load_count;
    T_EQ(CALL(kernel32, LoadLibraryExA, "caf\xE9.dll", NULL, 0), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    T_EQ(CALL(kernel32, LoadLibraryExA, "caf\xE9\\kernel32.dll", NULL, 0), base);
    T_EQ(k->load_count, n + 1);
    k->load_count--;

    T_CASE("a NULL name fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(kernel32, LoadLibraryExA, NULL, NULL, 0), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(CALL(kernel32, LoadLibraryExA, NULL, NULL, DONT_RESOLVE_DLL_REFERENCES), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);          /* the name is checked before the stop */

    T_CASE("hFile not NULL fails with ERROR_INVALID_PARAMETER");
    n = k->load_count;
    T_EQ(CALL(kernel32, LoadLibraryExA, "kernel32.dll", (HANDLE)(uintptr_t)4, 0), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(k->load_count, n);                                 /* the module is not touched */

    T_CASE("LOAD_WITH_ALTERED_SEARCH_PATH and the LOAD_LIBRARY_SEARCH flags are accepted and ignored");
    n = k->load_count;
    T_EQ(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_WITH_ALTERED_SEARCH_PATH), base);
    T_EQ(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR), base);
    T_EQ(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR), base);
    T_EQ(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_LIBRARY_SEARCH_USER_DIRS), base);
    T_EQ(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32), base);
    T_EQ(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS), base);
    T_EQ(k->load_count, n + 6);
    for (int i = 0; i < 6; i++) k->load_count--;

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, 0), base);
    T_EQ(T_LAST_ERROR(), 0x1234);
    k->load_count--;

    T_TRAPS(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, DONT_RESOLVE_DLL_REFERENCES), "with DONT_RESOLVE_DLL_REFERENCES (flags");
    T_TRAPS(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE), "with LOAD_LIBRARY_AS_DATAFILE (flags");
    T_TRAPS(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE), "with LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE (flags");
    T_TRAPS(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE), "with LOAD_LIBRARY_AS_IMAGE_RESOURCE (flags");
    T_TRAPS(CALL(kernel32, LoadLibraryExA, "kernel32.dll", NULL, LOAD_IGNORE_CODE_AUTHZ_LEVEL), "with LOAD_IGNORE_CODE_AUTHZ_LEVEL (flags");
}
