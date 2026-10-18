/* Tests of case mapping, character classification (GetStringTypeW), LCMapStringW and
 * CompareStringW in runtime/core/locale.c.  See locale.c in this directory for the conventions.
 *
 * Oracles: Python's unicodedata (Unicode 15.0 categories, bidirectional classes and simple case
 * mappings: locale_tables.c), the documentation of the C1_/C2_/C3_ constants, and values
 * written out by hand from the C runtime tables for code page 1252 (which Windows's GetStringTypeW
 * produces for U+0000..U+00FF).  Where the core's rule is a reading rather than a known Windows
 * value the comment says so; the list is in the report.
 */
#include <limits.h>
#include <string.h>

#include "rt/locale.h"
#include "rt/test.h"

extern const uint16_t locale_exp_upper[], locale_exp_lower[];
extern const uint16_t locale_exp_uncovered[][2];
extern const unsigned locale_exp_uncovered_n;
typedef struct { uint16_t cp, c1, c2, c3; } LocaleExpType;
extern const LocaleExpType locale_exp_types[];
extern const unsigned locale_exp_types_n;

#define SENTINEL 0x7A5A5A01u
static void set_err(void) { rt_set_last_error(T_CTX(), SENTINEL); }
#define FAILS(call, err)  do { set_err(); T_EQ((call), 0); T_EQ(T_LAST_ERROR(), (err)); } while (0)
#define OK_N(call, n)     do { set_err(); T_EQ((call), (n)); T_EQ(T_LAST_ERROR(), SENTINEL); } while (0)

#define FILL 0xAAAAu
static void fill_w(WCHAR *p, int n) { for (int i = 0; i < n; i++) p[i] = FILL; }
#define W(lit)  ((const WCHAR *)u##lit)
static bool eq_w(const WCHAR *a, const WCHAR *b, int n) { return memcmp(a, b, (size_t)n * 2) == 0; }

#define EN_US  0x0409u

/* ============================================================================================
 * Case mapping
 * ========================================================================================== */

TEST(core, locale_case_table_against_python)
{
    /* Every code unit of the table, against Python's simple case mappings. */
    unsigned bad = 0;
    for (unsigned c = 0; c < 0x530 && bad < 10; c++) {
        if (rt_wchar_upper((WCHAR)c) != locale_exp_upper[c] || rt_wchar_lower((WCHAR)c) != locale_exp_lower[c] || !rt_wchar_case_known((WCHAR)c)) {
            bad++;
            rt_test_fail(__FILE__, __LINE__, "U+%04X: upper %04X (expected %04X), lower %04X (expected %04X)", c, rt_wchar_upper((WCHAR)c),
                         locale_exp_upper[c], rt_wchar_lower((WCHAR)c), locale_exp_lower[c]);
        }
    }
    T_EQ(bad, 0);
    /* Mapping is idempotent: a mapped letter maps to itself. */
    for (unsigned c = 0; c < 0x530; c++) {
        T_EQ(rt_wchar_upper(rt_wchar_upper((WCHAR)c)), rt_wchar_upper((WCHAR)c));
        T_EQ(rt_wchar_lower(rt_wchar_lower((WCHAR)c)), rt_wchar_lower((WCHAR)c));
    }
}

TEST(core, locale_case_known_for_every_code_unit)
{
    /* Above the table a code unit is "known" if it has no case mapping in Unicode.  The expected
     * ranges are the generator's list of code units that do have one. */
    unsigned unknown = 0, bad = 0;
    for (unsigned c = 0; c < 0x10000; c++) {
        bool in_list = false;
        for (unsigned i = 0; i < locale_exp_uncovered_n; i++)
            if (c >= locale_exp_uncovered[i][0] && c <= locale_exp_uncovered[i][1]) { in_list = true; break; }
        if (rt_wchar_case_known((WCHAR)c) == in_list) { bad++; if (bad < 10) rt_test_fail(__FILE__, __LINE__, "U+%04X: known = %d", c, !in_list); }
        if (in_list) unknown++;
        /* Where the case is not known, nothing is mapped. */
        if (c >= 0x530 && (rt_wchar_upper((WCHAR)c) != c || rt_wchar_lower((WCHAR)c) != c)) { bad++; if (bad < 10) rt_test_fail(__FILE__, __LINE__, "U+%04X mapped outside the table", c); }
    }
    T_EQ(bad, 0);
    T_TRUE(unknown > 1000);                         /* the cased letters above U+052F: Armenian, Georgian, Latin Extended Additional, ... */
    /* Examples. */
    T_TRUE(!rt_wchar_case_known(0x0531));           /* ARMENIAN CAPITAL LETTER AYB */
    T_TRUE(!rt_wchar_case_known(0x1E00));           /* LATIN CAPITAL LETTER A WITH RING BELOW */
    T_TRUE(!rt_wchar_case_known(0x1F88));           /* Greek Extended */
    T_TRUE(!rt_wchar_case_known(0x212A));           /* KELVIN SIGN */
    T_TRUE(!rt_wchar_case_known(0xFF21));           /* FULLWIDTH LATIN CAPITAL LETTER A */
    T_TRUE(!rt_wchar_case_known(0x10D0));           /* Georgian */
    T_TRUE(rt_wchar_case_known(0x4E00));            /* ideographs have no case */
    T_TRUE(rt_wchar_case_known(0x3042));            /* hiragana */
    T_TRUE(rt_wchar_case_known(0x05D0));            /* Hebrew alef */
    T_TRUE(rt_wchar_case_known(0x0627));            /* Arabic alef */
    T_TRUE(rt_wchar_case_known(0x0E01));            /* Thai */
    T_TRUE(rt_wchar_case_known(0x20AC));
    T_TRUE(rt_wchar_case_known(0xD83D));            /* surrogate units have no case */
    T_TRUE(rt_wchar_case_known(0xDE00));
    T_TRUE(rt_wchar_case_known(0xFFFF));
    T_TRUE(rt_wchar_case_known(0xFB01));            /* the fi ligature has only a multi-character upper case: no simple mapping */
}

