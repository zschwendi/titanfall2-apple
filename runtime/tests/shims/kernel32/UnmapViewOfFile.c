#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/handle.h"
#include "rt/win/file.h"
#include "rt/win/memory.h"

#define SH  (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)

/* The helpers of runtime/tests/core/file_open.c: a name for the guest, a host path under the
 * install root, and a file the test makes there. */
static const WCHAR *W(const char *a) {
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
/* The first n bytes of a host file under the install root: the count read, or -1. */
static long host_read(const char *rel, char *buf, size_t n) {
    int fd = open(I(rel), O_RDONLY);
    ssize_t got;
    if (fd < 0) return -1;
    got = read(fd, buf, n);
    close(fd);
    return (long)got;
}

TEST(kernel32, UnmapViewOfFile)
{
    HANDLE sec, f, s;
    char *v;
    void *r;
    char buf[16];

    sec = rt_section_create(INVALID_HANDLE_VALUE, PAGE_READWRITE, 131072, NULL);   /* reads as zeros */
    T_REQUIRE(sec != NULL);

    T_CASE("a view unmapped once gives TRUE and then FALSE with ERROR_INVALID_ADDRESS, as NULL does");
    v = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 0, 4096);
    T_REQUIRE(v != NULL);
    T_EQ(CALL(kernel32, UnmapViewOfFile, v), TRUE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, UnmapViewOfFile, v), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, UnmapViewOfFile, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);

    T_CASE("the byte after a view fails with ERROR_INVALID_ADDRESS");
    v = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 0, 4096);
    T_REQUIRE(v != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, UnmapViewOfFile, v + 4096), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);
    T_EQ(CALL(kernel32, UnmapViewOfFile, v), TRUE);

    T_CASE("a VirtualAlloc region fails with ERROR_INVALID_ADDRESS");
    r = CALL(kernel32, VirtualAlloc, NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    T_REQUIRE(r != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, UnmapViewOfFile, r), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);
    T_EQ(CALL(kernel32, VirtualFree, r, 0, MEM_RELEASE), TRUE);

    T_CASE("an address inside a view unmaps the whole view");
    v = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 0, 4096);
    T_REQUIRE(v != NULL);
    T_EQ(CALL(kernel32, UnmapViewOfFile, v + 100), TRUE);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, UnmapViewOfFile, v), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, UnmapViewOfFile, v + 4095), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_ADDRESS);

    T_CASE("what was written through a shared view stays in the file after the unmap");
    put("rw.bin", "abcdefghij", 0644);
    f = rt_file_create(RT_NAME_W(W("C:\\Titanfall2\\rw.bin")), GENERIC_READ | GENERIC_WRITE, SH, OPEN_EXISTING, 0, NULL);
    T_REQUIRE(f != INVALID_HANDLE_VALUE);
    s = rt_section_create(f, PAGE_READWRITE, 0, NULL);
    T_REQUIRE(s != NULL);
    v = CALL(kernel32, MapViewOfFile, s, FILE_MAP_WRITE, 0, 0, 0);
    T_REQUIRE(v != NULL);
    v[2] = 'X';
    T_EQ(CALL(kernel32, UnmapViewOfFile, v), TRUE);
    T_EQ(host_read("rw.bin", buf, 10), 10);
    T_TRUE(memcmp(buf, "abXdefghij", 10) == 0);
    T_EQ(rt_handle_close(s), TRUE);
    T_EQ(rt_handle_close(f), TRUE);

    T_CASE("success leaves last error alone");
    v = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 0, 4096);
    T_REQUIRE(v != NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, UnmapViewOfFile, v), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
