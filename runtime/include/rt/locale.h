/* Code pages, character classification, case mapping and the en-US locale data
 * (docs/RUNTIME.md section 8.4).  Implementation: runtime/core/locale.c.
 *
 * This is the core under the shims MultiByteToWideChar, WideCharToMultiByte, GetACP,
 * GetOEMCP, GetCPInfo, IsValidCodePage, GetStringTypeW, LCMapStringW, CompareStringW,
 * GetLocaleInfoW, GetUserDefaultLCID, IsValidLocale, EnumSystemLocalesW, GetDateFormatW
 * and GetTimeFormatW.  Each function here has the arguments and the return value of the
 * Windows function it stands behind (minus the calling convention), so a shim is a one-line
 * forwarder:
 *
 *     SHIM(kernel32, int, MultiByteToWideChar, UINT cp, DWORD flags, LPCSTR src, int cb, LPWSTR dst, int cch)
 *     {
 *         return rt_mb_to_wide(cp, flags, src, cb, dst, cch);
 *     }
 *
 * The machine is en-US: locale 0x0409, ANSI code page 1252, OEM code page 437.  The core
 * is written to be exact for that machine and loud everywhere else.
 *
 * Three rules hold for every function below.
 *
 * 1. LAST ERROR.  A function that fails returns the Windows failure value (0 or FALSE) and
 *    sets the calling thread's last error (rt_thread_set_last_error) to the code named in
 *    its comment.  A function that succeeds leaves last error alone.  So a shim never sets
 *    last error itself and never needs to look at it.
 *
 * 2. A CASE WE DO NOT HANDLE IS FATAL, NOT A GUESS.  When a call asks for something the
 *    machine description does not cover (a locale other than en-US, a rarely used flag, a
 *    character whose class or case we have no table entry for, a best-fit conversion we
 *    have no table for), the function does not return.  It ends in rt_unimplemented
 *    (rt/log.h): one "FATAL unimplemented: ..." block that names the Windows function, the
 *    case and the offending value, and the guest caller.  Each such case is listed in the
 *    comment of the function.
 *    WHICH LOCALE VALUES MEAN en-US (this paragraph overrides the shorter wording at the
 *    functions): 0x0409, LOCALE_USER_DEFAULT (0x0400) and LOCALE_SYSTEM_DEFAULT (0x0800)
 *    in every function.  Locale 0 (LOCALE_NEUTRAL) is read as the user default, as
 *    Windows reads it, by rt_lcmap_string (LCMapStringW), rt_compare_string
 *    (CompareStringW), rt_locale_info (GetLocaleInfoW), rt_date_format (GetDateFormatW)
 *    and rt_time_format (GetTimeFormatW): the C runtime's start-up passes 0.
 *    LOCALE_INVARIANT (0x007F) is taken by rt_lcmap_string and rt_compare_string only
 *    (simple case mapping and comparison give what en-US gives; not verified beyond that
 *    neither is tailored), and by rt_locale_is_valid with dwFlags 0 only.  Every other
 *    locale value is fatal, except that with dwFlags 0 rt_locale_is_valid answers FALSE for
 *    LOCALE_NEUTRAL, LOCALE_USER_DEFAULT and LOCALE_SYSTEM_DEFAULT.  A failure that Windows
 *    itself reports (bad flags, buffer too
 *    small, invalid input with MB_ERR_INVALID_CHARS) is NOT fatal: it is a normal return.
 *    A code page the machine does not have is a normal failure of its own kind: the
 *    function fails with ERROR_INVALID_PARAMETER and logs one line on the `import` channel
 *    (the code pages the game asks for are what the first runs are for).
 *
 * 3. NO HOST LOCALE.  Nothing here calls wchar_t, mbstowcs, iconv, setlocale, towupper or
 *    any other host function that depends on the user's macOS settings.  Every table is
 *    constant data in locale.c, so every function is thread-safe and reentrant.
 *
 * Buffers are described the way the Windows functions describe them: a length of -1 (for
 * the string functions: any negative length) means "NUL-terminated, and the terminator is
 * part of the string"; an output length of 0 asks only for the size and the output pointer
 * is not used.  Sizes are int, as in Windows, and counts are in the unit of the output
 * (WCHARs or bytes).  Where Windows documents "the buffers must not overlap", identical
 * pointers are ERROR_INVALID_PARAMETER as documented, and any other overlap (which Windows
 * leaves undefined) is fatal.
 *
 * Guest-visible types are the fixed-width ones of rt/wintypes.h.  The constants below are
 * the values of winnls.h, winnt.h and stringapiset.h; rt/win/ does not have them yet.
 */
