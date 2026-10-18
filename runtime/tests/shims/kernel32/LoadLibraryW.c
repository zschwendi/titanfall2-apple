/* LoadLibraryW through the guest ABI, on native modules only: kernel32.dll is always loaded,
 * and no lifted module is loaded here.  The rules are the core's (rt_module_load, rt/module.h). */
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

/* LoadLibraryW with last error set to a marker first, so that "left alone on success" shows. */
static HMODULE ll(const WCHAR *name) {
    CALL(kernel32, SetLastError, 0xDEAD);
    return CALL(kernel32, LoadLibraryW, name);
}

TEST(kernel32, LoadLibraryW)
{
    RtModule *k = rt_module_find("kernel32.dll");
    HMODULE h;
    int32_t n;

    T_REQUIRE(k != NULL);

    T_CASE("loads a native module by its file name and counts the load");
    n = k->load_count;
    h = ll(W("kernel32.dll"));
    T_EQ((uintptr_t)h, (uintptr_t)k->base);
    T_EQ(k->load_count, n + 1);
    k->load_count--;                                /* put it back */

    T_CASE("success leaves last error alone");
    h = ll(W("kernel32.dll"));
    T_EQ((uintptr_t)h, (uintptr_t)k->base);
    T_EQ(T_LAST_ERROR(), 0xDEAD);                   /* the marker ll set */
    k->load_count--;

    T_CASE("a name no module has fails with ERROR_MOD_NOT_FOUND");
    n = k->load_count;
    h = ll(W("nosuchmodule.dll"));
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    T_EQ(k->load_count, n);

    T_CASE("a NULL or empty name fails with ERROR_INVALID_PARAMETER");
    h = ll(NULL);
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    h = ll(W(""));
    T_EQ((uintptr_t)h, 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
}
