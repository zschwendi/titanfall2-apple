#include "rt/test.h"
#include "rt/module.h"
#include <string.h>

/* The core's comment (rt/unwind.h) says what *ImageBase holds after a failed look-up: 0 for a
 * pc in no module, the module's base for a pc in a native module.  The sentinel is written
 * before every call, so a call that leaves it alone is seen.  A function entry (a success)
 * needs a lifted module with unwind data, which a shim test does not have: that is the
 * core's test, core/unwind_corpus. */
TEST(kernel32, RtlLookupFunctionEntry)
{
    RtModule *k = rt_module_handle_w(u"kernel32.dll");
    DWORD64 base;
    unsigned char hist[64];
    PRUNTIME_FUNCTION rf;
    unsigned i;

    T_REQUIRE(k != NULL);

    T_CASE("an address in no module gives NULL and sets *ImageBase to 0");
    base = 0x5a5a;
    rf = CALL(kernel32, RtlLookupFunctionEntry, (DWORD64)0x10, &base, NULL);
    T_TRUE(rf == NULL);
    T_EQ(base, 0);

    T_CASE("a native module's address gives NULL and sets *ImageBase to that module's base");
    base = 0x5a5a;
    rf = CALL(kernel32, RtlLookupFunctionEntry, (DWORD64)k->base, &base, NULL);
    T_TRUE(rf == NULL);
    T_EQ(base, k->base);
    base = 0x5a5a;
    rf = CALL(kernel32, RtlLookupFunctionEntry, (DWORD64)(k->base + k->size - 1), &base, NULL);
    T_TRUE(rf == NULL);
    T_EQ(base, k->base);

    T_CASE("a non-NULL HistoryTable changes nothing");
    memset(hist, 0xA5, sizeof hist);
    base = 0x5a5a;
    rf = CALL(kernel32, RtlLookupFunctionEntry, (DWORD64)0x10, &base, (PUNWIND_HISTORY_TABLE)hist);
    T_TRUE(rf == NULL);
    T_EQ(base, 0);
    for (i = 0; i < sizeof hist; i++) T_EQ(hist[i], 0xA5);
    base = 0x5a5a;
    rf = CALL(kernel32, RtlLookupFunctionEntry, (DWORD64)k->base, &base, (PUNWIND_HISTORY_TABLE)hist);
    T_TRUE(rf == NULL);
    T_EQ(base, k->base);
    for (i = 0; i < sizeof hist; i++) T_EQ(hist[i], 0xA5);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    base = 0x5a5a;
    rf = CALL(kernel32, RtlLookupFunctionEntry, (DWORD64)0x10, &base, NULL);
    T_TRUE(rf == NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    rf = CALL(kernel32, RtlLookupFunctionEntry, (DWORD64)k->base, &base, NULL);
    T_TRUE(rf == NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: after the checks of the last case, no T_CASE of its own. */
    T_TRAPS(CALL(kernel32, RtlLookupFunctionEntry, (DWORD64)0x10, NULL, NULL), "NULL ImageBase");
}