#ifndef RT_LOCALE_H
#define RT_LOCALE_H

#include "rt/wintypes.h"
#include "rt/win/base.h"       /* SYSTEMTIME */
#include "rt/win/nls.h"        /* the constants of winnls.h, CPINFO */

/* SYSTEMTIME under the name this unit was written with.  The date and time functions
 * below read it the way GetDateFormatW and GetTimeFormatW do. */
typedef SYSTEMTIME RtSystemTime;

/* ==== code pages ========================================================================== */

/* GetACP / GetOEMCP: 1252 and 437, read from the machine description (rt/machine.h). */
UINT rt_cp_acp(void);
UINT rt_cp_oemcp(void);

/* IsValidCodePage.  TRUE for 1252, 437, 20127, 28591 and 65001, which are the code pages
 * this core converts.  FALSE for anything else (0 and the other special values included;
 * Windows would say TRUE for several of them, but "valid" here means "this machine can
 * convert it"), and the first time a code page is refused its number is logged once on the
 * `import` channel.  Never fatal; never sets last error. */
BOOL rt_cp_is_valid(UINT cp);

/* GetCPInfo.  CP_ACP, CP_OEMCP and CP_THREAD_ACP mean 1252, 437 and 1252.  Fills *out
 * for a supported code page (see rt_cp_is_valid) and returns TRUE.  Returns FALSE with
 * ERROR_INVALID_PARAMETER for out == NULL and for any code page we do not convert (one
 * log line on `import`).  The single-byte pages report MaxCharSize 1, DefaultChar {'?', 0},
 * LeadByte all zero.  UTF-8 reports MaxCharSize 4, DefaultChar {'?', 0}, LeadByte all zero
 * (so on Windows: Wine kernel32/tests/locale.c test_GetCPInfo).  Never fatal. */
BOOL rt_cp_info(UINT cp, CPINFO *out);

