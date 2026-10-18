#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"

/* W, I and put are the helpers of runtime/tests/core/file_open.c: a wide name, host paths
 * under the install root, which is the guest's C:\Titanfall2. */
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

TEST(kernel32, SetFileAttributesW)
{
    struct stat st;
    DWORD code;

    put("five.bin", "hello", 0644);
    T_REQUIRE(mkdir(I("adir"), 0755) == 0);

    T_CASE("READONLY and then NORMAL on a file, seen through rt_file_get_attributes and the host mode");
    T_EQ(CALL(kernel32, SetFileAttributesW, W("C:\\Titanfall2\\five.bin"), FILE_ATTRIBUTE_READONLY), TRUE);
    T_EQ(rt_file_get_attributes(RT_NAME_W(W("C:\\Titanfall2\\five.bin"))), FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY);
    T_REQUIRE(stat(I("five.bin"), &st) == 0);
    T_EQ(st.st_mode & 0777, 0444);                     /* 0644 with every write bit removed */
    T_EQ(CALL(kernel32, SetFileAttributesW, W("C:\\Titanfall2\\five.bin"), FILE_ATTRIBUTE_NORMAL), TRUE);
    T_EQ(rt_file_get_attributes(RT_NAME_W(W("C:\\Titanfall2\\five.bin"))), FILE_ATTRIBUTE_ARCHIVE);
    T_REQUIRE(stat(I("five.bin"), &st) == 0);
    T_EQ(st.st_mode & 0777, 0644);                     /* the owner's write bit given back */

    T_CASE("a missing file fails with ERROR_FILE_NOT_FOUND");
    T_EQ(CALL(kernel32, SetFileAttributesW, W("C:\\Titanfall2\\missing.bin"), FILE_ATTRIBUTE_NORMAL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    T_TRUE(stat(I("missing.bin"), &st) != 0);

    T_CASE("a NULL name fails with the code rt_file_get_attributes gives");
    rt_file_get_attributes(RT_NAME_W(NULL));
    code = T_LAST_ERROR();
    T_EQ(CALL(kernel32, SetFileAttributesW, NULL, FILE_ATTRIBUTE_NORMAL), FALSE);
    T_EQ(T_LAST_ERROR(), code);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetFileAttributesW, W("C:\\Titanfall2\\five.bin"), FILE_ATTRIBUTE_NORMAL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a directory given FILE_ATTRIBUTE_DIRECTORY, as GetFileAttributesW returns it, is left alone and returns TRUE");
    T_REQUIRE(stat(I("adir"), &st) == 0);
    const mode_t dir_mode = st.st_mode & 07777;
    const DWORD dattr = rt_file_get_attributes(RT_NAME_W(W("C:\\Titanfall2\\adir")));
    T_EQ(dattr, FILE_ATTRIBUTE_DIRECTORY);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetFileAttributesW, W("C:\\Titanfall2\\adir"), dattr & ~FILE_ATTRIBUTE_READONLY), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, SetFileAttributesW, W("C:\\Titanfall2\\adir"), FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY), TRUE);
    T_REQUIRE(stat(I("adir"), &st) == 0);
    T_EQ(st.st_mode & 07777, dir_mode);                /* a directory's mode is not changed */
    T_EQ(rt_file_get_attributes(RT_NAME_W(W("C:\\Titanfall2\\adir"))), FILE_ATTRIBUTE_DIRECTORY);

    /* The core's stops (RT-TRAPS), after the checks of the last case.  Each ends in a fatal
     * report, proved in a forked child.  The FIFO and the NUL device are host objects of
     * another type: stat finds them and the attribute code stops before any chmod. */
    T_REQUIRE(mkfifo(I("pipe"), 0644) == 0);
    T_TRAPS(CALL(kernel32, SetFileAttributesW, W("C:\\Titanfall2\\five.bin"), FILE_ATTRIBUTE_DIRECTORY), "not implemented: attributes 0x10");
    T_TRAPS(CALL(kernel32, SetFileAttributesW, W("C:\\Titanfall2\\pipe"), FILE_ATTRIBUTE_READONLY), "a host object of type");
    T_TRAPS(CALL(kernel32, SetFileAttributesW, W("NUL"), FILE_ATTRIBUTE_READONLY), "a host object of type");
}
