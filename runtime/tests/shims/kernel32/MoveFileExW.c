/* MoveFileExW: the core's rt_file_move (rt/file.h) does the work.  The guest names are under
 * C:\Titanfall2, which is the install root the harness made; the host side is checked with
 * the host's own calls.  A file made with a handle is closed with rt_handle_close. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"

static const WCHAR *W(const char *a) {               /* a guest name: narrow text as wide */
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
static int exists(const char *rel) {                /* any host entry under the install root */
    struct stat st;
    return stat(I(rel), &st) == 0;
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

TEST(kernel32, MoveFileExW)
{
    HANDLE h;

    T_CASE("a file moves to a new name");
    h = rt_file_create(RT_NAME_W(W("C:\\Titanfall2\\a.txt")), GENERIC_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    T_REQUIRE(h != INVALID_HANDLE_VALUE);
    T_TRUE(rt_handle_close(h));
    T_EQ(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\a.txt"), W("C:\\Titanfall2\\b.txt"), 0), TRUE);
    T_TRUE(!exists("a.txt") && exists("b.txt"));

    T_CASE("a target that exists fails with ERROR_ALREADY_EXISTS without MOVEFILE_REPLACE_EXISTING");
    put("x.txt", "first", 0644);
    put("y.txt", "second!", 0644);
    T_EQ(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\x.txt"), W("C:\\Titanfall2\\y.txt"), 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_TRUE(is("x.txt", "first") && is("y.txt", "second!"));

    T_CASE("a target that exists is replaced with MOVEFILE_REPLACE_EXISTING");
    T_EQ(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\x.txt"), W("C:\\Titanfall2\\y.txt"), MOVEFILE_REPLACE_EXISTING), TRUE);
    T_TRUE(!exists("x.txt") && is("y.txt", "first"));

    T_CASE("a source that does not exist fails with ERROR_FILE_NOT_FOUND");
    T_EQ(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\nothere.txt"), W("C:\\Titanfall2\\new.txt"), 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    T_TRUE(!exists("new.txt"));

    T_CASE("a target in a directory that does not exist fails with ERROR_PATH_NOT_FOUND");
    put("m.txt", "m", 0644);
    T_EQ(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\m.txt"), W("C:\\Titanfall2\\nodir\\m.txt"), 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    T_TRUE(is("m.txt", "m") && !exists("nodir"));

    T_CASE("MOVEFILE_WRITE_THROUGH is accepted");
    put("w.txt", "w", 0644);
    T_EQ(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\w.txt"), W("C:\\Titanfall2\\w2.txt"), MOVEFILE_WRITE_THROUGH), TRUE);
    T_TRUE(!exists("w.txt") && is("w2.txt", "w"));

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\m.txt"), W("C:\\Titanfall2\\m2.txt"), 0), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\w2.txt"), W("C:\\Titanfall2\\w3.txt"), MOVEFILE_REPLACE_EXISTING), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* RT-TRAPS core: lpNewFileName NULL.  The core stops on a NULL target (delete at reboot). */
    T_TRAPS(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\m2.txt"), NULL, 0), "a NULL target: delete at reboot");

    /* RT-TRAPS core: a flag outside MOVEFILE_REPLACE_EXISTING, MOVEFILE_COPY_ALLOWED and
     * MOVEFILE_WRITE_THROUGH.  Any such flag stops the core, whatever the target: the
     * delete-at-reboot flag, and 0x10, which the documentation page (RT-DOC) names
     * MOVEFILE_CREATE_HARDLINK, "16 (0x10)", "Reserved for future use" (rt/win/file.h has no
     * name for it, so the value is written out), alone and beside a flag the core does build. */
    T_TRAPS(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\m2.txt"), W("C:\\Titanfall2\\m4.txt"), MOVEFILE_DELAY_UNTIL_REBOOT), "flags 0x4");
    T_TRAPS(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\m2.txt"), W("C:\\Titanfall2\\m4.txt"), 0x10u), "flags 0x10");
    T_TRAPS(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\m2.txt"), W("C:\\Titanfall2\\m4.txt"), MOVEFILE_REPLACE_EXISTING | 0x10u), "flags 0x11");

    /* RT-TRAPS core: a move between volumes with MOVEFILE_COPY_ALLOWED.  Not proved here: the
     * stop needs the source and the target on two host volumes, and a test has one temporary
     * directory.  The entry is taken from the core's code (rt_file_move, step 4). */

    /* RT-TRAPS core: read-only target with MOVEFILE_REPLACE_EXISTING.  The core stops (not
     * verified against Windows); the source is left as it was. */
    put("ro_src.txt", "src", 0644);
    put("ro.txt", "ro", 0444);
    T_TRAPS(CALL(kernel32, MoveFileExW, W("C:\\Titanfall2\\ro_src.txt"), W("C:\\Titanfall2\\ro.txt"), MOVEFILE_REPLACE_EXISTING), "replacing the read-only file");
}
