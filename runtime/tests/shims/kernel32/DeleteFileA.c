#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"

/* I and put: as in runtime/tests/core/file_open.c.  The guest's C:\Titanfall2\... is the
 * install root, so a host path is I(rel) and a guest name is spelled out in the test. */
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
static int exists(const char *rel) {
    return access(I(rel), F_OK) == 0;
}
static int is_dir(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) == 0 && S_ISDIR(st.st_mode);
}
static unsigned mode_of(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) ? 0 : (unsigned)(st.st_mode & 0777);
}

TEST(kernel32, DeleteFileA)
{
    HANDLE h;

    T_CASE("deletes a file, so the host no longer has it and a second delete fails with ERROR_FILE_NOT_FOUND");
    put("a.txt", "text", 0644);
    T_EQ(exists("a.txt"), 1);
    T_EQ(CALL(kernel32, DeleteFileA, "C:\\Titanfall2\\a.txt"), TRUE);
    T_EQ(exists("a.txt"), 0);
    T_EQ(CALL(kernel32, DeleteFileA, "C:\\Titanfall2\\a.txt"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);

    T_CASE("a read-only file (host mode 0444) stays and fails with ERROR_ACCESS_DENIED");
    put("ro.txt", "text", 0444);
    T_EQ(CALL(kernel32, DeleteFileA, "C:\\Titanfall2\\ro.txt"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(exists("ro.txt"), 1);
    T_EQ(mode_of("ro.txt"), 0444);

    T_CASE("a directory stays and fails with ERROR_ACCESS_DENIED");
    T_EQ(mkdir(I("sub"), 0755), 0);
    T_EQ(CALL(kernel32, DeleteFileA, "C:\\Titanfall2\\sub"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(is_dir("sub"), 1);

    T_CASE("a name in a directory that does not exist fails with ERROR_PATH_NOT_FOUND");
    T_EQ(CALL(kernel32, DeleteFileA, "C:\\Titanfall2\\nodir\\a.txt"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);

    T_CASE("a NULL name fails with ERROR_PATH_NOT_FOUND");
    T_EQ(CALL(kernel32, DeleteFileA, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);

    T_CASE("a file open with FILE_SHARE_DELETE can be deleted, and its handle stays open");
    put("open.txt", "text", 0644);
    h = rt_file_create(RT_NAME_A("C:\\Titanfall2\\open.txt"), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING, 0, NULL);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    T_EQ(CALL(kernel32, DeleteFileA, "C:\\Titanfall2\\open.txt"), TRUE);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("success leaves last error alone");
    put("keep.txt", "text", 0644);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, DeleteFileA, "C:\\Titanfall2\\keep.txt"), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(exists("keep.txt"), 0);
}
