/* Case (docs/RUNTIME.md 7.2).  On a case-insensitive volume names pass through; on a case-sensitive one each
 * component is resolved by exact match and then by an ASCII case-folded lookup in a cached directory listing.
 * This Mac's volume is case-insensitive, so the tests switch the volume probe's answer to "case-sensitive"
 * (rt_path_test_force_case) and the lookup is then done from the listing, as it would be on a case-sensitive
 * volume: the directory names it hears are the ones on disk, and the spelling it hands out is theirs.
 * The choice between an exact and a folded match, which a case-insensitive volume can not be made to show (it
 * will not hold "foo" and "FOO" together), is tested with a listing the test supplies.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/path.h"

void rt_path_test_force_case(int mode);
void rt_path_test_stats(unsigned *reads, unsigned *cached);
int  rt_path_test_probe(const char *win_root);
void rt_path_test_fake_listing(const char *dir, const char *const *names, size_t n);

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
static void touch(const char *abs) {
    int fd = open(abs, O_CREAT | O_WRONLY, 0644);
    if (fd < 0) { rt_test_fail(__FILE__, __LINE__, "create %s: %s", abs, strerror(errno)); return; }
    close(fd);
}
/* Set the modification time of a directory (and its access time) to `sec` seconds since 1970. */
static void set_mtime(const char *path, time_t sec) {
    struct timespec ts[2] = { { sec, 0 }, { sec, 0 } };
    if (utimensat(AT_FDCWD, path, ts, 0) != 0) rt_test_fail(__FILE__, __LINE__, "utimensat %s: %s", path, strerror(errno));
}
/* About an hour ago: old enough for its listing to be trusted.  Each call gives another time (a second earlier than
 * the last), so a directory that is aged again is always seen to have changed, as a real change would show. */
static void age(const char *path) {
    static time_t t;
    if (!t) t = time(NULL) - 3600;
    set_mtime(path, t--);
}

static bool chk(const char *file, int line, const WCHAR *win, uint32_t flags, DWORD want_err, const char *want_host) {
    char host[RT_PATH_MAX];
    memset(host, 'X', sizeof host);
    DWORD e = rt_path_to_host_w(win, flags, host);
    if (e != want_err) {
        rt_test_fail(file, line, "flags %#x: error %u, wanted %u (host buffer \"%.80s\")", flags, (unsigned)e, (unsigned)want_err, host);
        return false;
    }
    if (e) return host[0] == 0;
    if (strcmp(host, want_host)) {
        rt_test_fail(file, line, "flags %#x: host path\n    %s\n  wanted\n    %s", flags, host, want_host);
        return false;
    }
    return true;
}
#define HOK(win, flags, want)   T_TRUE(chk(__FILE__, __LINE__, W(win), (flags), 0, (want)))
#define HOKW(win, flags, want)  T_TRUE(chk(__FILE__, __LINE__, (win), (flags), 0, (want)))
#define HERR(win, flags, err)   T_TRUE(chk(__FILE__, __LINE__, W(win), (flags), (err), NULL))
#define HERRW(win, flags, err)  T_TRUE(chk(__FILE__, __LINE__, (win), (flags), (err), NULL))

#define EX   RT_PATH_EXISTING
#define CR   RT_PATH_CREATE
#define DI   RT_PATH_DIR
#define WC   RT_PATH_WILDCARD

static unsigned reads(void) { unsigned r; rt_path_test_stats(&r, NULL); return r; }
static unsigned cached(void) { unsigned c; rt_path_test_stats(NULL, &c); return c; }

static void tree(void)
{
    mkdir_p(I("Dir/Sub"));
    touch(I("Foo.TXT"));
    touch(I("readme.md"));
    touch(I("Dir/Sub/File.Bin"));
    touch(I("Dir/other.txt"));
}

/* ---- folded lookup ------------------------------------------------------------------------------------------ */

