#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"

/* The guest's C:\Titanfall2 is rt_test_install_root(); the host side of a name is looked at
 * under that root only. */
static const char *I(const char *rel) {
    static char buf[4][2048];
    static unsigned k;
    char *b = buf[k++ & 3];
    snprintf(b, sizeof buf[0], "%s/%s", rt_test_install_root(), rel);
    return b;
}
static int exists(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) == 0;
}
static int is_dir(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) == 0 && S_ISDIR(st.st_mode);
}
static int is_file(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) == 0 && S_ISREG(st.st_mode);
}
/* A directory made on the host, under the install root. */
static void make_dir(const char *rel) {
    T_TRUE(mkdir(I(rel), 0755) == 0);
}
/* A file made through the core (rt_file_create), then closed. */
static void make_file(const char *win) {
    HANDLE h = rt_file_create(RT_NAME_A(win), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    T_TRUE(rt_handle_close(h));
}

TEST(kernel32, RemoveDirectoryA)
{
    T_CASE("removes an empty directory");
    make_dir("e1");
    T_EQ(CALL(kernel32, RemoveDirectoryA, "C:\\Titanfall2\\e1"), TRUE);
    T_TRUE(!exists("e1"));

    T_CASE("a directory with a file in it is not removed and fails with ERROR_DIR_NOT_EMPTY");
    make_dir("n1");
    make_file("C:\\Titanfall2\\n1\\f.bin");
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(kernel32, RemoveDirectoryA, "C:\\Titanfall2\\n1"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_DIR_NOT_EMPTY);
    T_TRUE(is_dir("n1"));
    T_TRUE(is_file("n1/f.bin"));

    T_CASE("a name that does not exist fails with ERROR_PATH_NOT_FOUND");
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(kernel32, RemoveDirectoryA, "C:\\Titanfall2\\missing1"), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);

    T_CASE("success leaves last error alone");
    make_dir("e2");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, RemoveDirectoryA, "C:\\Titanfall2\\e2"), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(!exists("e2"));

    /* RT-TRAPS "core: RemoveDirectory on a file": the core stops; no case of its own. */
    make_file("C:\\Titanfall2\\f2.bin");
    T_TRAPS(CALL(kernel32, RemoveDirectoryA, "C:\\Titanfall2\\f2.bin"), "a name that is not a directory");
}
