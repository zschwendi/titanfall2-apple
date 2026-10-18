/* National language support: the constants and structures of winnls.h that the locale
 * functions take (docs/RUNTIME.md section 8.4).  Guest-visible.  The functions are in
 * rt/locale.h, which includes this file.
 */
#ifndef RT_WIN_NLS_H
#define RT_WIN_NLS_H

#include "rt/wintypes.h"


/* ---- code pages ---- */
#define CP_ACP                    0u       /* the system ANSI code page: 1252 */
#define CP_OEMCP                  1u       /* the system OEM code page: 437 */
#define CP_MACCP                  2u       /* the Macintosh code page: not supported */
#define CP_THREAD_ACP             3u       /* the thread's ANSI code page: 1252 (one locale) */
#define CP_SYMBOL                 42u      /* not supported */
#define CP_UTF7                   65000u   /* not supported */
#define CP_UTF8                   65001u

/* The code pages the core converts.  Any other value fails (see rule 2). */
#define RT_CP_ANSI                1252u
#define RT_CP_OEM                 437u
#define RT_CP_ASCII               20127u   /* US-ASCII */
#define RT_CP_LATIN1              28591u   /* ISO 8859-1 */

/* MultiByteToWideChar flags. */
#define MB_PRECOMPOSED            0x00000001u
#define MB_COMPOSITE              0x00000002u
#define MB_USEGLYPHCHARS          0x00000004u
#define MB_ERR_INVALID_CHARS      0x00000008u

/* WideCharToMultiByte flags. */
#define WC_DISCARDNS              0x00000010u
#define WC_SEPCHARS               0x00000020u
#define WC_DEFAULTCHAR            0x00000040u
#define WC_ERR_INVALID_CHARS      0x00000080u   /* UTF-8 only */
#define WC_COMPOSITECHECK         0x00000200u
#define WC_NO_BEST_FIT_CHARS      0x00000400u

#define MAX_DEFAULTCHAR           2
#define MAX_LEADBYTES             12

/* ---- GetStringTypeW ---- */
#define CT_CTYPE1                 0x00000001u
#define CT_CTYPE2                 0x00000002u
#define CT_CTYPE3                 0x00000004u

#define C1_UPPER                  0x0001
#define C1_LOWER                  0x0002
#define C1_DIGIT                  0x0004
#define C1_SPACE                  0x0008
#define C1_PUNCT                  0x0010
#define C1_CNTRL                  0x0020
#define C1_BLANK                  0x0040
#define C1_XDIGIT                 0x0080
#define C1_ALPHA                  0x0100
#define C1_DEFINED                0x0200

#define C2_NOTAPPLICABLE          0x0000
#define C2_LEFTTORIGHT            0x0001
#define C2_RIGHTTOLEFT            0x0002
#define C2_EUROPENUMBER           0x0003
#define C2_EUROPESEPARATOR        0x0004
#define C2_EUROPETERMINATOR       0x0005
#define C2_ARABICNUMBER           0x0006
#define C2_COMMONSEPARATOR        0x0007
#define C2_BLOCKSEPARATOR         0x0008
#define C2_SEGMENTSEPARATOR       0x0009
#define C2_WHITESPACE             0x000A
#define C2_OTHERNEUTRAL           0x000B

#define C3_NOTAPPLICABLE          0x0000
#define C3_NONSPACING             0x0001
#define C3_DIACRITIC              0x0002
#define C3_VOWELMARK              0x0004
#define C3_SYMBOL                 0x0008
#define C3_KATAKANA               0x0010
#define C3_HIRAGANA               0x0020
#define C3_HALFWIDTH              0x0040
#define C3_FULLWIDTH              0x0080
#define C3_IDEOGRAPH              0x0100
#define C3_KASHIDA                0x0200
#define C3_LEXICAL                0x0400
#define C3_HIGHSURROGATE          0x0800
#define C3_LOWSURROGATE           0x1000
#define C3_ALPHA                  0x8000

/* ---- LCMapStringW, CompareStringW ---- */
#define LCMAP_LOWERCASE           0x00000100u
#define LCMAP_UPPERCASE           0x00000200u
#define LCMAP_TITLECASE           0x00000300u   /* Windows 7: both case bits */
#define LCMAP_SORTKEY             0x00000400u
#define LCMAP_BYTEREV             0x00000800u
#define LCMAP_HASH                0x00040000u
#define LCMAP_HIRAGANA            0x00100000u
#define LCMAP_KATAKANA            0x00200000u
#define LCMAP_HALFWIDTH           0x00400000u
#define LCMAP_FULLWIDTH           0x00800000u
#define LCMAP_LINGUISTIC_CASING   0x01000000u
#define LCMAP_SIMPLIFIED_CHINESE  0x02000000u
#define LCMAP_TRADITIONAL_CHINESE 0x04000000u
#define LCMAP_SORTHANDLE          0x20000000u