TEST(core, path_case_folded_lookup)
{
    rt_path_test_force_case(1);
    tree();
    /* The name on disk is used whatever the guest wrote. */
    HOK("C:\\Titanfall2\\Foo.TXT", EX, I("Foo.TXT"));                         /* exact */
    HOK("c:\\titanfall2\\foo.txt", EX, I("Foo.TXT"));
    HOK("C:\\Titanfall2\\FOO.txt", EX, I("Foo.TXT"));
    HOK("C:\\Titanfall2\\README.MD", EX, I("readme.md"));
    HOK("C:\\Titanfall2\\dir\\SUB\\file.bin", EX, I("Dir/Sub/File.Bin"));
    HOK("C:\\Titanfall2\\DIR\\sub", EX | DI, I("Dir/Sub"));
    HOK("C:\\Titanfall2\\DIR", EX | DI, I("Dir"));
    HOK("C:\\Titanfall2\\dir\\", EX, slash(I("Dir")));
    HOK("C:/Titanfall2/DIR/./SUB/../OTHER.TXT", EX, I("Dir/other.txt"));      /* after "." and ".." are resolved */
    HOK("dir\\sub\\FILE.BIN", EX, I("Dir/Sub/File.Bin"));                     /* relative to the current directory */
    HOK("\\titanfall2\\foo.TXT", EX, I("Foo.TXT"));
    /* A new file keeps the caller's spelling; one that exists keeps its own. */
    HOK("C:\\Titanfall2\\foo.txt", CR, I("Foo.TXT"));
    HOK("C:\\Titanfall2\\NewFile.Txt", CR, I("NewFile.Txt"));
    HOK("C:\\Titanfall2\\DIR\\NEW.txt", CR, I("Dir/NEW.txt"));                /* the directory is on disk, the file is not */
    HOK("C:\\Titanfall2\\DIR\\sub\\file.BIN", CR, I("Dir/Sub/File.Bin"));
    HOK("C:\\Titanfall2\\dir\\Brand", CR | DI, I("Dir/Brand"));
    HOK("C:\\Titanfall2\\dir\\Brand", 0, I("Dir/Brand"));
    HOK("C:\\Titanfall2\\dir\\other.TXT", DI, I("Dir/other.txt"));
    HOK("C:\\Titanfall2\\Foo.TXT\\", CR, slash(I("Foo.TXT")));
    /* Missing things. */
    HERR("C:\\Titanfall2\\missing.txt", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\Fooo.TXT", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\dir\\nothing", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\dir\\nothing", EX | DI, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\nodir\\x", CR, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\NODIR\\x", EX, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\dir\\nodir\\x", CR, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\foo.txt\\x", CR, ERROR_PATH_NOT_FOUND);              /* a file where a directory is needed */
    HERR("C:\\Titanfall2\\FOO.TXT\\x\\y", EX, ERROR_PATH_NOT_FOUND);
    /* Searches: the directory is resolved, the pattern is not touched. */
    HOK("C:\\Titanfall2\\DIR\\*.TXT", WC, I("Dir/*.TXT"));
    HOK("C:\\Titanfall2\\dir\\sub\\*", WC, I("Dir/Sub/*"));
    HOK("C:\\Titanfall2\\*", WC, I("*"));
    HOK("C:\\Titanfall2\\dir\\FILE.BIN", WC, I("Dir/FILE.BIN"));               /* not looked up, so not corrected */
    HERR("C:\\Titanfall2\\nodir\\*", WC, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\foo.txt\\*", WC, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\dir\\nodir\\*", WC, ERROR_PATH_NOT_FOUND);
    /* Under \\?\ the names are looked up the same way (the prefix only turns normalisation off). */
    HOK("\\\\?\\C:\\Titanfall2\\DIR\\sub\\FILE.bin", EX, I("Dir/Sub/File.Bin"));
    /* The virtual directories have their own spelling and do not need a volume. */
    HOK("c:\\WINDOWS\\SYSTEM32", EX, S("vfs/Windows/system32"));
    HOK("c:\\", EX, slash(S("vfs")));
    /* The other mounts are looked up too. */
    touch(S("home/Documents/Save.DAT"));
    touch(S("tmp/A.tmp"));
    mkdir_p(S("tmp/Sub"));
    HOK("C:\\Users\\Player\\documents\\SAVE.dat", EX, S("home/Documents/Save.DAT"));
    HOK("C:\\Users\\Player\\AppData\\Local\\Temp\\a.TMP", EX, S("tmp/A.tmp"));
    HOK("C:\\Users\\Player\\AppData\\Local\\Temp\\sub\\New", CR, S("tmp/Sub/New"));
    HOK("C:\\Users\\Player\\appdata\\LOCAL\\x", CR, S("home/AppData/Local/x"));
    HOK("C:\\Users\\Player\\SAVED GAMES", EX, S("home/Saved Games"));
}

TEST(core, path_case_ascii_only)
{
    rt_path_test_force_case(1);
    /* Folding is ASCII only (section 7.2): U+00C9 and U+00E9 are different names even though NTFS would call them one. */
    static const WCHAR upper[] = { 'C', ':', '\\', 'T', 'i', 't', 'a', 'n', 'f', 'a', 'l', 'l', '2', '\\', 0x00C9, 'a', 0 };
    static const WCHAR lower[] = { 'C', ':', '\\', 'T', 'i', 't', 'a', 'n', 'f', 'a', 'l', 'l', '2', '\\', 0x00E9, 'a', 0 };
    touch(I("\xC3\x89" "a"));
    HOKW(upper, EX, I("\xC3\x89" "a"));
    HERRW(lower, EX, ERROR_FILE_NOT_FOUND);
    HOKW(lower, CR, I("\xC3\xA9" "a"));                                       /* so a new file of that spelling */
    /* ... and ASCII letters inside a name that has other characters are folded. */
    touch(I("Caf\xC3\xA9.TXT"));
    static const WCHAR caf[] = { 'C', ':', '\\', 'T', 'i', 't', 'a', 'n', 'f', 'a', 'l', 'l', '2', '\\', 'c', 'A', 'F', 0x00E9, '.', 't', 'x', 't', 0 };
    HOKW(caf, EX, I("Caf\xC3\xA9.TXT"));
}

TEST(core, path_case_probe_and_override)
{
    mkdir_p(I("Dir"));
    touch(I("Dir/Foo.TXT"));
    /* With the probe's own answer: whatever this volume is, the result is right for it.  On a case-insensitive
     * volume names pass through and the host finds the file itself; on a case-sensitive one the listing is used. */
    rt_path_test_force_case(-1);
    int sens = rt_path_test_probe("C:\\Titanfall2");
    T_TRUE(sens == 0 || sens == 1);
    if (sens) HOK("C:\\Titanfall2\\dir\\foo.txt", EX, I("Dir/Foo.TXT"));
    else      HOK("C:\\Titanfall2\\dir\\foo.txt", EX, I("dir/foo.txt"));        /* the caller's spelling goes through */
    /* Forced one way, then the other, then back to the probe. */
    rt_path_test_force_case(1);
    HOK("C:\\Titanfall2\\dir\\foo.txt", EX, I("Dir/Foo.TXT"));
    rt_path_test_force_case(0);
    if (!sens) {
        unsigned r = reads();
        HOK("C:\\Titanfall2\\dir\\foo.txt", EX, I("dir/foo.txt"));
        T_EQ(cached(), 0);                                                    /* names pass through: no listing is made */
        T_EQ(reads(), r);
    }
    rt_path_test_force_case(-1);
    if (sens) HOK("C:\\Titanfall2\\dir\\foo.txt", EX, I("Dir/Foo.TXT"));
    else      HOK("C:\\Titanfall2\\dir\\foo.txt", EX, I("dir/foo.txt"));
    /* A case-insensitive volume does not need the file to be spelled right; a missing one is still missing. */
    rt_path_test_force_case(0);
    if (!sens) HERR("C:\\Titanfall2\\dir\\bar.txt", EX, ERROR_FILE_NOT_FOUND);
    if (!sens) HERR("C:\\Titanfall2\\nodir\\bar.txt", EX, ERROR_PATH_NOT_FOUND);
}

/* ---- exact before folded ------------------------------------------------------------------------------------- */

TEST(core, path_case_exact_then_folded)
{
    rt_path_test_force_case(1);
    /* A case-sensitive volume can hold names that differ only in case; the exact one is the one asked for.
     * (Windows could never have made them, but the host may have been given them by someone else.) */
    const char *const names[] = { "readme", "README", "Readme.TXT", "ReadMe.txt", "x", "Y", "Dir" };
    rt_path_test_fake_listing(I(""), names, sizeof names / sizeof *names);
    HOK("C:\\Titanfall2\\README", EX, I("README"));
    HOK("C:\\Titanfall2\\readme", EX, I("readme"));
    HOK("C:\\Titanfall2\\README", CR, I("README"));
    HOK("C:\\Titanfall2\\Readme.TXT", EX, I("Readme.TXT"));
    HOK("C:\\Titanfall2\\ReadMe.txt", EX, I("ReadMe.txt"));
    /* one name that matches when folded: that is the one */
    HOK("C:\\Titanfall2\\X", EX, I("x"));
    HOK("C:\\Titanfall2\\x", EX, I("x"));
    HOK("C:\\Titanfall2\\y", EX, I("Y"));
    HOK("C:\\Titanfall2\\Y", EX, I("Y"));
    HOK("C:\\Titanfall2\\X", CR, I("x"));
    /* nothing matches */
    HERR("C:\\Titanfall2\\zzz", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\rea", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\readmee", EX, ERROR_FILE_NOT_FOUND);
    HOK("C:\\Titanfall2\\zzz", CR, I("zzz"));
    HOK("C:\\Titanfall2\\Zzz", 0, I("Zzz"));
    /* Several names that match when folded and none exactly: which is meant is not decided, so it stops. */
    char h[RT_PATH_MAX];
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\readme.txt"), EX, h), "\"readme.txt\" matches 2 names that differ only in case");
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\README.TXT"), CR, h), "matches 2 names that differ only in case");
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\ReAdMe"), EX, h), "\"ReAdMe\" matches 2 names that differ only in case");
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\Readme"), CR | DI, h), "(\"README\", \"readme\", ...)");
    T_TRAPS(rt_path_to_host_a("C:\\Titanfall2\\ReAdMe", EX, h), "differ only in case");
    T_TRAPS(rt_path_to_host_w(W("C:\\Titanfall2\\ReAdMe\\x"), CR, h), "differ only in case");      /* in the middle of a path too */
    HOK("C:\\Titanfall2\\ReAdMe", WC, I("ReAdMe"));                          /* a search pattern is not looked up */
}

