#include "rt/test.h"
#include "rt/locale.h"

/* The callbacks record each call: how many there were and the name each was given, a NUL-
 * terminated string of RT_LOCALE_ENUM_NAME_MAX WCHARs.  The core lists one locale, so a callback
 * that returns FALSE and one that returns TRUE both see exactly one call; the test checks the
 * count and the return value, and cannot see the enumeration stop with a single entry. */
static unsigned calls;
static WCHAR seen[RT_LOCALE_ENUM_NAME_MAX];

static void remember(Ctx *ctx)
{
    const WCHAR *name = (const WCHAR *)ctx->r[1];      /* BOOL CALLBACK LOCALE_ENUMPROCW(LPWSTR) */
    calls++;
    for (unsigned i = 0; i < RT_LOCALE_ENUM_NAME_MAX; i++) seen[i] = name[i];
}

GUEST_FN(cb_true)  { remember(ctx); ctx->r[0] = TRUE;  GUEST_RET(ctx); }
GUEST_FN(cb_false) { remember(ctx); ctx->r[0] = FALSE; GUEST_RET(ctx); }

static int seen_is_00000409(void)
{
    static const WCHAR want[RT_LOCALE_ENUM_NAME_MAX] = { '0', '0', '0', '0', '0', '4', '0', '9', 0 };
    for (unsigned i = 0; i < RT_LOCALE_ENUM_NAME_MAX; i++)
        if (seen[i] != want[i]) return 0;
    return 1;
}

TEST(kernel32, EnumSystemLocalesW)
{
    static const DWORD good_flags[] = { 0, LCID_SUPPORTED, LCID_INSTALLED };
    static const DWORD bad_flags[] = {
        LCID_INSTALLED | LCID_SUPPORTED, 0x8u, 0x80000000u, 0xFFFFFFFFu,
        LCID_INSTALLED | LCID_SUPPORTED | LCID_ALTERNATE_SORTS,
    };

    T_CASE("calls the callback once with the name 00000409");
    for (unsigned i = 0; i < sizeof good_flags / sizeof good_flags[0]; i++) {
        calls = 0;
        T_EQ(CALL(kernel32, EnumSystemLocalesW, T_GUEST_ADDR(cb_true), good_flags[i]), TRUE);
        T_EQ(calls, 1);
        T_TRUE(seen_is_00000409());
    }

    T_CASE("a callback returning FALSE: the function still returns TRUE");
    calls = 0;
    T_EQ(CALL(kernel32, EnumSystemLocalesW, T_GUEST_ADDR(cb_false), 0), TRUE);
    T_EQ(calls, 1);
    T_TRUE(seen_is_00000409());

    T_CASE("flags the core rejects fail with ERROR_INVALID_FLAGS");
    for (unsigned i = 0; i < sizeof bad_flags / sizeof bad_flags[0]; i++) {
        calls = 0;
        CALL(kernel32, SetLastError, 0);
        T_EQ(CALL(kernel32, EnumSystemLocalesW, T_GUEST_ADDR(cb_true), bad_flags[i]), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_FLAGS);
        T_EQ(calls, 0);
    }

    T_CASE("a NULL callback fails with ERROR_INVALID_PARAMETER");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, EnumSystemLocalesW, 0, LCID_SUPPORTED), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, EnumSystemLocalesW, 0, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, EnumSystemLocalesW, T_GUEST_ADDR(cb_true), 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, EnumSystemLocalesW, T_GUEST_ADDR(cb_false), LCID_SUPPORTED), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* LCID_ALTERNATE_SORTS (alone, and with a valid flag) is refused: a fatal report, not a return. */
    T_TRAPS(CALL(kernel32, EnumSystemLocalesW, T_GUEST_ADDR(cb_true), LCID_ALTERNATE_SORTS),
            "LCID_ALTERNATE_SORTS");
    T_TRAPS(CALL(kernel32, EnumSystemLocalesW, T_GUEST_ADDR(cb_true), LCID_INSTALLED | LCID_ALTERNATE_SORTS),
            "LCID_ALTERNATE_SORTS");
}