/* MultiByteToWideChar.  Converts `src_len` bytes at `src` (or the NUL-terminated string
 * including its NUL if src_len is negative) from code page `cp` to UTF-16.  With dst_len 0
 * nothing is written and the result is the number of WCHARs the conversion needs; otherwise
 * the result is the number of WCHARs written.  A result of 0 is a failure; the last error
 * is one of
 *   ERROR_INVALID_PARAMETER   src NULL; src_len 0; dst_len negative; dst NULL with dst_len
 *                             nonzero; dst == src; a code page we do not convert (this
 *                             one also logs)
 *   ERROR_INVALID_FLAGS       flags not valid for the code page: see below
 *   ERROR_INSUFFICIENT_BUFFER dst_len is nonzero and too small (as much as fits has been
 *                             written first; for UTF-8 only whole characters)
 *   ERROR_NO_UNICODE_TRANSLATION  MB_ERR_INVALID_CHARS is set and the input is invalid
 * checked in that order (parameters, code page, flags).  A code page we do not carry is
 * refused before the flags are looked at, bad flags or not.  A code page that is not valid
 * on Windows (0xdeadbeef: Wine kernel32/tests/codepage.c, test_other_invalid_parameters) gets
 * Windows's answer.  Windows checks the flags first for the real code pages we do not carry
 * (932 is one: Shift-JIS), so for those the answer differs from Windows only when the flags
 * are bad as well.
 *
 * Code pages: 1252, 437, 20127 (US-ASCII), 28591 (ISO 8859-1) and CP_UTF8 (65001), plus
 * the special values CP_ACP (1252), CP_OEMCP (437), CP_THREAD_ACP (1252).
 * Flags: single-byte pages accept MB_PRECOMPOSED, MB_COMPOSITE (fatal if set),
 * MB_USEGLYPHCHARS (fatal if set) and MB_ERR_INVALID_CHARS; PRECOMPOSED together with
 * COMPOSITE, or any other bit, is ERROR_INVALID_FLAGS.  UTF-8 accepts only 0 and
 * MB_ERR_INVALID_CHARS; everything else is ERROR_INVALID_FLAGS.
 *
 * 1252 maps every byte, as Windows does: the five bytes the code page leaves undefined
 * (0x81 0x8D 0x8F 0x90 0x9D) become the control characters of the same value.  437 maps
 * every byte to the character of the page (0x00 to 0x1F and 0x7F are controls; the glyph
 * shapes need MB_USEGLYPHCHARS).  28591 is the identity.  20127 maps 0x00 to 0x7F; a byte
 * from 0x80 up is invalid: with MB_ERR_INVALID_CHARS the call fails, without it the result
 * is fatal (Windows's replacement character for it was not verified).
 *
 * UTF-8 follows Unicode: overlong forms, encoded surrogates, values above U+10FFFF, stray
 * continuation bytes and truncated sequences are invalid.  Without MB_ERR_INVALID_CHARS
 * each maximal invalid subsequence becomes one U+FFFD (for the usual truncated-sequence
 * cases this is one per bad byte; Windows's count for a truncated sequence with valid
 * leading bytes was not verified); with it the call fails.  Supplementary characters
 * become surrogate pairs.  No byte-order mark is removed.
 *
 * The output is not NUL-terminated unless the input's NUL is part of it (src_len -1, or
 * counted).  Embedded NULs are converted like any other byte.  Fatal: MB_COMPOSITE,
 * MB_USEGLYPHCHARS, a byte >= 0x80 in 20127 without MB_ERR_INVALID_CHARS, a result that
 * would not fit in an int, partially overlapping buffers. */
int rt_mb_to_wide(UINT cp, DWORD flags, const char *src, int src_len, WCHAR *dst, int dst_len);

/* WideCharToMultiByte.  Converts `src_len` WCHARs at `src` (or the NUL-terminated string
 * including its NUL if src_len is negative) to code page `cp`.  With dst_len 0 the result
 * is the number of bytes needed; otherwise the number of bytes written.  0 is a failure:
 *   ERROR_INVALID_PARAMETER   src NULL; src_len 0; dst_len negative; dst NULL with dst_len
 *                             nonzero; dst == src; def_char or used_default given for
 *                             UTF-8; a code page we do not convert (logs)
 *   ERROR_INVALID_FLAGS       flags not valid for the code page
 *   ERROR_INSUFFICIENT_BUFFER dst_len nonzero and too small (whole characters that fit
 *                             have been written)
 *   ERROR_NO_UNICODE_TRANSLATION  WC_ERR_INVALID_CHARS (UTF-8 only) and the input has an
 *                             unpaired surrogate
 * checked in that order (parameters, UTF-8 default characters, code page, flags).  The code
 * page rule is the one stated for rt_mb_to_wide: a page we do not carry is refused before the
 * flags, and 932 differs from Windows only when the flags are bad as well.
 *
 * Single-byte pages: a character the page has directly (the 256 of the page, the 1252
 * controls U+0081 U+008D U+008F U+0090 U+009D included) becomes its byte.  Any other
 * character is "unrepresentable".  With WC_NO_BEST_FIT_CHARS it becomes the default
 * character: *def_char if def_char is not NULL, else '?'; and *used_default (if given)
 * is set TRUE if that happened at least once and FALSE otherwise (it is written on every
 * successful call, and on no failed one).  A surrogate pair counts as two unrepresentable
 * units (not verified).  WITHOUT WC_NO_BEST_FIT_CHARS Windows maps many such characters
 * to look-alikes ("best fit": U+0101 to 'a', U+FF21 to 'A').  Code page 1252 (and so
 * CP_ACP) has Windows's table here, 442 substitutions generated from Microsoft's
 * bestfit1252.txt: a substitution is NOT a use of the default character (*used_default
 * stays FALSE for it); a character the table lacks (U+039C, a CJK ideograph) becomes the
 * default character with *used_default TRUE.  The C runtime's start-up depends on both
 * (U+0191 becomes 0x83, U+039C the default character).  On the other single-byte pages
 * (437, 20127, 28591) an unrepresentable character without that flag is FATAL, naming the
 * character and the code page: their tables are not included.
 * Flags for the single-byte pages: WC_NO_BEST_FIT_CHARS, WC_COMPOSITECHECK (fatal if the
 * input has a combining mark U+0300..U+036F, otherwise no effect).  WC_DISCARDNS,
 * WC_SEPCHARS and WC_DEFAULTCHAR are fatal (their meaning without WC_COMPOSITECHECK was
 * not verified).  WC_ERR_INVALID_CHARS and any other bit: ERROR_INVALID_FLAGS.
 *
 * UTF-8 accepts 0 and WC_ERR_INVALID_CHARS only (else ERROR_INVALID_FLAGS).  Surrogate
 * pairs become 4-byte sequences; an unpaired surrogate becomes U+FFFD (EF BF BD), or fails
 * with WC_ERR_INVALID_CHARS.
 *
 * Fatal also: a result that would not fit in an int, partially overlapping buffers. */
