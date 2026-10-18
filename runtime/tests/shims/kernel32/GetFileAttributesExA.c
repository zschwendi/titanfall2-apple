#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"

/* I and put are the helpers of runtime/tests/core/file_open.c: host paths under the install
 * root, which is the guest's C:\Titanfall2. */
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

/* The 36 bytes preset to 0xA5, and a check that none of them changed. */
static void fill(WIN32_FILE_ATTRIBUTE_DATA *d) { memset(d, 0xA5, sizeof *d); }
static int still_filled(const WIN32_FILE_ATTRIBUTE_DATA *d)
{
    const unsigned char *p = (const unsigned char *)d;
    for (size_t i = 0; i < sizeof *d; i++)
        if (p[i] != 0xA5) return 0;
    return 1;
}

TEST(kernel32, GetFileAttributesExA)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    DWORD attrs;

    put("five.bin", "hello", 0644);
    T_REQUIRE(mkdir(I("adir"), 0755) == 0);

    T_CASE("a file of 5 bytes: attributes, size 5 and last write time");
    fill(&d);
    T_EQ(CALL(kernel32, GetFileAttributesExA, "C:\\Titanfall2\\five.bin", GetFileExInfoStandard, &d), TRUE);
    attrs = rt_file_get_attributes(RT_NAME_A("C:\\Titanfall2\\five.bin"));
    T_EQ(d.dwFileAttributes, attrs);
    T_EQ(d.dwFileAttributes, FILE_ATTRIBUTE_ARCHIVE);
    T_EQ(d.nFileSizeHigh, 0);
    T_EQ(d.nFileSizeLow, 5);
    T_TRUE(d.ftLastWriteTime.dwLowDateTime != 0 || d.ftLastWriteTime.dwHighDateTime != 0);

    T_CASE("a directory: size 0");
    fill(&d);
    T_EQ(CALL(kernel32, GetFileAttributesExA, "C:\\Titanfall2\\adir", GetFileExInfoStandard, &d), TRUE);
    T_EQ(d.dwFileAttributes, FILE_ATTRIBUTE_DIRECTORY);
    T_EQ(d.nFileSizeHigh, 0);
    T_EQ(d.nFileSizeLow, 0);

    T_CASE("a level other than GetFileExInfoStandard fails with ERROR_INVALID_PARAMETER and leaves the 36 bytes");
    fill(&d);
    T_EQ(CALL(kernel32, GetFileAttributesExA, "C:\\Titanfall2\\five.bin", 1, &d), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_TRUE(still_filled(&d));

    T_CASE("a missing file fails with ERROR_FILE_NOT_FOUND and leaves the 36 bytes");
    fill(&d);
    T_EQ(CALL(kernel32, GetFileAttributesExA, "C:\\Titanfall2\\missing.bin", GetFileExInfoStandard, &d), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    T_TRUE(still_filled(&d));

    T_CASE("success leaves last error alone");
    fill(&d);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileAttributesExA, "C:\\Titanfall2\\five.bin", GetFileExInfoStandard, &d), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core's stops (RT-TRAPS), after the checks of the last case.  Each ends in a fatal
     * report, proved in a forked child.  The FIFO is a host object of another type. */
    T_REQUIRE(mkfifo(I("pipe"), 0644) == 0);
    T_TRAPS(CALL(kernel32, GetFileAttributesExA, "C:\\Titanfall2\\five.bin", GetFileExInfoStandard, NULL), "not implemented: out NULL");
    T_TRAPS(CALL(kernel32, GetFileAttributesExA, "NUL", GetFileExInfoStandard, &d), "the NUL device");
    T_TRAPS(CALL(kernel32, GetFileAttributesExA, "C:\\Titanfall2\\pipe", GetFileExInfoStandard, &d), "a host object of type");
}