TEST(core, locale_case_facts)
{
    /* ASCII: exactly the letters. */
    for (unsigned c = 0; c < 128; c++) {
        bool lower = c >= 'a' && c <= 'z', upper = c >= 'A' && c <= 'Z';
        T_EQ(rt_wchar_upper((WCHAR)c), lower ? c - 32 : c);
        T_EQ(rt_wchar_lower((WCHAR)c), upper ? c + 32 : c);
    }
    /* Latin-1: the letters map by 0x20, the multiplication and division signs do not. */
    for (unsigned c = 0xC0; c <= 0xDE; c++) {
        if (c == 0xD7) { T_EQ(rt_wchar_lower((WCHAR)c), c); continue; }
        T_EQ(rt_wchar_lower((WCHAR)c), c + 0x20);
        T_EQ(rt_wchar_upper((WCHAR)(c + 0x20)), c);
    }
    T_EQ(rt_wchar_upper(0xF7), 0xF7);
    T_EQ(rt_wchar_upper(0xDF), 0xDF);               /* sharp s has no one-for-one upper case */
    T_EQ(rt_wchar_lower(0xDF), 0xDF);
    T_EQ(rt_wchar_upper(0xFF), 0x178);              /* y with diaeresis goes up out of Latin-1 */
    T_EQ(rt_wchar_lower(0x178), 0xFF);
    T_EQ(rt_wchar_upper(0xB5), 0x39C);              /* micro sign -> Greek capital mu (Unicode; not verified against Windows) */
    T_EQ(rt_wchar_lower(0xB5), 0xB5);
    T_EQ(rt_wchar_upper(0xAA), 0xAA);               /* ordinal indicators have no case */
    T_EQ(rt_wchar_upper(0xBA), 0xBA);
    /* Latin Extended-A: pairs alternate, with the exceptions. */
    T_EQ(rt_wchar_upper(0x101), 0x100);
    T_EQ(rt_wchar_lower(0x100), 0x101);
    T_EQ(rt_wchar_upper(0x133), 0x132);             /* ij ligature */
    T_EQ(rt_wchar_upper(0x149), 0x149);             /* n preceded by apostrophe: no simple upper case */
    T_EQ(rt_wchar_lower(0x139), 0x13A);             /* L with acute: the pairs shift parity here */
    T_EQ(rt_wchar_upper(0x13A), 0x139);
    T_EQ(rt_wchar_upper(0x17A), 0x179);
    T_EQ(rt_wchar_upper(0x17F), 'S');               /* long s (Unicode; not verified against Windows) */
    T_EQ(rt_wchar_lower(0x130), 0x130);             /* the Turkish i's are left alone (en-US without linguistic casing: Wine locale.c) */
    T_EQ(rt_wchar_upper(0x131), 0x131);
    T_EQ(rt_wchar_upper('i'), 'I');
    T_EQ(rt_wchar_lower('I'), 'i');
    /* Latin Extended-B: the digraph triples. */
    T_EQ(rt_wchar_upper(0x1C6), 0x1C4); T_EQ(rt_wchar_lower(0x1C4), 0x1C6);
    T_EQ(rt_wchar_upper(0x1C5), 0x1C4); T_EQ(rt_wchar_lower(0x1C5), 0x1C6);          /* titlecase maps both ways */
    T_EQ(rt_wchar_upper(0x1C9), 0x1C7); T_EQ(rt_wchar_lower(0x1C8), 0x1C9);
    T_EQ(rt_wchar_upper(0x1CC), 0x1CA); T_EQ(rt_wchar_lower(0x1CB), 0x1CC);
    T_EQ(rt_wchar_upper(0x1F3), 0x1F1); T_EQ(rt_wchar_lower(0x1F2), 0x1F3);
    T_EQ(rt_wchar_upper(0x1CE), 0x1CD);             /* a with caron */
    T_EQ(rt_wchar_upper(0x1F0), 0x1F0);             /* j with caron: no simple upper */
    T_EQ(rt_wchar_upper(0x1DD), 0x18E);             /* turned e <-> reversed E: not the +-1 pattern */
    T_EQ(rt_wchar_lower(0x18E), 0x1DD);
    T_EQ(rt_wchar_upper(0x253), 0x181);             /* IPA letters whose capitals are in Latin Extended-B */
    T_EQ(rt_wchar_lower(0x181), 0x253);
    T_EQ(rt_wchar_upper(0x259), 0x18F);
    /* Greek. */
    T_EQ(rt_wchar_upper(0x3B1), 0x391); T_EQ(rt_wchar_lower(0x391), 0x3B1);
    T_EQ(rt_wchar_upper(0x3C9), 0x3A9); T_EQ(rt_wchar_lower(0x3A9), 0x3C9);
    T_EQ(rt_wchar_upper(0x3C2), 0x3A3);             /* final sigma goes up to capital sigma */
    T_EQ(rt_wchar_upper(0x3C3), 0x3A3);
    T_EQ(rt_wchar_lower(0x3A3), 0x3C3);             /* and capital sigma down to the ordinary sigma */
    T_EQ(rt_wchar_upper(0x3AC), 0x386); T_EQ(rt_wchar_lower(0x386), 0x3AC);          /* with tonos */
    T_EQ(rt_wchar_upper(0x3CC), 0x38C); T_EQ(rt_wchar_upper(0x3CD), 0x38E); T_EQ(rt_wchar_upper(0x3CE), 0x38F);
    T_EQ(rt_wchar_upper(0x390), 0x390);             /* iota with dialytika and tonos: no simple upper */
    T_EQ(rt_wchar_upper(0x3D1), 0x398);             /* theta symbol */
    /* Cyrillic. */
    T_EQ(rt_wchar_upper(0x430), 0x410); T_EQ(rt_wchar_lower(0x410), 0x430);
    T_EQ(rt_wchar_upper(0x44F), 0x42F); T_EQ(rt_wchar_lower(0x42F), 0x44F);
    T_EQ(rt_wchar_upper(0x451), 0x401); T_EQ(rt_wchar_lower(0x401), 0x451);          /* io */
    T_EQ(rt_wchar_upper(0x450), 0x400); T_EQ(rt_wchar_upper(0x45F), 0x40F);          /* the 0x50 block */
    T_EQ(rt_wchar_upper(0x4CF), 0x4C0); T_EQ(rt_wchar_lower(0x4C0), 0x4CF);          /* palochka */
    T_EQ(rt_wchar_upper(0x4C2), 0x4C1);
    T_EQ(rt_wchar_upper(0x481), 0x480);
    T_EQ(rt_wchar_upper(0x4D1), 0x4D0);
    T_EQ(rt_wchar_upper(0x501), 0x500);             /* Cyrillic Supplement */
    /* Everything without case maps to itself, in particular the other bytes of 1252 and the
     * characters of 437. */
    static const WCHAR still[] = { 0x00A0, 0x00A9, 0x2013, 0x20AC, 0x2122, 0x2500, 0x0030, 0x0660, 0x3042, 0x4E00 };
    for (unsigned i = 0; i < sizeof still / sizeof still[0]; i++) { T_EQ(rt_wchar_upper(still[i]), still[i]); T_EQ(rt_wchar_lower(still[i]), still[i]); }
}

/* ============================================================================================
 * GetStringTypeW
 * ========================================================================================== */

static WORD type1(WCHAR c) { WORD w = 0xFFFF; T_EQ(rt_string_type(CT_CTYPE1, &c, 1, &w), TRUE); return w; }
static WORD type2(WCHAR c) { WORD w = 0xFFFF; T_EQ(rt_string_type(CT_CTYPE2, &c, 1, &w), TRUE); return w; }
static WORD type3(WCHAR c) { WORD w = 0xFFFF; T_EQ(rt_string_type(CT_CTYPE3, &c, 1, &w), TRUE); return w; }

TEST(core, locale_string_type_table_against_python)
{
    /* Every row of the generated table: the three type words of each character in the domain, and
     * the domain itself over all 65,536 code units. */
    unsigned bad = 0, row = 0;
    for (unsigned c = 0; c < 0x10000; c++) {
        bool in_table = row < locale_exp_types_n && locale_exp_types[row].cp == c;
        if (rt_wchar_type_known((WCHAR)c) != in_table) { bad++; if (bad < 10) rt_test_fail(__FILE__, __LINE__, "U+%04X: type_known = %d", c, !in_table); }
        if (!in_table) continue;
        const LocaleExpType *e = &locale_exp_types[row++];
        if (type1((WCHAR)c) != e->c1 || type2((WCHAR)c) != e->c2 || type3((WCHAR)c) != e->c3) {
            bad++;
            if (bad < 10) rt_test_fail(__FILE__, __LINE__, "U+%04X: got %04x %04x %04x, expected %04x %04x %04x", c, type1((WCHAR)c), type2((WCHAR)c), type3((WCHAR)c), e->c1, e->c2, e->c3);
        }
    }
    T_EQ(bad, 0);
    T_EQ(row, locale_exp_types_n);
    T_EQ(locale_exp_types_n > 1000, 1);
}

