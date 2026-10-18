/* What a Windows path may look like (rt/path.h): separators, "." and "..", relative, rooted and
 * drive-relative forms, trailing dots and spaces, the \\?\ prefix, names Windows forbids, names
 * that are too long, the paths this runtime does not provide (UNC, other drives, devices), and the
 * cases it refuses loudly (alternate data streams, DOS device names with an extension, lone
 * surrogates).  The expectations come from the Microsoft pages "Naming Files, Paths, and
 * Namespaces" and "File path formats on Windows systems", not from the code.
 */
#include <errno.h>
#include <fcntl.h>
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
static const char *slash(const char *p) {
    static char buf[4][RT_PATH_MAX * 2];
    static unsigned k;
    char *b = buf[k++ & 3];
    snprintf(b, RT_PATH_MAX * 2, "%s/", p);
    return b;
}

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

static bool chkw(const char *file, int line, const WCHAR *win, uint32_t flags, DWORD want_err, const char *want_host) {
    char host[RT_PATH_MAX];
    memset(host, 'X', sizeof host);
    DWORD e = rt_path_to_host_w(win, flags, host);
    if (e != want_err) {
        rt_test_fail(file, line, "flags %#x: error %u, wanted %u (host buffer \"%.80s\")", flags, (unsigned)e, (unsigned)want_err, host);
        return false;
    }
    if (e) {
        if (host[0] != 0) { rt_test_fail(file, line, "flags %#x: failed with %u but left \"%.80s\" in the host buffer", flags, (unsigned)e, host); return false; }
        return true;
    }
    if (strcmp(host, want_host)) {
        rt_test_fail(file, line, "flags %#x: host path\n    %s\n  wanted\n    %s", flags, host, want_host);
        return false;
    }
    return true;
}
#define HOKW(win, flags, want)  T_TRUE(chkw(__FILE__, __LINE__, (win), (flags), 0, (want)))
#define HOK(win, flags, want)   T_TRUE(chkw(__FILE__, __LINE__, W(win), (flags), 0, (want)))
#define HERR(win, flags, err)   T_TRUE(chkw(__FILE__, __LINE__, W(win), (flags), (err), NULL))

/* The same, through the A entry point (bytes of an ASCII-only string are the same in code page 1252). */
static bool chka(const char *file, int line, const char *win, uint32_t flags, DWORD want_err, const char *want_host) {
    char host[RT_PATH_MAX];
    memset(host, 'X', sizeof host);
    DWORD e = rt_path_to_host_a(win, flags, host);
    if (e != want_err) { rt_test_fail(file, line, "A form, flags %#x: error %u, wanted %u", flags, (unsigned)e, (unsigned)want_err); return false; }
    if (e) return host[0] == 0;
    if (strcmp(host, want_host)) { rt_test_fail(file, line, "A form: host path\n    %s\n  wanted\n    %s", host, want_host); return false; }
    return true;
}

#define EX   RT_PATH_EXISTING
#define CR   RT_PATH_CREATE
#define DI   RT_PATH_DIR
#define WC   RT_PATH_WILDCARD

static void make_tree(void) {
    rt_path_test_force_case(0);
    mkdir_p(I("a/b/c"));
    mkdir_p(I("a.."));
    mkdir_p(I("d "));
    mkdir_p(I("..."));
    mkdir_p(I("CON"));
}

/* ---- normalisation --------------------------------------------------------------------------------- */

