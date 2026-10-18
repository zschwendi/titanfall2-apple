#include "rt/test.h"
#include "rt/locale.h"
#include "rt/win/nls.h"

/* The oracle is the machine description: the user default locale is en-US, 0x0409
 * (LANG_ENGLISH_US_LCID), and the core's rt_locale_default() gives the same value. */
TEST(kernel32, GetUserDefaultLCID)
{
    LCID v;

    T_CASE("returns the user default locale, en-US (0x0409)");
    v = CALL(kernel32, GetUserDefaultLCID);
    T_EQ(v, LANG_ENGLISH_US_LCID);
    T_EQ(v, rt_locale_default());

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetUserDefaultLCID);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
