/* The find-file piece (rt/file.h, core/file_priv.h): rt_find_first_w/_a, rt_find_next_w/_a and
 * rt_find_close.  Each case of the plan is one T_PLAN block.  The directories are made on the
 * host under rt_test_install_root(), which is C:\Titanfall2.
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/time.h"

int rt_file_test_fd(HANDLE h);

#define RD    GENERIC_READ
#define SH    (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define BAD   INVALID_HANDLE_VALUE
#define COUNT(a)  ((unsigned)(sizeof(a) / sizeof((a)[0])))

static const char *I(const char *rel) {
    static char buf[4][2048];
    static unsigned k;
    char *b = buf[k++ & 3];
    snprintf(b, sizeof buf[0], "%s/%s", rt_test_install_root(), rel);
    return b;
}
static void put(const char *rel, const char *text, mode_t mode) {
    int fd = open(I(rel), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0 || write(fd, text, strlen(text)) != (ssize_t)strlen(text)) rt_test_fail(__FILE__, __LINE__, "put %s", rel);
    if (fd >= 0) { fchmod(fd, mode); close(fd); }
}
static HANDLE cfa(const char *win, DWORD access, DWORD share, DWORD disp, DWORD fa) {
    return rt_file_create(RT_NAME_A(win), access, share, disp, fa, NULL);
}
/* A host ASCII string as a guest WCHAR string, in one of eight rotating buffers. */
static WCHAR *W(const char *s) {
    static WCHAR buf[8][300];
    static unsigned k;
    WCHAR *b = buf[k++ & 7];
    unsigned i = 0;
    for (; s[i]; i++) b[i] = (WCHAR)(unsigned char)s[i];
    b[i] = 0;
    return b;
}
/* True if the NUL-terminated WCHAR string `w` spells the ASCII string `s`. */
static bool wis(const WCHAR *w, const char *s) {
    unsigned i = 0;
    for (; s[i]; i++) if (w[i] != (WCHAR)(unsigned char)s[i]) return false;
    return w[i] == 0;
}
static bool all_eq(const void *p, size_t n, unsigned char v) {
    const unsigned char *c = p;
    for (size_t i = 0; i < n; i++) if (c[i] != v) return false;
    return true;
}
static uint64_t ft64(FILETIME t) { return ((uint64_t)t.dwHighDateTime << 32) | t.dwLowDateTime; }

/* The directory F: the files of the plan, exactly. */
static void setup_f(void) {
    mkdir(I("F"), 0755);
    unlink(I("F/new.txt"));
    put("F/b.txt", "01234", 0644);
    put("F/A.txt", "A", 0644);
    put("F/_z.txt", "z", 0644);
    put("F/c.dat", "c", 0644);
    put("F/noext", "n", 0644);
    put("F/Zed.TXT", "Z", 0644);
    put("F/a.txt.bak", "bak", 0644);
    mkdir(I("F/Sub"), 0755);
}

/* The ten names of F\*, in the order of case 1. */
static const char *const TEN[] = {".", "..", "A.txt", "a.txt.bak", "b.txt", "c.dat", "noext",
                                  "Sub", "Zed.TXT", "_z.txt"};

/* Enumerates `pat` and checks the names in `want`, then the end (FALSE, ERROR_NO_MORE_FILES). */
static void listing(const char *pat, const char *const *want, unsigned n) {
    WIN32_FIND_DATAW d;
    memset(&d, 0xAA, sizeof d);
    HANDLE h = rt_find_first_w(W(pat), &d);
    T_TRUE(h != BAD);
    if (h == BAD) return;
    for (unsigned i = 0; i < n; i++) {
        if (i) { memset(&d, 0xAA, sizeof d); T_TRUE(rt_find_next_w(h, &d)); }
        T_TRUE(wis(d.cFileName, want[i]));
    }
    memset(&d, 0xAA, sizeof d);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_find_next_w(h, &d), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
    T_TRUE(all_eq(&d, sizeof d, 0xAA));
    (void)rt_find_close(h);
}

