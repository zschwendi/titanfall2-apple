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

/* Every field the guest sees for the entry `name` of the host object `rel`: the attributes
 * and size as the file system gives them, the times from stat (taken before the call, which
 * does not change them), the fields that stay zero.  A listing may move a directory's own
 * access time (the . entry is the directory it reads), so with_atime is 0 for . and .. */
static void check_entry(const WIN32_FIND_DATAA *f, const char *name, const char *rel, int with_atime) {
    struct stat st;
    int ok = stat(I(rel), &st) == 0;
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

TEST(kernel32, FindFirstFileA)
{
    WIN32_FIND_DATAA f;
    HANDLE h;
    char longname[MAX_PATH + 32];

    make_tree();

    T_CASE("a handle and the first entry, which is ., with its attributes, times and size");
    h = CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\d\\*", &f);
    T_TRUE(h != INVALID_HANDLE_VALUE);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, ".", "d/.", 0);

    T_CASE("the names follow in the core's order, .. first and then the rest sorted without case");
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    check_entry(&f, "..", "d/..", 0);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    check_entry(&f, "a.txt", "d/a.txt", 1);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    check_entry(&f, "sub", "d/sub", 1);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    check_entry(&f, "_x", "d/_x", 1);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("*.txt gives only the matching file");
    h = CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\d\\*.txt", &f);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, "a.txt", "d/a.txt", 1);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("*.* matches a name without a period");
    h = CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\d\\*.*", &f);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, ".", "d/.", 0);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    T_EQ(CALL(kernel32, FindNextFileA, h, &f), TRUE);
    check_entry(&f, "sub", "d/sub", 1);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("a pattern that matches nothing fails with ERROR_FILE_NOT_FOUND");
    h = CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\d\\*.zip", &f);
    T_TRUE(h == INVALID_HANDLE_VALUE);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    T_CASE("a directory that does not exist fails with ERROR_PATH_NOT_FOUND");
    h = CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\nope\\*", &f);
    T_TRUE(h == INVALID_HANDLE_VALUE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\d\\*", &f);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_find_close(h), TRUE);

    /* The stops and the refusals: no T_CASE of their own. */
    memset(longname, 'x', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    memcpy(longname, "C:\\Titanfall2\\", 14);
    T_TRAPS(CALL(kernel32, FindFirstFileA, NULL, &f), "a NULL name");
    T_TRAPS(CALL(kernel32, FindFirstFileA, "", &f), "an empty name");
    T_TRAPS(CALL(kernel32, FindFirstFileA, longname, &f), "MAX_PATH or more characters");
    T_TRAPS(CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\d\\*", NULL), "lpFindFileData NULL");
    T_TRAPS(CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\d\\a?", &f), "the pattern \"a?\"");
    T_TRAPS(CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\d\\a.", &f), "the pattern \"a.\"");
    T_TRAPS(CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\d\\a.*", &f), "the pattern \"a.*\"");
    T_TRAPS(CALL(kernel32, FindFirstFileA, "C:\\Titanfall2\\e\\*.txt", &f), "an entry name outside code page 1252");
}
