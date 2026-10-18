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

/* The position, read through the core (rt_file_seek with distance 0). */
static LONGLONG where(HANDLE h)
{
    LONGLONG q = -1;
    T_TRUE(rt_file_seek(h, 0, &q, FILE_CURRENT));
    return q;
}

TEST(kernel32, SetFilePointer)
{
    HANDLE h, nh, dh, nul;
    LONG hi;

    put("sfp.bin", "0123456789", 0644);                 /* 10 bytes */
    h = open_win("C:\\Titanfall2\\sfp.bin", RD | WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(h != BAD);

    T_CASE("a NULL lpDistanceToMoveHigh moves the position from the beginning, the current position or the end and returns the new position");
    T_EQ(CALL(kernel32, SetFilePointer, h, 4, NULL, FILE_BEGIN), 4);
    T_EQ(where(h), 4);
    T_EQ(CALL(kernel32, SetFilePointer, h, -1, NULL, FILE_CURRENT), 3);
    T_EQ(where(h), 3);
    T_EQ(CALL(kernel32, SetFilePointer, h, 0, NULL, FILE_END), 10);     /* 10 bytes less 0 */
    T_EQ(where(h), 10);

    T_CASE("with lpDistanceToMoveHigh the distance is 64 bits and the high part of the new position is stored there");
    hi = 1;
    T_EQ(CALL(kernel32, SetFilePointer, h, 0x10, &hi, FILE_BEGIN), 0x10);   /* position 0x100000010 */
    T_EQ(hi, 1);
    T_EQ(where(h), 0x100000010LL);
    hi = 0;                                             /* the input high part is 0 this time */
    T_EQ(CALL(kernel32, SetFilePointer, h, 0, &hi, FILE_CURRENT), 0x10);
    T_EQ(hi, 1);                                        /* so the 1 can only come from the shim */
    hi = 0;
    T_EQ(CALL(kernel32, SetFilePointer, h, 3, &hi, FILE_BEGIN), 3);
    T_EQ(hi, 0);
    T_EQ(where(h), 3);

    T_CASE("a position before the start fails with ERROR_NEGATIVE_SEEK, returns INVALID_SET_FILE_POINTER and leaves the position and the high value unchanged");
    hi = -1;                                            /* with -10 the distance is -10 */
    T_EQ(CALL(kernel32, SetFilePointer, h, -10, &hi, FILE_CURRENT), INVALID_SET_FILE_POINTER);
    T_EQ(T_LAST_ERROR(), ERROR_NEGATIVE_SEEK);
    T_EQ(hi, -1);
    T_EQ(where(h), 3);
    T_EQ(CALL(kernel32, SetFilePointer, h, -11, NULL, FILE_END), INVALID_SET_FILE_POINTER);   /* 10 less 11 */
    T_EQ(T_LAST_ERROR(), ERROR_NEGATIVE_SEEK);
    T_EQ(where(h), 3);

    T_CASE("a position of 0xFFFFFFFF is returned as it is and last error is then ERROR_SUCCESS");
    CALL(kernel32, SetLastError, 0x1234);
    hi = 0;
    T_EQ(CALL(kernel32, SetFilePointer, h, -1, &hi, FILE_BEGIN), 0xFFFFFFFF);   /* low 32 bits -1, high 0 */
    T_EQ(hi, 0);
    T_EQ(where(h), 0xFFFFFFFFLL);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetFilePointer, h, 0x7FFFFFFF, NULL, FILE_BEGIN), 0x7FFFFFFF);
    T_EQ(CALL(kernel32, SetFilePointer, h, 0x7FFFFFFF, NULL, FILE_CURRENT), 0xFFFFFFFE);
    T_EQ(CALL(kernel32, SetFilePointer, h, 1, NULL, FILE_CURRENT), 0xFFFFFFFF);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("a move method other than the three fails with ERROR_INVALID_PARAMETER");
    hi = 2;
    T_EQ(CALL(kernel32, SetFilePointer, h, 1, &hi, 3), INVALID_SET_FILE_POINTER);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(hi, 2);
    T_EQ(CALL(kernel32, SetFilePointer, h, 1, NULL, 3), INVALID_SET_FILE_POINTER);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(where(h), 0xFFFFFFFFLL);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    nh = open_win("C:\\Titanfall2\\sfp.bin", RD | WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(nh != BAD);
    T_TRUE(rt_handle_close(nh));
    hi = 7;
    T_EQ(CALL(kernel32, SetFilePointer, nh, 0, &hi, FILE_BEGIN), INVALID_SET_FILE_POINTER);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(hi, 7);
    T_EQ(CALL(kernel32, SetFilePointer, NULL, 0, NULL, FILE_BEGIN), INVALID_SET_FILE_POINTER);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetFilePointer, BAD, 0, NULL, FILE_BEGIN), INVALID_SET_FILE_POINTER);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetFilePointer, (HANDLE)(uintptr_t)4, 0, NULL, FILE_BEGIN), INVALID_SET_FILE_POINTER);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(where(h), 0xFFFFFFFFLL);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetFilePointer, h, 2, NULL, FILE_BEGIN), 2);
    T_EQ(T_LAST_ERROR(), 0x1234);
    hi = 0;
    T_EQ(CALL(kernel32, SetFilePointer, h, 2, &hi, FILE_BEGIN), 2);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: the shim's own refusal, then the core's stops (rt/file.h: a character device and a
     * directory handle).  After the checks of the last case.  The position is set first, outside
     * the traps, since the statement of each trap runs in a forked child. */
    hi = 1;
    T_EQ(CALL(kernel32, SetFilePointer, h, 0, &hi, FILE_BEGIN), 0);     /* position 0x100000000 */
    T_EQ(where(h), 0x100000000LL);
    T_TRAPS(CALL(kernel32, SetFilePointer, h, 0, NULL, FILE_CURRENT), "SetFilePointer with NULL lpDistanceToMoveHigh and a position above 0xFFFFFFFF");

    if (mkdir(I("sfpdir"), 0755)) rt_test_fail(__FILE__, __LINE__, "mkdir sfpdir");
    dh = open_win("C:\\Titanfall2\\sfpdir", GENERIC_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    T_REQUIRE(dh != BAD);
    T_TRAPS(CALL(kernel32, SetFilePointer, dh, 0, NULL, FILE_CURRENT), "a character device or a directory handle");

    nul = open_win("NUL", RD | WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(nul != BAD);
    T_TRAPS(CALL(kernel32, SetFilePointer, nul, 0, NULL, FILE_CURRENT), "a character device or a directory handle");
}