int rt_wide_to_mb(UINT cp, DWORD flags, const WCHAR *src, int src_len, char *dst, int dst_len,
                  const char *def_char, BOOL *used_default);

/* ==== characters, classification, case ==================================================== */

/* Simple (one-for-one) case mapping of one UTF-16 code unit, from the table behind
 * LCMapStringW.  The table is exact for U+0000..U+052F (Latin, IPA partners, Greek,
 * Cyrillic); U+0130 and U+0131 map to themselves.  Code units outside it have no
 * mapping here: they come back unchanged, and rt_wchar_case_known says whether that is
 * right.  Surrogates map to themselves. */
WCHAR rt_wchar_upper(WCHAR c);
WCHAR rt_wchar_lower(WCHAR c);

/* TRUE if the case of `c` is covered: it is in the table, or it has no Unicode simple case
 * mapping at all.  FALSE for the code units above U+052F that do have one (Armenian,
 * Georgian, Latin Extended Additional, Greek Extended, fullwidth letters, ...): the string
 * functions refuse those. */
bool rt_wchar_case_known(WCHAR c);

/* TRUE if GetStringTypeW answers for `c`: U+0000..U+024F, U+0370..U+052F, and the
 * characters of code pages 1252 and 437, except the ones whose class this core does not
 * trust (titlecase letters, combining marks, unassigned code points).  This is the answer
 * for CT_CTYPE1 and CT_CTYPE2; CT_CTYPE3 additionally answers for surrogate units. */
bool rt_wchar_type_known(WCHAR c);

/* GetStringTypeW.  `info_type` is exactly one of CT_CTYPE1, CT_CTYPE2, CT_CTYPE3.  Writes
 * one WORD to out[i] for each of the src_len WCHARs (all of the string and its NUL if
 * src_len is negative: any negative value), returns TRUE.  FALSE with
 *   ERROR_INVALID_FLAGS      info_type has a bit other than the three
 *   ERROR_INVALID_PARAMETER  none or more than one of the three bits; src or out NULL;
 *                            src_len 0; out == src (as a pointer)
 * Values (C1_*, C2_*, C3_*) are derived from the Unicode general category and
 * bidirectional class, with the corrections written in locale.c; they are exact for ASCII
 * and for the characters of code pages 1252 and 437, and a best reading for the rest of
 * the table.  CT_CTYPE3 of a surrogate unit is C3_HIGHSURROGATE or C3_LOWSURROGATE.
 * FATAL for a code unit rt_wchar_type_known refuses (surrogates in CT_CTYPE1 and
 * CT_CTYPE2 included), naming the code unit. */
