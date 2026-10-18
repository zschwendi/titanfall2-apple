/* Expected values: Microsoft's page of AVIFileInit (read 2026-10-10: "void AVIFileInit();",
 * return value "None"; no failure and no last error are named).  The function returns nothing
 * and takes nothing, so what can be checked is that the call comes back (without the shim the
 * import is a stop), the log line that says the library is not there, and last error.  That
 * the line is logged once is the rule for a shim of status absent (docs/RUNTIME.md 9.2). */
#include "rt/test.h"

TEST(avifil32, AVIFileInit)
{
    unsigned base = T_LOG_COUNT(RT_CH_FILE);

    T_CASE("returns, with the log line");
    CALL(avifil32, AVIFileInit);
    T_TRUE(T_LOGGED(RT_CH_FILE, "AVIFileInit: there is no AVIFile library"));
    T_EQ(T_LOG_COUNT(RT_CH_FILE), base + 1);

    T_CASE("a second and a third call return too and log nothing more");
    CALL(avifil32, AVIFileInit);
    CALL(avifil32, AVIFileInit);
    T_EQ(T_LOG_COUNT(RT_CH_FILE), base + 1);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(avifil32, AVIFileInit);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
