/* The Windows command line that the runtime builds for the guest (docs/RUNTIME.md section 8.2):
 * rt_cmdline_build_w in rt/env.h.
 *
 * The quoting is checked two ways.  Worked examples fix the exact output.  Then every short
 * argument made of spaces, tabs, quotes, backslashes and letters is built into a command line
 * and split again by a separate implementation of the rules the Microsoft C runtime uses to
 * split one ("Parsing C++ command-line arguments").  The split must return the arguments
 * exactly.  The splitter is written from the documented rules, not from the quoter, so the two
 * can disagree.
 */
#include <stdio.h>
#include <string.h>

#include "rt/env.h"
#include "rt/test.h"

#define MAXARGS  4
#define MAXLEN   64

/* ASCII text as a WCHAR string, into a caller's buffer. */
static void to_w(const char *s, WCHAR *out) {
    size_t i = 0;
    for (; s[i]; i++) out[i] = (unsigned char)s[i];
    out[i] = 0;
}

static bool is_w(const WCHAR *w, const char *s) {
    for (size_t i = 0;; i++) {
        if (w[i] != (unsigned char)s[i]) return false;
        if (!s[i]) return true;
    }
}

static bool filled(const WCHAR *b, size_t n, WCHAR v) {
    for (size_t i = 0; i < n; i++) if (b[i] != v) return false;
    return true;
}

/* ---- the splitter: the Microsoft C runtime's rules ------------------------------------------ */

/* Appends c to dst at *k; false if dst is full. */
static bool put(char *dst, size_t cap, size_t *k, char c) {
    if (*k + 1 >= cap) return false;
    dst[(*k)++] = c;
    return true;
}

/* The program name, then the arguments.  Rules, from the documentation:
 *   - arguments are separated by spaces or tabs outside quotes;
 *   - 2n backslashes then a quote: n backslashes, and the quote toggles quoting;
 *   - 2n+1 backslashes then a quote: n backslashes and a literal quote;
 *   - any other backslashes are literal;
 *   - inside quotes, a pair of quotes is one literal quote;
 *   - a program name that starts with a quote ends at the next quote, with no escapes.
 * Returns the number of arguments after the program name, or -1 if they do not fit. */
static int split(const WCHAR *cmd, char prog[MAXLEN], char args[MAXARGS][MAXLEN]) {
    const WCHAR *p = cmd;
    size_t k = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') { if (!put(prog, MAXLEN, &k, (char)*p)) return -1; p++; }
        if (*p == '"') p++;
    } else {
        while (*p && *p != ' ' && *p != '\t') { if (!put(prog, MAXLEN, &k, (char)*p)) return -1; p++; }
    }
    prog[k] = 0;

    int n = 0;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (n == MAXARGS) return -1;
        k = 0;
        bool inq = false;
        for (;;) {
            size_t nbs = 0;
            while (*p == '\\') { nbs++; p++; }
            if (*p == '"') {
                for (size_t i = 0; i < nbs / 2; i++) if (!put(args[n], MAXLEN, &k, '\\')) return -1;
                if (nbs % 2) {                                  /* escaped quote */
                    if (!put(args[n], MAXLEN, &k, '"')) return -1;
                    p++;
                } else if (inq && p[1] == '"') {               /* a pair inside quotes */
                    if (!put(args[n], MAXLEN, &k, '"')) return -1;
                    p += 2;
                } else {                                        /* a quote that toggles */
                    inq = !inq;
                    p++;
                }
            } else {
                for (size_t i = 0; i < nbs; i++) if (!put(args[n], MAXLEN, &k, '\\')) return -1;
                if (!*p || (!inq && (*p == ' ' || *p == '\t'))) break;
                if (!put(args[n], MAXLEN, &k, (char)*p)) return -1;
                p++;
            }
        }
        args[n][k] = 0;
        n++;
        if (!*p) break;
    }
    return n;
}

/* Build `program` and `n` arguments and split the result again; true if every argument comes
 * back unchanged and the program too. */