BOOL rt_string_type(DWORD info_type, const WCHAR *src, int src_len, WORD *out);

/* LCMapStringW for LCMAP_LOWERCASE or LCMAP_UPPERCASE, alone or with LCMAP_LINGUISTIC_CASING
 * (no effect for en-US).  Maps each code unit through rt_wchar_lower / rt_wchar_upper.
 * `locale` must resolve to en-US (0x0409, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT);
 * any other locale is fatal.  Lengths as MultiByteToWideChar (src_len negative: NUL-
 * terminated and the NUL is mapped and counted; dst_len 0: size query).  src may equal dst
 * (in-place mapping, which Windows allows for these two flags); partial overlap is fatal.
 * Returns the number of WCHARs written or needed; 0 is a failure with
 *   ERROR_INVALID_PARAMETER   src NULL; src_len 0; dst_len negative; dst NULL with
 *                             dst_len nonzero
 *   ERROR_INVALID_FLAGS       flags 0; a bit that is none of the LCMAP_* / NORM_* / SORT_* /
 *                             LINGUISTIC_* values; LCMAP_LINGUISTIC_CASING without a case flag;
 *                             LCMAP_LOWERCASE or LCMAP_UPPERCASE with SORT_STRINGSORT,
 *                             NORM_IGNORESYMBOLS or NORM_IGNORENONSPACE
 *   ERROR_INSUFFICIENT_BUFFER dst_len nonzero and too small (the part that fits is written)
 * checked in that order.  Fatal, after those checks: LCMAP_TITLECASE; without a case flag,
 * any flag but LCMAP_LINGUISTIC_CASING (sort keys, byte reversal, LCMAP_HASH, the kana, width
 * and Chinese flags, the NORM_* and SORT_* flags); with a case flag, any flag but
 * LCMAP_LINGUISTIC_CASING and the three ERROR_INVALID_FLAGS ones above; a code unit whose
 * case is not covered (rt_wchar_case_known); a locale other than en-US. */
int rt_lcmap_string(LCID locale, DWORD flags, const WCHAR *src, int src_len, WCHAR *dst, int dst_len);

/* CompareStringW.  Compares the two strings code unit by code unit and returns
 * CSTR_LESS_THAN, CSTR_EQUAL or CSTR_GREATER_THAN.  A negative length means NUL-terminated
 * (the NUL is not part of the string, unlike the conversion functions); a shorter string
 * that is a prefix of the other sorts first.  Embedded NULs are ordinary code units.
 * This is NOT Windows's linguistic order: it is code-unit order, so "A" sorts before "a";
 * equality is what it is right for.  With NORM_IGNORECASE or LINGUISTIC_IGNORECASE both
 * sides are folded first, each code unit c to rt_wchar_lower(rt_wchar_upper(c)): letters that
 * Windows compares equal ignoring case become equal (sigma and final sigma, the micro sign
 * and mu, long s and s), and the folded letters are lower case, so '_' sorts below letters as
 * in Windows.  SORT_STRINGSORT and NORM_LINGUISTIC_CASING are accepted and change nothing.
 * Returns 0 with
 *   ERROR_INVALID_FLAGS      a bit outside the known flags
 *   ERROR_INVALID_PARAMETER  s1 or s2 NULL
 * Fatal: NORM_IGNORENONSPACE, NORM_IGNORESYMBOLS, NORM_IGNOREKANATYPE, NORM_IGNOREWIDTH,
 * SORT_DIGITSASNUMBERS, LINGUISTIC_IGNOREDIACRITIC; with an ignore-case flag, a code unit
 * whose case is not covered; a locale other than en-US. */
