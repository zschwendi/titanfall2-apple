/* The virtual C: drive (rt/path.h; docs/RUNTIME.md 7.1): start-up, the mapping table, the flags, the
 * virtual directories, NUL, and the text-only full path (rt_path_full_w).  Further files: path_syntax.c (what a path may look like), path_escape.c
 * (the guest can not leave the roots), path_case.c (case-sensitive volumes and the listing cache),
 * path_cwd.c, path_reverse.c, path_cp1252.c, path_threads.c.
 *
 * Every test starts from scratch (rt_init) with a temporary install root and state directory that
 * the runner makes and removes.  Nothing here touches any other directory.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/path.h"

/* Test hooks of runtime/core/path.c (not in rt/path.h). */
void rt_path_test_force_case(int mode);
int  rt_path_test_probe(const char *win_root);

/* ---- helpers (repeated in each path_*.c file: a test file is self-contained) ---------------------- */

static const WCHAR *W(const char *a) {            /* ASCII to UTF-16, in one of four rotating buffers */
    static WCHAR buf[4][4096];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    size_t i = 0;
    for (; a[i]; i++) b[i] = (unsigned char)a[i];
    b[i] = 0;
    return b;
}

static const char *join(const char *base, const char *rel) {   /* base + "/" + rel, or base if rel is "" */
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
static const char *slash(const char *p) {          /* p + "/" */
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
static void touch(const char *abs) {
    int fd = open(abs, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) { rt_test_fail(__FILE__, __LINE__, "create %s: %s", abs, strerror(errno)); return; }
    close(fd);
}

/* Translate and compare.  want_err 0 means success with host path want_host. */
static bool chk(const char *file, int line, const WCHAR *win, uint32_t flags, DWORD want_err, const char *want_host) {
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
#define HOK(win, flags, want)  T_TRUE(chk(__FILE__, __LINE__, W(win), (flags), 0, (want)))
#define HERR(win, flags, err)  T_TRUE(chk(__FILE__, __LINE__, W(win), (flags), (err), NULL))

static int list_dir(const char *dir, char names[][64], int max) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (n < max) snprintf(names[n], 64, "%s", e->d_name);
        n++;
    }
    closedir(d);
    for (int i = 0; i < n && i < max; i++)     /* sort, for a stable comparison */
        for (int j = i + 1; j < n && j < max; j++)
            if (strcmp(names[j], names[i]) < 0) { char t[64]; strcpy(t, names[i]); strcpy(names[i], names[j]); strcpy(names[j], t); }
    return n;
}
static bool is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

#define EX   RT_PATH_EXISTING
#define CR   RT_PATH_CREATE
#define DI   RT_PATH_DIR
#define WC   RT_PATH_WILDCARD

/* ---- start-up --------------------------------------------------------------------------------------- */

TEST(core, path_init_tree)
{
    /* rt_path_init (called by rt_init) makes the tree the guest expects inside the state directory
     * (section 7.1) and nothing else. */
    T_TRUE(is_dir(S("home")));
    T_TRUE(is_dir(S("home/Documents")));
    T_TRUE(is_dir(S("home/AppData/Local")));
    T_TRUE(is_dir(S("home/AppData/Roaming")));
    T_TRUE(is_dir(S("home/Saved Games")));
    T_TRUE(is_dir(S("tmp")));
    T_TRUE(is_dir(S("home/AppData/Local/Temp")));   /* placeholder: ...\AppData\Local lists Temp; the real one is <state>/tmp */

    /* The virtual directories are real, empty host directories holding only placeholders for the
     * mounts, so that a shim can enumerate C:\ like any other directory. */
    char n[8][64];
    T_EQ(list_dir(S("vfs"), n, 8), 3);
    T_TRUE(!strcmp(n[0], "Titanfall2") && !strcmp(n[1], "Users") && !strcmp(n[2], "Windows"));
    T_EQ(list_dir(S("vfs/Users"), n, 8), 1);
    T_TRUE(!strcmp(n[0], "Player"));
    T_EQ(list_dir(S("vfs/Windows"), n, 8), 1);
    T_TRUE(!strcmp(n[0], "system32"));
    T_EQ(list_dir(S("vfs/Windows/system32"), n, 8), 0);
    T_EQ(list_dir(S("vfs/Titanfall2"), n, 8), 0);
    T_EQ(list_dir(S("vfs/Users/Player"), n, 8), 0);
    T_EQ(list_dir(I(""), n, 8), 0);                              /* the install root is not touched */

    /* The guest's current directory starts at C:\Titanfall2. */
    WCHAR cwd[64];
    T_EQ(rt_path_get_cwd_w(cwd, 64), 13);
    T_TRUE(!memcmp(cwd, W("C:\\Titanfall2"), 14 * sizeof(WCHAR)));

    /* Each mount was asked about its volume; the answer is the volume's own. */
    T_EQ(rt_path_test_probe("C:\\Titanfall2"), pathconf(rt_test_install_root(), _PC_CASE_SENSITIVE) != 0);
    T_EQ(rt_path_test_probe("C:\\Users\\Player"), pathconf(S("home"), _PC_CASE_SENSITIVE) != 0);
    T_EQ(rt_path_test_probe("C:\\Users\\Player\\AppData\\Local\\Temp"), pathconf(S("tmp"), _PC_CASE_SENSITIVE) != 0);
    T_EQ(rt_path_test_probe("C:\\Windows"), -1);                 /* a virtual directory has no volume of its own */

    /* Calling the start-up function again changes nothing that matters and breaks nothing. */
    rt_path_set_cwd_w(W("C:\\"));
    rt_path_init();
    T_EQ(rt_path_get_cwd_w(cwd, 64), 13);                        /* the current directory is back at the start */
    HOK("C:\\Users\\Player\\AppData\\Local\\Temp", EX, S("tmp"));
    T_EQ(list_dir(S("vfs"), n, 8), 3);
}

/* ---- the mapping table (7.1) ------------------------------------------------------------------------- */

TEST(core, path_mapping)
{
    rt_path_test_force_case(0);                  /* names pass through, whatever this volume is */
    mkdir_p(I("r2"));
    mkdir_p(S("tmp/sub"));

    /* C:\Titanfall2 is the install root. */
    HOK("C:\\Titanfall2", EX, I(""));
    HOK("C:\\Titanfall2\\r2", EX | DI, I("r2"));
    HOK("C:\\Titanfall2\\r2\\new.txt", CR, I("r2/new.txt"));

    /* C:\Users\Player is <state>/home. */
    HOK("C:\\Users\\Player", EX, S("home"));
    HOK("C:\\Users\\Player\\Documents\\save.dat", CR, S("home/Documents/save.dat"));
    HOK("C:\\Users\\Player\\AppData\\Local", EX, S("home/AppData/Local"));
    HOK("C:\\Users\\Player\\AppData\\Roaming\\cfg.ini", CR, S("home/AppData/Roaming/cfg.ini"));
    HOK("C:\\Users\\Player\\Saved Games", EX, S("home/Saved Games"));

    /* The temp directory is <state>/tmp, not <state>/home/AppData/Local/Temp, although it is
     * written below the home directory; a sibling of Temp is in the home tree. */
    HOK("C:\\Users\\Player\\AppData\\Local\\Temp", EX, S("tmp"));
    HOK("C:\\Users\\Player\\AppData\\Local\\Temp\\sub", EX | DI, S("tmp/sub"));
    HOK("C:\\Users\\Player\\AppData\\Local\\Temp\\sub\\a.tmp", CR, S("tmp/sub/a.tmp"));
    HOK("C:\\Users\\Player\\AppData\\Local\\Temp\\..\\other", CR, S("home/AppData/Local/other"));
    HOK("C:\\Users\\Player\\AppData\\Local\\Temp\\..\\Temp", EX, S("tmp"));

    /* Names of the table are matched without regard to case (ASCII); the host spelling is the
     * table's, whatever the guest wrote. */
    HOK("c:\\TITANFALL2", EX, I(""));
    HOK("c:\\titanfall2\\r2", EX, I("r2"));
    HOK("C:\\USERS\\player\\APPDATA\\local\\TEMP\\sub", EX, S("tmp/sub"));
    HOK("C:\\users\\PLAYER", EX, S("home"));

    /* Prefixes that are not the table's names are not mapped: they are under C:\ and do not exist. */
    HERR("C:\\Titanfall", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall22", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2x\\f", CR, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Users\\Players", EX, ERROR_FILE_NOT_FOUND);
    HOK("C:\\Users\\Player\\AppData\\Local\\Tempx", CR, S("home/AppData/Local/Tempx"));
}

/* ---- the flags (rt/path.h) --------------------------------------------------------------------------- */

TEST(core, path_flags)
{
    rt_path_test_force_case(0);
    touch(I("f.txt"));
    mkdir_p(I("d"));
    touch(I("d/inner.txt"));

    /* RT_PATH_EXISTING: the last component must exist. */
    HOK("C:\\Titanfall2\\f.txt", EX, I("f.txt"));
    HOK("C:\\Titanfall2\\d", EX | DI, I("d"));
    HOK("C:\\Titanfall2\\d\\inner.txt", EX, I("d/inner.txt"));
    HERR("C:\\Titanfall2\\missing.txt", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\d\\missing.txt", EX, ERROR_FILE_NOT_FOUND);
    /* ... and with RT_PATH_DIR a missing last component is a missing path. */
    HERR("C:\\Titanfall2\\missing", EX | DI, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\d\\missing", EX | DI, ERROR_PATH_NOT_FOUND);
    /* A missing directory in the middle is ERROR_PATH_NOT_FOUND, with either flag, however deep. */
    HERR("C:\\Titanfall2\\nodir\\x.txt", EX, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\nodir\\x.txt", CR, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\nodir\\deeper\\x.txt", EX, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\d\\nodir\\deeper\\x.txt", CR, ERROR_PATH_NOT_FOUND);
    /* A file where a directory is needed is a missing path too. */
    HERR("C:\\Titanfall2\\f.txt\\x", EX, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\f.txt\\x", CR, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\f.txt\\x\\y", EX, ERROR_PATH_NOT_FOUND);

    /* RT_PATH_CREATE: the last component may exist or be new; the directory it is in must exist. */
    HOK("C:\\Titanfall2\\f.txt", CR, I("f.txt"));
    HOK("C:\\Titanfall2\\new.txt", CR, I("new.txt"));
    HOK("C:\\Titanfall2\\d\\new.txt", CR, I("d/new.txt"));
    HOK("C:\\Titanfall2\\d\\newdir", CR | DI, I("d/newdir"));
    HOK("C:\\Titanfall2\\d", CR | DI, I("d"));
    /* RT_PATH_DIR alone, and no flag at all, behave as RT_PATH_CREATE (documented in path.c). */
    HOK("C:\\Titanfall2\\d\\new", DI, I("d/new"));
    HOK("C:\\Titanfall2\\d\\new", 0, I("d/new"));
    HOK("C:\\Titanfall2\\d\\inner.txt", 0, I("d/inner.txt"));
    HERR("C:\\Titanfall2\\nodir\\new", 0, ERROR_PATH_NOT_FOUND);

    /* The translation checks; it does not create anything. */
    struct stat st;
    T_TRUE(stat(I("new.txt"), &st) != 0 && stat(I("d/newdir"), &st) != 0 && stat(I("d/new"), &st) != 0);

    /* A trailing separator is kept as a trailing slash, so that the host call insists on a directory. */
    HOK("C:\\Titanfall2\\d\\", EX, slash(I("d")));
    HOK("C:\\Titanfall2\\", EX, slash(I("")));
    HOK("C:\\Titanfall2\\d/", CR | DI, slash(I("d")));
    HOK("C:\\Titanfall2\\f.txt\\", EX, slash(I("f.txt")));          /* the host call fails on it (ENOTDIR) */
    HERR("C:\\Titanfall2\\missing\\", EX, ERROR_FILE_NOT_FOUND);   /* UNSURE which Windows reports; RT_PATH_DIR decides */
    HERR("C:\\Titanfall2\\missing\\", EX | DI, ERROR_PATH_NOT_FOUND);

    /* RT_PATH_WILDCARD: the directory is translated and must exist; the last component is a pattern
     * appended unchanged, and is not looked up. */
    HOK("C:\\Titanfall2\\d\\*.txt", WC, I("d/*.txt"));
    HOK("C:\\Titanfall2\\*", WC, I("*"));
    HOK("C:\\Titanfall2\\d\\??.dll", WC, I("d/??.dll"));
    HOK("C:\\Titanfall2\\d\\inner.txt", WC, I("d/inner.txt"));       /* no wildcard characters: still not looked up */
    HOK("C:\\Titanfall2\\d\\absent.txt", WC, I("d/absent.txt"));
    HOK("d\\*", WC, I("d/*"));                                          /* relative to the guest's current directory */
    HOK("*.cfg", WC, I("*.cfg"));
    HOK("..\\Titanfall2\\d\\*", WC, I("d/*"));
    HOK("C:\\Titanfall2\\d\\*.", WC, I("d/*."));                     /* a pattern is not trimmed: "*." means "no extension" */
    HOK("C:\\Titanfall2\\d\\x. ", WC, I("d/x. "));
    HERR("C:\\Titanfall2\\nodir\\*", WC, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\f.txt\\*", WC, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\d\\", WC, ERROR_FILE_NOT_FOUND);           /* no pattern: nothing to find */
    HERR("C:\\", WC, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\d*\\x", WC, ERROR_INVALID_NAME);          /* only the last component may be a pattern */
    HERR("C:\\Titanfall2\\d\\a<b", WC, ERROR_INVALID_NAME);
    HERR("C:\\Titanfall2\\d\\a|b", WC, ERROR_INVALID_NAME);
    /* ... and without the flag, * and ? are invalid in a name. */
    HERR("C:\\Titanfall2\\d\\*.txt", EX, ERROR_INVALID_NAME);
    HERR("C:\\Titanfall2\\d\\a?", CR, ERROR_INVALID_NAME);
    HERR("C:\\Titanfall2\\d\\*", 0, ERROR_INVALID_NAME);
}

TEST(core, path_flag_errors)
{
    /* Flag combinations that mean nothing, and bits that do not exist, are a bug of the caller:
     * the unimplemented-case report names the flags. */
    char h[RT_PATH_MAX];
    const WCHAR *p = W("C:\\Titanfall2");
    T_TRAPS(rt_path_to_host_w(p, 0x40, h), "unknown flag bits 0x40 in flags 0x40");
    T_TRAPS(rt_path_to_host_w(p, 0x80000000u | EX, h), "unknown flag bits 0x80000000 in flags 0x80000001");
    T_TRAPS(rt_path_to_host_w(p, 0xFFFFFFFFu, h), "unknown flag bits");
    T_TRAPS(rt_path_to_host_w(p, EX | CR, h), "RT_PATH_EXISTING and RT_PATH_CREATE together");
    T_TRAPS(rt_path_to_host_w(p, EX | CR | DI, h), "RT_PATH_EXISTING and RT_PATH_CREATE together");
    T_TRAPS(rt_path_to_host_w(p, WC | EX, h), "RT_PATH_WILDCARD with another flag");
    T_TRAPS(rt_path_to_host_w(p, WC | CR, h), "RT_PATH_WILDCARD with another flag");
    T_TRAPS(rt_path_to_host_w(p, WC | DI, h), "RT_PATH_WILDCARD with another flag");
    T_TRAPS(rt_path_to_host_a("C:\\Titanfall2", 0x80, h), "rt_path_to_host_a: unknown flag bits 0x80");
    T_TRAPS(rt_path_to_host_a("C:\\Titanfall2", WC | EX, h), "rt_path_to_host_a: RT_PATH_WILDCARD with another flag");
    T_TRAPS(rt_path_to_host_w(p, EX, NULL), "rt_path_to_host_w: no output buffer");
    T_TRAPS(rt_path_to_host_a("C:\\Titanfall2", EX, NULL), "rt_path_to_host_a: no output buffer");

    /* The report names the shim that was running; outside a shim there is none, but the value is there. */
    T_TRAPS(rt_path_to_host_w(p, 0x40, h), "FATAL unimplemented");

    /* A NULL name is the guest's doing (it passed NULL), not the shim's: a Win32 error, not a report. */
    memset(h, 'X', sizeof h);
    T_EQ(rt_path_to_host_w(NULL, EX, h), ERROR_INVALID_PARAMETER);
    T_EQ(h[0], 0);
    memset(h, 'X', sizeof h);
    T_EQ(rt_path_to_host_a(NULL, EX, h), ERROR_INVALID_PARAMETER);
    T_EQ(h[0], 0);

    /* Every valid combination is accepted: 0, EX, CR, DI, EX|DI, CR|DI, WC. */
    const uint32_t ok[] = { 0, EX, CR, DI, EX | DI, CR | DI, WC };
    for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++) {
        DWORD e = rt_path_to_host_w(W("C:\\Titanfall2"), ok[i], h);
        T_TRUE(e == 0 || (ok[i] == WC && e == ERROR_FILE_NOT_FOUND));
    }
}

/* ---- the virtual directories ------------------------------------------------------------------------- */

TEST(core, path_delete_and_rename_flags)
{
    /* RT_PATH_DELETE and RT_PATH_RENAME: the caller is about to remove or move what the path
     * names, and the skeleton of the drive is refused with ERROR_ACCESS_DENIED (rt/path.h). */
    const uint32_t DE = RT_PATH_DELETE, RN = RT_PATH_RENAME;
    rt_path_test_force_case(0);
    mkdir_p(I("a"));
    touch(I("a/f.txt"));

    /* Below the skeleton the flags change nothing: the same host path, the same errors. */
    HOK("C:\\Titanfall2\\a\\f.txt", EX | DE, I("a/f.txt"));
    HOK("C:\\Titanfall2\\a\\f.txt", EX | RN, I("a/f.txt"));
    HOK("C:\\Titanfall2\\a\\f.txt", EX | DE | RN, I("a/f.txt"));
    HOK("C:\\Titanfall2\\a", EX | DI | DE, I("a"));
    HOK("a\\f.txt", EX | DE, I("a/f.txt"));
    HOK("C:\\Titanfall2\\a\\new.txt", CR | RN, I("a/new.txt"));
    HOK("C:\\Users\\Player\\AppData\\Local\\Temp\\x.tmp", CR | RN, S("tmp/x.tmp"));
    HOK("C:\\Users\\Player\\AppData\\Roaming", CR | DI | DE, S("home/AppData/Roaming"));
    HERR("C:\\Titanfall2\\a\\missing.txt", EX | DE, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\nodir\\f.txt", EX | RN, ERROR_PATH_NOT_FOUND);
    /* The errors of the path itself come first. */
    HERR("D:\\x", EX | DE, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\a<b", CR | RN, ERROR_INVALID_NAME);

    /* The skeleton: the root, every directory of the table, the directories that lead to one,
     * and NUL, however they are spelled and whatever the other flags are. */
    static const char *const skeleton[] = {
        "C:\\", "\\", "c:/", "C:\\Titanfall2", "c:\\TITANFALL2\\", "C:\\Titanfall2\\a\\..", ".", "..\\Titanfall2",
        "C:\\Users", "C:\\Users\\Player", "C:\\users\\player\\", "C:\\Users\\Player\\AppData",
        "C:\\Users\\Player\\AppData\\Local", "C:\\Users\\Player\\AppData\\Local\\Temp",
        "C:\\Users\\Player\\AppData\\Local\\Temp\\x\\..", "C:\\Windows", "C:\\Windows\\system32", "C:\\WINDOWS\\SYSTEM32\\",
        "\\\\?\\C:\\Windows", "NUL", "C:\\Titanfall2\\a\\nul", "\\\\.\\NUL", "C:\\nodir\\Nul",
    };
    static const uint32_t with[] = { EX, EX | DI, CR, CR | DI, 0 };
    for (unsigned i = 0; i < sizeof skeleton / sizeof skeleton[0]; i++) {
        WCHAR w[128];
        unsigned n = 0;
        for (const char *q = skeleton[i]; *q; q++) w[n++] = (WCHAR)(unsigned char)*q;
        w[n] = 0;
        for (unsigned j = 0; j < sizeof with / sizeof with[0]; j++) {
            char h[RT_PATH_MAX];
            T_EQ(rt_path_to_host_w(w, with[j] | DE, h), ERROR_ACCESS_DENIED);
            T_EQ(rt_path_to_host_w(w, with[j] | RN, h), ERROR_ACCESS_DENIED);
            T_EQ(rt_path_to_host_w(w, with[j] | DE | RN, h), ERROR_ACCESS_DENIED);
        }
    }
    T_EQ(rt_path_to_host_a("C:\\Windows", EX | DE, (char[RT_PATH_MAX]){ 0 }), ERROR_ACCESS_DENIED);
    /* Without the flags the same paths translate as before: the flag is the whole protection. */
    HOK("C:\\Windows", EX, S("vfs/Windows"));
    HOK("C:\\Users\\Player\\AppData\\Local", EX | DI, S("home/AppData/Local"));
    HOK("NUL", EX, "/dev/null");

    /* With a pattern they are a caller's bug, like every other flag. */
    char h[RT_PATH_MAX];
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\*"), WC | DE, h), "RT_PATH_WILDCARD with another flag");
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\*"), WC | RN, h), "RT_PATH_WILDCARD with another flag");
}

TEST(core, path_virtual_dirs)
{
    rt_path_test_force_case(0);

    /* C:\, C:\Users, C:\Windows and C:\Windows\system32 exist: they translate to real, empty host
     * directories. */
    HOK("C:\\", EX | DI, slash(S("vfs")));
    HOK("C:\\", EX, slash(S("vfs")));
    HOK("\\", EX, slash(S("vfs")));
    HOK("C:\\Users", EX, S("vfs/Users"));
    HOK("C:\\Windows", EX, S("vfs/Windows"));
    HOK("C:\\Windows\\system32", EX | DI, S("vfs/Windows/system32"));
    HOK("c:\\WINDOWS\\SYSTEM32\\", EX | DI, slash(S("vfs/Windows/system32")));
    HOK("C:\\Windows\\system32\\..\\..\\Users", EX, S("vfs/Users"));
    HOK("C:\\Titanfall2\\..", EX, S("vfs"));                  /* no trailing separator, none kept */
    HOK("C:\\Users\\..", EX, S("vfs"));
    HOK("C:\\Users\\Player\\..", EX, S("vfs/Users"));
    T_TRUE(is_dir(S("vfs")) && is_dir(S("vfs/Users")) && is_dir(S("vfs/Windows")) && is_dir(S("vfs/Windows/system32")));

    /* Nothing is in them but the mounts: anything else does not exist ... */
    HERR("C:\\foo", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\foo", EX | DI, ERROR_PATH_NOT_FOUND);
    HERR("C:\\autoexec.bat", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Windows\\system32\\kernel32.dll", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Windows\\win.ini", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Users\\Bob", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Users\\Public", EX | DI, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Program Files", EX | DI, ERROR_PATH_NOT_FOUND);
    /* ... and nothing can be put there: the guest has no right to (Windows says access denied to a
     * user who is not an administrator). */
    HERR("C:\\foo", CR, ERROR_ACCESS_DENIED);
    HERR("C:\\foo", CR | DI, ERROR_ACCESS_DENIED);
    HERR("C:\\foo", 0, ERROR_ACCESS_DENIED);
    HERR("C:\\foo", DI, ERROR_ACCESS_DENIED);
    HERR("C:\\Windows\\system32\\kernel32.dll", CR, ERROR_ACCESS_DENIED);
    HERR("C:\\Windows\\x.log", CR, ERROR_ACCESS_DENIED);
    HERR("C:\\Users\\Bob", CR | DI, ERROR_ACCESS_DENIED);
    /* Below something that does not exist is a missing path, whatever the flag. */
    HERR("C:\\foo\\bar", EX, ERROR_PATH_NOT_FOUND);
    HERR("C:\\foo\\bar", CR, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Windows\\system32\\x\\y", CR, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Users\\Bob\\Documents", EX, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Users\\Bob\\Documents\\a.txt", CR, ERROR_PATH_NOT_FOUND);
    /* The directories themselves can be "created": the host call then says they exist. */
    HOK("C:\\Windows", CR | DI, S("vfs/Windows"));
    HOK("C:\\Users\\Player", CR | DI, S("home"));

    /* Searches. */
    HOK("C:\\*", WC, S("vfs/*"));
    HOK("C:\\Users\\*", WC, S("vfs/Users/*"));
    HOK("C:\\Windows\\system32\\*.dll", WC, S("vfs/Windows/system32/*.dll"));
    HOK("C:\\Users\\Player\\*", WC, S("home/*"));
    HOK("C:\\Titanfall2\\*", WC, I("*"));
    HERR("C:\\foo\\*", WC, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Windows\\nodir\\*", WC, ERROR_PATH_NOT_FOUND);

    /* None of the refusals above made anything in the virtual tree. */
    char n[8][64];
    T_EQ(list_dir(S("vfs"), n, 8), 3);
    T_EQ(list_dir(S("vfs/Users"), n, 8), 1);
    T_EQ(list_dir(S("vfs/Windows"), n, 8), 1);
    T_EQ(list_dir(S("vfs/Windows/system32"), n, 8), 0);
}

/* ---- NUL --------------------------------------------------------------------------------------------- */

TEST(core, path_nul)
{
    rt_path_test_force_case(0);
    mkdir_p(I("a"));

    /* NUL is /dev/null: bare, in any case, in any directory, with the trailing dots and spaces that
     * Windows trims, and as the device path. */
    static const char *const nul[] = {
        "NUL", "nul", "Nul", "nUl", "NUL.", "NUL ", "NUL. .", "C:\\NUL", "c:\\nul", "C:\\Titanfall2\\NUL",
        "C:\\Titanfall2\\a\\NUL", "C:\\Titanfall2\\a\\..\\NUL", "a\\NUL", ".\\NUL", "\\NUL", "C:NUL", "C:/Titanfall2/a/nul",
        "\\\\.\\NUL", "\\\\.\\nul", "\\\\?\\NUL", "//./NUL", "//?/nul", "C:\\Users\\Player\\NUL", "C:\\Windows\\NUL",
        "C:\\nodir\\NUL",
    };
    static const uint32_t flags[] = { 0, EX, CR };
    for (unsigned i = 0; i < sizeof nul / sizeof *nul; i++)
        for (unsigned k = 0; k < sizeof flags / sizeof *flags; k++)
            HOK(nul[i], flags[k], "/dev/null");

    /* NUL is a device, not a directory and not a search. */
    HERR("NUL", DI, ERROR_PATH_NOT_FOUND);
    HERR("NUL", EX | DI, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\NUL", CR | DI, ERROR_PATH_NOT_FOUND);
    HERR("\\\\.\\NUL", WC, ERROR_FILE_NOT_FOUND);       /* UNSURE what FindFirstFile says for a device */
    HERR("\\\\.\\NUL", DI, ERROR_PATH_NOT_FOUND);

    /* Not NUL: other names that look alike, a directory called NUL in the middle, a trailing separator. */
    HOK("NULL", CR, I("NULL"));
    HOK("NUL1", CR, I("NUL1"));
    HOK("xNUL", CR, I("xNUL"));
    HOK("NU", CR, I("NU"));
    HOK("NUL\\", CR, slash(I("NUL")));
    HERR("NUL\\x", CR, ERROR_PATH_NOT_FOUND);
    HERR("C:\\NUL\\x", EX, ERROR_PATH_NOT_FOUND);
    HOK("C:\\Titanfall2\\a\\NUL.", WC, I("a/NUL."));         /* a search pattern is not a device name */

    /* With \\?\ on a drive path, NUL is an ordinary name: a file that Windows can only reach this way. */
    HOK("\\\\?\\C:\\Titanfall2\\NUL", CR, I("NUL"));
    HOK("\\\\?\\C:\\Titanfall2\\a\\nul", CR, I("a/nul"));
}

/* ---- rt_path_full_w: the full path as text (GetFullPathName) ------------------------------------------- */

/* The result for `win` must be `want` (ASCII), *len its length, and the file part the string
 * `want_part`, or none if that is NULL. */
static bool full_chk(const char *file, int line, const WCHAR *win, const char *want, const char *want_part) {
    WCHAR out[600];
    char got[600];
    DWORD len = 77, part = 77;
    for (unsigned i = 0; i < 600; i++) out[i] = 0xFFFF;
    DWORD e = rt_path_full_w(win, out, 600, &len, &part);
    if (e) { rt_test_fail(file, line, "rt_path_full_w: error %u, wanted \"%s\"", (unsigned)e, want); return false; }
    size_t n = 0;
    for (; n < 599 && out[n]; n++) got[n] = out[n] < 0x80 ? (char)out[n] : '?';
    got[n] = 0;
    const char *gp = part == RT_PATH_NO_PART ? NULL : part <= n ? got + part : "(index beyond the result)";
    bool ok = !strcmp(got, want) && len == strlen(want);
    ok = ok && (want_part ? gp && !strcmp(gp, want_part) : gp == NULL);
    if (!ok) rt_test_fail(file, line, "rt_path_full_w: got \"%s\" (length %u, file part %s%s%s), wanted \"%s\" (file part %s%s%s)",
                          got, (unsigned)len, gp ? "\"" : "", gp ? gp : "none", gp ? "\"" : "",
                          want, want_part ? "\"" : "", want_part ? want_part : "none", want_part ? "\"" : "");
    return ok;
}
#define FULL(win, want, part)  full_chk(__FILE__, __LINE__, W(win), (want), (part))

TEST(core, path_full_forms)
{
    /* Nothing below exists on the disk except C:\Titanfall2 itself: the function is text only. */
    FULL("C:\\Titanfall2\\a\\f.txt", "C:\\Titanfall2\\a\\f.txt", "f.txt");
    FULL("C:\\NoSuch\\Dir\\", "C:\\NoSuch\\Dir\\", NULL);
    FULL("C:\\a<b|c*?\"", "C:\\a<b|c*?\"", "a<b|c*?\"");          /* no character is checked */
    FULL("C:\\", "C:\\", NULL);
    FULL("c:/x/y", "c:\\x\\y", "y");                              /* the letter as written; / is \ */
    /* The current directory (C:\Titanfall2 at start): relative, and C: without a separator. */
    FULL("a\\f.txt", "C:\\Titanfall2\\a\\f.txt", "f.txt");
    FULL(".", "C:\\Titanfall2", "Titanfall2");
    FULL(".\\", "C:\\Titanfall2\\", NULL);
    FULL("..", "C:\\", NULL);
    FULL("..\\..\\..\\x", "C:\\x", "x");
    FULL("C:", "C:\\Titanfall2", "Titanfall2");
    FULL("c:x\\y", "C:\\Titanfall2\\x\\y", "y");
    FULL("ab:c", "C:\\Titanfall2\\ab:c", "ab:c");                 /* not a drive: text */
    /* Rooted: the root of the current drive.  \??\ is a rooted path to Win32. */
    FULL("\\x/y", "C:\\x\\y", "y");
    FULL("/", "C:\\", NULL);
    FULL("\\??\\C:\\x", "C:\\??\\C:\\x", "x");
    /* Other drives: answered, never looked for; their current directory is their root. */
    FULL("D:\\a\\..\\b", "D:\\b", "b");
    FULL("z:\\", "z:\\", NULL);
    FULL("D:", "D:\\", NULL);
    FULL("d:x\\..\\y", "d:\\y", "y");
    FULL("D:..", "D:\\", NULL);
    /* Device paths: the four characters, then normalised like any other, \\?\ too. */
    FULL("\\\\?\\C:\\a\\..\\b", "\\\\?\\C:\\b", "b");
    FULL("//./pipe//x", "\\\\.\\pipe\\x", "x");
    FULL("\\\\?\\C:\\..", "\\\\?\\", NULL);
    FULL("\\\\.\\", "\\\\.\\", NULL);
    FULL("\\\\.\\nul", "\\\\.\\nul", "nul");
    /* UNC: server and share are the root. */
    FULL("\\\\server\\share\\a\\..\\..\\b", "\\\\server\\share\\b", "b");
    FULL("//server//share//x/", "\\\\server\\share\\x\\", NULL);
    FULL("\\\\server\\share\\..", "\\\\server\\share\\", NULL);
    FULL("\\\\server\\share\\", "\\\\server\\share\\", NULL);
    FULL("\\\\server\\share", "\\\\server\\share", NULL);
    FULL("\\\\server\\", "\\\\server\\", NULL);
    FULL("\\\\server", "\\\\server", NULL);
    FULL("\\\\.x\\s. \\nul", "\\\\.x\\s. \\nul", "nul");          /* a server named .x; no device name on a share */

    /* The current directory is the guest's, read at each call. */
    mkdir_p(I("sub dir"));
    T_EQ(rt_path_set_cwd_w(W("C:\\Titanfall2\\sub dir")), 0);
    FULL("x", "C:\\Titanfall2\\sub dir\\x", "x");
    FULL("C:..\\y", "C:\\Titanfall2\\y", "y");
    FULL("\\x", "C:\\x", "x");
    T_EQ(rt_path_set_cwd_w(W("C:\\")), 0);
    FULL("x", "C:\\x", "x");
    FULL(".", "C:\\", NULL);
    FULL("C:", "C:\\", NULL);
}

TEST(core, path_full_normalisation)
{
    FULL("C:\\a\\\\b\\.\\c\\..\\d", "C:\\a\\b\\d", "d");
    FULL("C:\\..\\..\\a", "C:\\a", "a");                           /* .. stops at the root */
    FULL("C:\\a\\b\\..\\", "C:\\a\\", NULL);
    FULL("C:\\a\\..\\", "C:\\", NULL);
    FULL("C:\\a\\..", "C:\\", NULL);
    FULL("C:\\a\\b\\..", "C:\\a", "a");
    FULL("C:\\a\\.", "C:\\a", "a");
    /* A segment that is not the last loses one trailing period, and only one that stands alone. */
    FULL("C:\\a.\\b", "C:\\a\\b", "b");
    FULL("C:\\a..\\b", "C:\\a..\\b", "b");
    FULL("C:\\...\\b", "C:\\...\\b", "b");
    FULL("C:\\a \\b", "C:\\a \\b", "b");
    FULL("C:\\a.\\", "C:\\a\\", NULL);           /* "followed by a separator" is what counts */
    FULL("C:\\a. \\", "C:\\a. \\", NULL);
    /* The last segment loses all trailing periods and spaces. */
    FULL("C:\\a\\b. . ", "C:\\a\\b", "b");
    FULL("C:\\a\\b...", "C:\\a\\b", "b");
    FULL("x.txt ", "C:\\Titanfall2\\x.txt", "x.txt");
    /* ... and if nothing is left of it, the separator in front of it stays. */
    FULL("C:\\a\\...", "C:\\a\\", NULL);
    FULL("C:\\a\\ ", "C:\\a\\", NULL);
    FULL("C:\\a\\.. ", "C:\\a\\", NULL);
    FULL("C:\\ .", "C:\\", NULL);

    /* A DOS device name as the last component is the device, with the caller's spelling and no file part. */
    FULL("NUL", "\\\\.\\NUL", NULL);
    FULL("C:\\dir\\nul", "\\\\.\\nul", NULL);
    FULL("con", "\\\\.\\con", NULL);
    FULL("D:\\x\\Com1", "\\\\.\\Com1", NULL);
    FULL("\\lpt9", "\\\\.\\lpt9", NULL);
    FULL("C:\\a\\CONOUT$. ", "\\\\.\\CONOUT$", NULL);
    FULL("C:\\dir\\nul\\", "C:\\dir\\nul\\", NULL);               /* a directory, by its separator */
    FULL("C:\\nul\\x", "C:\\nul\\x", "x");
    FULL("C:\\dir\\null", "C:\\dir\\null", "null");
    FULL("C:\\dir\\com0", "C:\\dir\\com0", "com0");
    FULL("\\\\?\\C:\\dir\\nul", "\\\\?\\C:\\dir\\nul", "nul");    /* not in a device path */

    /* The two functions normalise with the same code: what the translation accepts, it and this agree on. */
    rt_path_test_force_case(0);
    mkdir_p(I("a/b"));
    touch(I("a/b/f.txt"));
    static const char *const same[] = { "a\\.\\b\\..\\b\\f.txt. ", "C:/Titanfall2//a/b./f.txt", "\\Titanfall2\\a\\b\\", "c:a\\b" };
    for (unsigned i = 0; i < sizeof same / sizeof *same; i++) {
        char h[RT_PATH_MAX];
        WCHAR back[300], full[300];
        DWORD len = 0;
        T_EQ(rt_path_to_host_w(W(same[i]), EX, h), 0);
        int bn = rt_path_to_win_w(h, back, 300);
        T_EQ(rt_path_full_w(W(same[i]), full, 300, &len, NULL), 0);
        size_t fl = len;
        if (fl && full[fl - 1] == '\\') fl--;                    /* the reverse translation has no trailing separator */
        T_EQ((size_t)bn, fl);
        T_TRUE(bn > 0 && !memcmp(back, full, fl * sizeof(WCHAR)));
    }
}

TEST(core, path_full_sizes_and_errors)
{
    const char *want = "C:\\Titanfall2\\a\\f.txt";               /* 22 units; the file part starts at 16 */
    const DWORD L = (DWORD)strlen(want);
    WCHAR out[64];
    DWORD len, part;

    /* Room for the result and its terminator: written, and *len is the length. */
    for (unsigned i = 0; i < 64; i++) out[i] = 0xFFFF;
    len = part = 77;
    T_EQ(rt_path_full_w(W("a\\f.txt"), out, L + 1, &len, &part), 0);
    T_EQ(len, L);
    T_EQ(part, 16);
    T_EQ(out[L], 0);
    T_EQ(out[L + 1], 0xFFFF);
    T_EQ(out[16], 'f');
    T_EQ(out[15], '\\');
    /* One short, and no buffer at all: nothing is written, *len is the size needed, and it is not an error. */
    for (unsigned i = 0; i < 64; i++) out[i] = 0xFFFF;
    len = part = 77;
    T_EQ(rt_path_full_w(W("a\\f.txt"), out, L, &len, &part), 0);
    T_EQ(len, L + 1);
    T_EQ(part, RT_PATH_NO_PART);
    T_EQ(out[0], 0xFFFF);
    len = part = 77;
    T_EQ(rt_path_full_w(W("a\\f.txt"), NULL, 0, &len, &part), 0);
    T_EQ(len, L + 1);
    T_EQ(part, RT_PATH_NO_PART);
    len = 77;
    T_EQ(rt_path_full_w(W("a\\f.txt"), out, 1, &len, NULL), 0);   /* file_part may be NULL */
    T_EQ(len, L + 1);
    T_EQ(out[0], 0xFFFF);

    /* Lengths and the file part are in UTF-16 units: U+00E9 is one, U+1F600 two. */
    static const WCHAR uni[] = { 0x00E9, '\\', 0xD83D, 0xDE00, '\\', 'x', 0x00E9, 0 };
    T_EQ(rt_path_full_w(uni, out, 64, &len, &part), 0);
    T_EQ(len, 14 + 7);
    T_EQ(part, 14 + 5);
    T_TRUE(!memcmp(out + 14, uni, 8 * sizeof(WCHAR)));

    /* No name, an empty one, nothing but spaces. */
    static const WCHAR empty[] = { 0 };
    const WCHAR *const bad[] = { NULL, empty, W(" "), W("    ") };
    for (unsigned i = 0; i < 4; i++) {
        out[0] = 0xFFFF;
        len = part = 77;
        T_EQ(rt_path_full_w(bad[i], out, 64, &len, &part), ERROR_INVALID_NAME);
        T_EQ(len, 0);
        T_EQ(part, RT_PATH_NO_PART);
        T_EQ(out[0], 0xFFFF);
    }

    /* Long names: 5000 units work (and take the allocating path); 32767 is the limit, for the name and
     * for the result. */
    static WCHAR big[32770], bigout[32770];
    for (unsigned i = 0; i < 32769; i++) big[i] = 'a';
    big[5000] = 0;
    T_EQ(rt_path_full_w(big, bigout, 32770, &len, &part), 0);
    T_EQ(len, 14 + 5000);
    T_EQ(part, 14);
    T_EQ(bigout[len - 1], 'a');
    big[5000] = 'a';
    big[0] = 'C'; big[1] = ':'; big[2] = '\\';
    big[32767] = 0;
    T_EQ(rt_path_full_w(big, bigout, 32770, &len, &part), 0);     /* exactly 32767, absolute: the result is the name */
    T_EQ(len, 32767);
    big[0] = big[1] = big[2] = 'a';
    T_EQ(rt_path_full_w(big, bigout, 32770, &len, &part), ERROR_FILENAME_EXCED_RANGE);   /* relative: the result is longer */
    T_EQ(len, 0);
    big[32767] = 'a';
    big[32768] = 0;
    T_EQ(rt_path_full_w(big, bigout, 32770, &len, &part), ERROR_FILENAME_EXCED_RANGE);

    /* What is reported instead of answered. */
    T_TRAPS(rt_path_full_w(W("x"), out, 64, NULL, &part), "rt_path_full_w: no place for the length");
    T_TRAPS(rt_path_full_w(W("x"), NULL, 5, &len, &part), "rt_path_full_w: no buffer for 5 units");
    T_TRAPS(rt_path_full_w(W("\\\\."), out, 64, &len, &part), "with nothing after it");
    T_TRAPS(rt_path_full_w(W("//?"), out, 64, &len, &part), "with nothing after it");
    T_TRAPS(rt_path_full_w(W("\\\\"), out, 64, &len, &part), "a UNC path without a server name");
    T_TRAPS(rt_path_full_w(W("\\\\\\x"), out, 64, &len, &part), "a UNC path without a server name");
    T_TRAPS(rt_path_full_w(W("C:\\a\\nul.txt"), out, 64, &len, &part), "a DOS device name followed by '.' or ':'");
    T_TRAPS(rt_path_full_w(W("NUL:"), out, 64, &len, &part), "a DOS device name followed by '.' or ':'");
    static const WCHAR lone[] = { 'a', 0xD800, 'b', 0 };
    T_TRAPS(rt_path_full_w(lone, out, 64, &len, &part), "unpaired UTF-16 surrogate");
}
