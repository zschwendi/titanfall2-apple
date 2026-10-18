/* ReadFile through the guest ABI, against files the test makes under the install root.  The
 * bytes are written by the host (put), so every read is checked against bytes the test chose.
 * Handles come from rt_file_create and go with rt_handle_close, as in runtime/tests/core/
 * file_open.c.  Overlapped reads are another unit's and are not tested here. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"

#define RD   GENERIC_READ
#define WR   GENERIC_WRITE
#define SH   (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define BAD  INVALID_HANDLE_VALUE

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
static HANDLE open_h(const char *win, DWORD access, DWORD disp) {
    return rt_file_create(RT_NAME_W(W(win)), access, SH, disp, FILE_ATTRIBUTE_NORMAL, NULL);
}

TEST(kernel32, ReadFile)
{
    BYTE buf[16];
    DWORD got;
    HANDLE h, hc, wo, dh;

    put("rf_data.bin", "0123456789", 0644);
    h = open_h("C:\\Titanfall2\\rf_data.bin", RD, OPEN_EXISTING);
    T_REQUIRE(h != NULL && h != BAD);

    T_CASE("synchronous read from a file");
    memset(buf, 0xAA, sizeof buf);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, ReadFile, h, buf, 4, &got, NULL), TRUE);
    T_EQ(got, 4);
    T_TRUE(memcmp(buf, "0123", 4) == 0);
    T_EQ(buf[4], 0xAA);                             /* nothing past the count is written */

    T_CASE("zero-length read");
    memset(buf, 0xAA, sizeof buf);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, ReadFile, h, buf, 0, &got, NULL), TRUE);
    T_EQ(got, 0);
    T_EQ(buf[0], 0xAA);
    T_EQ(CALL(kernel32, ReadFile, h, buf, 2, &got, NULL), TRUE);
    T_EQ(got, 2);                                   /* the position did not move: "45" */
    T_TRUE(memcmp(buf, "45", 2) == 0);

    T_CASE("short read at the end of the file");
    memset(buf, 0xAA, sizeof buf);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, ReadFile, h, buf, 16, &got, NULL), TRUE);   /* 4 bytes remain */
    T_EQ(got, 4);
    T_TRUE(memcmp(buf, "6789", 4) == 0);
    T_EQ(buf[4], 0xAA);

    T_CASE("end of file");
    memset(buf, 0xAA, sizeof buf);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, ReadFile, h, buf, 4, &got, NULL), TRUE);   /* at the end: TRUE, 0 bytes */
    T_EQ(got, 0);
    T_EQ(buf[0], 0xAA);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, ReadFile, NULL, buf, 4, &got, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    hc = open_h("C:\\Titanfall2\\rf_data.bin", RD, OPEN_EXISTING);
    T_REQUIRE(hc != NULL && hc != BAD);
    T_TRUE(rt_handle_close(hc));
    T_EQ(CALL(kernel32, ReadFile, hc, buf, 4, &got, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("a handle opened for writing only fails with ERROR_ACCESS_DENIED");
    wo = open_h("C:\\Titanfall2\\rf_data.bin", WR, OPEN_EXISTING);
    T_REQUIRE(wo != NULL && wo != BAD);
    memset(buf, 0xAA, sizeof buf);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, ReadFile, wo, buf, 4, &got, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(got, 0);
    T_EQ(buf[0], 0xAA);                             /* nothing was read */
    T_TRUE(rt_handle_close(wo));

    T_CASE("the count is set to 0 before a failure");
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, ReadFile, hc, buf, 4, &got, NULL), FALSE);
    T_EQ(got, 0);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, ReadFile, NULL, buf, 4, &got, NULL), FALSE);
    T_EQ(got, 0);

    T_CASE("does not change last error on success");
    T_TRUE(rt_file_seek(h, 0, NULL, FILE_BEGIN));
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, ReadFile, h, buf, 1, &got, NULL), TRUE);
    T_EQ(got, 1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, ReadFile, h, buf, 0, &got, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops (RT-TRAPS), after the checks of the last case.  A directory handle is
     * the install root, opened with FILE_FLAG_BACKUP_SEMANTICS. */
    dh = rt_file_create(RT_NAME_W(W("C:\\Titanfall2")), RD, SH, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    T_TRUE(dh != NULL && dh != BAD);
    T_TRAPS(CALL(kernel32, ReadFile, h, buf, 1, NULL, NULL), "got NULL and no OVERLAPPED");
    T_TRAPS(CALL(kernel32, ReadFile, dh, buf, 1, &got, NULL), "a directory handle");
    T_TRUE(rt_handle_close(dh));
    T_TRUE(rt_handle_close(h));
}
