#include "rt/test.h"
#include "rt/module.h"

/* The oracle is the core: rt_module_export on the native kernel32 module (or on the
 * executable), called directly.  The shim must give what the core gives for the same name or
 * ordinal.  kernel32 has a shim for GetTickCount and GetTickCount64, and none for FlsAlloc. */

TEST(kernel32, GetProcAddress)
{
    RtModule *m = rt_module_find("kernel32.dll");
    RtModule *exe = rt_module_exe();
    HMODULE h;
    GUESTFN want;
    DWORD want_err;

    T_REQUIRE(m != NULL);
    T_REQUIRE(exe != NULL);
    h = (HMODULE)(uintptr_t)m->base;

    T_CASE("a name the module exports gives the core's address (base + RVA)");
    want = rt_module_export(m, "GetTickCount", 0);
    T_TRUE(want != 0);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetProcAddress, h, "GetTickCount"), want);
    T_EQ(T_LAST_ERROR(), 0x1234);                   /* success leaves last error alone */

    T_CASE("a name the module does not have, or spelled in another case, gives NULL with ERROR_PROC_NOT_FOUND");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetProcAddress, h, "NoSuchKernel32Export"), 0);
    T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetProcAddress, h, "gettickcount"), 0);     /* names are case-sensitive */
    T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);

    T_CASE("FlsAlloc, a name the C runtime probes for and kernel32 does not have, gives NULL with ERROR_PROC_NOT_FOUND");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetProcAddress, h, "FlsAlloc"), 0);
    T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);

    T_CASE("an ordinal in the low word is answered as the core answers it");
    want = rt_module_export(m, NULL, 1);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetProcAddress, h, (LPCVOID)(uintptr_t)1), want);
    if (want == 0) T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);
    want = rt_module_export(m, NULL, 0xFFFF);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetProcAddress, h, (LPCVOID)(uintptr_t)0xFFFF), want);
    if (want == 0) T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);

    T_CASE("a NULL name is ordinal 0, which the core answers");
    want = rt_module_export(m, NULL, 0);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetProcAddress, h, NULL), want);
    if (want == 0) T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);

    T_CASE("a module handle that is no module gives NULL with ERROR_MOD_NOT_FOUND");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)4, "GetTickCount"), 0);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)(m->base + 1), "GetTickCount"), 0);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);

    T_CASE("the NULL handle is the executable, as the core answers it");
    CALL(kernel32, SetLastError, 0);
    want = rt_module_export(exe, "GetTickCount", 0);
    want_err = T_LAST_ERROR();                      /* whatever the core left */
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetProcAddress, NULL, "GetTickCount"), want);
    T_EQ(T_LAST_ERROR(), want_err);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_TRUE(CALL(kernel32, GetProcAddress, h, "GetTickCount64") != 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_TRUE(CALL(kernel32, GetProcAddress, h, "GetTickCount") != 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
