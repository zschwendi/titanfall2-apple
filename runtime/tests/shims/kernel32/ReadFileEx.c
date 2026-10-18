/* ReadFileEx through the guest ABI, against files the test makes under the install root.  The
 * bytes are written by the host (put), so every read is checked against bytes the test chose.
 * Handles come from rt_file_create and go with rt_handle_close, as in runtime/tests/core/
 * file_open.c.  The completion routine is a GUEST_FN that stores what the core passes it;
 * rt_sleep_ex(..., TRUE) is what runs it.  The core's stops are proven at the end. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"
#include "rt/sync.h"

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
static HANDLE open_h(const char *win, DWORD access, DWORD disp, DWORD fa) {
    return rt_file_create(RT_NAME_W(W(win)), access, SH, disp, fa, NULL);
}

/* VOID CALLBACK on_read_ex(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped) */
static DWORD    apc_n;
static DWORD    apc_err, apc_bytes;
static uint64_t apc_ov;
GUEST_FN(on_read_ex)
{
    apc_n++;
    apc_err = (DWORD)ctx->r[1];
    apc_bytes = (DWORD)ctx->r[2];
    apc_ov = ctx->r[8];
    GUEST_RET(ctx);
}

TEST(kernel32, ReadFileEx)
{
    BYTE buf[16];
    OVERLAPPED ov;
    HANDLE h, hc, hn, wo;

    put("rx_data.bin", "0123456789", 0644);
    h = open_h("C:\\Titanfall2\\rx_data.bin", RD, OPEN_EXISTING, FILE_FLAG_OVERLAPPED);
    T_REQUIRE(h != NULL && h != BAD);

    T_CASE("overlapped read at an offset");
    memset(buf, 0xAA, sizeof buf);
    memset(&ov, 0, sizeof ov);
    ov.Offset = 2;
    apc_n = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, ReadFileEx, h, buf, 4, &ov, T_GUEST_ADDR(on_read_ex)), TRUE);
    T_EQ(T_LAST_ERROR(), 0);                        /* documented: ERROR_SUCCESS after a success */
    T_TRUE(memcmp(buf, "2345", 4) == 0);
    T_EQ(buf[4], 0xAA);                             /* nothing past the count is written */
    T_EQ(ov.Internal, STATUS_SUCCESS);
    T_EQ(ov.InternalHigh, 4);
    T_EQ(apc_n, 0);                                 /* queued, not run by the call */
    T_EQ(rt_sleep_ex(T_CTX(), 0, TRUE), WAIT_IO_COMPLETION);
    T_EQ(apc_n, 1);
    T_EQ(apc_err, 0);
    T_EQ(apc_bytes, 4);
    T_EQ(apc_ov, (uint64_t)(uintptr_t)&ov);

    T_CASE("read at the end of the file");
    memset(buf, 0xAA, sizeof buf);
    memset(&ov, 0, sizeof ov);
    ov.Offset = 10;                                 /* the file is 10 bytes: the read begins at its end */
    apc_n = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, ReadFileEx, h, buf, 4, &ov, T_GUEST_ADDR(on_read_ex)), TRUE);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(ov.Internal, STATUS_END_OF_FILE);
    T_EQ(ov.InternalHigh, 0);
    T_EQ(apc_n, 0);
    T_EQ(rt_sleep_ex(T_CTX(), 0, TRUE), WAIT_IO_COMPLETION);
    T_EQ(apc_n, 1);
    T_EQ(apc_err, ERROR_HANDLE_EOF);
    T_EQ(apc_bytes, 0);
    T_EQ(apc_ov, (uint64_t)(uintptr_t)&ov);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE");
    memset(&ov, 0, sizeof ov);
    apc_n = 0;
    T_EQ(CALL(kernel32, ReadFileEx, NULL, buf, 4, &ov, T_GUEST_ADDR(on_read_ex)), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    hc = open_h("C:\\Titanfall2\\rx_data.bin", RD, OPEN_EXISTING, FILE_FLAG_OVERLAPPED);
    T_REQUIRE(hc != NULL && hc != BAD);
    T_TRUE(rt_handle_close(hc));
    T_EQ(CALL(kernel32, ReadFileEx, hc, buf, 4, &ov, T_GUEST_ADDR(on_read_ex)), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_sleep_ex(T_CTX(), 0, TRUE), 0);         /* nothing was queued */
    T_EQ(apc_n, 0);

    T_CASE("a handle opened for writing only fails with ERROR_ACCESS_DENIED");
    wo = open_h("C:\\Titanfall2\\rx_data.bin", WR, OPEN_EXISTING, FILE_FLAG_OVERLAPPED);
    T_REQUIRE(wo != NULL && wo != BAD);
    T_EQ(CALL(kernel32, ReadFileEx, wo, buf, 4, &ov, T_GUEST_ADDR(on_read_ex)), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(rt_sleep_ex(T_CTX(), 0, TRUE), 0);         /* nothing was queued */
    T_EQ(apc_n, 0);
    T_TRUE(rt_handle_close(wo));

    /* The core's stops (RT-TRAPS), after the checks of the last case.  The shim adds no guard
     * for them.  A handle without FILE_FLAG_OVERLAPPED, lpOverlapped NULL, and a completion
     * routine 0 each stop the core, as the core's own tests show. */
    hn = open_h("C:\\Titanfall2\\rx_data.bin", RD, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(hn != NULL && hn != BAD);
    memset(&ov, 0, sizeof ov);
    T_TRAPS(CALL(kernel32, ReadFileEx, hn, buf, 4, &ov, T_GUEST_ADDR(on_read_ex)), "without FILE_FLAG_OVERLAPPED");
    T_TRAPS(CALL(kernel32, ReadFileEx, h, buf, 4, NULL, T_GUEST_ADDR(on_read_ex)), "lpOverlapped NULL");
    T_TRAPS(CALL(kernel32, ReadFileEx, h, buf, 4, &ov, 0), "lpCompletionRoutine NULL");
    T_TRUE(rt_handle_close(hn));
    T_TRUE(rt_handle_close(h));
}
