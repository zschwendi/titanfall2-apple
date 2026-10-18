#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/time.h"

/* I and put: as in runtime/tests/core/file_open.c.  The tree is made on the host, under the
 * install root, which the guest sees as C:\Titanfall2. */
static const char *I(const char *rel) {             /* a host path under the install root */
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

/* d/a.txt (3 bytes), d/sub (a directory), d/_x (7 bytes, read-only), e/<U+65E5>.txt (its name
 * is not in code page 1252). */
static void make_tree(void) {
    if (mkdir(I("d"), 0755) || mkdir(I("d/sub"), 0755) || mkdir(I("e"), 0755))
        rt_test_fail(__FILE__, __LINE__, "mkdir");
    put("d/a.txt", "abc", 0644);
    put("d/_x", "1234567", 0444);
    put("e/\346\227\245.txt", "x", 0644);
}

static int all_zero(const void *p, size_t n) {
    const unsigned char *c = p;
    for (size_t i = 0; i < n; i++) if (c[i]) return 0;
    return 1;
}

static void check_time(const FILETIME *ft, const struct timespec *ts) {
    const uint64_t v = rt_time_filetime_from_unix(ts->tv_sec, ts->tv_nsec);
    T_EQ(ft->dwLowDateTime, (DWORD)v);
    T_EQ(ft->dwHighDateTime, (DWORD)(v >> 32));
}

/* The WCHAR name w is the ASCII name s (for the one listing that is read with rt_find_next_w). */
static int name_is(const WCHAR *w, const char *s) {
    size_t i = 0;
    for (; s[i]; i++) if (w[i] != (unsigned char)s[i]) return 0;
    return w[i] == 0;
}

/* Every field the guest sees for the entry `name` of the host object at `path`: as in the test of
 * FindFirstFileA.  with_atime is 0 for . and .. (a listing may move the directory's own access
 * time). */
static void check_host(const WIN32_FIND_DATAA *f, const char *name, const char *path, int with_atime) {
    struct stat st;
    int ok = stat(path, &st) == 0;
    T_TRUE(ok);
    if (!ok) return;
    const int dir = S_ISDIR(st.st_mode) != 0;
    const DWORD attr = dir ? FILE_ATTRIBUTE_DIRECTORY
                           : (FILE_ATTRIBUTE_ARCHIVE | ((st.st_mode & S_IWUSR) ? 0 : FILE_ATTRIBUTE_READONLY));
    T_EQ(strcmp(f->cFileName, name), 0);
    T_EQ(f->dwFileAttributes, attr);
    T_EQ(f->nFileSizeHigh, 0);
    T_EQ(f->nFileSizeLow, dir ? 0 : (DWORD)st.st_size);
    check_time(&f->ftCreationTime, &st.st_birthtimespec);
    if (with_atime) check_time(&f->ftLastAccessTime, &st.st_atimespec);
    check_time(&f->ftLastWriteTime, &st.st_mtimespec);
    T_EQ(f->dwReserved0, 0);
    T_EQ(f->dwReserved1, 0);
    T_TRUE(all_zero(f->cAlternateFileName, sizeof f->cAlternateFileName));
    T_TRUE(all_zero(f->cFileName + strlen(name) + 1, sizeof f->cFileName - strlen(name) - 1));
}

static void check_entry(const WIN32_FIND_DATAA *f, const char *name, const char *rel, int with_atime) {
    check_host(f, name, I(rel), with_atime);
}

/* The entries after the first one of the directory d, in the core's order, then the end of the
 * listing. */
static void rest_of_listing(HANDLE h, WIN32_FIND_DATAA *f) {
    T_EQ(CALL(kernel32, FindNextFileA, h, f), TRUE);
    check_entry(f, "..", "d/..", 0);
    T_EQ(CALL(kernel32, FindNextFileA, h, f), TRUE);
    check_entry(f, "a.txt", "d/a.txt", 1);
    T_EQ(CALL(kernel32, FindNextFileA, h, f), TRUE);
    check_entry(f, "sub", "d/sub", 1);
    T_EQ(CALL(kernel32, FindNextFileA, h, f), TRUE);
    check_entry(f, "_x", "d/_x", 1);
    T_EQ(CALL(kernel32, FindNextFileA, h, f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
    T_EQ(CALL(kernel32, FindNextFileA, h, f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
}

TEST(kernel32, FindFirstFileExA)
{
    WIN32_FIND_DATAA f;
    WIN32_FIND_DATAW wf;
    HANDLE h;
    char longname[MAX_PATH + 32];
    char vfs[2048], up[2048];

    make_tree();

    T_CASE("a handle and the first entry, which is ., with its attributes, times and size");
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0);
    T_TRUE(h != INVALID_HANDLE_VALUE);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, ".", "d/.", 0);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("FindExInfoBasic gives the same first entry");
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoBasic, &f, FindExSearchNameMatch, NULL, 0);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, ".", "d/.", 0);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("the names follow in the core's order, .. first and then the rest sorted without case");
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, ".", "d/.", 0);
    rest_of_listing(h, &f);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("*.txt gives only the matching file");
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*.txt", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, "a.txt", "d/a.txt", 1);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("*.* matches a name without a period");
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*.*", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, ".", "d/.", 0);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    check_entry(&f, "sub", "d/sub", 1);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("FindExSearchLimitToDirectories is accepted and files are still returned");
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoStandard, &f, FindExSearchLimitToDirectories, NULL, 0);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, ".", "d/.", 0);
    memset(&wf, 0, sizeof wf);
    for (int i = 0; i < 8 && !name_is(wf.cFileName, "a.txt"); i++)
        T_EQ(rt_find_next_w(h, &wf), TRUE);
    T_TRUE(name_is(wf.cFileName, "a.txt"));
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("FIND_FIRST_EX_LARGE_FETCH is accepted and ignored");
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, ".", "d/.", 0);
    rest_of_listing(h, &f);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("a pattern that matches nothing fails with ERROR_FILE_NOT_FOUND");
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*.zip", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0);
    T_TRUE(h == INVALID_HANDLE_VALUE);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    T_CASE("a directory that does not exist fails with ERROR_PATH_NOT_FOUND");
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\nope\\*", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0);
    T_TRUE(h == INVALID_HANDLE_VALUE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_find_close(h), TRUE);

    /* The drive root is the host directory <state>/vfs (it holds the mounts' placeholders); its parent is <state>. */
    T_CASE("the drive root also lists . and ..");
    snprintf(vfs, sizeof vfs, "%s/vfs", rt_test_state_dir());
    snprintf(up, sizeof up, "%s/vfs/..", rt_test_state_dir());
    h = CALL(kernel32, FindFirstFileExA, "C:\\*", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_host(&f, ".", vfs, 0);
    T_EQ(rt_find_next_a(h, &f), TRUE);
    check_host(&f, "..", up, 0);
    T_EQ(rt_find_close(h), TRUE);

    /* The stops and the refusals: no T_CASE of their own.  The level 2 and the operation 2 are
     * values that are neither of the two named ones. */
    memset(longname, 'x', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    memcpy(longname, "C:\\Titanfall2\\", 14);
    T_TRAPS(CALL(kernel32, FindFirstFileExA, NULL, FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0), "a NULL name");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, "", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0), "an empty name");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, longname, FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0), "a name of MAX_PATH or more characters");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", 2, &f, FindExSearchNameMatch, NULL, 0), "fInfoLevelId other than FindExInfoStandard or FindExInfoBasic");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoStandard, &f, 2, NULL, 0), "fSearchOp other than FindExSearchNameMatch or FindExSearchLimitToDirectories");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoStandard, &f, FindExSearchNameMatch, &f, 0), "lpSearchFilter not NULL");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, FIND_FIRST_EX_CASE_SENSITIVE), "dwAdditionalFlags other than FIND_FIRST_EX_LARGE_FETCH");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\*", FindExInfoStandard, NULL, FindExSearchNameMatch, NULL, 0), "lpFindFileData NULL");    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\a?", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0), "the pattern \"a?\"");    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\a.", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0), "the pattern \"a.\"");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\a.*", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0), "the pattern \"a.*\"");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\e\\*.txt", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0), "an entry name outside code page 1252");
    T_TRAPS(CALL(kernel32, FindFirstFileExA, "C:\\Titanfall2\\d\\.", FindExInfoStandard, &f, FindExSearchNameMatch, NULL, 0), "a search whose last component is \".\"");
}
