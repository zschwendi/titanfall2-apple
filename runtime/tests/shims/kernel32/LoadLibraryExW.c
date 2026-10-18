/* LoadLibraryExW through the guest ABI, on native modules only: kernel32.dll is always loaded,
 * and no lifted module is loaded here.  The rules are the core's (rt_module_load, rt/module.h).
 * The stops of the core are proved with T_TRAPS after the last case. */
#include "rt/test.h"
#include "rt/module.h"

/* ASCII to wide, as runtime/tests/shims/kernel32/CreateFileW.c makes its names (WCHAR is not
 * wchar_t).  Four buffers rotate, so one statement may use several names. */
static const WCHAR *W(const char *a) {
    static WCHAR buf[4][512];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    unsigned i = 0;
    for (; a[i]; i++) b[i] = (unsigned char)a[i];
    b[i] = 0;
    return b;
}

/* LoadLibraryExW with last error set to a marker first, so that "left alone on success" shows. */
static HMODULE ll(const WCHAR *name, HANDLE hFile, DWORD flags) {
    CALL(kernel32, SetLastError, 0xDEAD);
    return CALL(kernel32, LoadLibraryExW, name, hFile, flags);
}

TEST(kernel32, LoadLibraryExW)
{
    RtModule *k = rt_module_find("kernel32.dll");
    HMODULE h;
    int32_t n;

    T_REQUIRE(k != NULL);

    T_CASE("loads a native module by its file name and counts the load");
    n = k->load_count;
    h = ll(W("kernel32.dll"), NULL, 0);
    T_EQ((uintptr_t)h, (uintptr_t)k->base);
    T_EQ(k->load_count, n + 1);
    k->load_count--;                                /* put it back */

    T_CASE("success leaves last error alone");
    h = ll(W("kernel32.dll"), NULL, 0);
    T_EQ((uintptr_t)h, (uintptr_t)k->base);
    T_EQ(T_LAST_ERROR(), 0xDEAD);                   /* the marker ll set */
    k->load_count--;

    T_CASE("a name no module has fails with ERROR_MOD_NOT_FOUND");
    n = k->load_count;
    h = ll(W("nosuchmodule.dll"), NULL, 0);
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    T_EQ(k->load_count, n);

    T_CASE("a NULL or empty name fails with ERROR_INVALID_PARAMETER, whatever the flags");
    h = ll(NULL, NULL, 0);
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    h = ll(W(""), NULL, 0);
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    h = ll(NULL, NULL, LOAD_LIBRARY_AS_DATAFILE);   /* the name is checked before the stop flags */
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    h = ll(W(""), NULL, LOAD_LIBRARY_AS_DATAFILE);
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("a flag bit of 0x80000000 fails with ERROR_INVALID_PARAMETER");
    n = k->load_count;
    h = ll(W("kernel32.dll"), NULL, 0x80000000u);
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(k->load_count, n);

    T_CASE("LOAD_WITH_ALTERED_SEARCH_PATH is accepted");
    n = k->load_count;
    h = ll(W("kernel32.dll"), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    T_EQ((uintptr_t)h, (uintptr_t)k->base);
    T_EQ(k->load_count, n + 1);
    k->load_count--;

    T_CASE("hFile not NULL fails with ERROR_INVALID_PARAMETER");
    n = k->load_count;
    h = ll(W("kernel32.dll"), (HANDLE)(uintptr_t)4, 0);
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(k->load_count, n);

    /* The core's stops.  Each uses a real name, so the name check passes and the flag is reached. */
    T_TRAPS(ll(W("kernel32.dll"), NULL, LOAD_LIBRARY_AS_DATAFILE), "with LOAD_LIBRARY_AS_DATAFILE (flags 0x2)");
    T_TRAPS(ll(W("kernel32.dll"), NULL, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE), "with LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE (flags 0x40)");
    T_TRAPS(ll(W("kernel32.dll"), NULL, LOAD_LIBRARY_AS_IMAGE_RESOURCE), "with LOAD_LIBRARY_AS_IMAGE_RESOURCE (flags 0x20)");
    T_TRAPS(ll(W("kernel32.dll"), NULL, DONT_RESOLVE_DLL_REFERENCES), "with DONT_RESOLVE_DLL_REFERENCES (flags 0x1)");
    T_TRAPS(ll(W("kernel32.dll"), NULL, LOAD_IGNORE_CODE_AUTHZ_LEVEL), "with LOAD_IGNORE_CODE_AUTHZ_LEVEL (flags 0x10)");
}