TEST(core, path_normalise)
{
    make_tree();
    /* The current directory is C:\Titanfall2.  `rel` is the host path below the install root. */
    static const struct { const char *win; uint32_t flags; const char *rel; } ok[] = {
        /* separators: both kinds, mixed, runs of them */
        { "C:\\Titanfall2\\a\\b",                CR, "a/b" },
        { "C:/Titanfall2/a/b",                   CR, "a/b" },
        { "C:\\Titanfall2/a\\b",                 CR, "a/b" },
        { "C:/Titanfall2\\a/b",                  CR, "a/b" },
        { "C:\\Titanfall2\\\\\\a\\\\b",          CR, "a/b" },
        { "C:\\\\Titanfall2\\a",                 CR, "a" },
        { "C://Titanfall2//a//",                 CR, "a/" },
        { "C:\\Titanfall2\\a\\b\\",              CR, "a/b/" },
        { "C:\\Titanfall2\\a\\b/",               CR, "a/b/" },
        { "C:\\Titanfall2\\a\\b\\\\\\",          CR, "a/b/" },
        /* "." and ".." are resolved textually, whether or not the directory exists */
        { "C:\\Titanfall2\\a\\.\\b",             CR, "a/b" },
        { "C:\\Titanfall2\\a\\.\\.\\b\\.",       CR, "a/b" },
        { "C:\\Titanfall2\\.",                   CR, "" },
        { "C:\\Titanfall2\\a\\b\\..\\x",         CR, "a/x" },
        { "C:\\Titanfall2\\a\\b\\..",            CR, "a" },
        { "C:\\Titanfall2\\a\\b\\..\\",          CR, "a/" },
        { "C:\\Titanfall2\\a\\b\\c\\..\\..\\x",  CR, "a/x" },
        { "C:\\Titanfall2\\a\\nodir\\..\\b",     CR, "a/b" },          /* nodir is never looked at */
        { "C:\\Titanfall2\\nodir\\..\\a",        CR, "a" },
        { "C:\\Titanfall2\\a\\..\\..\\Titanfall2\\a\\b", CR, "a/b" },
        { "C:\\Titanfall2\\a*\\..\\b",           CR, "b" },            /* a name that would be invalid is gone before it is checked */
        { "C:\\Titanfall2\\a?\\<\\..\\..\\a",    CR, "a" },
        { "C:\\Titanfall2\\a\\b\\..\\..\\..\\Titanfall2", CR, "" },
        /* ".." at the root stays at the root */
        { "C:\\..\\Titanfall2\\a",               CR, "a" },
        { "C:\\..\\..\\..\\..\\Titanfall2",      CR, "" },
        { "C:\\.\\Titanfall2\\a",                CR, "a" },
        { "C:\\a\\..\\..\\Titanfall2",           CR, "" },
        { "C:/../Titanfall2",                    CR, "" },
        /* rooted: on the current drive, which is C: */
        { "\\Titanfall2\\a",                     CR, "a" },
        { "/Titanfall2/a",                       CR, "a" },
        { "\\..\\Titanfall2",                    CR, "" },
        { "\\.\\Titanfall2",                     CR, "" },
        /* relative to the guest's current directory */
        { "x",                                   CR, "x" },
        { "a\\b",                                CR, "a/b" },
        { ".\\a",                                CR, "a" },
        { "a/./b",                               CR, "a/b" },
        { "a\\..\\a\\b",                         CR, "a/b" },
        { "..\\Titanfall2\\a",                   CR, "a" },
        { "..\\..\\..\\Titanfall2",              CR, "" },
        { ".",                                   CR, "" },
        { "a\\b\\..",                            CR, "a" },
        { "a\\b\\c\\..\\..\\..\\x",              CR, "x" },
        { "a\\",                                 CR, "a/" },
        { "my file.txt",                         CR, "my file.txt" },
        /* relative to the current directory of drive C: ("C:name" is not "C:\name") */
        { "C:a\\b",                              CR, "a/b" },
        { "c:a",                                 CR, "a" },
        { "C:",                                  CR, "" },
        { "C:.",                                 CR, "" },
        { "C:..\\Titanfall2",                    CR, "" },
        { "C:a\\..\\x",                          CR, "x" },
        /* trailing dots and spaces of the last segment: all removed (a segment of dots disappears) */
        { "C:\\Titanfall2\\a\\x.",               CR, "a/x" },
        { "C:\\Titanfall2\\a\\x..",              CR, "a/x" },
        { "C:\\Titanfall2\\a\\x. . .",           CR, "a/x" },
        { "C:\\Titanfall2\\a\\x   ",             CR, "a/x" },
        { "C:\\Titanfall2\\a\\x. ",              CR, "a/x" },
        { "C:\\Titanfall2\\a\\x .",              CR, "a/x" },
        { "C:\\Titanfall2\\a\\x.txt.",           CR, "a/x.txt" },
        { "C:\\Titanfall2\\a\\x y",              CR, "a/x y" },
        { "C:\\Titanfall2\\a\\ x",               CR, "a/ x" },         /* leading spaces stay */
        { "C:\\Titanfall2\\a\\...",              CR, "a" },
        { "C:\\Titanfall2\\a\\ ",                CR, "a" },
        { "C:\\Titanfall2\\a\\. ",               CR, "a" },
        { "C:\\Titanfall2\\a\\.. ",              CR, "a" },            /* ".. " is not ".."; it is trimmed to nothing */
        { "C:\\Titanfall2\\...",                 CR, "" },
        /* ... of other segments: one trailing period goes ("a." is "a"), nothing else does */
        { "C:\\Titanfall2\\a.\\b",               CR, "a/b" },
        { "C:\\Titanfall2\\a.\\",                CR, "a/" },
        { "C:\\Titanfall2\\a..\\x",              CR, "a../x" },
        { "C:\\Titanfall2\\d \\x",               CR, "d /x" },
        { "C:\\Titanfall2\\...\\x",              CR, ".../x" },
        { "C:\\Titanfall2\\a\\b\\.\\",           CR, "a/b/" },
        { "C:\\Titanfall2\\a\\b.\\c.\\",         CR, "a/b/c/" },
    };
    for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++) {
        const char *want = I(ok[i].rel);
        HOK(ok[i].win, ok[i].flags, want);
        T_TRUE(chka(__FILE__, __LINE__, ok[i].win, ok[i].flags, 0, want));
    }
    /* Names that do not exist, in the same words. */
    static const struct { const char *win; uint32_t flags; DWORD err; } bad[] = {
        { "",                                    CR, ERROR_PATH_NOT_FOUND },
        { "",                                    EX, ERROR_PATH_NOT_FOUND },
        { "",                                    WC, ERROR_PATH_NOT_FOUND },
        { "C:\\Titanfall2\\nodir\\x",            CR, ERROR_PATH_NOT_FOUND },
        { "nodir\\x",                            CR, ERROR_PATH_NOT_FOUND },
        { "C:\\Titanfall2\\a\\b\\c\\nodir\\x",   EX, ERROR_PATH_NOT_FOUND },
        { "C:/a",                                CR, ERROR_ACCESS_DENIED },      /* "C:/a" is "C:\a": the root, not the current directory */
        { "C:/a",                                EX, ERROR_FILE_NOT_FOUND },
        { "\\a",                                 CR, ERROR_ACCESS_DENIED },
        { "..\\..\\a",                           CR, ERROR_ACCESS_DENIED },
        { "..\\..\\a\\b",                        CR, ERROR_PATH_NOT_FOUND },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        HERR(bad[i].win, bad[i].flags, bad[i].err);
        T_TRUE(chka(__FILE__, __LINE__, bad[i].win, bad[i].flags, bad[i].err, NULL));
    }

    /* "..\..": from C:\Titanfall2 that is C:\ , a virtual directory. */
    HOK("..", EX, S("vfs"));
    HOK("..\\", EX, slash(S("vfs")));
}

/* ---- paths this runtime does not provide: they fail, and are logged ---------------------------------- */

