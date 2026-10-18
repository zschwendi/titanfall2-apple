/* The rule of docs/RUNTIME.md 7.1: a guest can not name a host file outside the mapped roots, whatever mixture of
 * "..", slashes, prefixes, odd names and flags it uses.  Three kinds of test: strings chosen to get out; the same
 * with the root path of this very test substituted; and a few hundred thousand random strings checked against the
 * invariant (and, for a simple grammar, against an independent reference).
 *
 * The invariant: a successful translation is "/dev/null" or a path that starts with one of the four host roots, followed
 * only by names (never "", "." or ".." in the middle).  Nothing in these tests creates anything outside the runner's
 * temporary directories.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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

#define EX   RT_PATH_EXISTING
#define CR   RT_PATH_CREATE
#define DI   RT_PATH_DIR
#define WC   RT_PATH_WILDCARD

/* ---- the invariant -------------------------------------------------------------------------------------- */

static bool under(const char *h, const char *root) {
    size_t l = strlen(root);
    return !strncmp(h, root, l) && (h[l] == 0 || h[l] == '/');
}

/* True if `h` is something the guest is allowed to be given. */
static bool inside(const char *h) {
    if (!strcmp(h, "/dev/null")) return true;
    char roots[4][RT_PATH_MAX];
    snprintf(roots[0], RT_PATH_MAX, "%s", I(""));
    snprintf(roots[1], RT_PATH_MAX, "%s", S("home"));
    snprintf(roots[2], RT_PATH_MAX, "%s", S("tmp"));
    snprintf(roots[3], RT_PATH_MAX, "%s", S("vfs"));
    for (int r = 0; r < 4; r++) {
        if (!under(h, roots[r])) continue;
        const char *p = h + strlen(roots[r]);
        while (*p) {
            p++;                                            /* the '/' */
            const char *e = p;
            while (*e && *e != '/') e++;
            size_t l = (size_t)(e - p);
            if (l == 0 && *e == 0) return true;             /* a trailing slash */
            if (l == 0 || (l == 1 && p[0] == '.') || (l == 2 && p[0] == '.' && p[1] == '.')) return false;
            p = e;
        }
        return true;
    }
    return false;
}

static bool allowed_error(DWORD e) {
    return e == ERROR_PATH_NOT_FOUND || e == ERROR_FILE_NOT_FOUND || e == ERROR_ACCESS_DENIED || e == ERROR_INVALID_NAME ||
           e == ERROR_BAD_PATHNAME || e == ERROR_FILENAME_EXCED_RANGE;
}

/* Translate `win` (W form) with `flags` and check the invariant.  Returns the error. */
static DWORD probe(const char *file, int line, const WCHAR *w, uint32_t flags, const char *shown) {
    char host[RT_PATH_MAX];
    memset(host, 'X', sizeof host);
    DWORD e = rt_path_to_host_w(w, flags, host);
    if (e == 0 && !inside(host)) rt_test_fail(file, line, "ESCAPE: \"%s\" flags %#x gave %s", shown, flags, host);
    if (e != 0 && !allowed_error(e)) rt_test_fail(file, line, "\"%s\" flags %#x: unexpected error %u", shown, flags, (unsigned)e);
    if (e != 0 && host[0] != 0) rt_test_fail(file, line, "\"%s\" flags %#x: error %u but host buffer holds \"%.60s\"", shown, flags, (unsigned)e, host);
    return e;
}

/* ---- strings chosen to get out -------------------------------------------------------------------------- */

