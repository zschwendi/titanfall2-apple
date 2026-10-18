#include <fcntl.h>
#include <stdio.h>
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

static HANDLE open_win(const char *win, DWORD access, DWORD disp, DWORD fa)
{
    return rt_file_create(RT_NAME_W(W(win)), access, SH, disp, fa, NULL);
}

TEST(kernel32, GetFileSizeEx)
{
    HANDLE h, eh, big, nh, dh;
    LARGE_INTEGER sz;

    put("gfsx.bin", "0123456789", 0644);            /* 10 bytes */
    put("gfsxempty.bin", "", 0644);                 /* empty */
    h = open_win("C:\\Titanfall2\\gfsx.bin", RD, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(h != BAD);

    T_CASE("reports the size of a 10-byte file and of an empty file");
    sz.QuadPart = 0x5A5A5A5A5A5A5A5ALL;             /* every byte must be replaced */
    T_EQ(CALL(kernel32, GetFileSizeEx, h, &sz), TRUE);
    T_EQ(sz.QuadPart, 10);
    eh = open_win("C:\\Titanfall2\\gfsxempty.bin", RD, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(eh != BAD);
    sz.QuadPart = 0x5A5A5A5A5A5A5A5ALL;
    T_EQ(CALL(kernel32, GetFileSizeEx, eh, &sz), TRUE);
    T_EQ(sz.QuadPart, 0);
    T_TRUE(rt_handle_close(eh));

    T_CASE("reports a size above 4 GiB and the size 0xFFFFFFFF in full");
    big = open_win("C:\\Titanfall2\\gfsxbig.bin", RD | WR, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(big != BAD);
    T_TRUE(rt_file_seek(big, 0x100000005LL, NULL, FILE_BEGIN));
    T_TRUE(rt_file_set_eof(big));                   /* sparse: nothing is written */
    sz.QuadPart = 0x5A5A5A5A5A5A5A5ALL;
    T_EQ(CALL(kernel32, GetFileSizeEx, big, &sz), TRUE);
    T_EQ(sz.QuadPart, 0x100000005LL);
    T_TRUE(rt_file_seek(big, 0xFFFFFFFFLL, NULL, FILE_BEGIN));
    T_TRUE(rt_file_set_eof(big));
    sz.QuadPart = 0x5A5A5A5A5A5A5A5ALL;
    T_EQ(CALL(kernel32, GetFileSizeEx, big, &sz), TRUE);
    T_EQ(sz.QuadPart, 0xFFFFFFFFLL);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    nh = open_win("C:\\Titanfall2\\gfsx.bin", RD, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(nh != BAD);
    T_TRUE(rt_handle_close(nh));
    sz.QuadPart = 0x4444;
    T_EQ(CALL(kernel32, GetFileSizeEx, nh, &sz), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetFileSizeEx, NULL, &sz), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetFileSizeEx, BAD, &sz), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetFileSizeEx, (HANDLE)(uintptr_t)4, &sz), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileSizeEx, h, &sz), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: the core's stops (rt/file.h), after the checks of the last case. */
    T_TRAPS(CALL(kernel32, GetFileSizeEx, h, NULL), "size NULL");
    T_TRAPS(CALL(kernel32, GetFileSizeEx, rt_std_get(STD_OUTPUT_HANDLE), &sz), "a character device or a directory handle");

    if (mkdir(I("gfsxdir"), 0755)) rt_test_fail(__FILE__, __LINE__, "mkdir gfsxdir");
    dh = open_win("C:\\Titanfall2\\gfsxdir", GENERIC_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    T_REQUIRE(dh != BAD);
    T_TRAPS(CALL(kernel32, GetFileSizeEx, dh, &sz), "a character device or a directory handle");

    T_TRUE(rt_handle_close(big));
    if (unlink(I("gfsxbig.bin"))) rt_test_fail(__FILE__, __LINE__, "unlink gfsxbig.bin");
}
