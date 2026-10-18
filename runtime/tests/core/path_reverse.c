/* The reverse mapping, rt_path_to_win_w: a host path under a mapped root becomes its Windows path, or
 * 0 if the guest could not have named it.  Purely textual; no file needs to exist.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/path.h"

void rt_path_test_force_case(int mode);

/* ---- helpers (repeated in each path_*.c file: a test file is self-contained) ---------------------- */

static const WCHAR *W(const char *a) {
    static WCHAR buf[4][4096];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    size_t i = 0;
    for (; a[i]; i++) b[i] = (unsigned char)a[i];
    b[i] = 0;
    return b;
}
#define U16(s)  ((const WCHAR *)u##s)

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
static const char *S(const char *rel) { return join(rt_test_state_dir(), rel); }
/* base + suffix, copied as is (no clean-up of slashes) */
static const char *cat(const char *base, const char *suffix) {
    static char buf[4][RT_PATH_MAX * 2];
    static unsigned k;
    char *b = buf[k++ & 3];
    snprintf(b, RT_PATH_MAX * 2, "%s%s", base, suffix);
    return b;
}
static const char *slash(const char *p) { return cat(p, "/"); }

static void mkdir_p(const char *abs) {
    char b[RT_PATH_MAX * 2];
    snprintf(b, sizeof b, "%s", abs);
    for (char *p = b + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        mkdir(b, 0755);
        *p = '/';
    }
    if (mkdir(b, 0755) && errno != EEXIST) rt_test_fail(__FILE__, __LINE__, "mkdir %s: %s", abs, strerror(errno));
}

/* Reverse-map `host`; want == NULL means "under no root" (0). */
static bool rev(const char *file, int line, const char *host, const char *want) {
    WCHAR out[2048];
    for (int i = 0; i < 2048; i++) out[i] = 0x5555;
    int n = rt_path_to_win_w(host, out, 2048);
    if (!want) {
        if (n != 0) { rt_test_fail(file, line, "\"%.100s\" mapped to a Windows path of %d units; wanted 0", host, n); return false; }
        if (out[0] != 0x5555) { rt_test_fail(file, line, "\"%.100s\": 0 returned, but the buffer was written", host); return false; }
        return true;
    }
    size_t wl = strlen(want);
    if ((size_t)n != wl || memcmp(out, W(want), (wl + 1) * sizeof(WCHAR)) != 0 || out[wl + 1] != 0x5555) {
        char got[300];
        size_t i = 0;
        for (; i < 299 && i <= (size_t)(n < 0 ? 0 : n) && out[i]; i++) got[i] = out[i] < 128 ? (char)out[i] : '?';
        got[i] = 0;
        rt_test_fail(file, line, "\"%.100s\"\n   mapped to (%d) %s\n   wanted        %s", host, n, got, want);
        return false;
    }
    return true;
}
#define REV(host, want)  T_TRUE(rev(__FILE__, __LINE__, (host), (want)))
#define NOROOT(host)     T_TRUE(rev(__FILE__, __LINE__, (host), NULL))

#define EX   RT_PATH_EXISTING
#define CR   RT_PATH_CREATE

/* ---- the roots ------------------------------------------------------------------------------------------ */

