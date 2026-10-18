#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"

/* W, I and put are the helpers of runtime/tests/core/file_open.c: a guest name, a host path
 * under the install root (the guest's C:\Titanfall2), and a file made there. */
static const WCHAR *W(const char *a) {
    static WCHAR buf[4][512];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    size_t i = 0;
    for (; a[i]; i++) b[i] = (unsigned char)a[i];
    b[i] = 0;
    return b;
}
static const char *I(const char *rel) {
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

/* Opens a file or a directory under the install root for reading. */
static HANDLE open_it(const char *win, DWORD flags) {
    return rt_file_create(RT_NAME_W(W(win)), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, flags, NULL);
}

/* The 52 bytes preset to 0xA5, and a check that none of them changed. */
static void fill(BY_HANDLE_FILE_INFORMATION *d) { memset(d, 0xA5, sizeof *d); }
static int still_filled(const BY_HANDLE_FILE_INFORMATION *d)
{
    const unsigned char *p = (const unsigned char *)d;
    for (size_t i = 0; i < sizeof *d; i++)
        if (p[i] != 0xA5) return 0;
    return 1;
}

TEST(kernel32, GetFileInformationByHandle)
{
    BY_HANDLE_FILE_INFORMATION info;
    struct stat st;
    unsigned long long index;
    HANDLE h, dh;

    put("five.bin", "hello", 0644);
    T_REQUIRE(mkdir(I("adir"), 0755) == 0);

    T_CASE("a file of 5 bytes: attributes, size 5, last write time, volume serial, one link and file index");
    h = open_it("C:\\Titanfall2\\five.bin", FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    fill(&info);
    T_EQ(CALL(kernel32, GetFileInformationByHandle, h, &info), TRUE);
    T_EQ(info.dwFileAttributes, rt_file_get_attributes(RT_NAME_A("C:\\Titanfall2\\five.bin")));
    T_EQ(info.nFileSizeHigh, 0);
    T_EQ(info.nFileSizeLow, 5);
    T_TRUE(info.ftLastWriteTime.dwLowDateTime != 0 || info.ftLastWriteTime.dwHighDateTime != 0);
    T_EQ(info.dwVolumeSerialNumber, RT_VOLUME_SERIAL);
    T_EQ(info.nNumberOfLinks, 1);
    T_REQUIRE(stat(I("five.bin"), &st) == 0);
    index = ((unsigned long long)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    T_EQ(index, (unsigned long long)st.st_ino);

    T_CASE("a directory opened with FILE_FLAG_BACKUP_SEMANTICS: attributes FILE_ATTRIBUTE_DIRECTORY and size 0");
    dh = open_it("C:\\Titanfall2\\adir", FILE_FLAG_BACKUP_SEMANTICS);
    T_REQUIRE(dh != INVALID_HANDLE_VALUE);
    fill(&info);
    T_EQ(CALL(kernel32, GetFileInformationByHandle, dh, &info), TRUE);
    T_EQ(info.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY);
    T_EQ(info.nFileSizeHigh, 0);
    T_EQ(info.nFileSizeLow, 0);
    T_EQ(rt_handle_close(dh), TRUE);

    T_CASE("a handle that is not open fails with ERROR_INVALID_HANDLE and leaves the 52 bytes");
    T_EQ(rt_handle_close(h), TRUE);
    fill(&info);
    T_EQ(CALL(kernel32, GetFileInformationByHandle, h, &info), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(still_filled(&info));

    T_CASE("success leaves last error alone");
    h = open_it("C:\\Titanfall2\\five.bin", FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    fill(&info);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileInformationByHandle, h, &info), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stop (RT-TRAPS), after the checks of the last case.  It ends in a fatal
     * report, proved in a forked child, on the open handle above. */
    T_TRAPS(CALL(kernel32, GetFileInformationByHandle, h, NULL), "not implemented: out NULL");
    T_EQ(rt_handle_close(h), TRUE);
}