TEST(core, locale_string_type_facts)
{
    /* CT_CTYPE1 for ASCII, written out from the C runtime's table for the "C" and 1252 locales,
     * with C1_DEFINED on every defined character: Windows sets it (see locale_string_type_defined_bit). */
    T_EQ(type1(0x00), C1_CNTRL | C1_DEFINED);
    for (unsigned c = 1; c < 0x20; c++) {
        WORD want = C1_CNTRL | C1_DEFINED;
        if (c == 9) want = C1_CNTRL | C1_SPACE | C1_BLANK | C1_DEFINED;
        if (c >= 10 && c <= 13) want = C1_CNTRL | C1_SPACE | C1_DEFINED;
        T_EQ(type1((WCHAR)c), want);
    }
    T_EQ(type1(0x20), C1_SPACE | C1_BLANK | C1_DEFINED);
    for (unsigned c = 0x21; c < 0x7F; c++) {
        WORD want;
        if (c >= '0' && c <= '9') want = C1_DIGIT | C1_XDIGIT;
        else if (c >= 'A' && c <= 'Z') want = C1_UPPER | C1_ALPHA | (c <= 'F' ? C1_XDIGIT : 0);
        else if (c >= 'a' && c <= 'z') want = C1_LOWER | C1_ALPHA | (c <= 'f' ? C1_XDIGIT : 0);
        else want = C1_PUNCT;
        T_EQ(type1((WCHAR)c), want | C1_DEFINED);
    }
    T_EQ(type1(0x7F), C1_CNTRL | C1_DEFINED);
    T_EQ(type1('A'), 0x381); T_EQ(type1('Z'), 0x301); T_EQ(type1('a'), 0x382); T_EQ(type1('z'), 0x302);
    T_EQ(type1('0'), 0x284); T_EQ(type1('9'), 0x284); T_EQ(type1(' '), 0x248); T_EQ(type1('\t'), 0x268); T_EQ(type1('\n'), 0x228);
    T_EQ(type1('!'), 0x210); T_EQ(type1('_'), 0x210); T_EQ(type1('~'), 0x210);
    /* The upper half of 1252 as the C runtime's table has it. */
    T_EQ(type1(0x00A0), C1_SPACE | C1_BLANK | C1_DEFINED);
    T_EQ(type1(0x00C0), C1_UPPER | C1_ALPHA | C1_DEFINED); T_EQ(type1(0x00DE), C1_UPPER | C1_ALPHA | C1_DEFINED);
    T_EQ(type1(0x00E0), C1_LOWER | C1_ALPHA | C1_DEFINED); T_EQ(type1(0x00FF), C1_LOWER | C1_ALPHA | C1_DEFINED); T_EQ(type1(0x00DF), C1_LOWER | C1_ALPHA | C1_DEFINED);
    T_EQ(type1(0x00D7), C1_PUNCT | C1_DEFINED); T_EQ(type1(0x00F7), C1_PUNCT | C1_DEFINED);
    T_EQ(type1(0x00A1), C1_PUNCT | C1_DEFINED); T_EQ(type1(0x00BF), C1_PUNCT | C1_DEFINED); T_EQ(type1(0x00AD), C1_PUNCT | C1_DEFINED);
    T_EQ(type1(0x00B5), C1_LOWER | C1_ALPHA | C1_DEFINED);                   /* micro sign: a lower-case letter */
    T_EQ(type1(0x00AA), C1_ALPHA | C1_DEFINED); T_EQ(type1(0x00BA), C1_ALPHA | C1_DEFINED);   /* ordinal indicators: letters, not lower case (not verified) */
    T_EQ(type1(0x00B2), C1_DIGIT | C1_DEFINED); T_EQ(type1(0x00B3), C1_DIGIT | C1_DEFINED); T_EQ(type1(0x00B9), C1_DIGIT | C1_DEFINED);   /* superscripts have a digit value (Windows sets C1_DIGIT: Wine locale.c test_GetStringTypeW) */
    T_EQ(type1(0x00BC), C1_PUNCT | C1_DEFINED);                              /* fractions do not */
    T_EQ(type1(0x20AC), C1_PUNCT | C1_DEFINED); T_EQ(type1(0x2122), C1_PUNCT | C1_DEFINED); T_EQ(type1(0x2013), C1_PUNCT | C1_DEFINED); T_EQ(type1(0x201C), C1_PUNCT | C1_DEFINED);
    T_EQ(type1(0x0160), C1_UPPER | C1_ALPHA | C1_DEFINED); T_EQ(type1(0x0161), C1_LOWER | C1_ALPHA | C1_DEFINED);   /* S/s with caron (1252) */
    T_EQ(type1(0x0152), C1_UPPER | C1_ALPHA | C1_DEFINED); T_EQ(type1(0x0153), C1_LOWER | C1_ALPHA | C1_DEFINED);
    T_EQ(type1(0x0192), C1_LOWER | C1_ALPHA | C1_DEFINED);
    T_EQ(type1(0x02C6), C1_PUNCT | C1_DEFINED); T_EQ(type1(0x02DC), C1_PUNCT | C1_DEFINED);
    T_EQ(type1(0x0081), C1_CNTRL | C1_DEFINED); T_EQ(type1(0x008D), C1_CNTRL | C1_DEFINED); T_EQ(type1(0x009D), C1_CNTRL | C1_DEFINED);   /* the 1252 undefined bytes' controls */
    T_EQ(type1(0x0085), C1_CNTRL | C1_SPACE | C1_DEFINED);
    /* Greek and Cyrillic letters by case. */
    T_EQ(type1(0x0391), C1_UPPER | C1_ALPHA | C1_DEFINED); T_EQ(type1(0x03B1), C1_LOWER | C1_ALPHA | C1_DEFINED); T_EQ(type1(0x03C2), C1_LOWER | C1_ALPHA | C1_DEFINED);
    T_EQ(type1(0x0410), C1_UPPER | C1_ALPHA | C1_DEFINED); T_EQ(type1(0x0430), C1_LOWER | C1_ALPHA | C1_DEFINED); T_EQ(type1(0x0451), C1_LOWER | C1_ALPHA | C1_DEFINED);
    T_EQ(type1(0x2500), C1_PUNCT | C1_DEFINED); T_EQ(type1(0x2591), C1_PUNCT | C1_DEFINED); T_EQ(type1(0x221E), C1_PUNCT | C1_DEFINED);   /* box drawing and math of 437 */
    /* CT_CTYPE2: the bidirectional classes. */
    T_EQ(type2('a'), C2_LEFTTORIGHT); T_EQ(type2('Z'), C2_LEFTTORIGHT); T_EQ(type2(0x00E9), C2_LEFTTORIGHT); T_EQ(type2(0x03B1), C2_LEFTTORIGHT);
    T_EQ(type2('0'), C2_EUROPENUMBER); T_EQ(type2('9'), C2_EUROPENUMBER); T_EQ(type2(0x00B2), C2_EUROPENUMBER);
    T_EQ(type2('+'), C2_EUROPESEPARATOR); T_EQ(type2('-'), C2_EUROPESEPARATOR);
    T_EQ(type2('#'), C2_EUROPETERMINATOR); T_EQ(type2('$'), C2_EUROPETERMINATOR); T_EQ(type2('%'), C2_EUROPETERMINATOR); T_EQ(type2(0x00B0), C2_EUROPETERMINATOR);
    T_EQ(type2(','), C2_COMMONSEPARATOR); T_EQ(type2('.'), C2_COMMONSEPARATOR); T_EQ(type2('/'), C2_COMMONSEPARATOR); T_EQ(type2(':'), C2_COMMONSEPARATOR);
    T_EQ(type2(' '), C2_WHITESPACE); T_EQ(type2('\f'), C2_WHITESPACE);
    T_EQ(type2('\t'), C2_SEGMENTSEPARATOR); T_EQ(type2(0x0B), C2_SEGMENTSEPARATOR); T_EQ(type2(0x1F), C2_SEGMENTSEPARATOR);
    T_EQ(type2('\n'), C2_BLOCKSEPARATOR); T_EQ(type2('\r'), C2_BLOCKSEPARATOR); T_EQ(type2(0x1C), C2_BLOCKSEPARATOR);
    T_EQ(type2('!'), C2_OTHERNEUTRAL); T_EQ(type2('@'), C2_OTHERNEUTRAL); T_EQ(type2('_'), C2_OTHERNEUTRAL); T_EQ(type2('['), C2_OTHERNEUTRAL);
    T_EQ(type2(0x00), C2_NOTAPPLICABLE); T_EQ(type2(0x01), C2_NOTAPPLICABLE); T_EQ(type2(0x7F), C2_NOTAPPLICABLE); T_EQ(type2(0x00AD), C2_NOTAPPLICABLE);
    T_EQ(type2(0x00A0), C2_COMMONSEPARATOR);                    /* the non-breaking space is CS in Unicode (Windows's value not verified) */
    /* CT_CTYPE3. */
    T_EQ(type3('a'), C3_ALPHA); T_EQ(type3('Z'), C3_ALPHA); T_EQ(type3(0x00E9), C3_ALPHA); T_EQ(type3(0x0430), C3_ALPHA);
    T_EQ(type3('0'), 0); T_EQ(type3(' '), 0); T_EQ(type3('\n'), 0); T_EQ(type3('!'), 0); T_EQ(type3('_'), 0); T_EQ(type3('('), 0);
    T_EQ(type3('+'), C3_SYMBOL); T_EQ(type3('$'), C3_SYMBOL); T_EQ(type3('<'), C3_SYMBOL); T_EQ(type3('|'), C3_SYMBOL); T_EQ(type3('^'), C3_SYMBOL);
    T_EQ(type3(0x20AC), C3_SYMBOL); T_EQ(type3(0x00A9), C3_SYMBOL); T_EQ(type3(0x00D7), C3_SYMBOL);
    T_EQ(type3('-'), C3_LEXICAL);                               /* the documentation's examples of lexical punctuation */
    T_EQ(type3('='), C3_SYMBOL | C3_LEXICAL);
    T_EQ(type3(0x00AA), C3_ALPHA | C3_LEXICAL); T_EQ(type3(0x00BA), C3_ALPHA | C3_LEXICAL);
    T_EQ(type3(0xD800), C3_HIGHSURROGATE); T_EQ(type3(0xDBFF), C3_HIGHSURROGATE);
    T_EQ(type3(0xDC00), C3_LOWSURROGATE); T_EQ(type3(0xDFFF), C3_LOWSURROGATE);
}