TEST(core, path_not_provided)
{
    make_tree();
    /* Other drives, UNC paths, device paths: an error from the header's list, and a line on the
     * `file` channel that names the offending path. */
    static const struct { const char *win; DWORD err; const char *log; } t[] = {
        { "D:\\x",                                  ERROR_PATH_NOT_FOUND, "no such drive (only C: exists): \"D:\\x\"" },
        { "d:",                                     ERROR_PATH_NOT_FOUND, "no such drive (only C: exists): \"d:\"" },
        { "Z:\\Titanfall2",                         ERROR_PATH_NOT_FOUND, "no such drive (only C: exists): \"Z:\\Titanfall2\"" },
        { "A:/x/y",                                 ERROR_PATH_NOT_FOUND, "no such drive (only C: exists): \"A:/x/y\"" },
        { "E:file.txt",                             ERROR_PATH_NOT_FOUND, "no such drive (only C: exists): \"E:file.txt\"" },
        { "\\\\server\\share\\file.txt",            ERROR_PATH_NOT_FOUND, "UNC path (not provided): \"\\\\server\\share\\file.txt\"" },
        { "//server/share/file.txt",                ERROR_PATH_NOT_FOUND, "UNC path (not provided): \"//server/share/file.txt\"" },
        { "\\\\server",                             ERROR_PATH_NOT_FOUND, "UNC path (not provided): \"\\\\server\"" },
        { "\\\\localhost\\c$\\Titanfall2",          ERROR_PATH_NOT_FOUND, "UNC path (not provided): \"\\\\localhost\\c$\\Titanfall2\"" },
        { "\\\\?\\UNC\\server\\share\\f",           ERROR_PATH_NOT_FOUND, "UNC path (not provided): \"\\\\?\\UNC\\server\\share\\f\"" },
        { "\\\\?\\unc",                             ERROR_PATH_NOT_FOUND, "UNC path (not provided): \"\\\\?\\unc\"" },
        { "\\\\.\\COM1",                            ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\.\\COM1\"" },
        { "\\\\.\\PhysicalDrive0",                  ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\.\\PhysicalDrive0\"" },
        { "\\\\.\\pipe\\game",                      ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\.\\pipe\\game\"" },
        { "\\\\.\\C:\\Titanfall2\\a",               ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\.\\C:\\Titanfall2\\a\"" },
        { "\\\\.\\C:",                              ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\.\\C:\"" },
        { "\\\\.\\NUL\\x",                          ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\.\\NUL\\x\"" },
        { "\\\\.\\",                                ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\.\\\"" },
        { "\\\\.",                                  ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\.\"" },
        { "\\\\?\\GLOBALROOT\\Device\\Null",        ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\?\\GLOBALROOT\\Device\\Null\"" },
        { "\\\\?\\Volume{01234567-89ab-cdef-0123-456789abcdef}\\", ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\?\\Volume{" },
        { "\\\\?\\pipe\\x",                         ERROR_PATH_NOT_FOUND, "device path (not provided): \"\\\\?\\pipe\\x\"" },
        { "//./COM1",                               ERROR_PATH_NOT_FOUND, "device path (not provided): \"//./COM1\"" },
        { "//?/C:/Titanfall2",                      ERROR_PATH_NOT_FOUND, "device path (not provided): \"//?/C:/Titanfall2\"" },
        { "\\\\?\\C:",                              ERROR_PATH_NOT_FOUND, "volume device path (not provided): \"\\\\?\\C:\"" },
        { "\\\\?\\D:\\x",                           ERROR_PATH_NOT_FOUND, "no such drive (only C: exists): \"\\\\?\\D:\\x\"" },
        { "\\??\\C:\\Titanfall2",                   ERROR_PATH_NOT_FOUND, "NT-namespace path (not provided): \"\\??\\C:\\Titanfall2\"" },
        /* malformed prefixes: ERROR_BAD_PATHNAME */
        { "\\\\",                                   ERROR_BAD_PATHNAME,   "UNC path without a server name: \"\\\\\"" },
        { "//",                                     ERROR_BAD_PATHNAME,   "UNC path without a server name: \"//\"" },
        { "\\\\\\x",                                ERROR_BAD_PATHNAME,   "UNC path without a server name: \"\\\\\\x\"" },
        { "\\\\?\\",                                ERROR_BAD_PATHNAME,   "\\\\?\\ with nothing after it" },
        { "\\\\?\\C:x",                             ERROR_BAD_PATHNAME,   "malformed \\\\?\\ drive path: \"\\\\?\\C:x\"" },
        { "\\\\?\\C:/Titanfall2",                   ERROR_BAD_PATHNAME,   "malformed \\\\?\\ drive path: \"\\\\?\\C:/Titanfall2\"" },
    };
    static const uint32_t flags[] = { CR, EX, DI, WC };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        for (unsigned k = 0; k < sizeof flags / sizeof *flags; k++) {
            unsigned before = T_LOG_COUNT(RT_CH_FILE);
            HERR(t[i].win, flags[k], t[i].err);
            T_TRUE(T_LOG_COUNT(RT_CH_FILE) == before + 1);               /* one line per refusal */
        }
        T_TRUE(T_LOGGED(RT_CH_FILE, t[i].log));
        T_TRUE(chka(__FILE__, __LINE__, t[i].win, CR, t[i].err, NULL));
    }
    /* Nothing was created by any of them. */
    struct stat st;
    T_TRUE(stat(I("x"), &st) != 0 && stat(S("vfs/x"), &st) != 0);
}

/* ---- reserved device names ---------------------------------------------------------------------------- */

TEST(core, path_device_names)
{
    make_tree();
    /* CON, PRN, AUX, COM1-9, LPT1-9, CONIN$ and CONOUT$ are DOS devices, in any case, in any directory,
     * also with the trailing dots and spaces that are trimmed.  Windows has them; this runtime does not. */
    static const char *const dev[] = {
        "CON", "PRN", "AUX", "CONIN$", "CONOUT$",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",     /* each one: a name */
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",     /* dropped from the table is a gap */
        "con", "Prn", "aux", "com5", "lpt2", "conin$", "Conout$", "CON.", "AUX ", "COM1. .", "prn  ",
    };
    for (unsigned i = 0; i < sizeof dev / sizeof *dev; i++) {
        char p[256];
        for (unsigned form = 0; form < 4; form++) {
            static const char *const fmt[] = { "C:\\Titanfall2\\%s", "%s", "C:\\Titanfall2\\a\\%s", "C:\\Users\\Player\\%s" };
            snprintf(p, sizeof p, fmt[form], dev[i]);
            unsigned before = T_LOG_COUNT(RT_CH_FILE);
            HERR(p, CR, ERROR_PATH_NOT_FOUND);
            HERR(p, EX, ERROR_PATH_NOT_FOUND);
            T_TRUE(T_LOG_COUNT(RT_CH_FILE) == before + 2);
        }
        T_TRUE(T_LOGGED(RT_CH_FILE, "DOS device name (not provided)"));
    }

    /* Names that merely start or end like one, or are one in the middle of a path, or searched for, or
     * written for the \\?\ prefix, are ordinary. */
    static const char *const fine[] = {
        "COM0", "COM10", "LPT0", "LPT10", "CONSOLE", "CONIN", "CONOUT$$", "PRNX", "AU", "COM", "LPT", "XCON",
        "CON1", "CONIN$x", "NUL1", "NULL",
    };
    for (unsigned i = 0; i < sizeof fine / sizeof *fine; i++) {
        char p[256];
        snprintf(p, sizeof p, "C:\\Titanfall2\\%s", fine[i]);
        HOK(p, CR, I(fine[i]));
        snprintf(p, sizeof p, "%s", fine[i]);
        HOK(p, CR, I(fine[i]));
    }
    HOK("C:\\Titanfall2\\CON\\x", CR, I("CON/x"));                  /* CON is a directory here, in the middle of the path */
    HOK("C:\\Titanfall2\\CON\\", CR, slash(I("CON")));               /* a trailing separator: a directory, not the device */
    HOK("C:\\Titanfall2\\CON", WC, I("CON"));                       /* a search pattern is not a device name */
    HOK("\\\\?\\C:\\Titanfall2\\CON", CR, I("CON"));                  /* with \\?\ it is a file name like any other */
    HOK("\\\\?\\C:\\Titanfall2\\PRN", CR, I("PRN"));
    HOK("\\\\?\\C:\\Titanfall2\\a\\COM1", CR, I("a/COM1"));
}

TEST(core, path_device_names_with_extension)
{
    make_tree();
    /* "CON.txt" is the console on Windows 10 and, perhaps, a file on a later version: not established,
     * so the runtime stops and says so instead of choosing. */
    static const char *const ext[] = {
        "CON.txt", "aux.cfg", "com1.log", "lpt1.x", "NUL.txt", "nul.cfg", "Con.Tar.Gz", "CONIN$.x", "NUL.txt.", "PRN.a b",
    };
    char h[RT_PATH_MAX];
    for (unsigned i = 0; i < sizeof ext / sizeof *ext; i++) {
        char p[256], want[300];
        snprintf(p, sizeof p, "C:\\Titanfall2\\%s", ext[i]);
        snprintf(want, sizeof want, "\"%s\"", p);                  /* the report names the offending path */
        T_TRAPS(rt_path_to_host_w(W(p), CR, h), "a DOS device name with an extension as the last component");
        T_TRAPS(rt_path_to_host_w(W(p), EX, h), want);
        T_TRAPS(rt_path_to_host_a(p, CR, h), want);
        snprintf(p, sizeof p, "%s", ext[i]);
        T_TRAPS(rt_path_to_host_w(W(p), CR, h), "a DOS device name with an extension");
    }
    /* Not when it is not the last component, nor under the \\?\ prefix, nor a pattern. */
    HOK("C:\\Titanfall2\\con.txt\\", CR, slash(I("con.txt")));
    HOK("C:\\Titanfall2\\con.txt", WC, I("con.txt"));
    HOK("\\\\?\\C:\\Titanfall2\\con.txt", CR, I("con.txt"));
}

/* ---- alternate data streams, lone surrogates --------------------------------------------------------- */

TEST(core, path_colon_and_surrogates)
{
    make_tree();
    char h[RT_PATH_MAX];
    /* A colon in a name is an NTFS alternate data stream ("file.txt:stream", "file.txt::$DATA"), which
     * this runtime does not provide; Windows would accept it.  The report names the path. */
    static const char *const colon[] = {
        "C:\\Titanfall2\\a:b", "C:\\Titanfall2\\file.txt:stream", "C:\\Titanfall2\\file.txt::$DATA", "1:\\x", ":x", "C::",
        "C:\\Titanfall2\\a\\:", "\\\\?\\C:\\Titanfall2\\a:b", "C:\\Titanfall2\\a\\b:c\\d", "C:\\Titanfall2\\x:",
        "C:\\Titanfall2\\a\\..\\b:c",
    };
    for (unsigned i = 0; i < sizeof colon / sizeof *colon; i++) {
        char want[300];
        snprintf(want, sizeof want, "alternate data stream (or a bad drive prefix); streams are not provided: \"%s\"", colon[i]);
        T_TRAPS(rt_path_to_host_w(W(colon[i]), CR, h), want);
        T_TRAPS(rt_path_to_host_w(W(colon[i]), EX, h), "rt_path:");
        T_TRAPS(rt_path_to_host_a(colon[i], CR, h), want);
    }
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\a\\*:x"), WC, h), "alternate data stream");     /* in a pattern too */

    /* A UTF-16 surrogate without its partner can not be written in UTF-8.  Windows allows it in a name. */
    WCHAR b[64];
    const WCHAR *base = W("C:\\Titanfall2\\ab");
    unsigned n = 0;
    while (base[n]) { b[n] = base[n]; n++; }                       /* n == 16: b[14] and b[15] are the two letters */
    T_EQ(n, 16);
    b[16] = 0;
    b[14] = 0xD800;
    T_TRAPS(rt_path_to_host_w(b, CR, h), "unpaired UTF-16 surrogate U+D800 at index 14");          /* lone high, then an ordinary char */
    b[14] = 0xDC00;
    T_TRAPS(rt_path_to_host_w(b, CR, h), "unpaired UTF-16 surrogate U+DC00 at index 14");          /* lone low */
    b[14] = 0xDFFF;
    T_TRAPS(rt_path_to_host_w(b, CR, h), "unpaired UTF-16 surrogate U+DFFF at index 14");
    b[14] = 0xDBFF; b[15] = 0;
    T_TRAPS(rt_path_to_host_w(b, CR, h), "unpaired UTF-16 surrogate U+DBFF at index 14");          /* high at the very end */
    b[14] = 0xDE00; b[15] = 0xD83D; b[16] = 0;
    T_TRAPS(rt_path_to_host_w(b, CR, h), "unpaired UTF-16 surrogate U+DE00 at index 14");          /* low then high */
    b[14] = 0xD83D; b[15] = 0xD83D; b[16] = 0xDE00; b[17] = 0;
    T_TRAPS(rt_path_to_host_w(b, CR, h), "unpaired UTF-16 surrogate U+D83D at index 14");          /* high, high, low: the first is alone */
    b[14] = 'x'; b[15] = 0xD800; b[16] = '\\'; b[17] = '.'; b[18] = '.'; b[19] = '\\'; b[20] = 'a'; b[21] = 0;
    T_TRAPS(rt_path_to_host_w(b, CR, h), "unpaired UTF-16 surrogate U+D800 at index 15");          /* even one that ".." would remove */
    T_TRAPS(rt_path_to_host_w(b, WC, h), "U+D800");
    T_TRAPS(rt_path_to_host_w(b, EX, h), "\"C:\\Titanfall2\\x\\uD800\\..\\a\"");                    /* the report shows it */
    /* A pair is one character. */
    b[14] = 0xD83D; b[15] = 0xDE00; b[16] = 0;
    HOKW(b, CR, I("\xF0\x9F\x98\x80"));                            /* U+1F600 */
    b[14] = 0xDBFF; b[15] = 0xDFFF; b[16] = 0;
    HOKW(b, CR, I("\xF4\x8F\xBF\xBF"));                            /* U+10FFFF, the last */
    b[14] = 0xD800; b[15] = 0xDC00; b[16] = 0;
    HOKW(b, CR, I("\xF0\x90\x80\x80"));                            /* U+10000, the first */
}

/* ---- Unicode names ------------------------------------------------------------------------------------- */

TEST(core, path_unicode)
{
    make_tree();
    HOKW(U16("C:\\Titanfall2\\\u00e9.txt"), CR, I("\xC3\xA9" ".txt"));
    HOKW(U16("C:\\Titanfall2\\\u65e5\u672c"), CR, I("\xE6\x97\xA5\xE6\x9C\xAC"));
    HOKW(U16("C:\\Titanfall2\\\U0001F600"), CR, I("\xF0\x9F\x98\x80"));
    HOKW(U16("C:\\Titanfall2\\a\u00e9\\.."), CR, I(""));
    HOKW(U16("C:\\Titanfall2\\x\u00a0"), CR, I("x\xC2\xA0"));          /* only U+0020 is trimmed, not a no-break space */
    HOKW(U16("C:\\Titanfall2\\a\uff3cb"), CR, I("a\xEF\xBC\xBC" "b"));   /* U+FF3C, fullwidth backslash, is not a separator */
    HOKW(U16("C:\\Titanfall2\\a\uff0fb"), CR, I("a\xEF\xBC\x8F" "b"));   /* ... nor is U+FF0F */
    {
        WCHAR t[40];
        unsigned k = 0;
        for (const WCHAR *q = W("C:\\Titanfall2\\"); *q; q++) t[k++] = *q;
        t[k++] = 0x0080;                                            /* a C1 control character is a valid name character */
        t[k] = 0;
        HOKW(t, CR, I("\xC2\x80"));
    }
    HOKW(U16("C:\\Titanfall2\\\ufffe"), CR, I("\xEF\xBF\xBE"));
    HOKW(U16("C:\\Titanfall2\\\ufffd"), CR, I("\xEF\xBF\xBD"));
    HOKW(U16("C:\\Titanfall2\\a\x7f" "b"), CR, I("a\x7f" "b"));       /* DEL is allowed in a name */
    HOKW(U16("C:\\Titanfall2\\\u2024\u2024"), CR, I("\xE2\x80\xA4\xE2\x80\xA4"));  /* ONE DOT LEADER is not a dot */
    /* Fullwidth full stops are not dots: this is a directory with a strange name, and it is not there. */
    T_TRUE(chkw(__FILE__, __LINE__, U16("C:\\Titanfall2\\\uff0e\uff0e\\x"), CR, ERROR_PATH_NOT_FOUND, NULL));
}


/* ---- limits ------------------------------------------------------------------------------------------- */

TEST(core, path_name_lengths)
{
    make_tree();
    char name[600], p[700];

    /* A name is at most 255 UTF-16 units (NTFS) and, on this host, 255 bytes of UTF-8. */
    memset(name, 'a', 255); name[255] = 0;
    snprintf(p, sizeof p, "C:\\Titanfall2\\%s", name);
    HOK(p, CR, I(name));
    T_TRUE(chka(__FILE__, __LINE__, p, CR, 0, I(name)));
    name[255] = 'a'; name[256] = 0;
    snprintf(p, sizeof p, "C:\\Titanfall2\\%s", name);
    HERR(p, CR, ERROR_FILENAME_EXCED_RANGE);
    HERR(p, EX, ERROR_FILENAME_EXCED_RANGE);
    T_TRUE(chka(__FILE__, __LINE__, p, CR, ERROR_FILENAME_EXCED_RANGE, NULL));
    snprintf(p, sizeof p, "C:\\Titanfall2\\%s\\x", name);            /* in the middle of a path too */
    HERR(p, CR, ERROR_FILENAME_EXCED_RANGE);
    snprintf(p, sizeof p, "C:\\Titanfall2\\%s", name);
    HERR(p, WC, ERROR_FILENAME_EXCED_RANGE);                        /* and as a search pattern */
    snprintf(p, sizeof p, "\\\\?\\C:\\Titanfall2\\%s", name);        /* and under \\?\ */
    HERR(p, CR, ERROR_FILENAME_EXCED_RANGE);
    snprintf(p, sizeof p, "C:\\Titanfall2\\%s\\..\\x", name);        /* but not if ".." removes it first */
    HOK(p, CR, I("x"));

    /* Units and bytes are counted separately: 127 two-byte letters and one ASCII letter are 255 bytes, 128 letters 256. */
    WCHAR w[700];
    unsigned n = 0;
    for (const WCHAR *q = W("C:\\Titanfall2\\"); *q; q++) w[n++] = *q;
    unsigned base = n;
    char want[700];
    for (unsigned i = 0; i < 127; i++) w[n++] = 0x00e9;
    w[n++] = 'a'; w[n] = 0;
    for (unsigned i = 0; i < 127; i++) memcpy(want + 2 * i, "\xC3\xA9", 2);
    want[254] = 'a'; want[255] = 0;
    HOKW(w, CR, I(want));
    n = base;
    for (unsigned i = 0; i < 128; i++) w[n++] = 0x00e9;
    w[n] = 0;
    T_TRUE(chkw(__FILE__, __LINE__, w, CR, ERROR_FILENAME_EXCED_RANGE, NULL));
    n = base;                                                       /* 85 three-byte characters: 255 bytes; 86: 258 */
    for (unsigned i = 0; i < 85; i++) { w[n++] = 0x65e5; memcpy(want + 3 * i, "\xE6\x97\xA5", 3); }
    w[n] = 0; want[255] = 0;
    HOKW(w, CR, I(want));
    w[n++] = 0x65e5; w[n] = 0;
    T_TRUE(chkw(__FILE__, __LINE__, w, CR, ERROR_FILENAME_EXCED_RANGE, NULL));
    n = base;                                                       /* 63 pairs and one letter: 253 bytes, 127 units; 64 pairs: 256 bytes */
    for (unsigned i = 0; i < 63; i++) { w[n++] = 0xD83D; w[n++] = 0xDE00; memcpy(want + 4 * i, "\xF0\x9F\x98\x80", 4); }
    w[n++] = 'a'; w[n] = 0; want[252] = 'a'; want[253] = 0;
    HOKW(w, CR, I(want));
    n = base;
    for (unsigned i = 0; i < 64; i++) { w[n++] = 0xD83D; w[n++] = 0xDE00; }
    w[n] = 0;
    T_TRUE(chkw(__FILE__, __LINE__, w, CR, ERROR_FILENAME_EXCED_RANGE, NULL));
}

TEST(core, path_host_buffer_fit)
{
    make_tree();
    /* The host path is at most RT_PATH_MAX - 1 bytes.  Four directories of 200 letters, then a last
     * component of exactly the length that makes the host path 1023 bytes. */
    const size_t L = strlen(I(""));
    T_REQUIRE(L < 200);
    char d[4][201];
    for (int i = 0; i < 4; i++) { memset(d[i], 'p' + i, 200); d[i][200] = 0; }
    char dir[RT_PATH_MAX * 2];
    snprintf(dir, sizeof dir, "%s/%s/%s/%s/%s", I(""), d[0], d[1], d[2], d[3]);
    mkdir_p(dir);
    const size_t X = 1023 - L - 805;
    char win[1200], last[400], want[RT_PATH_MAX + 16];
    memset(last, 'z', sizeof last - 1);

    #define WINPATH(len, tail) \
        (last[len] = 0, snprintf(win, sizeof win, "C:\\Titanfall2\\%s\\%s\\%s\\%s\\%.*s%s", d[0], d[1], d[2], d[3], (int)(len), last, tail), last[len] = 'z', win)
    snprintf(want, sizeof want, "%s/%s/%s/%s/%.*s", I(d[0]), d[1], d[2], d[3], (int)X, last);
    T_EQ(strlen(want), 1023);
    HOK(WINPATH(X, ""), CR, want);                                  /* exactly 1023: fits */
    T_TRUE(chka(__FILE__, __LINE__, WINPATH(X, ""), CR, 0, want));
    HERR(WINPATH(X + 1, ""), CR, ERROR_FILENAME_EXCED_RANGE);       /* 1024: does not */
    T_TRUE(chka(__FILE__, __LINE__, WINPATH(X + 1, ""), CR, ERROR_FILENAME_EXCED_RANGE, NULL));
    {   /* fits, and the file is not there; but the host may find a path of 1023 bytes too long once it has followed a
         * symbolic link in it (/var is one on macOS), and then ENAMETOOLONG is mapped to the same error as above */
        char h[RT_PATH_MAX];
        DWORD e = rt_path_to_host_w(W(WINPATH(X, "")), EX, h);
        T_TRUE(e == ERROR_FILE_NOT_FOUND || e == ERROR_FILENAME_EXCED_RANGE);
    }
    snprintf(want, sizeof want, "%s/%s/%s/%s/%.*s/", I(d[0]), d[1], d[2], d[3], (int)(X - 1), last);
    T_EQ(strlen(want), 1023);
    HOK(WINPATH(X - 1, "\\"), CR, want);                            /* the trailing slash counts */
    HERR(WINPATH(X, "\\"), CR, ERROR_FILENAME_EXCED_RANGE);
    snprintf(want, sizeof want, "%s/%s/%s/%s/%.*s", I(d[0]), d[1], d[2], d[3], (int)X, last);
    HOK(WINPATH(X, ""), WC, want);                                  /* a search pattern too */
    HERR(WINPATH(X + 1, ""), WC, ERROR_FILENAME_EXCED_RANGE);
    #undef WINPATH
    /* Beyond the first three directories the path is past the limit however short the last name. */
    snprintf(win, sizeof win, "C:\\Titanfall2\\%s\\%s\\%s\\%s\\%s", d[0], d[1], d[2], d[3], d[0]);
    HERR(win, CR, ERROR_FILENAME_EXCED_RANGE);
}

TEST(core, path_input_length)
{
    make_tree();
    /* The longest path Windows takes is 32767 UTF-16 units.  "a\..\" repeated is the root again each time:
     * 14 characters of prefix, 6550 groups of 5, and a tail of 3 (32767) or 4 (32768). */
    const size_t groups = 6550;
    WCHAR *w = malloc((14 + 5 * groups + 8) * sizeof *w);
    char *a = malloc(14 + 5 * groups + 8);
    T_REQUIRE(w && a);
    size_t n = 0;
    for (const char *q = "C:\\Titanfall2\\"; *q; q++) { a[n] = *q; w[n] = (unsigned char)*q; n++; }
    for (size_t g = 0; g < groups; g++)
        for (const char *q = "a\\..\\"; *q; q++) { a[n] = *q; w[n] = (unsigned char)*q; n++; }
    size_t body = n;
    for (const char *q = "x.y"; *q; q++) { a[n] = *q; w[n] = (unsigned char)*q; n++; }
    a[n] = 0; w[n] = 0;
    T_EQ(n, 32767);
    HOKW(w, CR, I("x.y"));
    T_TRUE(chka(__FILE__, __LINE__, a, CR, 0, I("x.y")));
    n = body;
    for (const char *q = "x.yz"; *q; q++) { a[n] = *q; w[n] = (unsigned char)*q; n++; }
    a[n] = 0; w[n] = 0;
    T_EQ(n, 32768);
    T_TRUE(chkw(__FILE__, __LINE__, w, CR, ERROR_FILENAME_EXCED_RANGE, NULL));
    T_TRUE(chka(__FILE__, __LINE__, a, CR, ERROR_FILENAME_EXCED_RANGE, NULL));
    free(w);
    free(a);

    /* Far more than that: still just an error, and no read beyond the first 32768 units. */
    size_t big = 100000;
    w = malloc((big + 1) * sizeof *w);
    T_REQUIRE(w != NULL);
    for (size_t i = 0; i < big; i++) w[i] = 'a';
    w[big] = 0;
    T_TRUE(chkw(__FILE__, __LINE__, w, CR, ERROR_FILENAME_EXCED_RANGE, NULL));
    free(w);

    /* Many components: too long for the host buffer, a clean error; and many ".." that cancel out or cannot go further. */
    a = malloc(70000);
    T_REQUIRE(a != NULL);
    n = 0;
    for (int i = 0; i < 5000; i++) { a[n++] = 'a'; a[n++] = '\\'; }
    a[n] = 0;
    T_TRUE(chka(__FILE__, __LINE__, a, CR, ERROR_FILENAME_EXCED_RANGE, NULL));
    n = 0;
    for (int i = 0; i < 10000; i++) { memcpy(a + n, "..\\", 3); n += 3; }
    memcpy(a + n, "Titanfall2\\a", 13);
    T_TRUE(chka(__FILE__, __LINE__, a, CR, 0, I("a")));              /* 30000 characters, 10000 ".." (beyond the root they do nothing) */
    free(a);
}

/* ---- the \\?\ prefix ------------------------------------------------------------------------------------ */

TEST(core, path_raw_prefix)
{
    make_tree();
    /* Under \\?\C:\ the path is not normalised: no "." or "..", no "/", no trailing-dot trimming. */
    static const struct { const char *win; uint32_t flags; const char *rel; } ok[] = {
        { "\\\\?\\C:\\Titanfall2",                CR, "" },
        { "\\\\?\\c:\\TITANFALL2\\a",             CR, "a" },
        { "\\\\?\\C:\\Titanfall2\\a\\b",          CR, "a/b" },
        { "\\\\?\\C:\\Titanfall2\\a\\b\\c",       CR, "a/b/c" },
        { "\\\\?\\C:\\Titanfall2\\",              CR, "" },        /* with the slash added below */
        { "\\\\?\\C:\\Titanfall2\\a\\b\\",        CR, "a/b" },
        { "\\\\?\\C:\\Titanfall2\\a.",            CR, "a." },      /* trailing dots and spaces are part of the name */
        { "\\\\?\\C:\\Titanfall2\\x ",            CR, "x " },
        { "\\\\?\\C:\\Titanfall2\\x. .",          CR, "x. ." },
        { "\\\\?\\C:\\Titanfall2\\a..\\y",        CR, "a../y" },
        { "\\\\?\\C:\\Titanfall2\\d \\y",         CR, "d /y" },
        { "\\\\?\\C:\\Titanfall2\\...\\y",        CR, ".../y" },
        { "\\\\?\\C:\\Titanfall2\\CON",           CR, "CON" },
        { "\\\\?\\C:\\Titanfall2\\a\\*.txt",      WC, "a/*.txt" },
        { "\\\\?\\C:\\Titanfall2\\a\\x.",         WC, "a/x." },
    };
    for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++) {
        char want[RT_PATH_MAX];
        snprintf(want, sizeof want, "%s%s", I(ok[i].rel), ok[i].win[strlen(ok[i].win) - 1] == '\\' ? "/" : "");
        HOK(ok[i].win, ok[i].flags, want);
    }
    HOK("\\\\?\\C:\\", EX, slash(S("vfs")));                       /* the prefix does not change what exists */
    HOK("\\\\?\\C:\\Users\\Player", EX, S("home"));
    HOK("\\\\?\\C:\\Windows\\system32", EX, S("vfs/Windows/system32"));
    HERR("\\\\?\\C:\\foo", CR, ERROR_ACCESS_DENIED);
    HERR("\\\\?\\C:\\foo", EX, ERROR_FILE_NOT_FOUND);
    HERR("\\\\?\\C:\\nodir\\x", CR, ERROR_PATH_NOT_FOUND);

    /* What NTFS does not take: "." and ".." (as ERROR_INVALID_NAME: UNSURE whether Windows says that or
     * ERROR_PATH_NOT_FOUND), an empty component, "/" in a name, characters that are not allowed. */
    static const char *const invalid[] = {
        "\\\\?\\C:\\Titanfall2\\a\\..\\b", "\\\\?\\C:\\Titanfall2\\.", "\\\\?\\C:\\Titanfall2\\..", "\\\\?\\C:\\..\\x",
        "\\\\?\\C:\\Titanfall2\\a\\.\\b", "\\\\?\\C:\\Titanfall2\\\\a", "\\\\?\\C:\\\\x", "\\\\?\\C:\\Titanfall2\\a\\\\",
        "\\\\?\\C:\\Titanfall2\\a/b", "\\\\?\\C:\\Titanfall2/a", "\\\\?\\C:\\Titanfall2\\a*", "\\\\?\\C:\\Titanfall2\\a?",
        "\\\\?\\C:\\Titanfall2\\a<", "\\\\?\\C:\\Titanfall2\\a>", "\\\\?\\C:\\Titanfall2\\a\"", "\\\\?\\C:\\Titanfall2\\a|",
        "\\\\?\\C:\\Titanfall2\\a\001", "\\\\?\\C:\\Titanfall2\\a\037b", "\\\\?\\C:\\Titanfall2\\..\\..\\..\\etc\\passwd",
        "\\\\?\\C:\\Titanfall2\\a\\b\\..\\..\\..\\..\\x",
    };
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        HERR(invalid[i], CR, ERROR_INVALID_NAME);
        HERR(invalid[i], EX, ERROR_INVALID_NAME);
        T_TRUE(chka(__FILE__, __LINE__, invalid[i], CR, ERROR_INVALID_NAME, NULL));
    }
    char h[RT_PATH_MAX];
    HERR("\\\\?\\C:\\Titanfall2\\a\\..", WC, ERROR_INVALID_NAME);   /* a search for ".." is just invalid here */
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\a\\.."), WC, h), "a search whose last component is \"..\"");
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\a\\."), WC, h), "a search whose last component is \".\"");
    T_TRAPS(rt_path_to_host_w(W("."), WC, h), "a search whose last component is \".\"");
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\a:b"), CR, h), "alternate data stream");
}