TEST(core, path_reverse_mounts)
{
    REV(I(""), "C:\\Titanfall2");
    REV(rt_test_install_root(), "C:\\Titanfall2");                      /* as the runner spelled it, with its double slash */
    REV(I("r2"), "C:\\Titanfall2\\r2");
    REV(I("r2/paks/x.pak"), "C:\\Titanfall2\\r2\\paks\\x.pak");
    REV(slash(I("r2")), "C:\\Titanfall2\\r2");                          /* no trailing backslash */
    REV(cat(I(""), "/"), "C:\\Titanfall2");
    REV(cat(I(""), "//"), "C:\\Titanfall2");

    REV(S("home"), "C:\\Users\\Player");
    REV(S("home/Documents/a.txt"), "C:\\Users\\Player\\Documents\\a.txt");
    REV(S("home/AppData/Local"), "C:\\Users\\Player\\AppData\\Local");
    REV(S("home/AppData/Local/x"), "C:\\Users\\Player\\AppData\\Local\\x");
    REV(S("home/AppData/Roaming"), "C:\\Users\\Player\\AppData\\Roaming");
    REV(S("home/Saved Games/s.sav"), "C:\\Users\\Player\\Saved Games\\s.sav");
    REV(S("tmp"), "C:\\Users\\Player\\AppData\\Local\\Temp");
    REV(S("tmp/sub/f.tmp"), "C:\\Users\\Player\\AppData\\Local\\Temp\\sub\\f.tmp");

    REV(S("vfs"), "C:\\");
    REV(S("vfs/Users"), "C:\\Users");
    REV(S("vfs/Windows"), "C:\\Windows");
    REV(S("vfs/Windows/system32"), "C:\\Windows\\system32");
    REV(slash(S("vfs")), "C:\\");
}

TEST(core, path_reverse_not_mapped)
{
    /* Placeholders and strays in the virtual tree, and the real Temp placeholder in the home tree, are not
     * what any Windows path reaches: the guest could not name them. */
    NOROOT(S("vfs/Titanfall2"));
    NOROOT(S("vfs/Titanfall2/x"));
    NOROOT(S("vfs/Users/Player"));
    NOROOT(S("vfs/Users/Player/x"));
    NOROOT(S("vfs/Foo"));
    NOROOT(S("vfs/Windows/system32/x"));
    NOROOT(S("vfs/Windows/x"));
    NOROOT(S("home/AppData/Local/Temp"));
    NOROOT(S("home/AppData/Local/Temp/x"));
    REV(S("home/AppData/Local/Tempx"), "C:\\Users\\Player\\AppData\\Local\\Tempx");   /* a sibling is fine */

    /* Outside the roots. */
    NOROOT("");
    NOROOT("/");
    NOROOT("/etc/passwd");
    NOROOT("/dev/null");
    NOROOT("relative/path");
    NOROOT("a");
    NOROOT(rt_test_state_dir());
    NOROOT(S("elsewhere"));
    NOROOT(cat(I(""), "2/x"));                                          /* a longer name that starts like a root */
    NOROOT(cat(S("home"), "work/x"));
    NOROOT(cat(S("home"), "2"));
    NOROOT(cat(S("tmp"), "x"));
    NOROOT(cat(S("vfs"), "2/Users"));
    /* ".." and "." are taken textually, so a path cannot get out and in again to look like a mapped one */
    REV(cat(I(""), "/a/../b"), "C:\\Titanfall2\\b");
    REV(cat(I(""), "/a/./b"), "C:\\Titanfall2\\a\\b");
    REV(cat(I(""), "/a//b"), "C:\\Titanfall2\\a\\b");
    REV(cat(I(""), "/."), "C:\\Titanfall2");
    NOROOT(cat(I(""), "/.."));
    NOROOT(cat(I(""), "/../install2"));
    NOROOT(cat(I(""), "/a/../../.."));
    NOROOT(cat(I(""), "/../../../../../../../../../../etc/passwd"));
    REV(cat(I(""), "/../state/home/x"), "C:\\Users\\Player\\x");
    REV(cat(I(""), "/a/../../state/tmp/y"), "C:\\Users\\Player\\AppData\\Local\\Temp\\y");
}

