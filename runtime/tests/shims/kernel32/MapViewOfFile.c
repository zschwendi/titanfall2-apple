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

#define SH       (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define SECTION  131072u        /* 128 KB: two 64 KB units */
#define TEN      "0123456789"   /* the 10 bytes of ten.bin */

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

TEST(kernel32, MapViewOfFile)
{
    static const DWORD rw[] = { FILE_MAP_WRITE, FILE_MAP_WRITE | FILE_MAP_READ, FILE_MAP_ALL_ACCESS };
    HANDLE sec, ro, file, f2, s3;
    char *a, *b, *p;
    char buf[16];
    unsigned i;

    put("ten.bin", TEN, 0644);
    file = rt_file_create(RT_NAME_W(W("C:\\Titanfall2\\ten.bin")), GENERIC_READ, SH, OPEN_EXISTING, 0, NULL);
    T_REQUIRE(file != INVALID_HANDLE_VALUE);
    ro = rt_section_create(file, PAGE_READONLY, 0, NULL);          /* a section of the 10-byte file */
    T_REQUIRE(ro != NULL);
    sec = rt_section_create(INVALID_HANDLE_VALUE, PAGE_READWRITE, SECTION, NULL);   /* reads as zeros */
    T_REQUIRE(sec != NULL);

    T_CASE("a read-write view of the whole section takes a write and a second view shows it");
    for (i = 0; i < 3; i++) {
        a = CALL(kernel32, MapViewOfFile, sec, rw[i], 0, 0, 0);
        T_REQUIRE(a != NULL);
        a[100 + i] = (char)('A' + i);
        b = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 0, 0);
        T_REQUIRE(b != NULL);
        T_EQ(b[100 + i], 'A' + i);
        T_EQ(b[0], 0);
    }

    T_CASE("FILE_MAP_READ of a file shows its bytes");
    a = CALL(kernel32, MapViewOfFile, ro, FILE_MAP_READ, 0, 0, 0);
    T_REQUIRE(a != NULL);
    T_TRUE(memcmp(a, TEN, 10) == 0);

    T_CASE("bytes past the end of the file in its last page read as zeros");
    T_EQ(a[10], 0);
    T_EQ(a[4095], 0);

    T_CASE("an offset of 65536 with size 4096 maps that part of the section");
    a = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_WRITE, 0, 0, 0);
    T_REQUIRE(a != NULL);
    a[65536 + 5] = 'Z';
    b = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 65536, 4096);
    T_REQUIRE(b != NULL);
    T_EQ(b[5], 'Z');
    T_EQ(b[0], 0);

    T_CASE("an offset that is not a multiple of 64 KB fails with ERROR_MAPPED_ALIGNMENT");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 4096, 0);
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_MAPPED_ALIGNMENT);
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 65537, 0);
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_MAPPED_ALIGNMENT);

    T_CASE("a handle that is no section fails with ERROR_INVALID_HANDLE");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, file, FILE_MAP_READ, 0, 0, 0);     /* a file, not a section */
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    s3 = rt_section_create(INVALID_HANDLE_VALUE, PAGE_READWRITE, SECTION, NULL);
    T_REQUIRE(s3 != NULL);
    T_EQ(rt_handle_close(s3), TRUE);
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, s3, FILE_MAP_READ, 0, 0, 0);      /* closed */
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("a read-write view of a read-only section fails with ERROR_ACCESS_DENIED");
    for (i = 0; i < 3; i++) {
        CALL(kernel32, SetLastError, 0x1234);
        p = CALL(kernel32, MapViewOfFile, ro, rw[i], 0, 0, 0);
        T_TRUE(p == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    }

    T_CASE("an offset at or beyond the section's size fails with ERROR_ACCESS_DENIED");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, SECTION, 0);      /* at the end */
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 1, 0, 0);            /* 4 GB: the high word counts */
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, ro, FILE_MAP_READ, 0, 65536, 0);         /* past the 10 bytes */
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);

    T_CASE("a view that ends beyond the section's size rounded up to 4 KB fails with ERROR_ACCESS_DENIED");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, ro, FILE_MAP_READ, 0, 0, 4097);
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, ro, FILE_MAP_READ, 0, 0, 8192);
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 65536, 65537);   /* ends at 131073 */
    T_TRUE(p == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);

    T_CASE("a copy-on-write view takes a write and neither the file nor another view changes");
    a = CALL(kernel32, MapViewOfFile, ro, FILE_MAP_COPY, 0, 0, 0);
    T_REQUIRE(a != NULL);
    a[0] = 'X';
    T_EQ(a[0], 'X');
    b = CALL(kernel32, MapViewOfFile, ro, FILE_MAP_READ, 0, 0, 0);
    T_REQUIRE(b != NULL);
    T_EQ(b[0], '0');
    T_EQ(host_read("ten.bin", buf, 10), 10);
    T_TRUE(memcmp(buf, TEN, 10) == 0);

    T_CASE("the view still works after its section and file handles are closed");
    f2 = rt_file_create(RT_NAME_W(W("C:\\Titanfall2\\ten.bin")), GENERIC_READ, SH, OPEN_EXISTING, 0, NULL);
    T_REQUIRE(f2 != INVALID_HANDLE_VALUE);
    s3 = rt_section_create(f2, PAGE_READONLY, 0, NULL);
    T_REQUIRE(s3 != NULL);
    p = CALL(kernel32, MapViewOfFile, s3, FILE_MAP_READ, 0, 0, 0);
    T_REQUIRE(p != NULL);
    T_EQ(rt_handle_close(s3), TRUE);
    T_EQ(rt_handle_close(f2), TRUE);
    T_TRUE(memcmp(p, TEN, 10) == 0);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    p = CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ, 0, 0, 0);
    T_REQUIRE(p != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The core stops on these, with no guard in the shim; each report contains its text. */
    T_TRAPS(CALL(kernel32, MapViewOfFile, sec, FILE_MAP_EXECUTE, 0, 0, 0), "access 0x20");
    T_TRAPS(CALL(kernel32, MapViewOfFile, sec, 0, 0, 0, 0), "access 0x0");
    T_TRAPS(CALL(kernel32, MapViewOfFile, sec, FILE_MAP_WRITE | FILE_MAP_COPY, 0, 0, 0), "access 0x3");
    T_TRAPS(CALL(kernel32, MapViewOfFile, sec, FILE_MAP_READ | 0x80000000u, 0, 0, 0), "access 0x80000004");
    T_TRAPS(CALL(kernel32, MapViewOfFile, ro, FILE_MAP_READ, 0, 0, 100), "inside its last 4 KB page");
}
