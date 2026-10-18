#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"
#include "rt/time.h"

/* W, I and put are the helpers of runtime/tests/core/file_open.c: a wide name, and host
 * paths under the install root, which is the guest's C:\Titanfall2. */
static const WCHAR *W(const char *a) {
    static WCHAR buf[4][512];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    size_t i = 0;
    for (; a[i]; i++) b[i] = (unsigned char)a[i];
    b[i] = 0;
    return b;
}
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

/* A FILETIME for a unix time in whole seconds, by the core's conversion. */
static FILETIME unix_ft(int64_t sec) {
    uint64_t v = rt_time_filetime_from_unix(sec, 0);
    FILETIME f;
    f.dwLowDateTime = (DWORD)v;
    f.dwHighDateTime = (DWORD)(v >> 32);
    return f;
}
static FILETIME ft_of(DWORD lo, DWORD hi) {
    FILETIME f;
    f.dwLowDateTime = lo;
    f.dwHighDateTime = hi;
    return f;
}
static int same(const FILETIME *a, const FILETIME *b) {
    return a->dwLowDateTime == b->dwLowDateTime && a->dwHighDateTime == b->dwHighDateTime;
}
/* Opened with read and write (GENERIC_WRITE includes FILE_WRITE_ATTRIBUTES). */
static HANDLE open_rw(const char *win) {
    return rt_file_create(RT_NAME_W(W(win)), GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

TEST(kernel32, SetFileTime)
{
    BY_HANDLE_FILE_INFORMATION before, after;
    FILETIME wt, at, zero, nonzero, ff, pre, creation, write2;
    HANDLE h, h2, nul;

    put("set.bin", "hello", 0644);
    h = open_rw("C:\\Titanfall2\\set.bin");
    T_REQUIRE(h != NULL && h != INVALID_HANDLE_VALUE);
    T_REQUIRE(rt_file_info_by_handle(h, &before));

    T_CASE("a write time of 2001-09-09 01:46:40 UTC (unix 1000000000) is stored and the access time stays");
    wt = unix_ft(1000000000);
    T_EQ(CALL(kernel32, SetFileTime, h, NULL, NULL, &wt), TRUE);
    T_REQUIRE(rt_file_info_by_handle(h, &after));
    /* The creation time is not asserted in the two cases that set an earlier time: on this
     * host it followed the earlier write time, while the call leaves it alone (the cases
     * below that change nothing or only zero FILETIMEs check it). */
    T_TRUE(same(&after.ftLastWriteTime, &wt));
    T_TRUE(same(&after.ftLastAccessTime, &before.ftLastAccessTime));

    T_CASE("an access time alone");
    at = unix_ft(1100000000);
    T_EQ(CALL(kernel32, SetFileTime, h, NULL, &at, NULL), TRUE);
    T_REQUIRE(rt_file_info_by_handle(h, &after));
    T_TRUE(same(&after.ftLastAccessTime, &at));
    T_TRUE(same(&after.ftLastWriteTime, &wt));

    T_CASE("all three pointers NULL changes nothing and returns TRUE");
    T_REQUIRE(rt_file_info_by_handle(h, &before));
    T_EQ(CALL(kernel32, SetFileTime, h, NULL, NULL, NULL), TRUE);
    T_REQUIRE(rt_file_info_by_handle(h, &after));
    T_TRUE(same(&after.ftCreationTime, &before.ftCreationTime));
    T_TRUE(same(&after.ftLastAccessTime, &before.ftLastAccessTime));
    T_TRUE(same(&after.ftLastWriteTime, &before.ftLastWriteTime));

    T_CASE("a FILETIME of 0 leaves that time unchanged");
    T_REQUIRE(rt_file_info_by_handle(h, &before));
    zero = ft_of(0, 0);
    nonzero = unix_ft(1200000000);
    T_EQ(CALL(kernel32, SetFileTime, h, &zero, &zero, &nonzero), TRUE);
    T_REQUIRE(rt_file_info_by_handle(h, &after));
    T_TRUE(same(&after.ftCreationTime, &before.ftCreationTime));
    T_TRUE(same(&after.ftLastAccessTime, &before.ftLastAccessTime));
    T_TRUE(same(&after.ftLastWriteTime, &nonzero));

    T_CASE("a creation time is stored as the birth time, and a write time earlier than it does not undo it");
    creation = unix_ft(1200000000);
    write2 = unix_ft(1050000000);
    T_EQ(CALL(kernel32, SetFileTime, h, &creation, NULL, &write2), TRUE);
    T_REQUIRE(rt_file_info_by_handle(h, &after));
    T_TRUE(same(&after.ftCreationTime, &creation));
    T_TRUE(same(&after.ftLastWriteTime, &write2));
    T_TRUE(same(&after.ftLastAccessTime, &before.ftLastAccessTime));

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    h2 = open_rw("C:\\Titanfall2\\set.bin");
    T_REQUIRE(h2 != NULL && h2 != INVALID_HANDLE_VALUE);
    T_TRUE(rt_handle_close(h2));
    at = unix_ft(1300000000);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, SetFileTime, h2, NULL, &at, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetFileTime, h, NULL, &at, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops (RT-TRAPS), after the checks of the last case.  Each ends in a fatal
     * report, proved in a forked child.  NUL is a character device. */
    ff = ft_of(0xFFFFFFFF, 0xFFFFFFFF);
    T_TRAPS(CALL(kernel32, SetFileTime, h, &ff, NULL, NULL), "a creation time 0xffffffffffffffff");
    T_TRAPS(CALL(kernel32, SetFileTime, h, NULL, &ff, NULL), "an access time 0xffffffffffffffff");
    T_TRAPS(CALL(kernel32, SetFileTime, h, NULL, NULL, &ff), "a write time 0xffffffffffffffff");
    pre = unix_ft(-1);
    T_TRAPS(CALL(kernel32, SetFileTime, h, &pre, NULL, NULL), "a creation time 0x19db1ded4a5e980");
    T_TRAPS(CALL(kernel32, SetFileTime, h, NULL, NULL, &pre), "a write time 0x19db1ded4a5e980");
    nul = open_rw("NUL");
    T_REQUIRE(nul != NULL && nul != INVALID_HANDLE_VALUE);
    T_TRAPS(CALL(kernel32, SetFileTime, nul, NULL, &at, NULL), "a character device");
    T_TRUE(rt_handle_close(nul));
    T_TRUE(rt_handle_close(h));
}