int rt_compare_string(LCID locale, DWORD flags, const WCHAR *s1, int len1, const WCHAR *s2, int len2);

/* ==== locales ============================================================================= */

/* GetUserDefaultLCID (and GetSystemDefaultLCID, GetThreadLocale): 0x0409. */
LCID rt_locale_default(void);

/* IsValidLocale.  TRUE for en-US (0x0409, and the two *_DEFAULT values, which mean it here)
 * with exactly LCID_INSTALLED or exactly LCID_SUPPORTED: the machine has one locale.  (Not
 * verified: the answers for the two *_DEFAULT values with these two flags.)
 * FALSE with ERROR_INVALID_FLAGS for any other flags value except 0 (from memory of the NT
 * source; the documentation lists no error).  dwFlags 0 is a validity query (Wine
 * kernel32/tests/locale.c, test_LocaleNameToLCID, the block that runs when RtlLcidToLocaleName
 * exists): TRUE for LOCALE_INVARIANT, FALSE for LOCALE_NEUTRAL, LOCALE_USER_DEFAULT and
 * LOCALE_SYSTEM_DEFAULT, and TRUE for 0x0409 (inferred, not in that test).  Those FALSE
 * answers set no last error, because the source does not say which code Windows sets.
 * Fatal for dwFlags 0x39 (the documented "do not use" special case).  With LCID_INSTALLED or
 * LCID_SUPPORTED, fatal for any locale other than en-US, LOCALE_INVARIANT and LOCALE_NEUTRAL
 * included: the honest answer for those is not known, and the first run that asks should say
 * so.  With dwFlags 0, fatal for any locale other than the five above: that includes
 * 0x00010407, which the same test also asserts TRUE, because the locale table has no entry
 * for it and the brief did not ask for one. */
BOOL rt_locale_is_valid(LCID locale, DWORD flags);

/* GetLocaleInfoW.  `type` is an LCTYPE, optionally OR-ed with LOCALE_NOUSEROVERRIDE and
 * LOCALE_USE_CP_ACP (both accepted and ignored: the machine has no user overrides and the
 * wide function has no ANSI translation) and LOCALE_RETURN_NUMBER.  Without
 * LOCALE_RETURN_NUMBER the NUL-terminated string is copied to buf and the result is its
 * length with the NUL; with len 0 the result is the length needed and buf is not used.
 * With LOCALE_RETURN_NUMBER (numeric types only) the DWORD is stored at buf, which needs
 * len >= 2 WCHARs, and the result is 2; with len 0 the result is 2 and nothing is written.
 * Returns 0 with
 *   ERROR_INVALID_PARAMETER   len negative; buf NULL with len nonzero
 *   ERROR_INSUFFICIENT_BUFFER len nonzero and too small (nothing is written)
 * Fatal: any LCTYPE not in the table (the LOCALE_* list in this header), LOCALE_RETURN_NUMBER
 * with a type that has no number, LOCALE_RETURN_GENITIVE_NAMES, a locale other than en-US.
 * The table is the Windows 7 en-US data from memory; the entries I am least sure of are
 * marked in locale.c. */
int rt_locale_info(LCID locale, LCTYPE type, WCHAR *buf, int len);

/* EnumSystemLocalesW is a callback API; the shim runs the loop (it has the Ctx for
 * GUEST_CALL) and this is the list behind it:
 *
 *     RtLocaleEnum e;
 *     if (!rt_locale_enum_begin(flags, &e)) return FALSE;       // ERROR_INVALID_FLAGS is set
 *     WCHAR name[RT_LOCALE_ENUM_NAME_MAX];
 *     while (rt_locale_enum_next(&e, name))
 *         if (!(GUEST_CALL(ctx, proc, name) & 0xFFFFFFFF)) break;   // the callback returns a BOOL
 *     return TRUE;
 *
 * `name` is the locale id as eight upper-case hex digits and a NUL ("00000409"), the form
 * the C runtime parses with base 16.  The machine has one locale, so the list is that
 * one entry for LCID_INSTALLED, LCID_SUPPORTED and 0 (which behaves as LCID_SUPPORTED),
 * and empty for LCID_ALTERNATE_SORTS alone (no alternate sorts).  Flags: LCID_INSTALLED
 * and LCID_SUPPORTED together, or any other bit, is ERROR_INVALID_FLAGS. */