TEST(core, locale_string_type_defined_bit)
{
    /* Windows sets C1_DEFINED (0x0200) in CT_CTYPE1 for every defined character.  Wine
     * kernel32/tests/locale.c, test_GetStringTypeW: U+0009 is space, control, blank and defined
     * (0x268); U+0020 and U+00A0 are space, blank and defined (0x248).  The same words without
     * the bit are accepted there only as broken(). */
    static const WCHAR s[] = { ' ', '\t', 0x00A0 };
    WORD out[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
    T_EQ(rt_string_type(CT_CTYPE1, s, 3, out), TRUE);
    T_EQ(out[0], 0x248);
    T_EQ(out[1], 0x268);
    T_EQ(out[2], 0x248);
    T_EQ(out[3], 0xFFFF);
    /* Every character the table defines has the bit: the words are the expectations table's, which carry it. */
    unsigned bad = 0;
    for (unsigned row = 0; row < locale_exp_types_n; row++) {
        const LocaleExpType *e = &locale_exp_types[row];
        WORD w = type1((WCHAR)e->cp);
        if (!(w & C1_DEFINED) || w != e->c1) { bad++; if (bad < 10) rt_test_fail(__FILE__, __LINE__, "U+%04X: type1 %04x, expected %04x with C1_DEFINED", e->cp, w, e->c1); }
    }
    T_EQ(bad, 0);
}

TEST(core, locale_string_type_lengths_and_buffers)
{
    WORD out[16];
    for (int i = 0; i < 16; i++) out[i] = 0xBEEF;
    /* -1 (or any negative): the string and its NUL, one WORD each. */
    OK_N(rt_string_type(CT_CTYPE1, W("aB1 "), -1, out), TRUE);
    T_EQ(out[0], 0x382); T_EQ(out[1], 0x381); T_EQ(out[2], 0x284); T_EQ(out[3], 0x248); T_EQ(out[4], 0x220); T_EQ(out[5], 0xBEEF);   /* the NUL is a control, and defined */
    for (int i = 0; i < 16; i++) out[i] = 0xBEEF;
    OK_N(rt_string_type(CT_CTYPE2, W("a+"), -5, out), TRUE);
    T_EQ(out[0], 1); T_EQ(out[1], 4); T_EQ(out[2], 0); T_EQ(out[3], 0xBEEF);
    for (int i = 0; i < 16; i++) out[i] = 0xBEEF;
    OK_N(rt_string_type(CT_CTYPE3, W(""), -1, out), TRUE);                        /* just the NUL */
    T_EQ(out[0], 0); T_EQ(out[1], 0xBEEF);
    /* An explicit length: exactly that many, no more; a NUL inside is a character. */
    for (int i = 0; i < 16; i++) out[i] = 0xBEEF;
    OK_N(rt_string_type(CT_CTYPE1, W("abcd"), 2, out), TRUE);
    T_EQ(out[0], 0x382); T_EQ(out[1], 0x382); T_EQ(out[2], 0xBEEF);
    OK_N(rt_string_type(CT_CTYPE1, W("a\0b"), 3, out), TRUE);
    T_EQ(out[1], C1_CNTRL | C1_DEFINED);
    OK_N(rt_string_type(CT_CTYPE1, W("a"), 1, out), TRUE);
    /* Surrogates: answered for CT_CTYPE3 (documented for Vista and later), refused for the others. */
    OK_N(rt_string_type(CT_CTYPE3, W("a\xD83D\xDE00z"), 4, out), TRUE);
    T_EQ(out[0], C3_ALPHA); T_EQ(out[1], C3_HIGHSURROGATE); T_EQ(out[2], C3_LOWSURROGATE); T_EQ(out[3], C3_ALPHA);
}

TEST(core, locale_string_type_errors)
{
    WORD out[8];
    /* The info type: exactly one of the three bits. */
    FAILS(rt_string_type(0, W("a"), 1, out), ERROR_INVALID_PARAMETER);
    FAILS(rt_string_type(CT_CTYPE1 | CT_CTYPE2, W("a"), 1, out), ERROR_INVALID_PARAMETER);
    FAILS(rt_string_type(CT_CTYPE1 | CT_CTYPE3, W("a"), 1, out), ERROR_INVALID_PARAMETER);
    FAILS(rt_string_type(CT_CTYPE1 | CT_CTYPE2 | CT_CTYPE3, W("a"), 1, out), ERROR_INVALID_PARAMETER);
    static const DWORD badbits[] = { 8, 0x10, 0x100, 0x80000000u, 0xFFFFFFFFu, 9, 0xC };
    for (unsigned i = 0; i < sizeof badbits / sizeof badbits[0]; i++) FAILS(rt_string_type(badbits[i], W("a"), 1, out), ERROR_INVALID_FLAGS);
    /* Pointers and length. */
    FAILS(rt_string_type(CT_CTYPE1, NULL, 1, out), ERROR_INVALID_PARAMETER);
    FAILS(rt_string_type(CT_CTYPE1, NULL, -1, out), ERROR_INVALID_PARAMETER);
    FAILS(rt_string_type(CT_CTYPE1, W("a"), 1, NULL), ERROR_INVALID_PARAMETER);
    FAILS(rt_string_type(CT_CTYPE1, W("a"), 0, out), ERROR_INVALID_PARAMETER);
    /* The documented: source and output must not be the same. */
    WCHAR same[4] = { 'a', 'b', 0, 0 };
    FAILS(rt_string_type(CT_CTYPE1, same, 2, (WORD *)same), ERROR_INVALID_PARAMETER);
    FAILS(rt_string_type(CT_CTYPE2, same, -1, (WORD *)same), ERROR_INVALID_PARAMETER);
    /* Partial overlap is undefined in Windows and fatal here. */
    T_TRAPS(rt_string_type(CT_CTYPE1, same, 3, (WORD *)same + 1), "buffers overlap");
    WCHAR big[8] = { 'a', 'b', 'c', 'd', 0 };
    OK_N(rt_string_type(CT_CTYPE1, big, 2, (WORD *)big + 2), TRUE);          /* adjacent: src [0,4), out [4,8) */
    OK_N(rt_string_type(CT_CTYPE1, big + 4, 2, (WORD *)big), TRUE);          /* out [0,4), src [8,12) */
}

TEST(core, locale_string_type_unknown_characters_are_fatal)
{
    WORD out[8];
    /* A code unit the table has no entry for stops the run and names the unit and its index. */
    T_TRAPS(rt_string_type(CT_CTYPE1, W("a\u4E00"), 2, out), "no character type for U+4E00 at index 1 (info type 1)");
    T_TRAPS(rt_string_type(CT_CTYPE2, W("\u0300"), 1, out), "U+0300 at index 0 (info type 2)");
    T_TRAPS(rt_string_type(CT_CTYPE3, W("ab\u01C5"), 3, out), "U+01C5 at index 2 (info type 4)");                 /* titlecase letters: unverified */
    T_TRAPS(rt_string_type(CT_CTYPE1, W("\u0378"), 1, out), "U+0378");                                            /* unassigned */
    T_TRAPS(rt_string_type(CT_CTYPE1, W("\u0483"), 1, out), "U+0483");                                            /* combining mark */
    T_TRAPS(rt_string_type(CT_CTYPE1, W("\u0530"), 1, out), "U+0530");                                            /* one past the Cyrillic Supplement */
    T_TRAPS(rt_string_type(CT_CTYPE1, W("\u0250"), 1, out), "U+0250");                                            /* one past Latin Extended-B */
    T_TRAPS(rt_string_type(CT_CTYPE1, W("\u036F"), 1, out), "U+036F");                                            /* one before Greek */
    T_TRAPS(rt_string_type(CT_CTYPE1, W("\xD83D"), 1, out), "U+D83D");                                            /* surrogates in CT_CTYPE1 */
    T_TRAPS(rt_string_type(CT_CTYPE2, W("\xDE00"), 1, out), "U+DE00");
    T_TRAPS(rt_string_type(CT_CTYPE1, W("\uFFFF"), 1, out), "U+FFFF");
    T_TRAPS(rt_string_type(CT_CTYPE1, W("\u2014\u2015"), 2, out), "U+2015");                                      /* the em dash is in 1252, the horizontal bar is not */
    T_TRAPS(rt_string_type(CT_CTYPE1, W("\u2500\u2501"), 2, out), "U+2501");                                      /* a box-drawing character 437 does not have */
    /* The size of the report does not depend on the string length: the first unknown unit is named. */
    T_TRAPS(rt_string_type(CT_CTYPE1, W("abc\u4E00\u4E01"), -1, out), "U+4E00 at index 3");
}

/* ============================================================================================
 * LCMapStringW
 * ========================================================================================== */

TEST(core, locale_lcmap_case_mapping)
{
    WCHAR out[64];
    static const LCID locales[] = { EN_US, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
    for (unsigned l = 0; l < 3; l++) {
        LCID loc = locales[l];
        static const DWORD upper_flags[] = { LCMAP_UPPERCASE, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING };
        static const DWORD lower_flags[] = { LCMAP_LOWERCASE, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING };
        for (unsigned f = 0; f < 2; f++) {
            fill_w(out, 64);
            OK_N(rt_lcmap_string(loc, upper_flags[f], W("Hello, World! \u00E0\u00E9\u00FF \u03B1\u0431 \u00DF"), -1, out, 64), 23);
            T_TRUE(eq_w(out, W("HELLO, WORLD! \u00C0\u00C9\u0178 \u0391\u0411 \u00DF"), 22));        /* the NUL is mapped and counted */
            T_EQ(out[22], 0); T_EQ(out[23], FILL);
            fill_w(out, 64);
            OK_N(rt_lcmap_string(loc, lower_flags[f], W("Hello, World! \u00C0\u00C9\u0178 \u0391\u0411 \u00DF"), -1, out, 64), 23);
            T_TRUE(eq_w(out, W("hello, world! \u00E0\u00E9\u00FF \u03B1\u0431 \u00DF"), 22));
        }
    }
    /* Explicit length: that many, no NUL; an embedded NUL is a character. */
    fill_w(out, 64);
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abcdef"), 3, out, 64), 3);
    T_TRUE(eq_w(out, W("ABC"), 3)); T_EQ(out[3], FILL);
    fill_w(out, 64);
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("a\0b"), 3, out, 64), 3);
    T_EQ(out[0], 'A'); T_EQ(out[1], 0); T_EQ(out[2], 'B');
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("a\0b"), -1, out, 64), 2);
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("x"), -7, out, 64), 2);       /* any negative: NUL-terminated */
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("x"), INT_MIN, out, 64), 2);
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("x"), 1, out, 1), 1);          /* one character, exact fit */
    T_EQ(out[0], 'X');
    /* The size query; the destination is not used. */
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abc"), -1, NULL, 0), 4);
    OK_N(rt_lcmap_string(EN_US, LCMAP_LOWERCASE, W("abc"), 3, NULL, 0), 3);
    OK_N(rt_lcmap_string(EN_US, LCMAP_LOWERCASE, W("abc"), 3, out, 0), 3);
    /* Exact fit and one short (the part that fits is written, nothing beyond it). */
    fill_w(out, 64);
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abcd"), 4, out, 4), 4);
    T_EQ(out[3], 'D'); T_EQ(out[4], FILL);
    fill_w(out, 64);
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abcd"), 4, out, 3), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(out[2], 'C'); T_EQ(out[3], FILL);
    fill_w(out, 64);
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abcd"), -1, out, 4), ERROR_INSUFFICIENT_BUFFER);        /* the NUL does not fit */
    T_EQ(out[3], 'D'); T_EQ(out[4], FILL);
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abcd"), -1, out, 5), 5);
    /* In place, which Windows allows for these two flags. */
    WCHAR buf[8] = { 'a', 'B', 0xE9, 0, 'x', 'y', 0, 0 };
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, buf, -1, buf, 8), 4);
    T_EQ(buf[0], 'A'); T_EQ(buf[1], 'B'); T_EQ(buf[2], 0xC9); T_EQ(buf[4], 'x');
    OK_N(rt_lcmap_string(EN_US, LCMAP_LOWERCASE, buf, 3, buf, 3), 3);
    T_EQ(buf[0], 'a'); T_EQ(buf[1], 'b'); T_EQ(buf[2], 0xE9);
    OK_N(rt_lcmap_string(EN_US, LCMAP_LOWERCASE, buf, 3, buf, 0), 3);          /* a size query on the same buffer: nothing written */
    /* A partial overlap is undefined in Windows and fatal here. */
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, buf, 4, buf + 1, 4), "buffers overlap");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, buf + 2, 4, buf, 4), "buffers overlap");
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, buf, 2, buf + 2, 2), 2);        /* adjacent */
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, buf + 4, 2, buf, 4), 2);
}