TEST(core, path_escape_attempts)
{
    rt_path_test_force_case(0);
    mkdir_p(I("a/b/c"));
    mkdir_p(I("...."));
    mkdir_p(I(".. "));
    static const char *const attempts[] = {
        /* too many ".." in every form */
        "C:\\Titanfall2\\..\\..\\..\\etc\\passwd", "C:\\..\\..\\etc\\passwd", "\\..\\..\\etc\\passwd", "/../../etc/passwd",
        "../../../../../../etc/passwd", "..\\..\\..\\..\\..\\..\\..\\..\\etc\\passwd", "C:..\\..\\..\\etc", "C:/../../etc",
        "C:\\Titanfall2\\a\\..\\..\\..\\..\\..\\etc", "C:\\Titanfall2\\a\\b\\c\\d\\..\\..\\..\\..\\..\\..\\..\\..\\..\\..\\..\\etc",
        "C:/Titanfall2/../../../../../../../../../../../../tmp", "C:\\Titanfall2/..\\../..//etc", "C:\\Titanfall2\\\\..\\\\..\\\\etc",
        "C:\\Titanfall2\\..\\", "C:\\Titanfall2\\..\\..\\", "C:\\Titanfall2\\a\\..\\..", "..", "..\\..", "..\\..\\..",
        "C:\\Users\\Player\\..\\..\\..\\etc", "C:\\Users\\Player\\AppData\\Local\\Temp\\..\\..\\..\\..\\..\\etc",
        "C:\\Windows\\system32\\..\\..\\..\\etc", "C:\\Users\\Player\\AppData\\Local\\Temp\\..\\..\\..\\..",
        /* names made of dots, which are names and not "..": they stay inside, and mean something else */
        "C:\\Titanfall2\\....\\etc", "C:\\Titanfall2\\...\\..\\..\\etc", "C:\\Titanfall2\\.. \\..\\etc", "C:\\Titanfall2\\. .\\etc",
        "C:\\Titanfall2\\.. .\\..\\etc", "C:\\Titanfall2\\..;\\etc", "C:\\Titanfall2\\..%2f..\\etc", "C:\\Titanfall2\\%2e%2e\\etc",
        "C:\\Titanfall2\\.... \\..\\..", "C:\\Titanfall2\\....\\..\\..\\..", "C:\\Titanfall2\\.. \\..", "C:\\Titanfall2\\.. \\.. \\..",
        "C:\\Titanfall2\\a\\.. ", "C:\\Titanfall2\\a\\...", "C:\\Titanfall2\\a\\. .", "C:\\Titanfall2\\a\\ .. ", "C:\\Titanfall2\\.\\. .\\.. .",
        /* the prefixes */
        "\\\\?\\C:\\Titanfall2\\..\\..\\etc", "\\\\?\\C:\\..\\etc", "\\\\?\\C:\\Titanfall2\\a\\.\\.\\..", "\\\\?\\C:\\Titanfall2/../etc",
        "//?/C:/Titanfall2/../../etc", "\\\\.\\C:\\Titanfall2\\..\\..\\etc", "\\\\?\\GLOBALROOT\\..\\etc", "\\\\?\\UNC\\..\\..\\etc",
        "\\??\\C:\\..\\etc", "\\\\?\\C:\\Titanfall2\\....\\..", "\\\\?\\C:\\Titanfall2\\.. \\..", "\\\\?\\C:\\Titanfall2\\a\\..\\", "\\\\?\\C:\\..",
        "\\\\?\\C:\\Titanfall2\\\\..\\\\..\\\\etc", "\\\\?\\C:\\Titanfall2\\a\\\\", "\\\\?\\C:\\Titanfall2\\a/../..",
        "D:\\..\\..\\etc", "\\\\server\\share\\..\\..\\etc", "\\\\.\\..\\etc", "\\\\.\\C:\\..", "\\\\?\\", "\\\\", "//",
        /* host-looking paths in the guest's hands */
        "/etc/passwd", "/Users/someone", "/var/folders", "C:\\etc", "C:/private/var", "~", "~/..", "$HOME", "C:\\Titanfall2\\~", "C:\\Titanfall2\\`id`",
        "C:\\Titanfall2\\/etc/passwd", "C:\\Titanfall2\\\\etc\\passwd", "C:\\Titanfall2\\a\\/..\\/..\\/etc", "/", "\\", "C:\\", "C:", ".", "",
        "/dev/null", "/dev/tty", "C:\\dev\\null", "C:\\Titanfall2\\dev\\null", "\\\\.\\NUL", "NUL", "nul\\..\\..\\etc", "NUL\\..",
        "C:\\Titanfall2\\a\\NUL\\..\\..\\..\\etc", "C:\\NUL\\..\\..\\etc", "C:\\Titanfall2\\NUL.\\..\\..\\etc",
    };
    static const uint32_t flags[] = { 0, EX, CR, DI, EX | DI, CR | DI };
    for (unsigned i = 0; i < sizeof attempts / sizeof *attempts; i++) {
        const WCHAR *w = W(attempts[i]);
        for (unsigned k = 0; k < sizeof flags / sizeof *flags; k++) probe(__FILE__, __LINE__, w, flags[k], attempts[i]);
        /* as a search: the pattern is a name of its own below the path */
        char s[600];
        snprintf(s, sizeof s, "%s\\*", attempts[i]);
        probe(__FILE__, __LINE__, W(s), WC, s);
        snprintf(s, sizeof s, "%s/*", attempts[i]);
        probe(__FILE__, __LINE__, W(s), WC, s);
        /* the A entry point agrees (every string above is ASCII) */
        char h[RT_PATH_MAX], hw[RT_PATH_MAX];
        DWORD ea = rt_path_to_host_a(attempts[i], CR, h), ew = rt_path_to_host_w(w, CR, hw);
        T_EQ(ea, ew);
        T_TRUE(!strcmp(h, hw));
    }

    /* Some of them by name: what each one is. */
    char h[RT_PATH_MAX];
    T_EQ(rt_path_to_host_w(W("C:\\Titanfall2\\..\\..\\..\\etc\\passwd"), EX, h), ERROR_PATH_NOT_FOUND);   /* C:\etc\passwd */
    T_EQ(rt_path_to_host_w(W("C:\\..\\..\\etc\\passwd"), CR, h), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_to_host_w(W("..\\..\\..\\..\\etc\\passwd"), CR, h), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_to_host_w(W("/../../etc/passwd"), EX, h), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_to_host_w(W("C:\\Titanfall2\\..\\..\\etc"), EX, h), ERROR_FILE_NOT_FOUND);              /* C:\etc: not there */
    T_EQ(rt_path_to_host_w(W("C:\\Titanfall2\\..\\..\\etc"), CR, h), ERROR_ACCESS_DENIED);               /* ... and can not be made */
    T_EQ(rt_path_to_host_w(W("\\\\?\\C:\\Titanfall2\\..\\..\\etc"), CR, h), ERROR_INVALID_NAME);
    T_EQ(rt_path_to_host_w(W("\\\\?\\C:\\..\\etc"), CR, h), ERROR_INVALID_NAME);
    T_EQ(rt_path_to_host_w(W("/etc/passwd"), CR, h), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_to_host_w(W("/Users/someone"), EX, h), ERROR_FILE_NOT_FOUND);       /* C:\Users\someone: no such user in the virtual C:\Users */
    T_EQ(rt_path_to_host_w(W("/Users/someone"), CR, h), ERROR_ACCESS_DENIED);
    T_EQ(rt_path_to_host_w(W("/Users/someone/Library"), CR, h), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_to_host_w(W("C:\\Titanfall2\\....\\etc"), EX, h), ERROR_FILE_NOT_FOUND);                /* "...." is a directory here, "etc" is not in it */
    T_EQ(rt_path_to_host_w(W("C:\\Titanfall2\\....\\..\\..\\etc"), EX, h), ERROR_FILE_NOT_FOUND);       /* ".." twice from "....": C:\etc */
    T_EQ(rt_path_to_host_w(W("C:\\Titanfall2\\.. \\etc"), EX, h), ERROR_FILE_NOT_FOUND);
    /* The strings with this test's own host root in them, as a guest that found it out might write them. */
    const char *roots[] = { I(""), S("home"), S("tmp"), S("vfs"), rt_test_state_dir() };
    for (unsigned r = 0; r < sizeof roots / sizeof *roots; r++) {
        static const char *const tails[] = { "", "/", "/..", "/../..", "/../../..", "/../../../../../../../../../..", "/../etc/passwd", "/a/../..", "/./..", "//..//" };
        for (unsigned t = 0; t < sizeof tails / sizeof *tails; t++) {
            char s[2 * RT_PATH_MAX];
            snprintf(s, sizeof s, "%s%s", roots[r], tails[t]);
            for (unsigned k = 0; k < sizeof flags / sizeof *flags; k++) probe(__FILE__, __LINE__, W(s), flags[k], s);
            snprintf(s, sizeof s, "C:%s%s", roots[r], tails[t]);
            for (unsigned k = 0; k < sizeof flags / sizeof *flags; k++) probe(__FILE__, __LINE__, W(s), flags[k], s);
            snprintf(s, sizeof s, "\\\\?\\C:%s%s", roots[r], tails[t]);
            for (unsigned k = 0; k < sizeof flags / sizeof *flags; k++) probe(__FILE__, __LINE__, W(s), flags[k], s);
        }
        T_EQ(rt_path_to_host_w(W(roots[r]), EX, h), ERROR_PATH_NOT_FOUND);       /* a host path is a Windows path under C:\ and is not there */
    }
}

