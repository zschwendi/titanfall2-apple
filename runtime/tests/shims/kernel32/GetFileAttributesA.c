/* rt_file_get_attributes (rt/file.h) through the guest ABI: the attributes of a file and of a
 * directory, the failures of the name, last error on success, and the core's stops.  Files
 * are made on the host under the install root; the guest names them as C:\Titanfall2\...
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"

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

TEST(kernel32, GetFileAttributesA)
{
    T_CASE("a file with the owner's write bit is ARCHIVE");
    put("rw.txt", "x", 0644);
    T_EQ(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\rw.txt"), FILE_ATTRIBUTE_ARCHIVE);

    T_CASE("a file without the owner's write bit is ARCHIVE and READONLY");
    put("ro.txt", "x", 0444);
    T_EQ(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\ro.txt"), FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY);

    T_CASE("a directory is FILE_ATTRIBUTE_DIRECTORY");
    if (mkdir(I("dir"), 0755)) rt_test_fail(__FILE__, __LINE__, "mkdir dir");
    T_EQ(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\dir"), FILE_ATTRIBUTE_DIRECTORY);

    T_CASE("a missing name fails with ERROR_FILE_NOT_FOUND");
    T_EQ(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\none.txt"), INVALID_FILE_ATTRIBUTES);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    T_CASE("a name in a directory that does not exist fails with ERROR_PATH_NOT_FOUND");
    T_EQ(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\nodir\\x.txt"), INVALID_FILE_ATTRIBUTES);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);

    T_CASE("a NULL name fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(kernel32, GetFileAttributesA, NULL), INVALID_FILE_ATTRIBUTES);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\rw.txt"), FILE_ATTRIBUTE_ARCHIVE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: the core's stops (rt/file.h: NUL, and a host object of any other type).  After
     * the checks of the last case. */
    T_TRAPS(CALL(kernel32, GetFileAttributesA, "NUL"), "the NUL device");
    if (mkfifo(I("fifo"), 0644)) rt_test_fail(__FILE__, __LINE__, "mkfifo fifo");
    T_TRAPS(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\fifo"), "a host object of type");
}