TEST(core, locale_lcmap_every_code_unit)
{
    /* U+0000 to U+052F in one call each way, against Python's simple case mappings. */
    static WCHAR src[0x530], dst[0x530];
    for (unsigned c = 0; c < 0x530; c++) src[c] = (WCHAR)c;
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, src, 0x530, dst, 0x530), 0x530);
    for (unsigned c = 0; c < 0x530; c++) T_EQ(dst[c], locale_exp_upper[c]);
    OK_N(rt_lcmap_string(EN_US, LCMAP_LOWERCASE, src, 0x530, dst, 0x530), 0x530);
    for (unsigned c = 0; c < 0x530; c++) T_EQ(dst[c], locale_exp_lower[c]);
    /* Code units that have no case at all pass through, including a surrogate pair. */
    static const WCHAR plain[] = { 0x4E00, 0x3042, 0xD83D, 0xDE00, 0x20AC, 0xFFFF, 0x05D0, 'x' };
    WCHAR out[8];
    OK_N(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, plain, 8, out, 8), 8);
    T_TRUE(eq_w(out, plain, 7)); T_EQ(out[7], 'X');
}

TEST(core, locale_lcmap_flags)
{
    WCHAR out[8];
    /* Flags that are no flag at all. */
    static const DWORD junk[] = { 0x10000000u, 0x80000000u, 0x40000000u, 0x00080000u, 0x00002000u, 0x00004000u, 0x40, 0x80 };
    for (unsigned i = 0; i < sizeof junk / sizeof junk[0]; i++) {
        FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE | junk[i], W("a"), 1, out, 8), ERROR_INVALID_FLAGS);
        FAILS(rt_lcmap_string(EN_US, junk[i], W("a"), 1, out, 8), ERROR_INVALID_FLAGS);
    }
    FAILS(rt_lcmap_string(EN_US, 0xFFFFFFFFu, W("a"), 1, out, 8), ERROR_INVALID_FLAGS);
    /* LCMAP_LINGUISTIC_CASING is valid with an upper- or lower-case flag only. */
    FAILS(rt_lcmap_string(EN_US, LCMAP_LINGUISTIC_CASING, W("a"), 1, out, 8), ERROR_INVALID_FLAGS);
    /* Flags that are valid in Windows and not implemented here: stop, naming them. */
    /* Windows fails dwMapFlags 0, and a case flag with SORT_STRINGSORT, NORM_IGNORESYMBOLS or
     * NORM_IGNORENONSPACE, with ERROR_INVALID_FLAGS (Wine kernel32/tests/locale.c, the array
     * lcmap_invalid_flags used by test_lcmapstring_unicode for LCMapStringW and LCMapStringEx). */
    FAILS(rt_lcmap_string(EN_US, 0, W("a"), 1, out, 8), ERROR_INVALID_FLAGS);
    FAILS(rt_lcmap_string(EN_US, LCMAP_LOWERCASE | NORM_IGNORESYMBOLS, W("a"), 1, out, 8), ERROR_INVALID_FLAGS);
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE | NORM_IGNORENONSPACE, W("a"), 1, out, 8), ERROR_INVALID_FLAGS);
    FAILS(rt_lcmap_string(EN_US, LCMAP_LOWERCASE | SORT_STRINGSORT, W("a"), 1, out, 8), ERROR_INVALID_FLAGS);
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING | SORT_STRINGSORT, W("a"), 1, out, 8), ERROR_INVALID_FLAGS);
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_TITLECASE, W("a"), 1, out, 8), "LCMAP_TITLECASE (flags 0x300)");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_SORTKEY, W("a"), 1, out, 8), "flags 0x400 without LCMAP_LOWERCASE or LCMAP_UPPERCASE");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_BYTEREV, W("a"), 1, out, 8), "flags 0x800 without");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_HIRAGANA, W("a"), 1, out, 8), "flags 0x100000 without");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_KATAKANA, W("a"), 1, out, 8), "flags 0x200000 without");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_HALFWIDTH, W("a"), 1, out, 8), "flags 0x400000 without");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_FULLWIDTH, W("a"), 1, out, 8), "flags 0x800000 without");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_SIMPLIFIED_CHINESE, W("a"), 1, out, 8), "flags 0x2000000 without");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_TRADITIONAL_CHINESE, W("a"), 1, out, 8), "flags 0x4000000 without");
    T_TRAPS(rt_lcmap_string(EN_US, NORM_IGNORECASE, W("a"), 1, out, 8), "flags 0x1 without");
    T_TRAPS(rt_lcmap_string(EN_US, NORM_IGNORENONSPACE | NORM_IGNORESYMBOLS, W("a"), 1, out, 8), "flags 0x6 without");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_HASH, W("a"), 1, out, 8), "flags 0x40000 without");
    /* A case mapping combined with anything else but LCMAP_LINGUISTIC_CASING. */
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE | LCMAP_SORTKEY, W("a"), 1, out, 8), "combine a case mapping with 0x400");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_LOWERCASE | NORM_IGNORECASE, W("a"), 1, out, 8), "combine a case mapping with 0x1");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE | LCMAP_HALFWIDTH, W("a"), 1, out, 8), "combine a case mapping with 0x400000");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING | LCMAP_BYTEREV, W("a"), 1, out, 8), "combine a case mapping with 0x800");
}