#define NORM_IGNORECASE           0x00000001u
#define NORM_IGNORENONSPACE       0x00000002u
#define NORM_IGNORESYMBOLS        0x00000004u
#define SORT_DIGITSASNUMBERS      0x00000008u
#define LINGUISTIC_IGNORECASE     0x00000010u
#define LINGUISTIC_IGNOREDIACRITIC 0x00000020u
#define SORT_STRINGSORT           0x00001000u
#define NORM_IGNOREKANATYPE       0x00010000u
#define NORM_IGNOREWIDTH          0x00020000u
#define NORM_LINGUISTIC_CASING    0x08000000u

#define CSTR_LESS_THAN            1
#define CSTR_EQUAL                2
#define CSTR_GREATER_THAN         3

/* ---- locales ---- */
#define LANG_ENGLISH_US_LCID      0x0409u       /* MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT) */
#define LOCALE_NEUTRAL            0x0000u
#define LOCALE_INVARIANT          0x007Fu
#define LOCALE_USER_DEFAULT       0x0400u
#define LOCALE_SYSTEM_DEFAULT     0x0800u
#define LOCALE_CUSTOM_DEFAULT     0x0C00u
#define LOCALE_CUSTOM_UNSPECIFIED 0x1000u
#define LOCALE_CUSTOM_UI_DEFAULT  0x1400u

#define LCID_INSTALLED            0x00000001u
#define LCID_SUPPORTED            0x00000002u
#define LCID_ALTERNATE_SORTS      0x00000004u

/* Combining flags of the LCTYPE argument of GetLocaleInfoW. */
#define LOCALE_NOUSEROVERRIDE        0x80000000u
#define LOCALE_USE_CP_ACP            0x40000000u
#define LOCALE_RETURN_NUMBER         0x20000000u
#define LOCALE_RETURN_GENITIVE_NAMES 0x10000000u

/* The LCTYPE values the en-US table answers for.  Others are fatal (see rt_locale_info). */
#define LOCALE_ILANGUAGE             0x00000001u
#define LOCALE_SLANGUAGE             0x00000002u
#define LOCALE_SABBREVLANGNAME       0x00000003u
#define LOCALE_SNATIVELANGNAME       0x00000004u
#define LOCALE_ICOUNTRY              0x00000005u
#define LOCALE_SCOUNTRY              0x00000006u
#define LOCALE_SABBREVCTRYNAME       0x00000007u
#define LOCALE_SNATIVECTRYNAME       0x00000008u
#define LOCALE_IDEFAULTLANGUAGE      0x00000009u
#define LOCALE_IDEFAULTCOUNTRY       0x0000000Au
#define LOCALE_IDEFAULTCODEPAGE      0x0000000Bu
#define LOCALE_SLIST                 0x0000000Cu
#define LOCALE_IMEASURE              0x0000000Du
#define LOCALE_SDECIMAL              0x0000000Eu
#define LOCALE_STHOUSAND             0x0000000Fu
#define LOCALE_SGROUPING             0x00000010u
#define LOCALE_IDIGITS               0x00000011u
#define LOCALE_ILZERO                0x00000012u
#define LOCALE_SNATIVEDIGITS         0x00000013u
#define LOCALE_SCURRENCY             0x00000014u
#define LOCALE_SINTLSYMBOL           0x00000015u
#define LOCALE_SMONDECIMALSEP        0x00000016u
#define LOCALE_SMONTHOUSANDSEP       0x00000017u
#define LOCALE_SMONGROUPING          0x00000018u
#define LOCALE_ICURRDIGITS           0x00000019u
#define LOCALE_IINTLCURRDIGITS       0x0000001Au
#define LOCALE_ICURRENCY             0x0000001Bu
#define LOCALE_INEGCURR              0x0000001Cu
#define LOCALE_SDATE                 0x0000001Du
#define LOCALE_STIME                 0x0000001Eu
#define LOCALE_SSHORTDATE            0x0000001Fu
#define LOCALE_SLONGDATE             0x00000020u
#define LOCALE_IDATE                 0x00000021u
#define LOCALE_ILDATE                0x00000022u
#define LOCALE_ITIME                 0x00000023u
#define LOCALE_ICENTURY              0x00000024u
#define LOCALE_ITLZERO               0x00000025u
#define LOCALE_IDAYLZERO             0x00000026u
#define LOCALE_IMONLZERO             0x00000027u
#define LOCALE_S1159                 0x00000028u
#define LOCALE_S2359                 0x00000029u
#define LOCALE_SDAYNAME1             0x0000002Au   /* Monday; SDAYNAME7 is Sunday */
#define LOCALE_SDAYNAME7             0x00000030u
#define LOCALE_SABBREVDAYNAME1       0x00000031u
#define LOCALE_SABBREVDAYNAME7       0x00000037u
#define LOCALE_SMONTHNAME1           0x00000038u   /* January; SMONTHNAME12 is December */
#define LOCALE_SMONTHNAME12          0x00000043u
#define LOCALE_SABBREVMONTHNAME1     0x00000044u
#define LOCALE_SABBREVMONTHNAME12    0x0000004Fu
#define LOCALE_SPOSITIVESIGN         0x00000050u
#define LOCALE_SNEGATIVESIGN         0x00000051u
#define LOCALE_IPOSSIGNPOSN          0x00000052u
#define LOCALE_INEGSIGNPOSN          0x00000053u
#define LOCALE_IPOSSYMPRECEDES       0x00000054u
#define LOCALE_IPOSSEPBYSPACE        0x00000055u
#define LOCALE_INEGSYMPRECEDES       0x00000056u
#define LOCALE_INEGSEPBYSPACE        0x00000057u
#define LOCALE_SISO639LANGNAME       0x00000059u
#define LOCALE_SISO3166CTRYNAME      0x0000005Au
#define LOCALE_IGEOID                0x0000005Bu
#define LOCALE_SNAME                 0x0000005Cu
#define LOCALE_SDURATION             0x0000005Du
#define LOCALE_SPARENT               0x0000006Du
#define LOCALE_SENGLISHDISPLAYNAME   0x00000072u
#define LOCALE_SNATIVEDISPLAYNAME    0x00000073u
#define LOCALE_SMONTHDAY             0x00000078u
#define LOCALE_SSHORTTIME            0x00000079u
#define LOCALE_SENGLANGUAGE          0x00001001u
#define LOCALE_SENGCOUNTRY           0x00001002u
#define LOCALE_STIMEFORMAT           0x00001003u
#define LOCALE_IDEFAULTANSICODEPAGE  0x00001004u
#define LOCALE_SYEARMONTH            0x00001006u
#define LOCALE_ICALENDARTYPE         0x00001009u
#define LOCALE_IPAPERSIZE            0x0000100Au
#define LOCALE_IOPTIONALCALENDAR     0x0000100Bu
#define LOCALE_IFIRSTDAYOFWEEK       0x0000100Cu
#define LOCALE_IFIRSTWEEKOFYEAR      0x0000100Du
#define LOCALE_INEGNUMBER            0x00001010u
#define LOCALE_IDEFAULTMACCODEPAGE   0x00001011u
#define LOCALE_IDEFAULTEBCDICCODEPAGE 0x00001012u
#define LOCALE_SSORTNAME             0x00001013u

