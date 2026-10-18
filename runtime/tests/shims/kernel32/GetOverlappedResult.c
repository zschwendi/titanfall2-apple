/* GetOverlappedResult through the guest ABI, against a file the test makes under the install
 * root.  Each result is made by rt_file_read with an OVERLAPPED (the core's overlapped path),
 * so every check reads a result of a transfer the test chose; the handle is only checked.
 * Handles come from rt_file_create and go with rt_handle_close, as in ReadFile's test. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"
#include "rt/win/file.h"

#define RD   GENERIC_READ
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

TEST(kernel32, GetOverlappedResult)
{
    OVERLAPPED ov;
    BYTE buf[16];
    DWORD got;
    HANDLE h, hc;

    put("gor_data.bin", "0123456789", 0644);
    h = open_h("C:\\Titanfall2\\gor_data.bin", RD, OPEN_EXISTING);
    T_REQUIRE(h != NULL && h != BAD);

    T_CASE("completed read: TRUE and the byte count, with bWait FALSE or TRUE");
    memset(&ov, 0, sizeof ov);
    T_TRUE(rt_file_read(h, buf, 4, NULL, &ov));     /* Internal = STATUS_SUCCESS, InternalHigh = 4 */
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, GetOverlappedResult, h, &ov, &got, FALSE), TRUE);
    T_EQ(got, 4);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, GetOverlappedResult, h, &ov, &got, TRUE), TRUE);
    T_EQ(got, 4);

    T_CASE("end of file: FALSE, 0 bytes, ERROR_HANDLE_EOF");
    memset(&ov, 0, sizeof ov);
    ov.Offset = 10;                                 /* at the end of the 10-byte file */
    T_EQ(rt_file_read(h, buf, 4, NULL, &ov), FALSE);   /* Internal = STATUS_END_OF_FILE */
    CALL(kernel32, SetLastError, 0x1234);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, GetOverlappedResult, h, &ov, &got, FALSE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_HANDLE_EOF);
    T_EQ(got, 0);
    CALL(kernel32, SetLastError, 0x1234);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, GetOverlappedResult, h, &ov, &got, TRUE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_HANDLE_EOF);
    T_EQ(got, 0);

    T_CASE("pending with bWait FALSE: FALSE, ERROR_IO_INCOMPLETE");
    memset(&ov, 0, sizeof ov);
    ov.Internal = STATUS_PENDING;                   /* the guest's own initial value */
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetOverlappedResult, h, &ov, &got, FALSE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_IO_INCOMPLETE);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    memset(&ov, 0, sizeof ov);
    T_TRUE(rt_file_read(h, buf, 4, NULL, &ov));     /* a completed result: only the handle is wrong */
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetOverlappedResult, NULL, &ov, &got, FALSE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetOverlappedResult, BAD, &ov, &got, TRUE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    hc = open_h("C:\\Titanfall2\\gor_data.bin", RD, OPEN_EXISTING);
    T_REQUIRE(hc != NULL && hc != BAD);
    T_TRUE(rt_handle_close(hc));
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetOverlappedResult, hc, &ov, &got, FALSE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("does not change last error on success");
    memset(&ov, 0, sizeof ov);
    T_TRUE(rt_file_read(h, buf, 4, NULL, &ov));
    CALL(kernel32, SetLastError, 0x1234);
    got = 0xFFFFFFFF;
    T_EQ(CALL(kernel32, GetOverlappedResult, h, &ov, &got, FALSE), TRUE);
    T_EQ(got, 4);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, GetOverlappedResult, h, &ov, &got, TRUE), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops (RT-TRAPS), after the checks of the last case. */
    memset(&ov, 0, sizeof ov);
    ov.Internal = STATUS_PENDING;
    T_TRAPS(CALL(kernel32, GetOverlappedResult, h, &ov, &got, TRUE), "bWait TRUE with Internal STATUS_PENDING");
    T_TRAPS(CALL(kernel32, GetOverlappedResult, h, NULL, &got, FALSE), "lpOverlapped NULL");
    memset(&ov, 0, sizeof ov);
    T_TRUE(rt_file_read(h, buf, 4, NULL, &ov));     /* a completed result, so only the count is missing */
    T_TRAPS(CALL(kernel32, GetOverlappedResult, h, &ov, NULL, FALSE), "lpNumberOfBytesTransferred NULL");
    memset(&ov, 0, sizeof ov);
    ov.Internal = 0xC0000001;                       /* STATUS_UNSUCCESSFUL: what file_ov.c stores after its own failed call */
    T_TRAPS(CALL(kernel32, GetOverlappedResult, h, &ov, &got, FALSE), "the result of an OVERLAPPED whose Internal is 0xc0000001");
    T_TRUE(rt_handle_close(h));
}