static bool round_trip(const char *const *args, unsigned n) {
    WCHAR wargs[MAXARGS][MAXLEN];
    const WCHAR *pa[MAXARGS];
    for (unsigned i = 0; i < n; i++) {
        to_w(args[i], wargs[i]);
        pa[i] = wargs[i];
    }
    WCHAR prog[8];
    to_w("p", prog);
    WCHAR cmd[512];
    SIZE_T need = rt_cmdline_build_w(prog, pa, n, NULL, 0);
    if (need > 512) return false;
    if (rt_cmdline_build_w(prog, pa, n, cmd, need) != need) return false;

    char gp[MAXLEN], got[MAXARGS][MAXLEN];
    int k = split(cmd, gp, got);
    if (k != (int)n || strcmp(gp, "p") != 0) return false;
    for (unsigned i = 0; i < n; i++) if (strcmp(got[i], args[i]) != 0) return false;
    return true;
}

/* ---- worked examples ----------------------------------------------------------------------------- */

/* The command line for the one argument `arg`, after the program "p". */
static void quoted(const char *arg, WCHAR out[256]) {
    WCHAR prog[8], wa[64];
    to_w("p", prog);
    to_w(arg, wa);
    const WCHAR *args[1] = { wa };
    SIZE_T need = rt_cmdline_build_w(prog, args, 1, NULL, 0);
    T_REQUIRE(need < 256);
    rt_cmdline_build_w(prog, args, 1, out, need);
}

TEST(core, cmdline_quoting_examples)
{
    T_CASE("the quoting of single arguments, against worked examples");
    struct { const char *arg, *want; } cases[] = {
        { "",            "\"\"" },            /* an empty argument must be quoted */
        { "abc",         "abc" },             /* nothing to escape: as is */
        { "a b",         "\"a b\"" },
        { "a\tb",        "\"a\tb\"" },
        { "a\"b",        "\"a\\\"b\"" },      /* one backslash escapes the quote */
        { "a\\",         "a\\" },             /* a lone trailing backslash, unquoted, is literal */
        { "a b\\",       "\"a b\\\\\"" },     /* trailing backslashes double before the closing quote */
        { "a\\\"b",      "\"a\\\\\\\"b\"" },  /* 1 backslash before a quote: 3 backslashes */
        { "\"",          "\"\\\"\"" },
        { "\"\"",        "\"\\\"\\\"\"" },
        { "\\\\a",       "\\\\a" },           /* backslashes not before a quote are literal */
        { "ab ",         "\"ab \"" },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        WCHAR out[256];
        quoted(cases[i].arg, out);
        char want[256];
        snprintf(want, sizeof want, "\"p\" %s", cases[i].want);
        T_TRUE(is_w(out, want));
    }

    /* The program is always quoted, and its backslashes are literal. */
    WCHAR prog[] = { 'C', ':', '\\', 'T', 'i', 't', 'a', 'n', 'f', 'a', 'l', 'l', '2', '\\', 'T', 'i', 't', 'a', 'n', 'f', 'a', 'l', 'l', '2', '.', 'e', 'x', 'e', 0 };
    WCHAR out[64];
    /* 28 characters, quoted to 30, and the terminator: 31. */
    T_EQ(rt_cmdline_build_w(prog, NULL, 0, out, 64), 31);
    T_TRUE(rt_cmdline_build_w(prog, NULL, 0, out, 64) == 31);
    T_TRUE(is_w(out, "\"C:\\Titanfall2\\Titanfall2.exe\""));
}

TEST(core, cmdline_build_sizes)
{
    T_CASE("the size needed includes the terminator; a buffer one short is written to by nothing");
    WCHAR prog[] = { 'p', 0 };
    WCHAR ab[] = { 'a', ' ', 'b', 0 };
    const WCHAR *args[1] = { ab };

    /* "p" "a b" is 9 characters, so 10 with the terminator. */
    T_EQ(rt_cmdline_build_w(prog, args, 1, NULL, 0), 10);
    WCHAR out[16];
    for (int i = 0; i < 16; i++) out[i] = 0x1234;

    T_EQ(rt_cmdline_build_w(prog, args, 1, out, 9), 10);      /* one short */
    T_TRUE(filled(out, 16, 0x1234));
    T_EQ(rt_cmdline_build_w(prog, args, 1, out, 0), 10);      /* zero size */
    T_TRUE(filled(out, 16, 0x1234));

    T_EQ(rt_cmdline_build_w(prog, args, 1, out, 10), 10);     /* exact */
    T_TRUE(is_w(out, "\"p\" \"a b\""));
    T_EQ(out[9], 0);
    T_EQ(out[10], 0x1234);                                    /* nothing past the terminator */

    /* The program alone, with no arguments: "p" and the terminator. */
    T_EQ(rt_cmdline_build_w(prog, NULL, 0, NULL, 0), 4);
    /* One empty argument: "p" "" and the terminator. */
    const WCHAR empty[] = { 0 };
    const WCHAR *ea[1] = { empty };
    T_EQ(rt_cmdline_build_w(prog, ea, 1, NULL, 0), 7);
    T_EQ(rt_cmdline_build_w(prog, ea, 1, out, 7), 7);
    T_TRUE(is_w(out, "\"p\" \"\""));
}