TEST(core, path_reverse_other_spelling)
{
    /* A root can be spelled two ways, as configured and as realpath() gives it (the temporary directory is below
     * /var, which is a link to /private/var on macOS); F_GETPATH and realpath give the second. */
    char real[PATH_MAX];
    T_TRUE(realpath(I(""), real) != NULL);
    REV(real, "C:\\Titanfall2");
    REV(cat(real, "/r2/x"), "C:\\Titanfall2\\r2\\x");
    T_TRUE(realpath(S("home"), real) != NULL);
    REV(real, "C:\\Users\\Player");
    REV(cat(real, "/Documents/a"), "C:\\Users\\Player\\Documents\\a");
    T_TRUE(realpath(S("tmp"), real) != NULL);
    REV(real, "C:\\Users\\Player\\AppData\\Local\\Temp");
    T_TRUE(realpath(S("vfs"), real) != NULL);
    REV(real, "C:\\");
    T_TRUE(realpath(S("vfs/Windows"), real) != NULL);
    REV(real, "C:\\Windows");
    T_TRUE(realpath(S("vfs/Titanfall2"), real) != NULL);
    NOROOT(real);                                                       /* a placeholder, the other way round */
}

/* ---- names the guest could not have written ----------------------------------------------------------- */

TEST(core, path_reverse_unnameable)
{
    /* A file whose name Windows would have changed on the way in, or would not take, can not be named by the guest in an
     * ordinary path; translating it back would not give the same host path.  Under no root, then. */
    static const char *const unnameable[] = {
        "foo.", "foo ", "a/bar.", "a/bar /c", "a:b", "a\001b", "a*b", "a?b", "a<b", "a>b", "a|b", "a\"b", "a\037", "a\tb",
        "CON", "nul", "aux.txt", "com1", "a/LPT1", "CONIN$", "x/prn/y", "Con.Tar.Gz", ".. ", "x.. ",
    };
    for (unsigned i = 0; i < sizeof unnameable / sizeof *unnameable; i++) NOROOT(I(unnameable[i]));
    /* ... and ordinary ones, which look a little like them. */
    REV(I("foo.bar"), "C:\\Titanfall2\\foo.bar");
    REV(I(".hidden"), "C:\\Titanfall2\\.hidden");
    REV(I("..hidden"), "C:\\Titanfall2\\..hidden");
    REV(I("a b"), "C:\\Titanfall2\\a b");
    REV(I("x...y"), "C:\\Titanfall2\\x...y");
    REV(I("COM0"), "C:\\Titanfall2\\COM0");
    REV(I("CONSOLE"), "C:\\Titanfall2\\CONSOLE");
    REV(I("a/b c/d"), "C:\\Titanfall2\\a\\b c\\d");
    REV(I("NUL1"), "C:\\Titanfall2\\NUL1");

    /* A name longer than a file name can be is no name. */
    char name[300];
    memset(name, 'a', 255); name[255] = 0;
    char want[400];
    snprintf(want, sizeof want, "C:\\Titanfall2\\%s", name);
    REV(I(name), want);
    name[255] = 'a'; name[256] = 0;
    NOROOT(I(name));
}

/* ---- UTF-8 to UTF-16 -------------------------------------------------------------------------------- */

static bool revw(const char *file, int line, const char *host, const WCHAR *want) {
    WCHAR out[256];
    int n = rt_path_to_win_w(host, out, 256);
    unsigned wl = 0;
    while (want[wl]) wl++;
    if ((unsigned)n != wl || memcmp(out, want, (wl + 1) * sizeof(WCHAR))) {
        rt_test_fail(file, line, "\"%.100s\": %d units, wanted %u (first unit %#x, wanted %#x)", host, n, wl, n > 0 ? out[0] : 0u, want[0]);
        return false;
    }
    return true;
}

