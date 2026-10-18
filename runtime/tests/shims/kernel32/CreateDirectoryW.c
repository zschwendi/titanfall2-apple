#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"

/* The guest's C:\Titanfall2 is rt_test_install_root(); the host side of a name is looked at
 * under that root only. */
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
/* A file made through the core (rt_file_create), then closed, so that the name is taken. */
static void make_file(const char *win) {
    HANDLE h = rt_file_create(RT_NAME_W(W(win)), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    T_TRUE(rt_handle_close(h));
}

TEST(kernel32, CreateDirectoryW)
{
    SECURITY_ATTRIBUTES sa;

    T_CASE("creates a directory the host can see");
    T_EQ(CALL(kernel32, CreateDirectoryW, W("C:\\Titanfall2\\d1"), NULL), TRUE);
    T_TRUE(is_dir("d1"));

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, CreateDirectoryW, W("C:\\Titanfall2\\d2"), NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(is_dir("d2"));

    T_CASE("a second create fails with ERROR_ALREADY_EXISTS");
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(kernel32, CreateDirectoryW, W("C:\\Titanfall2\\d1"), NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_TRUE(is_dir("d1"));

    T_CASE("a create over a file fails with ERROR_ALREADY_EXISTS");
    put("f1.bin", "x", 0644);
    make_file("C:\\Titanfall2\\f2.bin");
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(kernel32, CreateDirectoryW, W("C:\\Titanfall2\\f1.bin"), NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_TRUE(is_file("f1.bin"));
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(kernel32, CreateDirectoryW, W("C:\\Titanfall2\\f2.bin"), NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_TRUE(is_file("f2.bin"));

    T_CASE("a create under a parent that does not exist fails with ERROR_PATH_NOT_FOUND");
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(kernel32, CreateDirectoryW, W("C:\\Titanfall2\\nodir\\sub"), NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    T_TRUE(!exists("nodir"));

    T_CASE("a NULL name fails with ERROR_PATH_NOT_FOUND");
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(kernel32, CreateDirectoryW, NULL, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);

    T_CASE("lpSecurityAttributes is ignored, NULL or not");
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    T_EQ(CALL(kernel32, CreateDirectoryW, W("C:\\Titanfall2\\d3"), &sa), TRUE);
    T_TRUE(is_dir("d3"));
    T_EQ(CALL(kernel32, CreateDirectoryW, W("C:\\Titanfall2\\d4"), NULL), TRUE);
    T_TRUE(is_dir("d4"));

    /* The cases the core stops on by itself (rt_path_to_host_w, runtime/core/path.c): fatal
     * reports, each naming the core's text.  The shim adds no guard for them. */
    T_TRAPS(CALL(kernel32, CreateDirectoryW, W("C:\\x\\a:b"), NULL), "rt_path: ':' in a path component");
    T_TRAPS(CALL(kernel32, CreateDirectoryW, W("C:\\x\\con.txt"), NULL), "rt_path: a DOS device name with an extension");
}