/* ---- names Windows forbids, in an ordinary path ---------------------------------------------------------- */

TEST(core, path_invalid_names)
{
    make_tree();
    static const char *const bad[] = {
        "C:\\Titanfall2\\a<b", "C:\\Titanfall2\\a>b", "C:\\Titanfall2\\a\"b", "C:\\Titanfall2\\a|b", "C:\\Titanfall2\\a*b",
        "C:\\Titanfall2\\a?b", "C:\\Titanfall2\\a\001b", "C:\\Titanfall2\\a\037b", "C:\\Titanfall2\\a\tb", "C:\\Titanfall2\\a\nb",
        "C:\\Titanfall2\\a\rb", "C:\\Titanfall2\\a\033", "C:\\Titanfall2\\<", "C:\\Titanfall2\\*", "C:\\Titanfall2\\?\\x",
        "C:\\Titanfall2\\a\\b|c\\d", "a<b", "x\001", "C:\\Titanfall2\\a\\\"",
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        HERR(bad[i], CR, ERROR_INVALID_NAME);
        HERR(bad[i], EX, ERROR_INVALID_NAME);
        HERR(bad[i], DI, ERROR_INVALID_NAME);
        T_TRUE(chka(__FILE__, __LINE__, bad[i], CR, ERROR_INVALID_NAME, NULL));
    }
    /* The same characters are fine where they are allowed: space, the other punctuation, DEL, high bytes. */
    HOK("C:\\Titanfall2\\a b", CR, I("a b"));
    HOK("C:\\Titanfall2\\a,b;c=d[e]{f}'g`h~i!j@k#l$m%n^o&p(q)r+s-t_u", CR, I("a,b;c=d[e]{f}'g`h~i!j@k#l$m%n^o&p(q)r+s-t_u"));
    HOK("C:\\Titanfall2\\a\177b", CR, I("a\177b"));
    HOK("C:\\Titanfall2\\.hidden", CR, I(".hidden"));
    HOK("C:\\Titanfall2\\..hidden", CR, I("..hidden"));
    HOK("C:\\Titanfall2\\a.b.c", CR, I("a.b.c"));
    HOK("C:\\Titanfall2\\ ..x", CR, I(" ..x"));
    HOK("C:\\Titanfall2\\x..y", CR, I("x..y"));
    HOK("C:\\Titanfall2\\....x", CR, I("....x"));
}
