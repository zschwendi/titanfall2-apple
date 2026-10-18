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

TEST(kernel32, GetFileSize)
{
    HANDLE h, eh, big, nh, dh;
    DWORD hi;

    put("gfs.bin", "0123456789", 0644);            /* 10 bytes */
    put("gfsempty.bin", "", 0644);                 /* empty */
    h = open_win("C:\\Titanfall2\\gfs.bin", RD, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(h != BAD);

    T_CASE("returns the size of a 10-byte file, with high part 0");
    hi = 0x5A5A5A5A;                               /* every byte must be replaced */
    T_EQ(CALL(kernel32, GetFileSize, h, &hi), 10);
    T_EQ(hi, 0);
    T_EQ(CALL(kernel32, GetFileSize, h, NULL), 10);

    T_CASE("returns 0 for an empty file");
    eh = open_win("C:\\Titanfall2\\gfsempty.bin", RD, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(eh != BAD);
    hi = 0x5A5A5A5A;
    T_EQ(CALL(kernel32, GetFileSize, eh, &hi), 0);
    T_EQ(hi, 0);
    T_TRUE(rt_handle_close(eh));

    T_CASE("returns the low part and stores the high part of a size above 4 GiB");
    big = open_win("C:\\Titanfall2\\gfsbig.bin", RD | WR, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(big != BAD);
    T_TRUE(rt_file_seek(big, 0x100000005LL, NULL, FILE_BEGIN));
    T_TRUE(rt_file_set_eof(big));                  /* sparse: nothing is written */
    hi = 0x5A5A5A5A;
    T_EQ(CALL(kernel32, GetFileSize, big, &hi), 5);
    T_EQ(hi, 1);

    T_CASE("returns the low part of a size above 4 GiB with a NULL lpFileSizeHigh, and ERROR_SUCCESS for a low part of 0xFFFFFFFF");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileSize, big, NULL), 5);
    T_EQ(T_LAST_ERROR(), 0x1234);                  /* success leaves last error alone */
    T_TRUE(rt_file_seek(big, 0x1FFFFFFFFLL, NULL, FILE_BEGIN));
    T_TRUE(rt_file_set_eof(big));                  /* low part 0xFFFFFFFF */
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileSize, big, NULL), INVALID_FILE_SIZE);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("a size of 0xFFFFFFFF bytes returns INVALID_FILE_SIZE and sets last error to ERROR_SUCCESS");
    T_TRUE(rt_file_seek(big, 0xFFFFFFFFLL, NULL, FILE_BEGIN));
    T_TRUE(rt_file_set_eof(big));
    CALL(kernel32, SetLastError, 0x1234);
    hi = 0x5A5A5A5A;
    T_EQ(CALL(kernel32, GetFileSize, big, &hi), INVALID_FILE_SIZE);
    T_EQ(hi, 0);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileSize, big, NULL), INVALID_FILE_SIZE);
    T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);

    T_CASE("a handle that is not open fails with INVALID_FILE_SIZE and ERROR_INVALID_HANDLE and stores no high part");
    nh = open_win("C:\\Titanfall2\\gfs.bin", RD, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(nh != BAD);
    T_TRUE(rt_handle_close(nh));
    hi = 0x7777;
    T_EQ(CALL(kernel32, GetFileSize, nh, &hi), INVALID_FILE_SIZE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(hi, 0x7777);
    T_EQ(CALL(kernel32, GetFileSize, NULL, &hi), INVALID_FILE_SIZE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetFileSize, BAD, &hi), INVALID_FILE_SIZE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetFileSize, (HANDLE)(uintptr_t)4, &hi), INVALID_FILE_SIZE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetFileSize, nh, NULL), INVALID_FILE_SIZE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(hi, 0x7777);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    hi = 0x5A5A5A5A;
    T_EQ(CALL(kernel32, GetFileSize, h, &hi), 10);
    T_EQ(hi, 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: the core's stops (rt/file.h), after the checks of the last case. */
    T_TRAPS(CALL(kernel32, GetFileSize, rt_std_get(STD_OUTPUT_HANDLE), &hi), "a character device or a directory handle");

    if (mkdir(I("gfsdir"), 0755)) rt_test_fail(__FILE__, __LINE__, "mkdir gfsdir");
    dh = open_win("C:\\Titanfall2\\gfsdir", GENERIC_READ, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    T_REQUIRE(dh != BAD);
    T_TRAPS(CALL(kernel32, GetFileSize, dh, &hi), "a character device or a directory handle");

    T_TRUE(rt_handle_close(big));
    if (unlink(I("gfsbig.bin"))) rt_test_fail(__FILE__, __LINE__, "unlink gfsbig.bin");
}