TEST(core, cmdline_build_fatal_cases)
{
    T_CASE("a NULL program, a NULL argument list and a NULL argument are fatal");
    WCHAR prog[] = { 'p', 0 };
    const WCHAR *none[1] = { NULL };
    T_TRAPS(rt_cmdline_build_w(NULL, NULL, 0, NULL, 0), "NULL program");
    T_TRAPS(rt_cmdline_build_w(prog, NULL, 2, NULL, 0), "2 arguments but a NULL list");
    T_TRAPS(rt_cmdline_build_w(prog, none, 1, NULL, 0), "argument 0 is NULL");
}

/* ---- round trips ------------------------------------------------------------------------------ */

static const char alpha[5] = { 'a', ' ', '\t', '\\', '"' };

/* The string of `len` characters whose base-5 digits are idx, one character each. */
static void spell(unsigned idx, unsigned len, char *out) {
    for (unsigned i = 0; i < len; i++) {
        out[i] = alpha[idx % 5];
        idx /= 5;
    }
    out[len] = 0;
}

TEST(core, cmdline_round_trip_single)
{
    T_CASE("every argument of up to five characters from spaces, tabs, quotes, backslashes and letters");
    unsigned bad = 0;
    char first[MAXLEN] = "";
    for (unsigned len = 0; len <= 5; len++) {
        unsigned count = 1;
        for (unsigned i = 0; i < len; i++) count *= 5;
        for (unsigned idx = 0; idx < count; idx++) {
            char s[MAXLEN];
            spell(idx, len, s);
            const char *args[1] = { s };
            if (!round_trip(args, 1)) {
                if (!bad) strcpy(first, s);
                bad++;
            }
        }
    }
    if (bad) rt_test_fail(__FILE__, __LINE__, "%u single arguments did not come back unchanged; the first was [%s]",
                          bad, first);
    T_EQ(bad, 0);
}

TEST(core, cmdline_round_trip_pairs)
{
    T_CASE("every pair of arguments of up to three characters each, separated by a space");
    unsigned bad = 0;
    char first_a[MAXLEN] = "", first_b[MAXLEN] = "";
    for (unsigned la = 0; la <= 3; la++) {
        for (unsigned lb = 0; lb <= 3; lb++) {
            unsigned ca = 1, cb = 1;
            for (unsigned i = 0; i < la; i++) ca *= 5;
            for (unsigned i = 0; i < lb; i++) cb *= 5;
            for (unsigned ia = 0; ia < ca; ia++) {
                for (unsigned ib = 0; ib < cb; ib++) {
                    char a[MAXLEN], b[MAXLEN];
                    spell(ia, la, a);
                    spell(ib, lb, b);
                    const char *args[2] = { a, b };
                    if (!round_trip(args, 2)) {
                        if (!bad) { strcpy(first_a, a); strcpy(first_b, b); }
                        bad++;
                    }
                }
            }
        }
    }
    if (bad) rt_test_fail(__FILE__, __LINE__, "%u argument pairs did not come back unchanged; the first was [%s] [%s]",
                          bad, first_a, first_b);
    T_EQ(bad, 0);
}

TEST(core, cmdline_round_trip_program_with_spaces)
{
    T_CASE("a program path with spaces and backslashes is split back to the same program");
    WCHAR prog[] = { 'C', ':', '\\', 'a', ' ', 'b', '\\', 'p', '.', 'e', 'x', 'e', 0 };
    WCHAR arg[] = { 'x', '"', 'y', '\\', 0 };
    const WCHAR *args[1] = { arg };
    WCHAR cmd[128];
    SIZE_T need = rt_cmdline_build_w(prog, args, 1, cmd, 128);
    T_TRUE(need > 1 && need < 128);
    char gp[MAXLEN], ga[MAXARGS][MAXLEN];
    T_EQ(split(cmd, gp, ga), 1);
    T_TRUE(strcmp(gp, "C:\\a b\\p.exe") == 0);
    T_TRUE(strcmp(ga[0], "x\"y\\") == 0);
}