TEST(core, locale_lcmap_parameters_and_locales)
{
    WCHAR out[8];
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, NULL, 3, out, 8), ERROR_INVALID_PARAMETER);
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, NULL, -1, out, 8), ERROR_INVALID_PARAMETER);
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abc"), 0, out, 8), ERROR_INVALID_PARAMETER);        /* "cannot be 0" */
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abc"), 3, out, -1), ERROR_INVALID_PARAMETER);
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abc"), 3, NULL, 1), ERROR_INVALID_PARAMETER);
    FAILS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("abc"), 3, NULL, 8), ERROR_INVALID_PARAMETER);
    /* Parameters are checked before the locale and the flags. */
    FAILS(rt_lcmap_string(0x407, 0xFFFFFFFFu, NULL, 3, out, 8), ERROR_INVALID_PARAMETER);
    FAILS(rt_lcmap_string(EN_US, 0, W("abc"), 0, out, 8), ERROR_INVALID_PARAMETER);
    /* Locales other than en-US are not in the table: fatal, naming the value. */
    static const LCID others[] = { 0x0407, 0x0809, 0x1000, 0x10409, 0x0C00, 0x1400, 0x0009, 0x0004, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) T_TRAPS(rt_lcmap_string(others[i], LCMAP_UPPERCASE, W("a"), 1, out, 8), "LCMapStringW: locale");
    T_TRAPS(rt_lcmap_string(0x407, LCMAP_UPPERCASE, W("a"), 1, out, 8), "locale 0x407");
    /* Locale 0 is the user default (the C runtime's start-up passes it) and the invariant locale maps case as en-US does. */
    static const LCID same[] = { LOCALE_NEUTRAL, LOCALE_INVARIANT, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
    for (unsigned i = 0; i < sizeof same / sizeof same[0]; i++) {
        out[0] = 0;
        OK_N(rt_lcmap_string(same[i], LCMAP_UPPERCASE, W("a\u00E9"), 2, out, 8), 2);
        T_EQ(out[0], 'A');
        T_EQ(out[1], 0x00C9);
        OK_N(rt_lcmap_string(same[i], LCMAP_LOWERCASE, W("A"), 1, NULL, 0), 1);
    }
    /* Characters whose case is not in the table (above U+052F) stop the run, with the index. */
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_UPPERCASE, W("ab\u0561"), 3, out, 8), "the case of U+0561 at index 2");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_LOWERCASE, W("\u1E00"), 1, NULL, 0), "U+1E00 at index 0");
    T_TRAPS(rt_lcmap_string(EN_US, LCMAP_LOWERCASE, W("x\uFF21"), -1, out, 8), "U+FF21 at index 1");
    OK_N(rt_lcmap_string(EN_US, LCMAP_LOWERCASE, W("\u4E00\u3042"), 2, out, 8), 2);    /* caseless scripts are fine */
}