#define RT_LOCALE_ENUM_NAME_MAX 9
typedef struct { DWORD flags; DWORD next; } RtLocaleEnum;
BOOL rt_locale_enum_begin(DWORD flags, RtLocaleEnum *e);
BOOL rt_locale_enum_next(RtLocaleEnum *e, WCHAR name[RT_LOCALE_ENUM_NAME_MAX]);

/* GetDateFormatW.  Formats the date of *date (the current local date if date is NULL, which
 * reads the clock of rt/time.h) with the locale's format selected by `flags`, or with the
 * format picture `format` if it is not NULL.  Result and buffer rules as GetLocaleInfoW
 * without numbers: the length written or needed, including the NUL; len 0 asks for the
 * size; 0 is a failure with
 *   ERROR_INVALID_FLAGS       flags with a bit outside DATE_* and the two LOCALE_ flags;
 *                             DATE_MONTHDAY (Windows 10 only); more than one of
 *                             DATE_SHORTDATE, DATE_LONGDATE, DATE_YEARMONTH; both of
 *                             DATE_LTRREADING and DATE_RTLREADING; a DATE_ format flag with
 *                             a format picture; LOCALE_NOUSEROVERRIDE with a format picture
 *                             (checked before the other flags)
 *   ERROR_INVALID_PARAMETER   len negative; buf NULL with len nonzero; a date that is not
 *                             valid (year outside 1601..9999, month, day; the day of the week
 *                             is ignored and recomputed, the time fields are ignored)
 *   ERROR_INSUFFICIENT_BUFFER len nonzero and too small
 * Locale formats (en-US): short "M/d/yyyy" (the default), long "dddd, MMMM dd, yyyy",
 * year-month "MMMM, yyyy".
 * Picture: d dd ddd dddd (more than four d count as four), M MM MMM MMMM (likewise),
 * yy, yyyy (yyy and yyyyy count as yyyy), text in single quotes with '' for a quote inside
 * them, any other character as itself.  "y" and "g"/"gg" are fatal (Windows's documentation
 * contradicts itself on "y"; the era string was not verified).
 * An empty format picture ("") is a picture like any other: the result is the empty string
 * (1 with the NUL; last error unchanged); with a DATE_ format flag it is ERROR_INVALID_FLAGS.
 * Fatal, after the flag checks above: DATE_USE_ALT_CALENDAR, DATE_LTRREADING, DATE_RTLREADING,
 * DATE_AUTOLAYOUT; a locale other than en-US. */
int rt_date_format(LCID locale, DWORD flags, const RtSystemTime *date, const WCHAR *format, WCHAR *buf, int len);

/* GetTimeFormatW.  As rt_date_format, for the time of day of *t (date fields ignored;
 * NULL: the current local time).  Locale format (en-US) "h:mm:ss tt".  Flags:
 * TIME_NOMINUTESORSECONDS, TIME_NOSECONDS, TIME_NOTIMEMARKER (each also removes the
 * separator it leaves behind), TIME_FORCE24HOURFORMAT (hours 0..23 with no leading zero,
 * and the marker is still shown unless TIME_NOTIMEMARKER is also set, as documented).
 * ERROR_INVALID_FLAGS for a bit outside TIME_* and the two LOCALE_ flags, and for
 * LOCALE_NOUSEROVERRIDE with a format picture (checked before the TIME_* flags);
 * ERROR_INVALID_PARAMETER for an hour above 23, minute or second above 59, milliseconds
 * above 999, and the buffer cases of rt_date_format.  Picture: h hh H HH m mm s ss t tt
 * (a longer run counts as two), quoting as in dates, any other character as itself.
 * Fatal: TIME_* flags together with a format picture (not verified; the same pair with
 * LOCALE_NOUSEROVERRIDE is ERROR_INVALID_FLAGS), an empty format picture, a locale other
 * than en-US. */
