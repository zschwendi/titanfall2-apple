/* The guest's current directory (rt_path_get_cwd_w, rt_path_set_cwd_w): runtime state of its own,
 * never the host's.  Relative paths are translated against it.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/path.h"

void rt_path_test_force_case(int mode);
int  rt_path_test_probe(const char *win_root);

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
static void touch(const char *abs) {
    int fd = open(abs, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) { rt_test_fail(__FILE__, __LINE__, "create %s: %s", abs, strerror(errno)); return; }
    close(fd);
}

static bool chk(const char *file, int line, const char *win, uint32_t flags, DWORD want_err, const char *want_host) {
    char host[RT_PATH_MAX];
    memset(host, 'X', sizeof host);
    DWORD e = rt_path_to_host_w(W(win), flags, host);
    if (e != want_err) {
        rt_test_fail(file, line, "\"%s\" flags %#x: error %u, wanted %u (host buffer \"%.80s\")", win, flags, (unsigned)e, (unsigned)want_err, host);
        return false;
    }
    if (e) return host[0] == 0;
    if (strcmp(host, want_host)) {
        rt_test_fail(file, line, "\"%s\" flags %#x: host path\n    %s\n  wanted\n    %s", win, flags, host, want_host);
        return false;
    }
    return true;
}
#define HOK(win, flags, want)  T_TRUE(chk(__FILE__, __LINE__, (win), (flags), 0, (want)))
#define HERR(win, flags, err)  T_TRUE(chk(__FILE__, __LINE__, (win), (flags), (err), NULL))

#define EX   RT_PATH_EXISTING
#define CR   RT_PATH_CREATE
#define DI   RT_PATH_DIR
#define WC   RT_PATH_WILDCARD

/* The current directory as ASCII, or "?" if it is not what a read of it gives. */
static const char *cwd(void) {
    static char buf[4][1024];
    static unsigned k;
    char *b = buf[k++ & 3];
    WCHAR w[1024];
    DWORD n = rt_path_get_cwd_w(w, 1024);
    if (n == 0 || n >= 1024) return "?";
    for (DWORD i = 0; i <= n; i++) b[i] = w[i] < 128 ? (char)w[i] : '?';
    if (w[n] != 0) return "?";
    return b;
}
#define T_CWD(want)  T_TRUE(!strcmp(cwd(), (want)))

static void tree(void)
{
    rt_path_test_force_case(0);
    mkdir_p(I("a/b"));
    touch(I("f.txt"));
}

/* ---- get ----------------------------------------------------------------------------------------------- */

TEST(core, path_cwd_get_sizes)
{
    /* GetCurrentDirectoryW: the length without the terminator when the buffer holds the terminator too;
     * otherwise the size needed with it.  "C:\Titanfall2" is 13 units. */
    WCHAR w[64];
    for (int i = 0; i < 64; i++) w[i] = 0x5555;
    T_EQ(rt_path_get_cwd_w(w, 14), 13);
    T_TRUE(!memcmp(w, W("C:\\Titanfall2"), 14 * sizeof(WCHAR)));
    T_EQ(w[14], 0x5555);                                            /* nothing past the terminator */
    for (int i = 0; i < 64; i++) w[i] = 0x5555;
    T_EQ(rt_path_get_cwd_w(w, 64), 13);
    T_EQ(w[13], 0);
    T_EQ(w[14], 0x5555);

    for (int i = 0; i < 64; i++) w[i] = 0x5555;
    T_EQ(rt_path_get_cwd_w(w, 13), 14);                              /* one too small: the size with the terminator */
    T_EQ(w[0], 0);                                                  /* ... and the buffer holds an empty string, nothing else */
    T_EQ(w[1], 0x5555);
    for (int i = 0; i < 64; i++) w[i] = 0x5555;
    T_EQ(rt_path_get_cwd_w(w, 1), 14);
    T_EQ(w[0], 0);
    T_EQ(w[1], 0x5555);
    for (int i = 0; i < 64; i++) w[i] = 0x5555;
    T_EQ(rt_path_get_cwd_w(w, 0), 14);                              /* nothing at all is written for size 0 */
    T_EQ(w[0], 0x5555);
    T_EQ(rt_path_get_cwd_w(NULL, 0), 14);                           /* the usual size query */
    T_EQ(rt_path_get_cwd_w(w, 0xFFFFFFFFu), 13);                     /* a huge size is just a big buffer; only 14 units are written */
    T_EQ(w[14], 0x5555);

    /* A buffer that does not exist for a size that is not 0 is a bug of the caller. */
    T_TRAPS(rt_path_get_cwd_w(NULL, 5), "rt_path_get_cwd_w: no buffer for 5 units");

    /* The root is "C:\" with its backslash, the one directory that ends in one. */
    T_EQ(rt_path_set_cwd_w(W("C:\\")), 0);
    T_EQ(rt_path_get_cwd_w(NULL, 0), 4);
    T_EQ(rt_path_get_cwd_w(w, 4), 3);
    T_TRUE(!memcmp(w, W("C:\\"), 4 * sizeof(WCHAR)));
    T_EQ(rt_path_get_cwd_w(w, 3), 4);
}