TEST(core, path_reverse_unicode)
{
    T_TRUE(revw(__FILE__, __LINE__, I("\xC3\xA9" ".txt"), U16("C:\\Titanfall2\\\u00e9.txt")));
    T_TRUE(revw(__FILE__, __LINE__, I("\xE6\x97\xA5\xE6\x9C\xAC"), U16("C:\\Titanfall2\\\u65e5\u672c")));
    T_TRUE(revw(__FILE__, __LINE__, I("\xF0\x9F\x98\x80"), U16("C:\\Titanfall2\\\U0001F600")));    /* a surrogate pair */
    T_TRUE(revw(__FILE__, __LINE__, I("\xF4\x8F\xBF\xBF"), U16("C:\\Titanfall2\\\U0010FFFF")));
    T_TRUE(revw(__FILE__, __LINE__, I("\xF0\x90\x80\x80"), U16("C:\\Titanfall2\\\U00010000")));
    T_TRUE(revw(__FILE__, __LINE__, I("\xC2\xA0x"), U16("C:\\Titanfall2\\\u00a0x")));
    T_TRUE(revw(__FILE__, __LINE__, S("home/\xC3\xA9/\xE6\x97\xA5"), U16("C:\\Users\\Player\\\u00e9\\\u65e5")));
    /* A host name that is not UTF-8 can not become UTF-16: the unimplemented-case report, with the host path. */
    WCHAR out[100];
    T_TRAPS(rt_path_to_win_w(I("\xFF"), out, 100), "not well-formed UTF-8");
    T_TRAPS(rt_path_to_win_w(I("a\x80"), out, 100), "not well-formed UTF-8");          /* a continuation byte alone */
    T_TRAPS(rt_path_to_win_w(I("\xC0\x80"), out, 100), "not well-formed UTF-8");       /* an overlong NUL */
    T_TRAPS(rt_path_to_win_w(I("\xC1\xBF"), out, 100), "not well-formed UTF-8");       /* an overlong DEL */
    T_TRAPS(rt_path_to_win_w(I("\xE0\x80\x80"), out, 100), "not well-formed UTF-8");   /* an overlong three-byte form */
    T_TRAPS(rt_path_to_win_w(I("\xED\xA0\x80"), out, 100), "not well-formed UTF-8");   /* a surrogate written in UTF-8 */
    T_TRAPS(rt_path_to_win_w(I("\xE2\x82"), out, 100), "not well-formed UTF-8");       /* cut off */
    T_TRAPS(rt_path_to_win_w(I("\xF4\x90\x80\x80"), out, 100), "not well-formed UTF-8"); /* above U+10FFFF */
    T_TRAPS(rt_path_to_win_w(I("\xF8\x88\x80\x80\x80"), out, 100), "not well-formed UTF-8");
}

/* ---- sizes ------------------------------------------------------------------------------------------- */

TEST(core, path_reverse_sizes)
{
    /* "C:\Titanfall2\r2" is 16 units: the buffer must hold 17. */
    WCHAR o[64];
    #define FILL  for (int i_ = 0; i_ < 64; i_++) o[i_] = 0x5555
    FILL;
    T_EQ(rt_path_to_win_w(I("r2"), o, 17), 16);
    T_TRUE(!memcmp(o, W("C:\\Titanfall2\\r2"), 17 * sizeof(WCHAR)));
    T_EQ(o[17], 0x5555);
    FILL;
    T_EQ(rt_path_to_win_w(I("r2"), o, 64), 16);
    T_EQ(o[16], 0);
    T_EQ(o[17], 0x5555);
    FILL;
    T_EQ(rt_path_to_win_w(I("r2"), o, 16), 17);                          /* one too small: the size with the terminator */
    T_EQ(o[0], 0);                                                      /* an empty string in the buffer, nothing more */
    T_EQ(o[1], 0x5555);
    FILL;
    T_EQ(rt_path_to_win_w(I("r2"), o, 1), 17);
    T_EQ(o[0], 0);
    T_EQ(o[1], 0x5555);
    FILL;
    T_EQ(rt_path_to_win_w(I("r2"), o, 0), 17);                           /* size 0: nothing written */
    T_EQ(o[0], 0x5555);
    T_EQ(rt_path_to_win_w(I("r2"), NULL, 0), 17);                        /* the size query */
    FILL;
    T_EQ(rt_path_to_win_w("/etc/passwd", o, 64), 0);                     /* under no root: 0, nothing written */
    T_EQ(o[0], 0x5555);
    T_EQ(rt_path_to_win_w("/etc/passwd", NULL, 0), 0);
    /* the root, which ends in a backslash: "C:\" is 3 units */
    FILL;
    T_EQ(rt_path_to_win_w(S("vfs"), o, 4), 3);
    T_TRUE(!memcmp(o, W("C:\\"), 4 * sizeof(WCHAR)));
    T_EQ(rt_path_to_win_w(S("vfs"), o, 3), 4);
    T_EQ(rt_path_to_win_w(S("vfs"), NULL, 0), 4);
    #undef FILL

    /* Arguments no guest could have made up. */
    T_TRAPS(rt_path_to_win_w(I("r2"), NULL, 5), "rt_path_to_win_w: no buffer for 5 units");
    T_TRAPS(rt_path_to_win_w(I("r2"), o, -1), "rt_path_to_win_w: negative buffer size -1");
    T_TRAPS(rt_path_to_win_w(NULL, o, 64), "rt_path_to_win_w: no host path");
}

