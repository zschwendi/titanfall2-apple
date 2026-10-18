#include "rt/test.h"
#include "rt/locale.h"
#include "rt/win/nls.h"

/* The oracle is the machine description: the user default locale is en-US, 0x0409
 * (LANG_ENGLISH_US_LCID), and the language identifier is its low 16 bits. */
TEST(kernel32, GetUserDefaultUILanguage)
{
    LANGID v;

    T_CASE("returns the low 16 bits of the user default locale, en-US (0x0409)");
    v = CALL(kernel32, GetUserDefaultUILanguage);
    T_EQ(v, 0x0409);
    T_EQ(v, (LANGID)(LANG_ENGLISH_US_LCID & 0xFFFF));
    T_EQ(v, (LANGID)(rt_locale_default() & 0xFFFF));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetUserDefaultUILanguage);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