/* ============================================================================================
 * CompareStringW
 * ========================================================================================== */

TEST(core, locale_compare_basics)
{
    static const LCID locales[] = { EN_US, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
    for (unsigned l = 0; l < 3; l++) {
        LCID loc = locales[l];
        OK_N(rt_compare_string(loc, 0, W("abc"), 3, W("abc"), 3), CSTR_EQUAL);
        OK_N(rt_compare_string(loc, 0, W("abc"), -1, W("abc"), -1), CSTR_EQUAL);
        OK_N(rt_compare_string(loc, 0, W("abc"), 3, W("abd"), 3), CSTR_LESS_THAN);
        OK_N(rt_compare_string(loc, 0, W("abd"), 3, W("abc"), 3), CSTR_GREATER_THAN);
        OK_N(rt_compare_string(loc, 0, W("ab"), -1, W("abc"), -1), CSTR_LESS_THAN);      /* a prefix sorts first */
        OK_N(rt_compare_string(loc, 0, W("abc"), -1, W("ab"), -1), CSTR_GREATER_THAN);
        OK_N(rt_compare_string(loc, 0, W(""), -1, W(""), -1), CSTR_EQUAL);
        OK_N(rt_compare_string(loc, 0, W(""), -1, W("a"), -1), CSTR_LESS_THAN);
        OK_N(rt_compare_string(loc, 0, W("a"), -1, W(""), -1), CSTR_GREATER_THAN);
        OK_N(rt_compare_string(loc, 0, W("abc"), 0, W("abc"), 0), CSTR_EQUAL);            /* empty strings by length */
        OK_N(rt_compare_string(loc, 0, W("abc"), 0, W("abc"), 1), CSTR_LESS_THAN);
        OK_N(rt_compare_string(loc, 0, W("abc"), 1, W("abc"), 0), CSTR_GREATER_THAN);
    }
    /* Case matters without a flag: code-unit order, so upper case sorts first (NOT Windows's linguistic order). */
    OK_N(rt_compare_string(EN_US, 0, W("a"), 1, W("A"), 1), CSTR_GREATER_THAN);
    OK_N(rt_compare_string(EN_US, 0, W("A"), 1, W("a"), 1), CSTR_LESS_THAN);
    OK_N(rt_compare_string(EN_US, 0, W("\u00E9"), 1, W("\u00C9"), 1), CSTR_GREATER_THAN);
    /* Lengths: mixed explicit and terminated; -1 stops at the first NUL; any negative value counts as -1. */
    OK_N(rt_compare_string(EN_US, 0, W("abcdef"), 3, W("abc"), -1), CSTR_EQUAL);
    OK_N(rt_compare_string(EN_US, 0, W("abc"), -3, W("abc"), INT_MIN), CSTR_EQUAL);
    OK_N(rt_compare_string(EN_US, 0, W("a\0b"), -1, W("a"), -1), CSTR_EQUAL);
    OK_N(rt_compare_string(EN_US, 0, W("a\0b"), 3, W("a"), -1), CSTR_GREATER_THAN);       /* embedded NULs count with an explicit length */
    OK_N(rt_compare_string(EN_US, 0, W("a\0b"), 3, W("a\0c"), 3), CSTR_LESS_THAN);
    OK_N(rt_compare_string(EN_US, 0, W("a\0b"), 3, W("a\0b"), 3), CSTR_EQUAL);
    OK_N(rt_compare_string(EN_US, 0, W("abc"), 3, W("abcd"), 3), CSTR_EQUAL);
    OK_N(rt_compare_string(EN_US, 0, W("abc"), 3, W("abcd"), 4), CSTR_LESS_THAN);
    /* Any code unit, including surrogates and the extremes. */
    OK_N(rt_compare_string(EN_US, 0, W("\uFFFF"), 1, W("\uFFFE"), 1), CSTR_GREATER_THAN);
    OK_N(rt_compare_string(EN_US, 0, W("\xD83D"), 1, W("\xDE00"), 1), CSTR_LESS_THAN);
    OK_N(rt_compare_string(EN_US, 0, W("\xD83D\xDE00"), 2, W("\xD83D\xDE00"), 2), CSTR_EQUAL);
    OK_N(rt_compare_string(EN_US, 0, W("\u4E00"), 1, W("\u4E01"), 1), CSTR_LESS_THAN);
    /* The result is antisymmetric for a set of strings. */
    static const WCHAR *set[] = { W(""), W("a"), W("A"), W("ab"), W("aB"), W("b"), W("\u00E9"), W("\u0391"), W("\u4E00"), W("a\0") };
    for (unsigned i = 0; i < sizeof set / sizeof set[0]; i++)
        for (unsigned j = 0; j < sizeof set / sizeof set[0]; j++) {
            int ab = rt_compare_string(EN_US, 0, set[i], -1, set[j], -1), ba = rt_compare_string(EN_US, 0, set[j], -1, set[i], -1);
            T_EQ(ab + ba, 4);
            /* Equal only to itself, except that "a\0" read as NUL-terminated is "a". */
            T_EQ(ab == CSTR_EQUAL, i == j || (i == 1 && j == 9) || (i == 9 && j == 1));
        }
}

TEST(core, locale_compare_ignore_case)
{
    static const DWORD ic[] = { NORM_IGNORECASE, LINGUISTIC_IGNORECASE, NORM_IGNORECASE | LINGUISTIC_IGNORECASE,
                                NORM_IGNORECASE | SORT_STRINGSORT, NORM_IGNORECASE | NORM_LINGUISTIC_CASING,
                                LINGUISTIC_IGNORECASE | NORM_LINGUISTIC_CASING | SORT_STRINGSORT };
    for (unsigned f = 0; f < sizeof ic / sizeof ic[0]; f++) {
        DWORD fl = ic[f];
        OK_N(rt_compare_string(EN_US, fl, W("Hello"), -1, W("hELLO"), -1), CSTR_EQUAL);
        OK_N(rt_compare_string(EN_US, fl, W("\u00C9cole"), -1, W("\u00E9COLE"), -1), CSTR_EQUAL);
        OK_N(rt_compare_string(EN_US, fl, W("\u0391\u03A3"), -1, W("\u03B1\u03C2"), -1), CSTR_EQUAL);       /* capital sigma vs final sigma */
        OK_N(rt_compare_string(EN_US, fl, W("\u03C3"), 1, W("\u03C2"), 1), CSTR_EQUAL);                      /* sigma vs final sigma */
        OK_N(rt_compare_string(EN_US, fl, W("\u0416"), 1, W("\u0436"), 1), CSTR_EQUAL);                      /* Cyrillic zhe */
        OK_N(rt_compare_string(EN_US, fl, W("\u00B5"), 1, W("\u039C"), 1), CSTR_EQUAL);                      /* micro sign vs capital mu */
        OK_N(rt_compare_string(EN_US, fl, W("\u00B5"), 1, W("\u03BC"), 1), CSTR_EQUAL);                      /* micro sign vs small mu */
        OK_N(rt_compare_string(EN_US, fl, W("\u017F"), 1, W("s"), 1), CSTR_EQUAL);                           /* long s vs s */
        OK_N(rt_compare_string(EN_US, fl, W("abc"), 3, W("ABD"), 3), CSTR_LESS_THAN);
        OK_N(rt_compare_string(EN_US, fl, W("ABD"), 3, W("abc"), 3), CSTR_GREATER_THAN);
        OK_N(rt_compare_string(EN_US, fl, W("ab"), -1, W("ABC"), -1), CSTR_LESS_THAN);
        /* Letters still differ from other letters; the sharp s has no one-for-one upper case. */
        OK_N(rt_compare_string(EN_US, fl, W("Stra\u00DFe"), -1, W("STRASSE"), -1), CSTR_GREATER_THAN);
        OK_N(rt_compare_string(EN_US, fl, W("a"), 1, W("b"), 1), CSTR_LESS_THAN);
        /* Punctuation sorts below letters (the folded letters are lower case). */
        OK_N(rt_compare_string(EN_US, fl, W("A_"), -1, W("AA"), -1), CSTR_LESS_THAN);
        OK_N(rt_compare_string(EN_US, fl, W("a["), -1, W("AA"), -1), CSTR_LESS_THAN);
        /* Only code units that are looked at need a known case: the second pair is never reached. */
        OK_N(rt_compare_string(EN_US, fl, W("a\u0531"), 2, W("b\u0531"), 2), CSTR_LESS_THAN);
        /* Caseless scripts compare by code unit. */
        OK_N(rt_compare_string(EN_US, fl, W("\u4E00"), 1, W("\u4E00"), 1), CSTR_EQUAL);
        OK_N(rt_compare_string(EN_US, fl, W("\u4E00"), 1, W("\u4E01"), 1), CSTR_LESS_THAN);
        /* The Turkish i's are not folded (see locale.c). */
        OK_N(rt_compare_string(EN_US, fl, W("\u0131"), 1, W("I"), 1), CSTR_GREATER_THAN);
        OK_N(rt_compare_string(EN_US, fl, W("\u0130"), 1, W("i"), 1), CSTR_GREATER_THAN);
    }
}

TEST(core, locale_compare_flags_parameters_locales)
{
    /* Flags that are no flag. */
    static const DWORD junk[] = { 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x2000, 0x4000, 0x8000, 0x40000, 0x100000, 0x80000000u, 0xFFFFFFFFu, 0x04000000u };
    for (unsigned i = 0; i < sizeof junk / sizeof junk[0]; i++) {
        FAILS(rt_compare_string(EN_US, junk[i], W("a"), 1, W("a"), 1), ERROR_INVALID_FLAGS);
        FAILS(rt_compare_string(EN_US, junk[i] | NORM_IGNORECASE, W("a"), 1, W("a"), 1), ERROR_INVALID_FLAGS);
    }
    /* Flags that are valid and not implemented. */
    T_TRAPS(rt_compare_string(EN_US, NORM_IGNORENONSPACE, W("a"), 1, W("a"), 1), "flags 0x2 include 0x2");
    T_TRAPS(rt_compare_string(EN_US, NORM_IGNORESYMBOLS, W("a"), 1, W("a"), 1), "NORM_IGNORESYMBOLS");
    T_TRAPS(rt_compare_string(EN_US, NORM_IGNOREKANATYPE, W("a"), 1, W("a"), 1), "flags 0x10000 include 0x10000");
    T_TRAPS(rt_compare_string(EN_US, NORM_IGNOREWIDTH, W("a"), 1, W("a"), 1), "flags 0x20000 include 0x20000");
    T_TRAPS(rt_compare_string(EN_US, SORT_DIGITSASNUMBERS, W("a"), 1, W("a"), 1), "SORT_DIGITSASNUMBERS");
    T_TRAPS(rt_compare_string(EN_US, LINGUISTIC_IGNOREDIACRITIC, W("a"), 1, W("a"), 1), "flags 0x20 include 0x20");
    T_TRAPS(rt_compare_string(EN_US, NORM_IGNORECASE | NORM_IGNORENONSPACE, W("a"), 1, W("a"), 1), "flags 0x3 include 0x2");
    /* Accepted and without effect on this ordinal comparison. */
    OK_N(rt_compare_string(EN_US, SORT_STRINGSORT, W("a"), 1, W("a"), 1), CSTR_EQUAL);
    OK_N(rt_compare_string(EN_US, NORM_LINGUISTIC_CASING, W("a"), 1, W("A"), 1), CSTR_GREATER_THAN);
    /* NULL strings. */
    FAILS(rt_compare_string(EN_US, 0, NULL, 1, W("a"), 1), ERROR_INVALID_PARAMETER);
    FAILS(rt_compare_string(EN_US, 0, W("a"), 1, NULL, 1), ERROR_INVALID_PARAMETER);
    FAILS(rt_compare_string(EN_US, 0, NULL, -1, NULL, -1), ERROR_INVALID_PARAMETER);
    FAILS(rt_compare_string(EN_US, 0, NULL, 0, W("a"), 1), ERROR_INVALID_PARAMETER);      /* a NULL pointer with length 0 is still NULL (not verified) */
    FAILS(rt_compare_string(0x407, 0, NULL, 1, W("a"), 1), ERROR_INVALID_PARAMETER);       /* parameters before the locale */
    /* Locales. */
    static const LCID others[] = { 0x0407, 0x0809, 0x1000, 0x10409, 0x0C00, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof others / sizeof others[0]; i++) T_TRAPS(rt_compare_string(others[i], 0, W("a"), 1, W("a"), 1), "CompareStringW: locale");
    static const LCID same[] = { LOCALE_NEUTRAL, LOCALE_INVARIANT, LOCALE_USER_DEFAULT, LOCALE_SYSTEM_DEFAULT };
    for (unsigned i = 0; i < sizeof same / sizeof same[0]; i++) {
        OK_N(rt_compare_string(same[i], 0, W("a"), 1, W("a"), 1), CSTR_EQUAL);
        OK_N(rt_compare_string(same[i], 0, W("a"), 1, W("b"), 1), CSTR_LESS_THAN);
    }
    T_TRAPS(rt_compare_string(0x407, 0, W("a"), 1, W("a"), 1), "locale 0x407");
    /* Case folding of a code unit with no table entry stops the run, naming it (only when the flag asks). */
    T_TRAPS(rt_compare_string(EN_US, NORM_IGNORECASE, W("\u0531"), 1, W("\u0561"), 1), "the case of U+0531 (index 0)");
    T_TRAPS(rt_compare_string(EN_US, LINGUISTIC_IGNORECASE, W("a\u0561"), 2, W("a\u0561"), 2), "U+0561 (index 1)");
    T_TRAPS(rt_compare_string(EN_US, NORM_IGNORECASE, W("a"), 1, W("\u1E00"), 1), "U+1E00 (index 0)");
    OK_N(rt_compare_string(EN_US, 0, W("\u0531"), 1, W("\u0561"), 1), CSTR_LESS_THAN);    /* without the flag the case is irrelevant */
    OK_N(rt_compare_string(EN_US, 0, W("\u1E00"), 1, W("\u1E00"), 1), CSTR_EQUAL);
}