/* ---- a link inside a root ---------------------------------------------------------------------------------- */

TEST(core, path_escape_symlink_is_followed)
{
    /* What the translation does NOT do: look at the files.  A symbolic link inside a root is followed by the host
     * when the shim opens the path, so it can lead out.  The guest has no call that makes one (and no shim does);
     * a link there is the user's doing, and a game installed through a linked directory has to work.  This pins the
     * behaviour so that a change to it is deliberate. */
    rt_path_test_force_case(0);
    T_EQ(symlink(rt_test_state_dir(), I("link")), 0);
    char h[RT_PATH_MAX];
    T_EQ(rt_path_to_host_w(W("C:\\Titanfall2\\link\\home"), EX, h), 0);
    T_TRUE(!strcmp(h, I("link/home")));
    T_TRUE(inside(h));                                            /* textually inside the root, although the kernel will go through the link */
    T_EQ(rt_path_to_host_w(W("C:\\Titanfall2\\link\\..\\..\\etc"), EX, h), ERROR_FILE_NOT_FOUND);    /* ".." is taken before the link is looked at */
}

/* ---- random strings ------------------------------------------------------------------------------------------- */

static uint64_t rng = UINT64_C(0x9E3779B97F4A7C15);
static uint64_t rnd(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

TEST(core, path_escape_random)
{
    mkdir_p(I("a/b/c"));
    /* Tokens contain no ':' (an alternate data stream is a report, not an error) and no device names. */
    static const char *const tok[] = {
        "..", "..", "..", ".", ".", "\\", "\\", "\\", "/", "/", "a", "b", "Titanfall2", "Users", "Player", "Windows", "system32",
        "AppData", "Local", "Temp", " ", "...", "....", "x.", "x ", "*", "?", "<", ">", "|", "\"", "\001", "\\\\", "//", "\\.\\", "\\..\\",
        "/../", ".. ", ". ", "NULL", "nu", "co", "a.b",
    };
    static const char *const prefix[] = {
        "", "", "", "C:", "C:\\", "C:/", "c:\\", "\\", "/", "\\\\?\\C:\\", "\\\\?\\c:\\", "\\\\?\\", ".\\", "..\\", "D:\\", "\\\\", "\\\\.\\",
        "//", "C:\\Titanfall2\\", "C:\\Users\\Player\\", "C:\\Windows\\", "C:\\Users\\Player\\AppData\\Local\\Temp\\", "\\\\?\\C:\\Titanfall2\\",
    };
    static const uint32_t flagset[] = { 0, EX, CR, DI, EX | DI, CR | DI, WC };
    unsigned ok = 0, total = 0;
    for (int mode = 0; mode < 2; mode++) {
        rt_path_test_force_case(mode);
        const int iterations = mode == 0 ? 150000 : 20000;
        for (int it = 0; it < iterations; it++) {
            char s[1500];
            size_t n = 0;
            const char *p = prefix[rnd() % (sizeof prefix / sizeof *prefix)];
            n += (size_t)snprintf(s + n, sizeof s - n, "%s", p);
            unsigned count = (unsigned)(rnd() % 15);
            for (unsigned t = 0; t < count; t++) n += (size_t)snprintf(s + n, sizeof s - n, "%s", tok[rnd() % (sizeof tok / sizeof *tok)]);
            const uint32_t flags = flagset[rnd() % (sizeof flagset / sizeof *flagset)];
            if (flags == WC && n > 0 && s[n - 1] != '\\' && s[n - 1] != '/') { s[n++] = 'q'; s[n] = 0; }   /* a search for "." or ".." is a report */
            if (flags == WC && n == 0) { s[n++] = 'q'; s[n] = 0; }
            DWORD e = probe(__FILE__, __LINE__, W(s), flags, s);
            total++;
            ok += e == 0;
            if ((it & 15) == 0) {                                        /* the A form gives the same answer */
                char h[RT_PATH_MAX], hw[RT_PATH_MAX];
                DWORD ea = rt_path_to_host_a(s, flags, h), ew = rt_path_to_host_w(W(s), flags, hw);
                if (ea != ew || strcmp(h, hw)) rt_test_fail(__FILE__, __LINE__, "A and W forms differ for \"%s\"", s);
            }
        }
    }
    T_TRUE(ok > total / 20);                                             /* the random strings reach the success paths too */
}

/* ---- an independent reference for a simple grammar ---------------------------------------------------------------- */

TEST(core, path_escape_reference)
{
    rt_path_test_force_case(0);
    /* Directories a, b, c nested three deep exist; paths are a prefix, then segments from {a, b, c, ".", ".."} joined by
     * separators.  The reference below is a plain stack of names. */
    static const char *const letters[] = { "a", "b", "c" };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++) {
                char d[64];
                snprintf(d, sizeof d, "%s/%s/%s", letters[i], letters[j], letters[k]);
                mkdir_p(I(d));
            }
    static const char *const pre[] = { "C:\\Titanfall2\\", "C:/Titanfall2/", "\\Titanfall2\\", "", ".\\", "C:", "c:\\TITANFALL2\\", "..\\Titanfall2\\", "C:\\x\\..\\Titanfall2\\", "/titanfall2/" };
    static const char *const seg[] = { "a", "b", "c", ".", ".." };
    static const char *const sep[] = { "\\", "/", "\\\\", "/\\", "//", "\\/" };
    unsigned checked = 0;
    for (int it = 0; it < 100000; it++) {
        const unsigned pi = (unsigned)(rnd() % (sizeof pre / sizeof *pre));
        const unsigned nseg = (unsigned)(rnd() % 8);
        const bool trailing = rnd() % 3 == 0;
        char s[400];
        size_t n = (size_t)snprintf(s, sizeof s, "%s", pre[pi]);
        const char *comps[16];
        size_t nc = 1;
        comps[0] = "Titanfall2";
        for (unsigned t = 0; t < nseg; t++) {
            const char *g = seg[rnd() % 5];
            if (t > 0) n += (size_t)snprintf(s + n, sizeof s - n, "%s", sep[rnd() % 6]);
            n += (size_t)snprintf(s + n, sizeof s - n, "%s", g);
            if (!strcmp(g, ".")) { }
            else if (!strcmp(g, "..")) { if (nc) nc--; }
            else comps[nc++] = g;
        }
        if (trailing && nseg > 0) n += (size_t)snprintf(s + n, sizeof s - n, "%s", sep[rnd() % 6]);
        const bool ends_in_sep = n > 0 && (s[n - 1] == '\\' || s[n - 1] == '/');
        if (n == 0) continue;                                              /* the empty string is its own case, tested elsewhere */

        /* the reference answer */
        DWORD want_err = 0;
        char want[RT_PATH_MAX];
        if (nc == 0) {
            snprintf(want, sizeof want, "%s%s", S("vfs"), ends_in_sep ? "/" : "");
        } else if (strcmp(comps[0], "Titanfall2") != 0) {
            want_err = nc == 1 ? ERROR_ACCESS_DENIED : ERROR_PATH_NOT_FOUND;                  /* new in C:\, or below something that is not */
        } else {
            char rel[200] = "";
            for (size_t i = 1; i < nc; i++) {
                if (i > 1) strcat(rel, "/");
                strcat(rel, comps[i]);
            }
            if (nc > 2) {                                                                   /* the directory it is in must exist */
                char parent[200] = "";
                for (size_t i = 1; i + 1 < nc; i++) {
                    if (i > 1) strcat(parent, "/");
                    strcat(parent, comps[i]);
                }
                struct stat st;
                if (stat(I(parent), &st) != 0 || !S_ISDIR(st.st_mode)) want_err = ERROR_PATH_NOT_FOUND;
            }
            snprintf(want, sizeof want, "%s%s", I(rel), ends_in_sep ? "/" : "");
        }

        char h[RT_PATH_MAX];
        DWORD e = rt_path_to_host_w(W(s), CR, h);
        checked++;
        if (e != want_err || (e == 0 && strcmp(h, want))) {
            rt_test_fail(__FILE__, __LINE__, "\"%s\": error %u host %s\n   wanted error %u host %s", s, (unsigned)e, h, (unsigned)want_err, want_err ? "-" : want);
            break;
        }
        if (it % 7 == 0) {
            DWORD ea = rt_path_to_host_a(s, CR, h);
            if (ea != want_err || (ea == 0 && strcmp(h, want))) { rt_test_fail(__FILE__, __LINE__, "A form of \"%s\" differs", s); break; }
        }
    }
    T_TRUE(checked > 90000);
}
