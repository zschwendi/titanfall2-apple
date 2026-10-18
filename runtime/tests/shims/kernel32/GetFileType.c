#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"
#include "rt/sync.h"

/* The helpers of runtime/tests/core/file_open.c: a guest name, a host path under the install
 * root, and a file made there with the host's calls. */
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

TEST(kernel32, GetFileType)
{
    static const DWORD std_ids[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
    HANDLE h, ev;
    unsigned i;

    T_CASE("each standard handle is FILE_TYPE_CHAR and leaves last error alone");
    for (i = 0; i < 3; i++) {
        h = rt_std_get(std_ids[i]);
        T_REQUIRE(h != NULL && h != INVALID_HANDLE_VALUE);
        CALL(kernel32, SetLastError, 0x1234);
        T_EQ(CALL(kernel32, GetFileType, h), FILE_TYPE_CHAR);
        T_EQ(T_LAST_ERROR(), 0x1234);
    }

    T_CASE("a file under the install root is FILE_TYPE_DISK and leaves last error alone");
    put("gft-disk.txt", "disk", 0644);
    h = rt_file_create(RT_NAME_W(W("C:\\Titanfall2\\gft-disk.txt")), GENERIC_READ, FILE_SHARE_READ,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileType, h), FILE_TYPE_DISK);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("NULL, INVALID_HANDLE_VALUE and an event handle are FILE_TYPE_UNKNOWN with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileType, NULL), FILE_TYPE_UNKNOWN);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileType, INVALID_HANDLE_VALUE), FILE_TYPE_UNKNOWN);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    ev = rt_event_create(T_CTX(), FALSE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileType, ev), FILE_TYPE_UNKNOWN);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_handle_close(ev), TRUE);
}
