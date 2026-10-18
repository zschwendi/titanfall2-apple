/* The A entry point takes bytes in code page 1252 (rt/path.h), converted by rt_path_to_host_a itself.  Every
 * byte from 0x01 to 0xFF is tried; the five that 1252 leaves undefined are mapped to the C1 control character of the same
 * value, as Windows' own table does (UNSURE: from memory of that table; the choice is stated in runtime/core/path.c).
 */
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/path.h"

void rt_path_test_force_case(int mode);

static const char *join(const char *base, const char *rel) {
    static char buf[4][RT_PATH_MAX * 2];
    static unsigned k;
    char *b = buf[k++ & 3];
    char tmp[RT_PATH_MAX * 2];
    snprintf(tmp, sizeof tmp, *rel ? "%s/%s" : "%s", base, rel);
    size_t o = 0;                                   /* the runner's TMPDIR ends in a slash: the root is spelled with "//" */
    for (const char *q = tmp; *q; q++) if (!(*q == '/' && o > 0 && b[o - 1] == '/')) b[o++] = *q;
    b[o] = 0;
    return b;
}
static const char *I(const char *rel) { return join(rt_test_install_root(), rel); }

#define CR   RT_PATH_CREATE

/* Code page 1252, bytes 0x80 to 0x9F, as UTF-8.  The five undefined ones (81 8D 8F 90 9D) are the C1 controls U+0081 and so on. */
static const char *const hi[32] = {
    "\xE2\x82\xAC", "\xC2\x81", "\xE2\x80\x9A", "\xC6\x92", "\xE2\x80\x9E", "\xE2\x80\xA6", "\xE2\x80\xA0", "\xE2\x80\xA1",
    "\xCB\x86", "\xE2\x80\xB0", "\xC5\xA0", "\xE2\x80\xB9", "\xC5\x92", "\xC2\x8D", "\xC5\xBD", "\xC2\x8F",
    "\xC2\x90", "\xE2\x80\x98", "\xE2\x80\x99", "\xE2\x80\x9C", "\xE2\x80\x9D", "\xE2\x80\xA2", "\xE2\x80\x93", "\xE2\x80\x94",
    "\xCB\x9C", "\xE2\x84\xA2", "\xC5\xA1", "\xE2\x80\xBA", "\xC5\x93", "\xC2\x9D", "\xC5\xBE", "\xC5\xB8",
};
static const uint16_t hi_cp[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

TEST(core, path_cp1252_high_bytes)
{
    rt_path_test_force_case(0);
    char h[RT_PATH_MAX];
    for (unsigned b = 0x80; b <= 0xFF; b++) {
        char a[64], want[16], rel[32];
        uint16_t cp;
        if (b < 0xA0)      { snprintf(want, sizeof want, "%s", hi[b - 0x80]); cp = hi_cp[b - 0x80]; }
        else if (b < 0xC0) { want[0] = (char)0xC2; want[1] = (char)b; want[2] = 0; cp = (uint16_t)b; }
        else               { want[0] = (char)0xC3; want[1] = (char)(b - 0x40); want[2] = 0; cp = (uint16_t)b; }
        snprintf(a, sizeof a, "C:\\Titanfall2\\x%cy", (char)b);
        snprintf(rel, sizeof rel, "x%sy", want);
        DWORD e = rt_path_to_host_a(a, CR, h);
        if (e != 0 || strcmp(h, I(rel)))
            rt_test_fail(__FILE__, __LINE__, "byte %#x: error %u host %s, wanted %s", b, (unsigned)e, h, I(rel));
        /* The same character through the W entry point gives the same host path. */
        WCHAR w[32];
        const char *pre = "C:\\Titanfall2\\x";
        unsigned n = 0;
        for (; pre[n]; n++) w[n] = (unsigned char)pre[n];
        w[n++] = cp;
        w[n++] = 'y';
        w[n] = 0;
        char h2[RT_PATH_MAX];
        T_EQ(rt_path_to_host_w(w, CR, h2), 0);
        T_TRUE(!strcmp(h, h2));
        /* ... and as the first and the last character of the name */
        snprintf(a, sizeof a, "%c", (char)b);
        snprintf(rel, sizeof rel, "%s", want);
        T_EQ(rt_path_to_host_a(a, CR, h), 0);
        T_TRUE(!strcmp(h, I(rel)));
        snprintf(a, sizeof a, "x%c", (char)b);
        snprintf(rel, sizeof rel, "x%s", want);
        T_EQ(rt_path_to_host_a(a, CR, h), 0);
        T_TRUE(!strcmp(h, I(rel)));
    }
    /* The five bytes 1252 leaves undefined are valid characters (the C1 controls) and make files like any other. */
    static const unsigned undefined[5] = { 0x81, 0x8D, 0x8F, 0x90, 0x9D };
    for (unsigned i = 0; i < 5; i++) {
        char a[32];
        snprintf(a, sizeof a, "u%c", (char)undefined[i]);
        const char want[4] = { 'u', (char)0xC2, (char)undefined[i], 0 };
        T_EQ(rt_path_to_host_a(a, CR, h), 0);
        T_TRUE(!strcmp(h, I(want)));
    }
    /* A UTF-8 string handed to the A entry point is read as the bytes it is: U+00E9 (C3 A9) is two characters of 1252, A-tilde
     * and the copyright sign; the euro sign (E2 82 AC) is three: a-circumflex, the single low quote, the not sign. */
    T_EQ(rt_path_to_host_a("C:\\Titanfall2\\\xC3\xA9", CR, h), 0);
    T_TRUE(!strcmp(h, I("\xC3\x83\xC2\xA9")));
    T_EQ(rt_path_to_host_a("C:\\Titanfall2\\\xE2\x82\xAC", CR, h), 0);
    T_TRUE(!strcmp(h, I("\xC3\xA2\xE2\x80\x9A\xC2\xAC")));
}

TEST(core, path_cp1252_low_bytes)
{
    rt_path_test_force_case(0);
    char h[RT_PATH_MAX];
    for (unsigned b = 1; b < 0x80; b++) {
        char a[64], rel[16];
        if (b == '\\' || b == '/' || b == ':') continue;                       /* separators; ':' is a report (tested elsewhere) */
        snprintf(a, sizeof a, "C:\\Titanfall2\\x%cy", (char)b);
        snprintf(rel, sizeof rel, "x%cy", (char)b);
        const bool forbidden = b < 0x20 || b == '"' || b == '*' || b == '<' || b == '>' || b == '?' || b == '|';
        DWORD e = rt_path_to_host_a(a, CR, h);
        if (forbidden) {
            if (e != ERROR_INVALID_NAME || h[0]) rt_test_fail(__FILE__, __LINE__, "byte %#x: error %u, wanted ERROR_INVALID_NAME", b, (unsigned)e);
        } else {
            if (e != 0 || strcmp(h, I(rel))) rt_test_fail(__FILE__, __LINE__, "byte %#x: error %u host %s, wanted %s", b, (unsigned)e, h, I(rel));
        }
        /* at the end of the name, a space or a period is trimmed, whatever else is */
        snprintf(a, sizeof a, "C:\\Titanfall2\\x%c", (char)b);
        e = rt_path_to_host_a(a, CR, h);
        if (forbidden) {
            T_EQ(e, ERROR_INVALID_NAME);
        } else {
            T_EQ(e, 0);
            if (b == ' ' || b == '.') snprintf(rel, sizeof rel, "x");
            else snprintf(rel, sizeof rel, "x%c", (char)b);
            T_TRUE(!strcmp(h, I(rel)));
        }
    }
    /* '/' and '\' separate; ':' is a report. */
    T_EQ(rt_path_to_host_a("C:\\Titanfall2\\x/y", CR, h), ERROR_PATH_NOT_FOUND);      /* directory x is not there */
    T_EQ(rt_path_to_host_a("C:\\Titanfall2\\x\\y", CR, h), ERROR_PATH_NOT_FOUND);
    T_TRAPS(rt_path_to_host_a("C:\\Titanfall2\\x:y", CR, h), "alternate data stream");
}

TEST(core, path_cp1252_back_to_the_guest)
{
    rt_path_test_force_case(0);
    /* The reverse mapping gives UTF-16, so a host name made from the five undefined bytes comes back as the C1 controls. */
    char host[RT_PATH_MAX], a[32];
    WCHAR w[64];
    snprintf(a, sizeof a, "C:\\Titanfall2\\q%c", 0x81);
    T_EQ(rt_path_to_host_a(a, CR, host), 0);
    T_EQ(rt_path_to_win_w(host, w, 64), 16);
    T_EQ(w[14], 'q');
    T_EQ(w[15], 0x0081);
    snprintf(a, sizeof a, "C:\\Titanfall2\\q%c", 0x80);
    T_EQ(rt_path_to_host_a(a, CR, host), 0);
    T_EQ(rt_path_to_win_w(host, w, 64), 16);
    T_EQ(w[15], 0x20AC);
}