int rt_time_format(LCID locale, DWORD flags, const RtSystemTime *t, const WCHAR *format, WCHAR *buf, int len);

/* FILETIME (100 ns intervals since 1601-01-01 UTC) to broken-down time, proleptic Gregorian,
 * wDayOfWeek 0 = Sunday.  Valid for the years 1601 to 30827 that SYSTEMTIME can hold.  The
 * date and time functions use it for their "current time" case; the time shims may too. */
void rt_systemtime_from_filetime(uint64_t filetime, RtSystemTime *out);

/* Broken-down time to FILETIME: the inverse of rt_systemtime_from_filetime, and the core of
 * SystemTimeToFileTime, DosDateTimeToFileTime and SystemTimeToTzSpecificLocalTime.  Proleptic
 * Gregorian calendar; no time zone is applied and there is no leap second.  wDayOfWeek is
 * not read (the page of SystemTimeToFileTime: "The wDayOfWeek member of the SYSTEMTIME
 * structure is ignored").
 * A time is VALID when every member is in the range the page of SYSTEMTIME gives (wYear 1601
 * to 30827, wMonth 1 to 12, wDay 1 to 31, wHour 0 to 23, wMinute 0 to 59, wSecond 0 to 59,
 * wMilliseconds 0 to 999) and the day exists in that month of that year: 29 February in a
 * leap year only, which is every fourth year but not a century that 400 does not divide.
 * Returns true with *filetime = the 100 ns units from 1601-01-01 00:00:00.000 to that time
 * (a multiple of 10,000).  For every valid time rt_systemtime_from_filetime(*filetime) gives
 * the same members back, with the day of the week filled in.  Last error is left alone.
 * Returns false with last error ERROR_INVALID_PARAMETER, *filetime not written, for a time
 * that is not valid (the page: "A False return value can indicate that the passed SYSTEMTIME
 * structure represents an invalid date").  This is rule 1 of this header: the shim returns
 * FALSE and sets nothing.
 * WHAT IS CHECKED AGAINST WINDOWS (Wine kernel32/tests/time.c): the values 0 for 1601-01-01,
 * 0x01A8E79FE1D58000 for 1980-01-01, 0x01C1F107B82B6000 for 2002-05-01 12:00 and
 * 0x01C2349B580716B0 for 2002-07-26 11:55:32.123 (test_conversions); the refusal of
 * 1600-12-31 with exactly this error code (test_conversions); the refusal of month 0, hour 24
 * and minute 60 (test_invalid_arg, which does not look at the code: for those it is from
 * memory).  NOT CHECKED by any test read: that a day past the end of its month (30 February)
 * and a year above 30827 are refused and not carried over; both follow from the ranges of
 * the SYSTEMTIME page and from "an invalid date".
 * Fatal: st or filetime NULL.  Arithmetic only: any thread, no lock, no clock.
 * Sources: Microsoft's pages of SystemTimeToFileTime (timezoneapi.h) and SYSTEMTIME
 * (minwinbase.h) and Wine's kernel32/tests/time.c, read 2026-10-10. */
bool rt_filetime_from_systemtime(const RtSystemTime *st, uint64_t *filetime);

/* Start-up.  rt_init calls rt_locale_init; core/init.c holds a weak no-op of the same name that
 * this file's definition replaces when it is linked.  It checks that the machine description
 * (rt/machine.h) says en-US, code pages 1252 and 437, which is all the tables here are for, and is
 * fatal otherwise; it also checks that the lookup tables are sorted.  Every function above works
 * without it, because the tables are constant data.  Safe to call again. */
void rt_locale_init(void);

/* TRUE once rt_init has run the real rt_locale_init (and not the weak no-op).  For tests. */
bool rt_locale_initialized(void);

#endif /* RT_LOCALE_H */
