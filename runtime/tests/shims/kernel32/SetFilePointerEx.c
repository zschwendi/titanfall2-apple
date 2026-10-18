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

TEST(kernel32, SetFilePointerEx)
{
    HANDLE h, nh, dh, nul;
    LARGE_INTEGER d, pos;

    put("sfp.bin", "0123456789", 0644);                 /* 10 bytes */
    h = open_win("C:\\Titanfall2\\sfp.bin", RD | WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(h != BAD);

    T_CASE("moves the position from the beginning, the current position or the end, and stores the new position");
    pos.QuadPart = 0x5A5A5A5A5A5A5A5ALL;                /* every byte must be replaced */
    d.QuadPart = 3;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, &pos, FILE_BEGIN), TRUE);
    T_EQ(pos.QuadPart, 3);
    T_EQ(where(h), 3);
    d.QuadPart = 2;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, &pos, FILE_CURRENT), TRUE);
    T_EQ(pos.QuadPart, 5);
    T_EQ(where(h), 5);
    d.QuadPart = -4;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, &pos, FILE_END), TRUE);
    T_EQ(pos.QuadPart, 6);                              /* 10 bytes less 4 */
    T_EQ(where(h), 6);

    T_CASE("a NULL lpNewFilePointer moves the position and stores nothing");
    d.QuadPart = 1;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, NULL, FILE_BEGIN), TRUE);
    T_EQ(where(h), 1);

    T_CASE("a position before the start fails with ERROR_NEGATIVE_SEEK and leaves the position and the stored value unchanged");
    d.QuadPart = 2;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, NULL, FILE_BEGIN), TRUE);
    pos.QuadPart = 0x4444;
    d.QuadPart = -3;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, &pos, FILE_CURRENT), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NEGATIVE_SEEK);
    T_EQ(pos.QuadPart, 0x4444);
    T_EQ(where(h), 2);
    d.QuadPart = -11;                                   /* 10 bytes less 11 */
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, &pos, FILE_END), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_NEGATIVE_SEEK);
    T_EQ(pos.QuadPart, 0x4444);
    T_EQ(where(h), 2);

    T_CASE("a position beyond the end is allowed and does not change the file size");
    T_EQ(size_now(h), 10);
    d.QuadPart = 100;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, &pos, FILE_BEGIN), TRUE);
    T_EQ(pos.QuadPart, 100);
    d.QuadPart = 5;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, &pos, FILE_END), TRUE);
    T_EQ(pos.QuadPart, 15);
    T_EQ(where(h), 15);
    T_EQ(size_now(h), 10);

    T_CASE("a move method other than the three fails with ERROR_INVALID_PARAMETER");
    d.QuadPart = 1;
    pos.QuadPart = 0x4444;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, &pos, 3), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(pos.QuadPart, 0x4444);
    T_EQ(where(h), 15);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    nh = open_win("C:\\Titanfall2\\sfp.bin", RD | WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(nh != BAD);
    T_TRUE(rt_handle_close(nh));
    d.QuadPart = 0;
    pos.QuadPart = 0x4444;
    T_EQ(CALL(kernel32, SetFilePointerEx, nh, d, &pos, FILE_BEGIN), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetFilePointerEx, NULL, d, &pos, FILE_BEGIN), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetFilePointerEx, BAD, d, &pos, FILE_BEGIN), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetFilePointerEx, (HANDLE)(uintptr_t)4, d, &pos, FILE_BEGIN), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(pos.QuadPart, 0x4444);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    d.QuadPart = 0;
    T_EQ(CALL(kernel32, SetFilePointerEx, h, d, &pos, FILE_BEGIN), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: the shim's own refusal, then the core's stops (rt/file.h: a character device
     * and a directory handle).  After the checks of the last case. */
    d.QuadPart = -1;
    T_TRAPS(CALL(kernel32, SetFilePointerEx, h, d, &pos, FILE_BEGIN), "FILE_BEGIN with a negative distance");

    if (mkdir(I("sfpdir"), 0755)) rt_test_fail(__FILE__, __LINE__, "mkdir sfpdir");
    dh = open_win("C:\\Titanfall2\\sfpdir", GENERIC_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    T_REQUIRE(dh != BAD);
    d.QuadPart = 0;
    T_TRAPS(CALL(kernel32, SetFilePointerEx, dh, d, &pos, FILE_CURRENT), "a character device or a directory handle");

    nul = open_win("NUL", RD | WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(nul != BAD);
    T_TRAPS(CALL(kernel32, SetFilePointerEx, nul, d, &pos, FILE_CURRENT), "a character device or a directory handle");
}
