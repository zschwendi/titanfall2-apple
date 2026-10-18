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
static HANDLE open_win(const char *win, DWORD access, DWORD disp, DWORD fa) {
    return rt_file_create(RT_NAME_W(W(win)), access, SH, disp, fa, NULL);
}

TEST(kernel32, FlushFileBuffers)
{
    HANDLE h, ro, nul, dir;

    T_CASE("flushes a file opened for writing");
    h = open_win("C:\\Titanfall2\\flush-rw.bin", WR, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(h != NULL && h != BAD);
    T_EQ(CALL(kernel32, FlushFileBuffers, h), TRUE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FlushFileBuffers, h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a handle opened for reading only fails with ERROR_ACCESS_DENIED");
    put("flush-ro.bin", "data", 0644);
    ro = open_win("C:\\Titanfall2\\flush-ro.bin", RD, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(ro != NULL && ro != BAD);
    T_EQ(CALL(kernel32, FlushFileBuffers, ro), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_TRUE(rt_handle_close(ro));

    T_CASE("NULL and a handle that is not open fail with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, FlushFileBuffers, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, FlushFileBuffers, BAD), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, FlushFileBuffers, (HANDLE)(uintptr_t)4), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(rt_handle_close(h));
    T_EQ(CALL(kernel32, FlushFileBuffers, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* The stops (RT-TRAPS): no case of their own, after the checks of the last case. */
    nul = open_win("NUL", WR, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(nul != NULL && nul != BAD);
    T_TRAPS(CALL(kernel32, FlushFileBuffers, nul), "a character device");

    T_REQUIRE(mkdir(I("flushdir"), 0755) == 0);
    dir = open_win("C:\\Titanfall2\\flushdir", RD, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    T_REQUIRE(dir != NULL && dir != BAD);
    T_TRAPS(CALL(kernel32, FlushFileBuffers, dir), "a directory handle");
}