/* GetDateFormatW / GetTimeFormatW flags.  (The two functions also accept LOCALE_NOUSEROVERRIDE
 * and LOCALE_USE_CP_ACP in dwFlags.) */
#define DATE_SHORTDATE            0x00000001u
#define DATE_LONGDATE             0x00000002u
#define DATE_USE_ALT_CALENDAR     0x00000004u
#define DATE_YEARMONTH            0x00000008u
#define DATE_LTRREADING           0x00000010u
#define DATE_RTLREADING           0x00000020u
#define DATE_AUTOLAYOUT           0x00000040u
#define DATE_MONTHDAY             0x00000080u   /* Windows 10 only: ERROR_INVALID_FLAGS here */

#define TIME_NOMINUTESORSECONDS   0x00000001u
#define TIME_NOSECONDS            0x00000002u
#define TIME_NOTIMEMARKER         0x00000004u
#define TIME_FORCE24HOURFORMAT    0x00000008u

/* CPINFO, as GetCPInfo fills it: 20 bytes. */
typedef struct {
    UINT MaxCharSize;                  /* longest character in bytes: 1 for the single-byte pages, 4 for UTF-8 */
    BYTE DefaultChar[MAX_DEFAULTCHAR]; /* what an unrepresentable character becomes: '?' then 0 */
    BYTE LeadByte[MAX_LEADBYTES];      /* ranges of DBCS lead bytes, ended by a 0 0 pair: all zero here */
} CPINFO, *LPCPINFO;
_Static_assert(sizeof(CPINFO) == 20, "CPINFO");
_Static_assert(__builtin_offsetof(CPINFO, DefaultChar) == 4 && __builtin_offsetof(CPINFO, LeadByte) == 6, "CPINFO");

/* Counted text that need not end in a NUL (winnt.h). */
typedef CHAR        *LPCH;
typedef const CHAR  *LPCCH;
typedef WCHAR       *LPWCH;
typedef const WCHAR *LPCWCH;

#endif /* RT_WIN_NLS_H */