/* ---- set ----------------------------------------------------------------------------------------------- */

TEST(core, path_cwd_set)
{
    tree();
    rt_path_test_force_case(-1);                   /* "A" for the directory "a": the host finds it itself, or the listing does */
    char before[1024];
    T_TRUE(getcwd(before, sizeof before) != NULL);

    T_CWD("C:\\Titanfall2");
    /* relative to the current directory */
    T_EQ(rt_path_set_cwd_w(W("a")), 0);
    T_CWD("C:\\Titanfall2\\a");
    T_EQ(rt_path_set_cwd_w(W("b")), 0);
    T_CWD("C:\\Titanfall2\\a\\b");
    T_EQ(rt_path_set_cwd_w(W("..")), 0);
    T_CWD("C:\\Titanfall2\\a");
    T_EQ(rt_path_set_cwd_w(W(".")), 0);
    T_CWD("C:\\Titanfall2\\a");
    T_EQ(rt_path_set_cwd_w(W("b\\..\\..")), 0);
    T_CWD("C:\\Titanfall2");
    T_EQ(rt_path_set_cwd_w(W("..")), 0);                              /* up to the virtual root */
    T_CWD("C:\\");
    T_EQ(rt_path_set_cwd_w(W("..")), 0);                              /* ".." at the root stays there */
    T_CWD("C:\\");
    T_EQ(rt_path_set_cwd_w(W("..\\..\\.")), 0);
    T_CWD("C:\\");
    T_EQ(rt_path_set_cwd_w(W("Titanfall2\\a")), 0);
    T_CWD("C:\\Titanfall2\\a");

    /* absolute, rooted, with either slash, with a trailing one, with the case the guest used (the drive letter is
     * upper case: UNSURE for Windows, which keeps what the guest wrote after normalising) */
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2\\")), 0);
    T_CWD("C:\\Titanfall2");
    T_EQ(rt_path_set_cwd_w(W("c:/TITANFALL2/A/")), 0);
    T_CWD("C:\\TITANFALL2\\A");
    T_EQ(rt_path_set_cwd_w(W("\\Titanfall2\\a\\b")), 0);
    T_CWD("C:\\Titanfall2\\a\\b");
    T_EQ(rt_path_set_cwd_w(W("/")), 0);
    T_CWD("C:\\");
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2\\a\\.\\b\\..\\")), 0);
    T_CWD("C:\\Titanfall2\\a");
    T_EQ(rt_path_set_cwd_w(W("C:")), 0);                              /* "C:" is the current directory of drive C */
    T_CWD("C:\\Titanfall2\\a");
    T_EQ(rt_path_set_cwd_w(W("C:b")), 0);
    T_CWD("C:\\Titanfall2\\a\\b");
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2\\a. ")), 0);              /* "a. " is trimmed like any last component ... */
    T_CWD("C:\\Titanfall2\\a");
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2\\a. \\")), ERROR_PATH_NOT_FOUND);   /* ... but is a name of its own when a separator follows it */
    T_CWD("C:\\Titanfall2\\a");
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2\\a.\\b")), 0);           /* ... and "a." in the middle loses its period */
    T_CWD("C:\\Titanfall2\\a\\b");

    /* the virtual directories and the other mounts */
    T_EQ(rt_path_set_cwd_w(W("C:\\Users")), 0);
    T_CWD("C:\\Users");
    T_EQ(rt_path_set_cwd_w(W("Player\\AppData\\Local\\Temp")), 0);
    T_CWD("C:\\Users\\Player\\AppData\\Local\\Temp");
    T_EQ(rt_path_set_cwd_w(W("C:\\Windows\\system32\\")), 0);
    T_CWD("C:\\Windows\\system32");
    T_EQ(rt_path_set_cwd_w(W("..\\..\\Users\\Player\\Documents")), 0);
    T_CWD("C:\\Users\\Player\\Documents");

    /* the host's own current directory was not touched by any of it */
    char after[1024];
    T_TRUE(getcwd(after, sizeof after) != NULL);
    T_TRUE(!strcmp(before, after));
}