TEST(core, file_find_list)
{
    T_PLAN(1, "F\\* in its order, then the end twice") {
        setup_f();
        WIN32_FIND_DATAW d;
        memset(&d, 0xAA, sizeof d);
        rt_thread_set_last_error(0xDEAD);
        HANDLE h = rt_find_first_w(W("C:\\Titanfall2\\F\\*"), &d);
        T_REQUIRE(h != BAD);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(wis(d.cFileName, TEN[0]));
        for (unsigned i = 1; i < COUNT(TEN); i++) {
            memset(&d, 0xAA, sizeof d);
            rt_thread_set_last_error(0xDEAD);
            T_TRUE(rt_find_next_w(h, &d));
            T_EQ(T_LAST_ERROR(), 0xDEAD);
            T_TRUE(wis(d.cFileName, TEN[i]));
        }
        for (int twice = 0; twice < 2; twice++) {
            memset(&d, 0xAA, sizeof d);
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_find_next_w(h, &d), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
            T_TRUE(all_eq(&d, sizeof d, 0xAA));
        }
        (void)rt_find_close(h);
    }
    T_PLAN(2, "F\\*.* gives the same ten") {
        listing("C:\\Titanfall2\\F\\*.*", TEN, COUNT(TEN));
    }
    T_PLAN(3, "the patterns *.txt, a* and *t*") {
        static const char *const TXT[] = {"A.txt", "b.txt", "Zed.TXT", "_z.txt"};
        static const char *const A_[] = {"A.txt", "a.txt.bak"};
        static const char *const T_[] = {"A.txt", "a.txt.bak", "b.txt", "c.dat", "noext", "Zed.TXT", "_z.txt"};
        listing("C:\\Titanfall2\\F\\*.txt", TXT, COUNT(TXT));
        listing("C:\\Titanfall2\\F\\a*", A_, COUNT(A_));
        listing("C:\\Titanfall2\\F\\*t*", T_, COUNT(T_));
    }
    T_PLAN(4, "B.TXT gives b.txt; sub gives Sub with attributes 0x10 and size 0") {
        static const char *const B[] = {"b.txt"};
        listing("C:\\Titanfall2\\F\\B.TXT", B, COUNT(B));
        WIN32_FIND_DATAW d;
        memset(&d, 0xAA, sizeof d);
        HANDLE h = rt_find_first_w(W("C:\\Titanfall2\\F\\sub"), &d);
        T_REQUIRE(h != BAD);
        T_TRUE(wis(d.cFileName, "Sub"));
        T_EQ(d.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY);
        T_EQ(d.nFileSizeHigh, 0);
        T_EQ(d.nFileSizeLow, 0);
        (void)rt_find_close(h);
    }
    T_PLAN(5, "no match gives ERROR_FILE_NOT_FOUND; a missing directory ERROR_PATH_NOT_FOUND") {
        WIN32_FIND_DATAW d;
        rt_thread_set_last_error(0xDEAD);
        HANDLE h = rt_find_first_w(W("C:\\Titanfall2\\F\\*.xyz"), &d);
        T_TRUE(h == BAD);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        h = rt_find_first_w(W("C:\\Titanfall2\\Nope\\*"), &d);
        T_TRUE(h == BAD);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    }
    T_PLAN(6, "the entry fields: b.txt, a 0444 file, and .") {
        mkdir(I("G6"), 0755);
        put("G6/ro.txt", "r", 0444);
        WIN32_FIND_DATAW d;
        memset(&d, 0xAA, sizeof d);
        HANDLE h = rt_find_first_w(W("C:\\Titanfall2\\F\\b.txt"), &d);
        T_REQUIRE(h != BAD);
        T_EQ(d.dwFileAttributes, FILE_ATTRIBUTE_ARCHIVE);
        T_EQ(d.nFileSizeLow, 5);
        T_EQ(d.nFileSizeHigh, 0);
        struct stat st;
        T_EQ(stat(I("F/b.txt"), &st), 0);
        T_EQ(ft64(d.ftLastWriteTime),
             rt_time_filetime_from_unix(st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec));
        T_EQ(d.dwReserved0, 0);
        T_EQ(d.dwReserved1, 0);
        T_EQ(d.cAlternateFileName[0], 0);
        T_TRUE(wis(d.cFileName, "b.txt"));
        bool tail_zero = true;
        for (unsigned i = 6; i < COUNT(d.cFileName); i++) if (d.cFileName[i] != 0) tail_zero = false;
        T_TRUE(tail_zero);
        (void)rt_find_close(h);

        h = rt_find_first_w(W("C:\\Titanfall2\\G6\\ro.txt"), &d);
        T_REQUIRE(h != BAD);
        T_EQ(d.dwFileAttributes, FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY);
        (void)rt_find_close(h);

        h = rt_find_first_w(W("C:\\Titanfall2\\F\\*"), &d);
        T_REQUIRE(h != BAD);
        T_TRUE(wis(d.cFileName, "."));
        T_EQ(d.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY);
        (void)rt_find_close(h);
    }
    T_PLAN(7, "a find handle closes only by rt_find_close; a file handle is no find handle") {
        WIN32_FIND_DATAW d;
        HANDLE h = rt_find_first_w(W("C:\\Titanfall2\\F\\*"), &d);
        T_REQUIRE(h != BAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_handle_close(h), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_TRUE(rt_find_next_w(h, &d));
        T_TRUE(wis(d.cFileName, TEN[1]));
        T_EQ(rt_find_close(h), TRUE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_find_close(h), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        HANDLE fh = cfa("C:\\Titanfall2\\F\\b.txt", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(fh != BAD);
        T_EQ(rt_find_close(fh), FALSE);
        T_TRUE(rt_file_test_fd(fh) != -1);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_find_next_w(fh, &d), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(rt_handle_close(fh), TRUE);
    }
    T_PLAN(8, "a?.txt, x.* and a NULL out stop rt_find_first_w") {
        WIN32_FIND_DATAW d;
        T_TRAPS(rt_find_first_w(W("C:\\Titanfall2\\F\\a?.txt"), &d), "rt_find_first_w: not implemented");
        T_TRAPS(rt_find_first_w(W("C:\\Titanfall2\\F\\x.*"), &d), "rt_find_first_w: not implemented");
        T_TRAPS(rt_find_first_w(W("C:\\Titanfall2\\F\\*"), NULL), "rt_find_first_w: not implemented");
    }
}

/* Case 9 changes F, so it has a process of its own. */
TEST(core, file_find_snapshot)
{
    T_PLAN(9, "after the first entry a file is made and one is deleted; the ten names stay") {
        setup_f();
        WIN32_FIND_DATAW d;
        memset(&d, 0xAA, sizeof d);
        HANDLE h = rt_find_first_w(W("C:\\Titanfall2\\F\\*"), &d);
        T_REQUIRE(h != BAD);
        T_TRUE(wis(d.cFileName, TEN[0]));
        put("F/new.txt", "new", 0644);
        unlink(I("F/c.dat"));
        for (unsigned i = 1; i < COUNT(TEN); i++) {
            memset(&d, 0xAA, sizeof d);
            T_TRUE(rt_find_next_w(h, &d));
            T_TRUE(wis(d.cFileName, TEN[i]));
        }
        memset(&d, 0xAA, sizeof d);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_find_next_w(h, &d), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
        (void)rt_find_close(h);
    }
}

/* Case 10: the links and the FIFO of a directory of their own. */
TEST(core, file_find_links)
{
    T_PLAN(10, "a dangling link and a FIFO are not returned; a link to b.txt is, with size 5") {
        setup_f();
        mkdir(I("G10"), 0755);
        unlink(I("G10/dangle"));
        unlink(I("G10/fifo"));
        unlink(I("G10/link.txt"));
        T_EQ(symlink("nowhere/none", I("G10/dangle")), 0);
        T_EQ(mkfifo(I("G10/fifo"), 0644), 0);
        T_EQ(symlink(I("F/b.txt"), I("G10/link.txt")), 0);
        WIN32_FIND_DATAW d;
        memset(&d, 0xAA, sizeof d);
        HANDLE h = rt_find_first_w(W("C:\\Titanfall2\\G10\\*"), &d);
        T_REQUIRE(h != BAD);
        T_TRUE(wis(d.cFileName, "."));
        memset(&d, 0xAA, sizeof d);
        T_TRUE(rt_find_next_w(h, &d));
        T_TRUE(wis(d.cFileName, ".."));
        memset(&d, 0xAA, sizeof d);
        T_TRUE(rt_find_next_w(h, &d));
        T_TRUE(wis(d.cFileName, "link.txt"));
        T_EQ(d.nFileSizeHigh, 0);
        T_EQ(d.nFileSizeLow, 5);
        memset(&d, 0xAA, sizeof d);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_find_next_w(h, &d), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
        (void)rt_find_close(h);
    }
}

/* Case 11: the A forms; their names are bytes of code page 1252. */
TEST(core, file_find_forms)
{
    T_PLAN(11, "rt_find_first_a/next_a give the four names of case 3; U+00E9 is 0xE9; U+0101 traps") {
        setup_f();
        WIN32_FIND_DATAA a;
        memset(&a, 0xAA, sizeof a);
        rt_thread_set_last_error(0xDEAD);
        HANDLE h = rt_find_first_a("C:\\Titanfall2\\F\\*.txt", &a);
        T_REQUIRE(h != BAD);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        static const char *const TXT[] = {"A.txt", "b.txt", "Zed.TXT", "_z.txt"};
        for (unsigned i = 0; i < COUNT(TXT); i++) {
            if (i) {
                memset(&a, 0xAA, sizeof a);
                rt_thread_set_last_error(0xDEAD);
                T_TRUE(rt_find_next_a(h, &a));
                T_EQ(T_LAST_ERROR(), 0xDEAD);
            }
            T_TRUE(strcmp(a.cFileName, TXT[i]) == 0);
            T_EQ(a.dwFileAttributes, FILE_ATTRIBUTE_ARCHIVE);
        }
        T_EQ(a.nFileSizeLow, 1);
        memset(&a, 0xAA, sizeof a);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_find_next_a(h, &a), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
        (void)rt_find_close(h);

        mkdir(I("G11"), 0755);
        put("G11/\xC3\xA9.txt", "e", 0644);
        WIN32_FIND_DATAW w;
        memset(&w, 0xAA, sizeof w);
        h = rt_find_first_w(W("C:\\Titanfall2\\G11\\*.txt"), &w);
        T_REQUIRE(h != BAD);
        T_TRUE(w.cFileName[0] == 0x00E9 && wis(w.cFileName + 1, ".txt"));
        (void)rt_find_close(h);
        h = rt_find_first_a("C:\\Titanfall2\\G11\\*.txt", &a);
        T_REQUIRE(h != BAD);
        T_TRUE((unsigned char)a.cFileName[0] == 0xE9 && strcmp(a.cFileName + 1, ".txt") == 0);
        (void)rt_find_close(h);

        mkdir(I("G11b"), 0755);
        put("G11b/\xC4\x81.txt", "a", 0644);
        T_TRAPS(rt_find_first_a("C:\\Titanfall2\\G11b\\*.txt", &a), "rt_find_first_a: not implemented");
    }
}

/* Case 12: 2,000 files and one handle, read by eight host threads.  The workers only count
 * what they saw; slot 2000 is ".", 2001 is "..", slot k is the file f<k>.dat. */
#define NFILES    2000
#define NTHR      8
#define SLOT_DOT  2000
#define SLOT_DDOT 2001
static HANDLE gfind;
static atomic_uint seen[NFILES + 2];
static atomic_uint junk;

static int slot_of(const WCHAR *w) {
    if (w[0] == '.' && w[1] == 0) return SLOT_DOT;
    if (w[0] == '.' && w[1] == '.' && w[2] == 0) return SLOT_DDOT;
    /* "f" + four decimal digits + ".dat" */
    if (w[0] != 'f' || w[5] != '.' || w[6] != 'd' || w[7] != 'a' || w[8] != 't' || w[9] != 0) return -1;
    int v = 0;
    for (int i = 1; i <= 4; i++) {
        if (w[i] < '0' || w[i] > '9') return -1;
        v = v * 10 + (w[i] - '0');
    }
    return v < NFILES ? v : -1;
}
static void *walker(void *arg) {
    (void)arg;
    WIN32_FIND_DATAW d;
    while (rt_find_next_w(gfind, &d)) {
        int k = slot_of(d.cFileName);
        if (k < 0) atomic_fetch_add(&junk, 1);
        else atomic_fetch_add(&seen[k], 1);
    }
    return NULL;
}

TEST(core, file_find_threads)
{
    T_PLAN(12, "2,002 names from one handle, read by eight threads: each exactly once") {
        mkdir(I("G12"), 0755);
        for (unsigned i = 0; i < NFILES; i++) {
            char rel[64];
            snprintf(rel, sizeof rel, "G12/f%04u.dat", i);
            put(rel, "x", 0644);
        }
        WIN32_FIND_DATAW d;
        memset(&d, 0xAA, sizeof d);
        gfind = rt_find_first_w(W("C:\\Titanfall2\\G12\\*"), &d);
        T_REQUIRE(gfind != BAD);
        int k = slot_of(d.cFileName);
        T_TRUE(k >= 0);
        if (k >= 0) atomic_fetch_add(&seen[k], 1);
        pthread_t t[NTHR];
        for (int i = 0; i < NTHR; i++) T_REQUIRE(pthread_create(&t[i], NULL, walker, NULL) == 0);
        for (int i = 0; i < NTHR; i++) pthread_join(t[i], NULL);
        unsigned wrong = 0;
        for (unsigned j = 0; j < NFILES + 2; j++) wrong += atomic_load(&seen[j]) != 1;
        T_EQ(wrong, 0);
        T_EQ(atomic_load(&junk), 0);
        (void)rt_find_close(gfind);
    }
}
