#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"

#define SENT ((ULONGLONG)0x5A5A5A5A5A5A5A5AULL)

/* I and put of runtime/tests/core/file_open.c: a host path under the install root, and a file
 * made there (the guest's C:\Titanfall2\<rel>). */
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

TEST(kernel32, GetDiskFreeSpaceExA)
{
    ULARGE_INTEGER a, t, f;
    ULONGLONG want_total = 0, dir_total = 0;

    T_CASE("C root gives the core total, and free is not above total");
    /* The oracle: the core's total of the C root, asked by the test itself. */
    T_REQUIRE(rt_disk_space(RT_NAME_A("C:\\"), NULL, &want_total, NULL));
    a.QuadPart = t.QuadPart = f.QuadPart = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "C:\\", &a, &t, &f), TRUE);
    T_EQ(t.QuadPart, want_total);
    T_TRUE(f.QuadPart <= t.QuadPart);
    T_TRUE(a.QuadPart != SENT);

    T_CASE("NULL root is the root of the current disk and gives the same total as C root");
    t.QuadPart = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, NULL, NULL, &t, NULL), TRUE);
    T_EQ(t.QuadPart, want_total);

    T_CASE("a directory of the install root gives the total of the volume it lies on");
    T_REQUIRE(rt_disk_space(RT_NAME_A("C:\\Titanfall2"), NULL, &dir_total, NULL));
    a.QuadPart = t.QuadPart = f.QuadPart = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "C:\\Titanfall2", &a, &t, &f), TRUE);
    T_EQ(t.QuadPart, dir_total);
    T_TRUE(f.QuadPart <= t.QuadPart);

    T_CASE("a pointer passed as NULL is not written and the others are");
    a.QuadPart = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "C:\\", &a, NULL, NULL), TRUE);
    T_TRUE(a.QuadPart != SENT);
    t.QuadPart = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "C:\\", NULL, &t, NULL), TRUE);
    T_EQ(t.QuadPart, want_total);
    f.QuadPart = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "C:\\", NULL, NULL, &f), TRUE);
    T_TRUE(f.QuadPart != SENT);
    T_TRUE(f.QuadPart <= want_total);

    T_CASE("all three pointers NULL is TRUE");
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "C:\\", NULL, NULL, NULL), TRUE);
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, NULL, NULL, NULL, NULL), TRUE);

    T_CASE("D root, empty root and a missing directory fail with ERROR_PATH_NOT_FOUND and write nothing");
    CALL(kernel32, SetLastError, 0x1234);
    a.QuadPart = t.QuadPart = f.QuadPart = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "D:\\", &a, &t, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    T_EQ(a.QuadPart, SENT);
    T_EQ(t.QuadPart, SENT);
    T_EQ(f.QuadPart, SENT);
    CALL(kernel32, SetLastError, 0x1234);
    a.QuadPart = t.QuadPart = f.QuadPart = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "", &a, &t, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    T_EQ(a.QuadPart, SENT);
    T_EQ(t.QuadPart, SENT);
    T_EQ(f.QuadPart, SENT);
    CALL(kernel32, SetLastError, 0x1234);
    a.QuadPart = t.QuadPart = f.QuadPart = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "C:\\Titanfall2\\no_such_dir", &a, &t, &f), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    T_EQ(a.QuadPart, SENT);
    T_EQ(t.QuadPart, SENT);
    T_EQ(f.QuadPart, SENT);

    T_CASE("success leaves a preset last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, "C:\\", &a, &t, &f), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetDiskFreeSpaceExA, NULL, NULL, NULL, NULL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stop of the core: a name that leads to a file.  Listed in RT-TRAPS. */
    put("disk_space_file.txt", "x", 0644);
    T_TRAPS(CALL(kernel32, GetDiskFreeSpaceExA, "C:\\Titanfall2\\disk_space_file.txt", &a, &t, &f),
            "rt_disk_space: not implemented: a name that is not a directory");
}
