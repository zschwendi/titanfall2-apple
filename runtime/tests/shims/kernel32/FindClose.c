#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"

#define SH   (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)

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

TEST(kernel32, FindClose)
{
    WIN32_FIND_DATAW data;
    HANDLE find, file;

    T_CASE("closes a find handle, so the next FindNextFile and the next FindClose fail with ERROR_INVALID_HANDLE");
    find = rt_find_first_w(W("C:\\Titanfall2\\*"), &data);
    T_REQUIRE(find != INVALID_HANDLE_VALUE);
    T_EQ(CALL(kernel32, FindClose, find), TRUE);
    T_EQ(rt_find_next_w(find, &data), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, FindClose, find), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("NULL, INVALID_HANDLE_VALUE and a file handle fail with ERROR_INVALID_HANDLE, and the file handle still works afterwards");
    put("f.txt", "data", 0644);
    file = rt_file_create(RT_NAME_W(W("C:\\Titanfall2\\f.txt")), GENERIC_READ, SH, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    T_REQUIRE(file != INVALID_HANDLE_VALUE);
    T_EQ(CALL(kernel32, FindClose, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, FindClose, INVALID_HANDLE_VALUE), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, FindClose, file), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(rt_handle_close(file), TRUE);              /* still open: the core closes it now */

    T_CASE("does not change last error on success");
    find = rt_find_first_w(W("C:\\Titanfall2\\*"), &data);
    T_REQUIRE(find != INVALID_HANDLE_VALUE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, FindClose, find), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
