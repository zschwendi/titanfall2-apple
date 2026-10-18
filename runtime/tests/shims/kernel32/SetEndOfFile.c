#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"

/* Files are made under the install root with the host, and opened with the core's own call
 * (rt_file_create), as in runtime/tests/core/file_open.c. */
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

static HANDLE open_win(const char *win, DWORD access, DWORD disp, DWORD fa)
{
    return rt_file_create(RT_NAME_W(W(win)), access, SH, disp, fa, NULL);
}

/* The position and the size, read through the core (rt_file_seek with distance 0, rt_file_size). */
static LONGLONG where(HANDLE h)
{
    LONGLONG q = -1;
    T_TRUE(rt_file_seek(h, 0, &q, FILE_CURRENT));
    return q;
}
static LONGLONG size_now(HANDLE h)
{
    LONGLONG s = -1;
    T_TRUE(rt_file_size(h, &s));
    return s;
}

TEST(kernel32, SetEndOfFile)
{
    HANDLE h, rh, nh, dh, nul;

    put("seof.bin", "0123456789", 0644);                /* 10 bytes */
    h = open_win("C:\\Titanfall2\\seof.bin", RD | WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(h != BAD);

    T_CASE("cuts the file at the position, which does not move");
    T_TRUE(rt_file_seek(h, 4, NULL, FILE_BEGIN));
    T_EQ(CALL(kernel32, SetEndOfFile, h), TRUE);
    T_EQ(size_now(h), 4);
    T_EQ(where(h), 4);

    T_CASE("extends the file to a position beyond the end, which does not move the position");
    T_TRUE(rt_file_seek(h, 12, NULL, FILE_BEGIN));
    T_EQ(CALL(kernel32, SetEndOfFile, h), TRUE);
    T_EQ(size_now(h), 12);
    T_EQ(where(h), 12);

    T_CASE("a handle that cannot write fails with ERROR_ACCESS_DENIED");
    rh = open_win("C:\\Titanfall2\\seof.bin", RD, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(rh != BAD);
    T_TRUE(rt_file_seek(rh, 2, NULL, FILE_BEGIN));
    T_EQ(CALL(kernel32, SetEndOfFile, rh), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(size_now(h), 12);
    T_EQ(where(rh), 2);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    nh = open_win("C:\\Titanfall2\\seof.bin", RD | WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(nh != BAD);
    T_TRUE(rt_handle_close(nh));
    T_EQ(CALL(kernel32, SetEndOfFile, nh), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetEndOfFile, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetEndOfFile, BAD), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetEndOfFile, (HANDLE)(uintptr_t)4), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(size_now(h), 12);

    T_CASE("success does not change last error");
    T_TRUE(rt_file_seek(h, 6, NULL, FILE_BEGIN));
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetEndOfFile, h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(size_now(h), 6);

    /* RT-TRAPS: the core's stops (rt/file.h: a character device and a directory handle).
     * After the checks of the last case. */
    if (mkdir(I("seofdir"), 0755)) rt_test_fail(__FILE__, __LINE__, "mkdir seofdir");
    dh = open_win("C:\\Titanfall2\\seofdir", GENERIC_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    T_REQUIRE(dh != BAD);
    T_TRAPS(CALL(kernel32, SetEndOfFile, dh), "a character device or a directory handle");

    nul = open_win("NUL", RD | WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(nul != BAD);
    T_TRAPS(CALL(kernel32, SetEndOfFile, nul), "a character device or a directory handle");
}
