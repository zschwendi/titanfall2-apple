/* MoveFileA: the core's rt_file_move (rt/file.h) does the work.  The guest names are under
 * C:\Titanfall2, which is the install root the harness made; the host side is checked with
 * the host's own calls.  Files that are made with a handle are closed with rt_handle_close. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"

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
static int exists(const char *rel) {                /* any host entry under the install root */
    struct stat st;
    return stat(I(rel), &st) == 0;
}
static int is_dir(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) == 0 && S_ISDIR(st.st_mode);
}
static int is(const char *rel, const char *text) {  /* a regular file whose whole content is text */
    char buf[64];
    int fd = open(I(rel), O_RDONLY);
    ssize_t n = fd < 0 ? -1 : read(fd, buf, sizeof buf - 1);
    if (fd >= 0) close(fd);
    if (n < 0) return 0;
    buf[n] = 0;
    return strcmp(buf, text) == 0;
}

TEST(kernel32, MoveFileA)
{
    HANDLE h;

    T_CASE("a file moves to a new name in the same directory and into a sub-directory");
    h = rt_file_create(RT_NAME_A("C:\\Titanfall2\\a.txt"), GENERIC_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    T_TRUE(rt_handle_close(h));
    T_EQ(CALL(kernel32, MoveFileA, "C:\\Titanfall2\\a.txt", "C:\\Titanfall2\\b.txt"), TRUE);
    T_TRUE(!exists("a.txt") && exists("b.txt"));
    T_EQ(mkdir(I("sub"), 0755), 0);
    T_EQ(CALL(kernel32, MoveFileA, "C:\\Titanfall2\\b.txt", "C:\\Titanfall2\\sub\\c.txt"), TRUE);
    T_TRUE(!exists("b.txt") && exists("sub/c.txt"));

    T_CASE("a directory moves");
    T_EQ(mkdir(I("dir1"), 0755), 0);
    put("dir1/f.txt", "inside", 0644);
    T_EQ(CALL(kernel32, MoveFileA, "C:\\Titanfall2\\dir1", "C:\\Titanfall2\\dir2"), TRUE);
    T_TRUE(!exists("dir1") && is_dir("dir2") && is("dir2/f.txt", "inside"));

    T_CASE("a target that exists fails with ERROR_ALREADY_EXISTS and both files stay unchanged");
    put("x.txt", "first", 0644);
    put("y.txt", "second!", 0644);
    T_EQ(CALL(kernel32, MoveFileA, "C:\\Titanfall2\\x.txt", "C:\\Titanfall2\\y.txt"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_TRUE(is("x.txt", "first") && is("y.txt", "second!"));

    T_CASE("a source that does not exist fails with ERROR_FILE_NOT_FOUND");
    T_EQ(CALL(kernel32, MoveFileA, "C:\\Titanfall2\\nothere.txt", "C:\\Titanfall2\\new.txt"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    T_TRUE(!exists("new.txt"));

    T_CASE("a target in a directory that does not exist fails with ERROR_PATH_NOT_FOUND");
    put("m.txt", "m", 0644);
    T_EQ(CALL(kernel32, MoveFileA, "C:\\Titanfall2\\m.txt", "C:\\Titanfall2\\nodir\\m.txt"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    T_TRUE(is("m.txt", "m") && !exists("nodir"));

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, MoveFileA, "C:\\Titanfall2\\m.txt", "C:\\Titanfall2\\m2.txt"), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, MoveFileA, "C:\\Titanfall2\\m2.txt", "C:\\Titanfall2\\sub\\m3.txt"), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS core: lpNewFileName NULL.  The core stops on a NULL target (delete at reboot). */
    T_TRAPS(CALL(kernel32, MoveFileA, "C:\\Titanfall2\\sub\\m3.txt", NULL), "a NULL target: delete at reboot");
}
