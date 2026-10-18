#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"
#include "rt/time.h"

/* W, I and put: as in runtime/tests/core/file_open.c.  The tree is made on the host, under the
 * install root, which the guest sees as C:\Titanfall2. */
static const WCHAR *W(const char *a) {
    static WCHAR buf[4][512];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    size_t i = 0;
    for (; a[i]; i++) b[i] = (unsigned char)a[i];
    b[i] = 0;
    return b;
}
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

/* d/a.txt (3 bytes), d/sub (a directory), d/_x (7 bytes, read-only). */
static void make_tree(void) {
    if (mkdir(I("d"), 0755) || mkdir(I("d/sub"), 0755))
        rt_test_fail(__FILE__, __LINE__, "mkdir");
    put("d/a.txt", "abc", 0644);
    put("d/_x", "1234567", 0444);
}

static int all_zero(const void *p, size_t n) {
    const unsigned char *c = p;
    for (size_t i = 0; i < n; i++) if (c[i]) return 0;
    return 1;
}

static int all_eq(const void *p, unsigned char v, size_t n) {
    const unsigned char *c = p;
    for (size_t i = 0; i < n; i++) if (c[i] != v) return 0;
    return 1;
}

/* The WCHARs of w are the ASCII of s, then NUL. */
static int wis(const WCHAR *w, const char *s) {
    size_t i = 0;
    for (; s[i]; i++) if (w[i] != (unsigned char)s[i]) return 0;
    return w[i] == 0;
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
static void check_entry(const WIN32_FIND_DATAW *f, const char *name, const char *rel, int with_atime) {
    struct stat st;
    int ok = stat(I(rel), &st) == 0;
    T_TRUE(ok);
    if (!ok) return;
    const int dir = S_ISDIR(st.st_mode) != 0;
    const DWORD attr = dir ? FILE_ATTRIBUTE_DIRECTORY
                           : (FILE_ATTRIBUTE_ARCHIVE | ((st.st_mode & S_IWUSR) ? 0 : FILE_ATTRIBUTE_READONLY));
    const size_t len = strlen(name);
    const size_t cap = sizeof f->cFileName / sizeof f->cFileName[0];
    T_TRUE(wis(f->cFileName, name));
    T_EQ(f->dwFileAttributes, attr);
    T_EQ(f->nFileSizeHigh, 0);
    T_EQ(f->nFileSizeLow, dir ? 0 : (DWORD)st.st_size);
    check_time(&f->ftCreationTime, &st.st_birthtimespec);
    if (with_atime) check_time(&f->ftLastAccessTime, &st.st_atimespec);
    check_time(&f->ftLastWriteTime, &st.st_mtimespec);
    T_EQ(f->dwReserved0, 0);
    T_EQ(f->dwReserved1, 0);
    T_TRUE(all_zero(f->cAlternateFileName, sizeof f->cAlternateFileName));
    T_TRUE(all_zero(&f->cFileName[len + 1], (cap - len - 1) * sizeof(WCHAR)));
}

TEST(kernel32, FindNextFileW)
{
    WIN32_FIND_DATAW f;
    HANDLE h, fh;

    make_tree();

    T_CASE("each next entry in the core's order, with its attributes, times and size");
    h = CALL(kernel32, FindFirstFileW, W("C:\\Titanfall2\\d\\*"), &f);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, ".", "d/.", 0);
    T_EQ(CALL(kernel32, FindNextFileW, h, &f), TRUE);
    check_entry(&f, "..", "d/..", 0);
    T_EQ(CALL(kernel32, FindNextFileW, h, &f), TRUE);
    check_entry(&f, "a.txt", "d/a.txt", 1);
    T_EQ(CALL(kernel32, FindNextFileW, h, &f), TRUE);
    check_entry(&f, "sub", "d/sub", 1);
    T_EQ(CALL(kernel32, FindNextFileW, h, &f), TRUE);
    check_entry(&f, "_x", "d/_x", 1);

    T_CASE("after the last entry FALSE with ERROR_NO_MORE_FILES, every time, and the buffer untouched");
    memset(&f, 0xA5, sizeof f);
    T_EQ(CALL(kernel32, FindNextFileW, h, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
    T_TRUE(all_eq(&f, 0xA5, sizeof f));
    T_EQ(CALL(kernel32, FindNextFileW, h, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
    T_TRUE(all_eq(&f, 0xA5, sizeof f));
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("a pattern with one match has no next entry");
    h = CALL(kernel32, FindFirstFileW, W("C:\\Titanfall2\\d\\*.txt"), &f);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    check_entry(&f, "a.txt", "d/a.txt", 1);
    T_EQ(CALL(kernel32, FindNextFileW, h, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NO_MORE_FILES);
    T_EQ(rt_find_close(h), TRUE);

    T_CASE("a NULL handle and a file handle fail with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, FindNextFileW, NULL, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    fh = rt_file_create(RT_NAME_W(W("C:\\Titanfall2\\d\\a.txt")), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    T_REQUIRE(fh != INVALID_HANDLE_VALUE);
    T_EQ(CALL(kernel32, FindNextFileW, fh, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_handle_close(fh), TRUE);

    T_CASE("success leaves last error alone");
    h = CALL(kernel32, FindFirstFileW, W("C:\\Titanfall2\\d\\*"), &f);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FindNextFileW, h, &f), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_find_close(h), TRUE);

    /* The stop: no T_CASE of its own. */
    h = CALL(kernel32, FindFirstFileW, W("C:\\Titanfall2\\d\\*"), &f);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    T_TRAPS(CALL(kernel32, FindNextFileW, h, NULL), "lpFindFileData NULL");
    T_EQ(rt_find_close(h), TRUE);
}
