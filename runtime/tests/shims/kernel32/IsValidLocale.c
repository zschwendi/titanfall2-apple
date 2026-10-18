#include "rt/test.h"
#include "rt/win/error.h"
#include "rt/win/nls.h"

/* IsValidLocale has no output, so the checks are its return value and its last error.  The
 * oracle is the answer for en-US in rt/locale.h: TRUE for 0x0409 and the two *_DEFAULT values
 * with LCID_INSTALLED or LCID_SUPPORTED; FALSE with ERROR_INVALID_FLAGS for any other dwFlags
 * except 0.  dwFlags 0 is a validity query (Wine kernel32/tests/locale.c, test_LocaleNameToLCID,
 * the block that runs when RtlLcidToLocaleName exists): TRUE for LOCALE_INVARIANT, FALSE for
 * LOCALE_NEUTRAL, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT, no last error asserted there.
 * TRUE for 0x0409 with dwFlags 0 is inferred (the same test asserts TRUE for two other valid
 * locales).  The traps are fatal reports, each in its own forked child. */
TEST(kernel32, IsValidLocale)
{
    static const DWORD bad_flags[] = { 3, 0x10, 0x80000000u };

    T_CASE("en-US with LCID_INSTALLED is TRUE");
    T_EQ(CALL(kernel32, IsValidLocale, 0x0409, LCID_INSTALLED), TRUE);

    T_CASE("en-US with LCID_SUPPORTED is TRUE");
    T_EQ(CALL(kernel32, IsValidLocale, 0x0409, LCID_SUPPORTED), TRUE);

    T_CASE("LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT mean en-US");
    T_EQ(CALL(kernel32, IsValidLocale, LOCALE_USER_DEFAULT, LCID_INSTALLED), TRUE);
    T_EQ(CALL(kernel32, IsValidLocale, LOCALE_USER_DEFAULT, LCID_SUPPORTED), TRUE);
    T_EQ(CALL(kernel32, IsValidLocale, LOCALE_SYSTEM_DEFAULT, LCID_INSTALLED), TRUE);
    T_EQ(CALL(kernel32, IsValidLocale, LOCALE_SYSTEM_DEFAULT, LCID_SUPPORTED), TRUE);
    /* Traps: the other locales (de-DE, LOCALE_NEUTRAL, LOCALE_INVARIANT, LOCALE_CUSTOM_*). */
    T_TRAPS(CALL(kernel32, IsValidLocale, 0x0407, LCID_INSTALLED), "IsValidLocale with a locale other than en-US: 0x0407");
    T_TRAPS(CALL(kernel32, IsValidLocale, LOCALE_NEUTRAL, LCID_SUPPORTED), "IsValidLocale with a locale other than en-US: 0x0000");
    T_TRAPS(CALL(kernel32, IsValidLocale, LOCALE_INVARIANT, LCID_INSTALLED), "IsValidLocale with a locale other than en-US: 0x007F");
    T_TRAPS(CALL(kernel32, IsValidLocale, LOCALE_CUSTOM_DEFAULT, LCID_INSTALLED), "IsValidLocale with a locale other than en-US: 0x0C00");
    T_TRAPS(CALL(kernel32, IsValidLocale, LOCALE_CUSTOM_UNSPECIFIED, LCID_SUPPORTED), "IsValidLocale with a locale other than en-US: 0x1000");
    T_TRAPS(CALL(kernel32, IsValidLocale, LOCALE_CUSTOM_UI_DEFAULT, LCID_SUPPORTED), "IsValidLocale with a locale other than en-US: 0x1400");

    T_CASE("dwFlags 0 is a validity query: en-US and LOCALE_INVARIANT are TRUE, and LOCALE_NEUTRAL, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT are FALSE");
    T_EQ(CALL(kernel32, IsValidLocale, 0x0409, 0), TRUE);
    T_EQ(CALL(kernel32, IsValidLocale, LOCALE_INVARIANT, 0), TRUE);
    T_EQ(CALL(kernel32, IsValidLocale, LOCALE_NEUTRAL, 0), FALSE);
    T_EQ(CALL(kernel32, IsValidLocale, LOCALE_USER_DEFAULT, 0), FALSE);
    T_EQ(CALL(kernel32, IsValidLocale, LOCALE_SYSTEM_DEFAULT, 0), FALSE);
    /* Traps: dwFlags 0 settles only those locales; any other locale still stops. */
    T_TRAPS(CALL(kernel32, IsValidLocale, 0x0407, 0), "IsValidLocale with a locale other than en-US: 0x0407");

    T_CASE("any other dwFlags value is FALSE with ERROR_INVALID_FLAGS");
    for (unsigned i = 0; i < sizeof bad_flags / sizeof bad_flags[0]; i++) {
        CALL(kernel32, SetLastError, 0);
        T_EQ(CALL(kernel32, IsValidLocale, 0x0409, bad_flags[i]), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
    }
    /* Trap: 0x39 is not "any other" value; it is the documented "do not use" flag. */
    T_TRAPS(CALL(kernel32, IsValidLocale, 0x0409, 0x39), "IsValidLocale with dwFlags 0x39");

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, IsValidLocale, 0x0409, LCID_INSTALLED), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, IsValidLocale, LOCALE_SYSTEM_DEFAULT, LCID_SUPPORTED), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, IsValidLocale, 0x0409, 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
