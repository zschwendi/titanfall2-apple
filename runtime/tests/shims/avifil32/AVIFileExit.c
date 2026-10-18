/* Expected values: Microsoft's page of AVIFileExit (read 2026-10-10: "void AVIFileExit();",
 * return value "None"; no failure and no last error are named).  As for AVIFileInit, what can
 * be checked is that the call comes back, the log line, and last error.  A call with no
 * AVIFileInit before it is not on the page: that it returns is this runtime's choice. */
#include "rt/test.h"

TEST(avifil32, AVIFileExit)
{
    unsigned base = T_LOG_COUNT(RT_CH_FILE);

    T_CASE("returns after AVIFileInit, with the log line");
    CALL(avifil32, AVIFileInit);
    base = T_LOG_COUNT(RT_CH_FILE);
    CALL(avifil32, AVIFileExit);
    T_TRUE(T_LOGGED(RT_CH_FILE, "AVIFileExit: there is no AVIFile library to leave"));
    T_EQ(T_LOG_COUNT(RT_CH_FILE), base + 1);

    T_CASE("returns with no AVIFileInit before it, and more often than AVIFileInit was called");
    CALL(avifil32, AVIFileExit);
    CALL(avifil32, AVIFileExit);
    T_EQ(T_LOG_COUNT(RT_CH_FILE), base + 1);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(avifil32, AVIFileExit);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
