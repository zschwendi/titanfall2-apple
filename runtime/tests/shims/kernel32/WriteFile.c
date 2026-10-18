/* WriteFile through the guest ABI, against files the test makes under the install root.  Each
 * write is checked by what the file holds: bytes are read back with the core's rt_file_read
 * after rt_file_seek (rt/file.h), and sizes are read from the host.  Handles come from
 * rt_file_create and go with rt_handle_close, as in runtime/tests/core/file_open.c.  Overlapped
 * writes are another unit's and are not tested here. */
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
static long long size_of(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) ? -1 : (long long)st.st_size;
}
static HANDLE open_h(const char *win, DWORD access, DWORD disp) {
    return rt_file_create(RT_NAME_W(W(win)), access, SH, disp, FILE_ATTRIBUTE_NORMAL, NULL);
}

TEST(kernel32, WriteFile)
{
    static const BYTE tail[7] = { 0, 0, 0, 0, 0, 'x', 'y' };    /* the gap of a seek, then "xy" */
    BYTE rb[16];
    DWORD n, got;
    LONGLONG pos;
    HANDLE h, hc, ro, dh;

    h = open_h("C:\\Titanfall2\\wf_out.bin", RD | WR, CREATE_ALWAYS);
    T_REQUIRE(h != NULL && h != BAD);

    T_CASE("synchronous write to a file");
    n = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, WriteFile, h, "hello", 5, &n, NULL), TRUE);
    T_EQ(n, 5);
    T_EQ(size_of("wf_out.bin"), 5);

    T_CASE("bytes written through one handle are read back");
    T_EQ(rt_file_seek(h, 0, NULL, FILE_BEGIN), TRUE);
    memset(rb, 0xAA, sizeof rb);
    got = 0xFFFFFFFF;
    T_EQ(rt_file_read(h, rb, 5, &got, NULL), TRUE);
    T_EQ(got, 5);
    T_TRUE(memcmp(rb, "hello", 5) == 0);
    T_EQ(rb[5], 0xAA);

    T_CASE("zero-length write does not truncate or extend the file");
    n = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, WriteFile, h, "xyz", 0, &n, NULL), TRUE);
    T_EQ(n, 0);
    T_EQ(size_of("wf_out.bin"), 5);
    pos = -1;
    T_EQ(rt_file_seek(h, 0, &pos, FILE_CURRENT), TRUE);
    T_EQ(pos, 5);                                   /* the position did not move either */

    T_CASE("a write beyond the end extends the file");
    T_EQ(rt_file_seek(h, 10, NULL, FILE_BEGIN), TRUE);  /* 5 bytes long: bytes 5 to 9 are a gap */
    n = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, WriteFile, h, "xy", 2, &n, NULL), TRUE);
    T_EQ(n, 2);
    T_EQ(size_of("wf_out.bin"), 12);
    T_EQ(rt_file_seek(h, 5, NULL, FILE_BEGIN), TRUE);
    memset(rb, 0xAA, sizeof rb);
    got = 0xFFFFFFFF;
    T_EQ(rt_file_read(h, rb, 7, &got, NULL), TRUE);
    T_EQ(got, 7);
    T_TRUE(memcmp(rb, tail, 7) == 0);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    n = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, WriteFile, NULL, "a", 1, &n, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    hc = open_h("C:\\Titanfall2\\wf_out.bin", RD | WR, OPEN_EXISTING);
    T_REQUIRE(hc != NULL && hc != BAD);
    T_TRUE(rt_handle_close(hc));
    T_EQ(CALL(kernel32, WriteFile, hc, "a", 1, &n, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(size_of("wf_out.bin"), 12);                /* nothing was written */

    T_CASE("a handle opened for reading only fails with ERROR_ACCESS_DENIED");
    put("wf_rd.bin", "keep", 0644);
    ro = open_h("C:\\Titanfall2\\wf_rd.bin", RD, OPEN_EXISTING);
    T_REQUIRE(ro != NULL && ro != BAD);
    n = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, WriteFile, ro, "zz", 2, &n, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(n, 0);
    T_EQ(size_of("wf_rd.bin"), 4);                  /* the file is as it was */
    T_TRUE(rt_handle_close(ro));

    T_CASE("the count is set to 0 before a failure");
    n = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, WriteFile, hc, "a", 1, &n, NULL), FALSE);
    T_EQ(n, 0);
    n = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, WriteFile, NULL, "a", 1, &n, NULL), FALSE);
    T_EQ(n, 0);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, WriteFile, h, "!", 1, &n, NULL), TRUE);
    T_EQ(n, 1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, WriteFile, h, "", 0, &n, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops (RT-TRAPS), after the checks of the last case.  A directory handle is
     * the install root, opened with FILE_FLAG_BACKUP_SEMANTICS. */
    dh = rt_file_create(RT_NAME_W(W("C:\\Titanfall2")), RD, SH, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    T_TRUE(dh != NULL && dh != BAD);
    T_TRAPS(CALL(kernel32, WriteFile, h, "a", 1, NULL, NULL), "put NULL and no OVERLAPPED");
    T_TRAPS(CALL(kernel32, WriteFile, dh, "a", 1, &n, NULL), "a directory handle");
    T_TRUE(rt_handle_close(dh));
    T_TRUE(rt_handle_close(h));
}