TEST(core, path_cwd_set_fails)
{
    tree();
    T_EQ(rt_path_set_cwd_w(W("a")), 0);
    T_CWD("C:\\Titanfall2\\a");
    /* Each failure leaves the directory where it was. */
    T_EQ(rt_path_set_cwd_w(W("nodir")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("b\\nodir")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2\\nodir\\deeper")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("C:\\foo")), ERROR_PATH_NOT_FOUND);       /* not in the virtual root */
    T_EQ(rt_path_set_cwd_w(W("C:\\Windows\\Fonts")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("..\\f.txt")), ERROR_DIRECTORY);         /* a file: "The directory name is invalid" */
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2\\f.txt\\")), ERROR_DIRECTORY);
    T_EQ(rt_path_set_cwd_w(W("..\\f.txt\\x")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(NULL), ERROR_INVALID_PARAMETER);
    T_EQ(rt_path_set_cwd_w(W("D:\\")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("\\\\server\\share")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("\\\\.\\C:")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("a*")), ERROR_INVALID_NAME);
    T_EQ(rt_path_set_cwd_w(W("a?")), ERROR_INVALID_NAME);
    T_EQ(rt_path_set_cwd_w(W("NUL")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("\\\\.\\NUL")), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("CON")), ERROR_PATH_NOT_FOUND);
    char deep[1300];
    memset(deep, 0, sizeof deep);
    for (int i = 0; i < 600; i++) strcat(deep, "a\\");
    T_EQ(rt_path_set_cwd_w(W(deep)), ERROR_FILENAME_EXCED_RANGE);
    T_CWD("C:\\Titanfall2\\a");

    /* A directory that was there and is gone. */
    mkdir_p(I("gone"));
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2\\gone")), 0);
    T_EQ(rmdir(I("gone")), 0);
    HERR("x", CR, ERROR_PATH_NOT_FOUND);                              /* its directory is not there: the same answer as for any missing directory */
    HERR("x", EX, ERROR_PATH_NOT_FOUND);
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2")), 0);                    /* and the guest can leave it */
    HOK("x", CR, I("x"));
}

TEST(core, path_cwd_traps)
{
    tree();
    /* Cases Windows accepts and this runtime does not stop with a report that names the value; the directory stays. */
    T_TRAPS(rt_path_set_cwd_w(W("\\\\?\\C:\\Titanfall2\\a")), "a \\\\?\\ path as the current directory");
    T_TRAPS(rt_path_set_cwd_w(W("\\\\?\\C:\\Titanfall2\\a")), "\"\\\\?\\C:\\Titanfall2\\a\"");
    T_TRAPS(rt_path_set_cwd_w(W("a\\b:c")), "alternate data stream");
    T_TRAPS(rt_path_set_cwd_w(W("CON.txt")), "a DOS device name with an extension");
    const WCHAR bad[] = { 'a', 0xD800, 0 };
    T_TRAPS(rt_path_set_cwd_w(bad), "unpaired UTF-16 surrogate U+D800 at index 1");
    T_CWD("C:\\Titanfall2");
}

/* ---- relative paths follow it ---------------------------------------------------------------------------- */

TEST(core, path_cwd_translation)
{
    tree();
    rt_path_test_force_case(-1);
    T_EQ(rt_path_set_cwd_w(W("a")), 0);
    HOK("x", CR, I("a/x"));
    HOK("b\\y", CR, I("a/b/y"));
    HOK(".\\x", CR, I("a/x"));
    HOK("..\\x", CR, I("x"));
    HOK("C:x", CR, I("a/x"));                                       /* "C:x" is relative to the current directory of C: */
    HOK("C:", EX, I("a"));
    HOK("C:.", EX, I("a"));
    HOK("C:..", EX, I(""));
    HOK("*.dll", WC, I("a/*.dll"));
    HOK("b\\*", WC, I("a/b/*"));
    HERR("..\\..\\x", CR, ERROR_ACCESS_DENIED);                      /* that is C:\x */
    HERR("nodir\\x", CR, ERROR_PATH_NOT_FOUND);
    HOK("NUL", CR, "/dev/null");
    /* An absolute or rooted path does not care where the current directory is. */
    HOK("C:\\Titanfall2\\x", CR, I("x"));
    HOK("\\Titanfall2\\x", CR, I("x"));
    HOK("/Titanfall2/a/b", CR, I("a/b"));

    /* In the virtual root. */
    T_EQ(rt_path_set_cwd_w(W("C:\\")), 0);
    HERR("x", CR, ERROR_ACCESS_DENIED);
    HERR("x", EX, ERROR_FILE_NOT_FOUND);
    HOK("Titanfall2\\x", CR, I("x"));
    HOK("Users\\Player\\Documents", EX, S("home/Documents"));
    HOK("..\\Titanfall2", EX, I(""));
    HOK("C:Windows", EX, S("vfs/Windows"));
    HOK("*", WC, S("vfs/*"));

    /* In the temp mount, which is <state>/tmp although it is written below the home directory. */
    T_EQ(rt_path_set_cwd_w(W("C:\\Users\\Player\\AppData\\Local\\Temp")), 0);
    HOK("f.tmp", CR, S("tmp/f.tmp"));
    HOK("..\\x", CR, S("home/AppData/Local/x"));
    HOK("..\\Temp\\y", CR, S("tmp/y"));
    HOK("..\\..\\Roaming", EX, S("home/AppData/Roaming"));

    /* Spelled in another case, it still names the same place (names are matched without regard to case). */
    T_EQ(rt_path_set_cwd_w(W("c:\\TITANFALL2\\A")), 0);
    /* a case-insensitive volume sees the spelling the guest used; a case-sensitive one is given the name on disk */
    HOK("x", CR, rt_path_test_probe("C:\\Titanfall2") ? I("a/x") : I("A/x"));
    T_EQ(rt_path_set_cwd_w(W("C:\\titanfall2")), 0);
    HOK("a\\b", EX, I("a/b"));
}

TEST(core, path_cwd_after_init)
{
    tree();
    T_EQ(rt_path_set_cwd_w(W("C:\\Users")), 0);
    T_CWD("C:\\Users");
    rt_path_init();
    T_CWD("C:\\Titanfall2");                                        /* the start-up directory again */
}
