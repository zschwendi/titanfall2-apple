/* rt_file_set_attributes (rt/file.h) through the guest ABI: READONLY and NORMAL seen through
 * GetFileAttributesA and through the host mode, a directory, the failures of the name, last
 * error on success, and the core's stop.  Files are made on the host under the install root;
 * the guest names them as C:\Titanfall2\...
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
static unsigned mode_of(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) ? 0 : (unsigned)(st.st_mode & 0777);
}

TEST(kernel32, SetFileAttributesA)
{
    struct stat st;

    T_CASE("FILE_ATTRIBUTE_READONLY removes every write bit of the host mode");
    put("w.txt", "x", 0666);
    T_EQ(CALL(kernel32, SetFileAttributesA, "C:\\Titanfall2\\w.txt", FILE_ATTRIBUTE_READONLY), TRUE);
    T_EQ(mode_of("w.txt"), 0444);
    T_EQ(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\w.txt"), FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY);

    T_CASE("FILE_ATTRIBUTE_NORMAL clears READONLY and adds only the owner's write bit");
    T_EQ(CALL(kernel32, SetFileAttributesA, "C:\\Titanfall2\\w.txt", FILE_ATTRIBUTE_NORMAL), TRUE);
    T_EQ(mode_of("w.txt"), 0644);
    T_EQ(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\w.txt"), FILE_ATTRIBUTE_ARCHIVE);

    T_CASE("a directory is unchanged and succeeds");
    if (mkdir(I("d"), 0755)) rt_test_fail(__FILE__, __LINE__, "mkdir d");
    T_EQ(mode_of("d"), 0755);
    T_EQ(CALL(kernel32, SetFileAttributesA, "C:\\Titanfall2\\d", FILE_ATTRIBUTE_READONLY), TRUE);
    T_EQ(mode_of("d"), 0755);
    T_EQ(CALL(kernel32, GetFileAttributesA, "C:\\Titanfall2\\d"), FILE_ATTRIBUTE_DIRECTORY);

    T_CASE("a missing name fails with ERROR_FILE_NOT_FOUND");
    T_EQ(CALL(kernel32, SetFileAttributesA, "C:\\Titanfall2\\none.txt", FILE_ATTRIBUTE_NORMAL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    T_TRUE(stat(I("none.txt"), &st) != 0);          /* and nothing was created */

    T_CASE("a name in a directory that does not exist fails with ERROR_PATH_NOT_FOUND");
    T_EQ(CALL(kernel32, SetFileAttributesA, "C:\\Titanfall2\\nodir\\x.txt", FILE_ATTRIBUTE_NORMAL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);

    T_CASE("a NULL name fails with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(kernel32, SetFileAttributesA, NULL, FILE_ATTRIBUTE_NORMAL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetFileAttributesA, "C:\\Titanfall2\\w.txt", FILE_ATTRIBUTE_NORMAL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS: the core's stop (rt/file.h: a bit outside its list, checked first).  After the
     * checks of the last case. */
    T_TRAPS(CALL(kernel32, SetFileAttributesA, "C:\\Titanfall2\\w.txt", FILE_ATTRIBUTE_DIRECTORY), "attributes 0x10");
}