/* ---- both directions ----------------------------------------------------------------------------------- */

TEST(core, path_reverse_round_trip)
{
    rt_path_test_force_case(0);
    mkdir_p(I("r2/paks"));
    mkdir_p(I("a b"));
    mkdir_p(S("home/Documents"));
    mkdir_p(S("tmp/sub"));
    /* Whatever the guest can name, the reverse mapping gives a path that translates to the same host path. */
    const char *const hosts[] = {
        I(""), I("r2"), I("r2/paks"), I("r2/paks/x.pak"), I("a b"), I("a b/c.d"), I("\xC3\xA9.txt"), I("\xF0\x9F\x98\x80"),
        S("home"), S("home/Documents"), S("home/Documents/s.sav"), S("home/AppData/Local"), S("home/AppData/Local/z"),
        S("tmp"), S("tmp/x"), S("tmp/sub/y"), S("vfs"), S("vfs/Users"), S("vfs/Windows"), S("vfs/Windows/system32"),
    };
    for (unsigned i = 0; i < sizeof hosts / sizeof *hosts; i++) {
        WCHAR w[512];
        int n = rt_path_to_win_w(hosts[i], w, 512);
        T_REQUIRE(n > 0 && n < 512);
        char back[RT_PATH_MAX];
        DWORD e = rt_path_to_host_w(w, CR, back);
        size_t bl = strlen(back);
        if (e == 0 && n == 3 && bl > 1 && back[bl - 1] == '/') back[bl - 1] = 0;     /* "C:\" keeps the separator the guest wrote */
        if (e || strcmp(back, hosts[i])) rt_test_fail(__FILE__, __LINE__, "%s\n   came back as (error %u) %s", hosts[i], (unsigned)e, back);
        T_EQ(e, 0);
    }
}

TEST(core, path_reverse_longest)
{
    /* A host path of 1023 bytes can have come from rt_path_to_host_*; one of 1024 or more can not, and is under no root. */
    const size_t L = strlen(I(""));
    T_REQUIRE(L < 200);
    char d[4][201];
    for (int i = 0; i < 4; i++) { memset(d[i], 'p' + i, 200); d[i][200] = 0; }
    const size_t X = 1023 - L - 805;
    char last[400], host[RT_PATH_MAX * 2], win[RT_PATH_MAX * 2];
    memset(last, 'z', X + 1);
    last[X] = 0;
    snprintf(host, sizeof host, "%s/%s/%s/%s/%s", I(""), d[0], d[1], d[2], d[3]);
    snprintf(host + strlen(host), sizeof host - strlen(host), "/%s", last);
    T_EQ(strlen(host), 1023);
    snprintf(win, sizeof win, "C:\\Titanfall2\\%s\\%s\\%s\\%s\\%s", d[0], d[1], d[2], d[3], last);
    REV(host, win);
    last[X] = 'z'; last[X + 1] = 0;
    snprintf(host, sizeof host, "%s/%s/%s/%s/%s/%s", I(""), d[0], d[1], d[2], d[3], last);
    T_EQ(strlen(host), 1024);
    NOROOT(host);
}