TEST(core, path_case_many_names)
{
    rt_path_test_force_case(1);
    /* A big listing, given in no order: every name is found by exact and by folded match, the ones around the ends and
     * the ones between are not. */
    enum { N = 2000 };
    static char store[N][16];
    static const char *names[N];
    for (int i = 0; i < N; i++) {
        snprintf(store[i], sizeof store[i], "%s%04d.dat", i % 3 == 0 ? "N" : (i % 3 == 1 ? "n" : "Nx"), (i * 7919) % N);
        names[i] = store[i];
    }
    rt_path_test_fake_listing(I(""), names, N);
    for (int i = 0; i < N; i++) {
        char win[64], up[64], lo[64];
        snprintf(win, sizeof win, "C:\\Titanfall2\\%s", names[i]);
        HOK(win, EX, I(names[i]));
        snprintf(up, sizeof up, "C:\\Titanfall2\\%s", names[i]);
        for (char *q = up + 14; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
        snprintf(lo, sizeof lo, "C:\\Titanfall2\\%s", names[i]);
        for (char *q = lo + 14; *q; q++) if (*q >= 'A' && *q <= 'Z') *q = (char)(*q + 32);
        if (strcmp(up + 14, names[i]) != 0 && strcasecmp(up + 14, names[i]) == 0) {
            /* Two names can fold to one only if both are in the list; they are not (the numbers differ), so the folded form finds the one. */
            HOK(up, EX, I(names[i]));
            HOK(lo, EX, I(names[i]));
        }
    }
    HERR("C:\\Titanfall2\\N0000x.dat", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\a", EX, ERROR_FILE_NOT_FOUND);                      /* before the first */
    HERR("C:\\Titanfall2\\~", EX, ERROR_FILE_NOT_FOUND);                      /* after the last */
    HERR("C:\\Titanfall2\\n2000.dat", EX, ERROR_FILE_NOT_FOUND);
    HERR("C:\\Titanfall2\\n", EX, ERROR_FILE_NOT_FOUND);
}

/* ---- the cache ------------------------------------------------------------------------------------------------ */

TEST(core, path_case_cache)
{
    rt_path_test_force_case(1);
    mkdir_p(I("c"));
    touch(I("c/File1"));
    age(I("c"));
    age(I(""));
    unsigned r0 = reads();
    T_EQ(r0, 0);

    /* A listing is read once and used until something changes. */
    HOK("C:\\Titanfall2\\C\\FILE1", EX, I("c/File1"));
    T_EQ(reads(), r0 + 2);                                                  /* the install root and c */
    T_EQ(cached(), 2);
    HOK("C:\\Titanfall2\\C\\FILE1", EX, I("c/File1"));
    HOK("C:\\Titanfall2\\c\\file1", EX, I("c/File1"));
    HOK("C:\\Titanfall2\\c\\file1", CR, I("c/File1"));
    HERR("C:\\Titanfall2\\c\\file9", EX, ERROR_FILE_NOT_FOUND);
    T_EQ(reads(), r0 + 2);

    /* A change in the directory's modification time (someone else made a file) is noticed. */
    touch(I("c/File2"));
    unsigned r1 = reads();
    HOK("C:\\Titanfall2\\c\\file2", EX, I("c/File2"));
    T_EQ(reads(), r1 + 1);
    /* ... and a directory that changed within the last two seconds is not trusted: its time could change again without a
     * different value (on a volume with one-second times), so it is read each time until it has been quiet. */
    HOK("C:\\Titanfall2\\c\\file2", EX, I("c/File2"));
    HOK("C:\\Titanfall2\\c\\file2", EX, I("c/File2"));
    T_EQ(reads(), r1 + 3);
    age(I("c"));
    HOK("C:\\Titanfall2\\c\\file2", EX, I("c/File2"));                  /* the time changed again: read once more ... */
    T_EQ(reads(), r1 + 4);
    HOK("C:\\Titanfall2\\c\\file2", EX, I("c/File2"));                  /* ... and now it is quiet and trusted */
    HOK("C:\\Titanfall2\\c\\FILE1", EX, I("c/File1"));
    T_EQ(reads(), r1 + 4);

    /* A change the directory's time does not show (the time was set back, or is too coarse) is missed until the runtime is told:
     * rt_path_invalidate, which the code that creates, deletes or renames in a directory calls. */
    set_mtime(I("c"), 1000000000);
    HOK("C:\\Titanfall2\\c\\file1", EX, I("c/File1"));                  /* time differs: read, and trusted (very old) */
    unsigned r2 = reads();
    touch(I("c/File3"));
    set_mtime(I("c"), 1000000000);                                          /* the same time as before */
    HERR("C:\\Titanfall2\\c\\file3", EX, ERROR_FILE_NOT_FOUND);          /* stale: the runtime has not been told */
    T_EQ(reads(), r2);
    rt_path_invalidate(I("c"));
    HOK("C:\\Titanfall2\\c\\file3", EX, I("c/File3"));
    T_EQ(reads(), r2 + 1);
    /* Other spellings of the directory name: a trailing slash, doubled slashes. */
    touch(I("c/File4"));
    set_mtime(I("c"), 1000000000);
    HERR("C:\\Titanfall2\\c\\file4", EX, ERROR_FILE_NOT_FOUND);
    rt_path_invalidate(slash(I("c")));
    HOK("C:\\Titanfall2\\c\\file4", EX, I("c/File4"));
    touch(I("c/File5"));
    set_mtime(I("c"), 1000000000);
    HERR("C:\\Titanfall2\\c\\file5", EX, ERROR_FILE_NOT_FOUND);
    rt_path_invalidate(cat(I(""), "//c//"));
    HOK("C:\\Titanfall2\\c\\file5", EX, I("c/File5"));
    /* Another directory's invalidation leaves it alone; so does one that is not cached; NULL is a bug. */
    touch(I("c/File6"));
    set_mtime(I("c"), 1000000000);
    rt_path_invalidate(I(""));
    rt_path_invalidate(I("not/cached"));
    rt_path_invalidate("/");
    rt_path_invalidate("");
    HERR("C:\\Titanfall2\\c\\file6", EX, ERROR_FILE_NOT_FOUND);
    rt_path_invalidate(I("c"));
    HOK("C:\\Titanfall2\\c\\file6", EX, I("c/File6"));
    T_TRAPS(rt_path_invalidate(NULL), "rt_path_invalidate: no directory given");

    /* A directory that is gone is gone. */
    mkdir_p(I("gone"));
    touch(I("gone/X"));
    age(I("gone"));
    age(I(""));
    HOK("C:\\Titanfall2\\gone\\x", EX, I("gone/X"));
    unsigned c1 = cached();
    T_EQ(unlink(I("gone/X")), 0);
    T_EQ(rmdir(I("gone")), 0);
    HERR("C:\\Titanfall2\\gone\\x", EX, ERROR_PATH_NOT_FOUND);
    HERR("C:\\Titanfall2\\gone\\x", CR, ERROR_PATH_NOT_FOUND);
    T_TRUE(cached() <= c1);                                                 /* (a dead listing stays until it is used again or evicted) */
    /* ... and a new one of that name is read afresh, not taken for the old one. */
    mkdir_p(I("gone"));
    touch(I("gone/Y"));
    HOK("C:\\Titanfall2\\GONE\\y", EX, I("gone/Y"));

    /* Starting over (rt_path_init) forgets everything, the counter too. */
    rt_path_init();
    T_EQ(reads(), 0);
    T_EQ(cached(), 0);
}

TEST(core, path_case_cache_eviction)
{
    rt_path_test_force_case(1);
    /* More directories than the cache holds (32): every answer is still right, the oldest listings are dropped. */
    enum { N = 40 };
    for (int i = 0; i < N; i++) {
        char d[32], f[48];
        snprintf(d, sizeof d, "e%02d", i);
        snprintf(f, sizeof f, "%s/Item", d);
        mkdir_p(I(d));
        touch(I(f));
        age(I(d));
    }
    age(I(""));
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < N; i++) {
            char win[64], want[64];
            snprintf(win, sizeof win, "C:\\Titanfall2\\E%02d\\ITEM", i);
            snprintf(want, sizeof want, "e%02d/Item", i);
            HOK(win, EX, I(want));
        }
        T_TRUE(cached() <= 32);
    }
    unsigned r = reads();
    HOK("C:\\Titanfall2\\E39\\item", EX, I("e39/Item"));                 /* the most recent one is still cached */
    T_EQ(reads(), r);
    HOK("C:\\Titanfall2\\E00\\item", EX, I("e00/Item"));                 /* the oldest was dropped */
    T_TRUE(reads() > r);
}
