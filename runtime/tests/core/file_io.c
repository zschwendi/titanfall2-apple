/* The read, write and position piece of the file layer (rt/file.h, core/file_priv.h): the
 * plan's cases 1 to 22, one T_PLAN block each.  The functions are stubs today, so every case
 * stops or fails until the body is written.  Files are made under C:\Titanfall2 as in
 * file_lifetime.c; the host side is checked with the host's own calls.
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"

int      rt_file_test_fd(HANDLE h);
unsigned rt_file_test_open_count(void);

#define RD   GENERIC_READ
#define WR   GENERIC_WRITE
#define SH   (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define BAD  INVALID_HANDLE_VALUE

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
static HANDLE cfa(const char *win, DWORD access, DWORD share, DWORD disp, DWORD fa) {
    return rt_file_create(RT_NAME_A(win), access, share, disp, fa, NULL);
}
/* The test file d.bin holds "0123456789", as the plan's setup says. */
static void reset_d(void) { put("d.bin", "0123456789", 0644); }
static HANDLE open_d(DWORD access) { return cfa("C:\\Titanfall2\\d.bin", access, SH, OPEN_EXISTING, 0); }
/* True if the host file `rel` holds exactly the n bytes at `want` (n is at most 64). */
static bool host_is(const char *rel, const void *want, size_t n) {
    char got[64];
    int fd = open(I(rel), O_RDONLY);
    if (fd < 0) return false;
    ssize_t r = read(fd, got, sizeof got);
    close(fd);
    return r == (ssize_t)n && memcmp(got, want, n) == 0;
}

TEST(core, file_io_read)
{
    T_PLAN(1, "read four bytes, then the rest") {
        reset_d();
        HANDLE h = open_d(RD | WR);
        T_REQUIRE(h != BAD);
        char buf[128];
        DWORD got = 77;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_read(h, buf, 4, &got, NULL), TRUE);
        T_EQ(got, 4);
        T_TRUE(memcmp(buf, "0123", 4) == 0);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        got = 77;
        T_EQ(rt_file_read(h, buf, 100, &got, NULL), TRUE);
        T_EQ(got, 6);
        T_TRUE(memcmp(buf, "456789", 6) == 0);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(2, "at the end, len 5 is TRUE with got 0") {
        reset_d();
        HANDLE h = open_d(RD | WR);
        T_REQUIRE(h != BAD);
        char buf[128];
        DWORD got = 77;
        T_EQ(rt_file_read(h, buf, 100, &got, NULL), TRUE);    /* the position is now at the end */
        T_EQ(got, 10);
        got = 77;
        T_EQ(rt_file_read(h, buf, 5, &got, NULL), TRUE);
        T_EQ(got, 0);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(3, "len 0 with buf NULL reads nothing and moves nothing") {
        reset_d();
        HANDLE h = open_d(RD | WR);
        T_REQUIRE(h != BAD);
        const int fd = rt_file_test_fd(h);
        T_EQ(lseek(fd, 3, SEEK_SET), 3);              /* a position that is not 0 */
        DWORD got = 77;
        T_EQ(rt_file_read(h, NULL, 0, &got, NULL), TRUE);
        T_EQ(got, 0);
        T_EQ(lseek(fd, 0, SEEK_CUR), 3);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(4, "an invalid handle: FALSE, ERROR_INVALID_HANDLE, got 0") {
        char buf[16];
        DWORD got = 77;
        T_EQ(rt_file_read(NULL, buf, 4, &got, NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(got, 0);
        got = 77;
        T_EQ(rt_file_read(INVALID_HANDLE_VALUE, buf, 4, &got, NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(got, 0);
        reset_d();
        HANDLE h = open_d(RD);
        T_REQUIRE(h != BAD);
        T_EQ(rt_handle_close(h), TRUE);               /* a closed handle */
        got = 77;
        T_EQ(rt_file_read(h, buf, 4, &got, NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(got, 0);
    }

    T_PLAN(5, "a handle that cannot read or write: ERROR_ACCESS_DENIED") {
        reset_d();
        HANDLE wo = open_d(WR);
        HANDLE ro = open_d(RD);
        T_REQUIRE(wo != BAD && ro != BAD);
        char buf[16];
        DWORD got = 77;
        T_EQ(rt_file_read(wo, buf, 4, &got, NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(got, 0);
        DWORD wrote = 77;
        T_EQ(rt_file_write(ro, "x", 1, &wrote, NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(wrote, 0);
        T_TRUE(host_is("d.bin", "0123456789", 10));
        T_EQ(rt_handle_close(wo), TRUE);
        T_EQ(rt_handle_close(ro), TRUE);
    }

    T_PLAN(6, "a NULL buffer away from the end: ERROR_NOACCESS") {
        reset_d();
        HANDLE ro = open_d(RD);
        T_REQUIRE(ro != BAD);
        DWORD got = 77;
        T_EQ(rt_file_read(ro, NULL, 4, &got, NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_NOACCESS);
        T_EQ(rt_handle_close(ro), TRUE);
    }

    T_PLAN(7, "a NULL got traps rt_file_read and rt_file_write") {
        reset_d();
        HANDLE rw = open_d(RD | WR);
        T_REQUIRE(rw != BAD);
        char buf[16] = "abcd";
        T_TRAPS(rt_file_read(rw, buf, 4, NULL, NULL), "rt_file_read: not implemented");
        T_TRAPS(rt_file_write(rw, buf, 4, NULL, NULL), "rt_file_write: not implemented");
        T_EQ(rt_handle_close(rw), TRUE);
    }

    T_PLAN(23, "no OVERLAPPED on a handle opened with FILE_FLAG_OVERLAPPED: ERROR_INVALID_PARAMETER") {
        reset_d();
        HANDLE h = cfa("C:\\Titanfall2\\d.bin", RD | WR, SH, OPEN_EXISTING, FILE_FLAG_OVERLAPPED);
        HANDLE wo = cfa("C:\\Titanfall2\\d.bin", WR, SH, OPEN_EXISTING, FILE_FLAG_OVERLAPPED);
        T_REQUIRE(h != BAD && wo != BAD);
        char buf[16] = "----";
        static const DWORD lens[2] = { 4, 0 };              /* a length of 0 is refused too */
        for (unsigned i = 0; i < 2; i++) {
            DWORD n = 77;
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_file_read(h, buf, lens[i], &n, NULL), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
            T_EQ(n, 0);
            n = 77;
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_file_write(h, "xy", lens[i] / 2, &n, NULL), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
            T_EQ(n, 0);
        }
        T_TRUE(memcmp(buf, "----", 4) == 0);
        T_TRUE(host_is("d.bin", "0123456789", 10));
        T_EQ(lseek(rt_file_test_fd(h), 0, SEEK_CUR), 0);
        DWORD n = 77;                                       /* the access test comes first */
        T_EQ(rt_file_read(wo, buf, 4, &n, NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(wo), TRUE);
    }

    T_PLAN(8, "an OVERLAPPED is given: the delegation to rt_file_ov_read and _write") {
        reset_d();
        HANDLE rw = open_d(RD | WR);
        T_REQUIRE(rw != BAD);
        char buf[16] = "abcd";
        OVERLAPPED ov;
        memset(&ov, 0, sizeof ov);
        /* The transfer is at the OVERLAPPED's offset, not at the position (0), and its result
         * is in the OVERLAPPED too: the work of core/file_ov.c (tests/core/file_ov.c). */
        ov.Offset = 6;
        DWORD got = 77;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_read(rw, buf, 3, &got, &ov), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(got, 3);
        T_TRUE(memcmp(buf, "678", 3) == 0);
        T_EQ(ov.Internal, STATUS_SUCCESS);
        T_EQ(ov.InternalHigh, 3);
        ov.Offset = 2;
        got = 77;
        T_EQ(rt_file_write(rw, "XY", 2, &got, &ov), TRUE);
        T_EQ(got, 2);
        T_EQ(ov.InternalHigh, 2);
        T_TRUE(host_is("d.bin", "01XY456789", 10));
        /* Steps 1 to 3 come before the delegation: NULL for the count is allowed with an
         * OVERLAPPED, the count is zeroed first, and a bad handle never reaches it. */
        T_EQ(rt_file_read(rw, buf, 2, NULL, &ov), TRUE);
        T_TRUE(memcmp(buf, "XY", 2) == 0);
        T_EQ(rt_file_write(rw, "Q", 1, NULL, &ov), TRUE);
        T_TRUE(host_is("d.bin", "01QY456789", 10));
        ov.Internal = 0x5A5A;
        got = 77;
        T_EQ(rt_file_read(NULL, buf, 2, &got, &ov), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(got, 0);
        got = 77;
        T_EQ(rt_file_write(INVALID_HANDLE_VALUE, buf, 2, &got, &ov), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(got, 0);
        T_EQ(ov.Internal, 0x5A5A);                          /* the OVERLAPPED was not looked at */
        T_EQ(rt_handle_close(rw), TRUE);
        T_EQ(rt_file_test_open_count(), 3);
    }
}

/* The host size of `rel`, or -1. */
static off_t host_size(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) == 0 ? st.st_size : -1;
}
/* Reads up to cap bytes of the host file `rel` into out; returns the count, or -1. */
static ssize_t host_read_all(const char *rel, char *out, size_t cap) {
    int fd = open(I(rel), O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, out, cap);
    close(fd);
    return r;
}

TEST(core, file_io_write)
{
    T_PLAN(9, "write three bytes to a new file; len 0 writes nothing") {
        HANDLE h = cfa("C:\\Titanfall2\\w.bin", WR, SH, CREATE_ALWAYS, 0);
        T_REQUIRE(h != BAD);
        DWORD wrote = 77;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_write(h, "abc", 3, &wrote, NULL), TRUE);
        T_EQ(wrote, 3);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(host_is("w.bin", "abc", 3));
        wrote = 77;
        T_EQ(rt_file_write(h, "zzz", 0, &wrote, NULL), TRUE);
        T_EQ(wrote, 0);
        T_EQ(host_size("w.bin"), 3);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(10, "a seek to 10 and a write of Z leave a zero gap") {
        HANDLE h = cfa("C:\\Titanfall2\\w.bin", WR, SH, CREATE_ALWAYS, 0);
        T_REQUIRE(h != BAD);
        T_EQ(rt_file_seek(h, 10, NULL, FILE_BEGIN), TRUE);
        DWORD wrote = 77;
        T_EQ(rt_file_write(h, "Z", 1, &wrote, NULL), TRUE);
        T_EQ(wrote, 1);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(host_size("w.bin"), 11);
        char b[64] = { 0 };
        T_EQ(host_read_all("w.bin", b, sizeof b), 11);
        unsigned nonzero = 0;
        for (int i = 3; i <= 9; i++) nonzero += b[i] != 0;
        T_EQ(nonzero, 0);
        T_EQ(b[10], 'Z');
    }

    T_PLAN(11, "the NUL device takes a write and reads as end of file") {
        HANDLE nul = cfa("NUL", RD | WR, SH, OPEN_EXISTING, 0);
        T_REQUIRE(nul != BAD);
        DWORD wrote = 77;
        T_EQ(rt_file_write(nul, "gone", 4, &wrote, NULL), TRUE);
        T_EQ(wrote, 4);
        char buf[16];
        DWORD got = 77;
        T_EQ(rt_file_read(nul, buf, 4, &got, NULL), TRUE);
        T_EQ(got, 0);
        T_EQ(rt_handle_close(nul), TRUE);
    }
}

TEST(core, file_io_seek)
{
    T_PLAN(12, "seek by each method; new_pos NULL; a position past the end") {
        reset_d();
        HANDLE h = open_d(RD | WR);
        T_REQUIRE(h != BAD);
        LONGLONG pos = 77;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_seek(h, 3, &pos, FILE_BEGIN), TRUE);
        T_EQ(pos, 3);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_seek(h, 2, &pos, FILE_CURRENT), TRUE);
        T_EQ(pos, 5);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_seek(h, -1, &pos, FILE_CURRENT), TRUE);
        T_EQ(pos, 4);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_seek(h, 0, &pos, FILE_END), TRUE);
        T_EQ(pos, 10);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_seek(h, -4, &pos, FILE_END), TRUE);
        T_EQ(pos, 6);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_seek(h, 2, NULL, FILE_BEGIN), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_seek(h, 100, &pos, FILE_BEGIN), TRUE);
        T_EQ(pos, 100);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(host_size("d.bin"), 10);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(13, "a bad method and a position before the start: FALSE, the position unchanged") {
        reset_d();
        HANDLE h = open_d(RD | WR);
        T_REQUIRE(h != BAD);
        const int fd = rt_file_test_fd(h);
        T_EQ(lseek(fd, 4, SEEK_SET), 4);
        LONGLONG pos = 77;
        T_EQ(rt_file_seek(h, 0, &pos, 3), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_EQ(pos, 77);
        T_EQ(lseek(fd, 0, SEEK_CUR), 4);
        T_EQ(rt_file_seek(h, -1, &pos, FILE_BEGIN), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_NEGATIVE_SEEK);
        T_EQ(pos, 77);
        T_EQ(lseek(fd, 0, SEEK_CUR), 4);
        pos = 77;
        T_EQ(rt_file_seek(h, -5, &pos, FILE_CURRENT), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_NEGATIVE_SEEK);
        T_EQ(lseek(fd, 0, SEEK_CUR), 4);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(14, "a duplicate shares the position") {
        reset_d();
        HANDLE h = open_d(RD);
        T_REQUIRE(h != BAD);
        HANDLE d = rt_handle_dup(h);
        T_REQUIRE(d != NULL);
        T_EQ(rt_file_seek(h, 2, NULL, FILE_BEGIN), TRUE);
        char buf[4] = { 0 };
        DWORD got = 77;
        T_EQ(rt_file_read(d, buf, 1, &got, NULL), TRUE);
        T_EQ(got, 1);
        T_EQ(buf[0], '2');
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(d), TRUE);
    }
}

TEST(core, file_io_size_eof)
{
    T_PLAN(16, "the size of a read-only and of a write-only handle; it follows the host") {
        reset_d();
        HANDLE ro = open_d(RD);
        HANDLE wo = open_d(WR);
        T_REQUIRE(ro != BAD && wo != BAD);
        LONGLONG sz = 77;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_size(ro, &sz), TRUE);
        T_EQ(sz, 10);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        int fd = open(I("d.bin"), O_WRONLY | O_APPEND);
        T_REQUIRE(fd >= 0);
        T_EQ(write(fd, "klmno", 5), 5);
        close(fd);
        T_EQ(rt_file_size(ro, &sz), TRUE);
        T_EQ(sz, 15);
        sz = 77;
        T_EQ(rt_file_size(wo, &sz), TRUE);
        T_EQ(sz, 15);
        T_TRAPS(rt_file_size(ro, NULL), "rt_file_size: not implemented");
        T_EQ(rt_handle_close(ro), TRUE);
        T_EQ(rt_handle_close(wo), TRUE);
    }

    T_PLAN(17, "set the end of file at the position; zeros fill; a read-only handle is refused") {
        reset_d();
        HANDLE rw = open_d(RD | WR);
        HANDLE ro = open_d(RD);
        T_REQUIRE(rw != BAD && ro != BAD);
        const int fd = rt_file_test_fd(rw);
        T_EQ(rt_file_seek(rw, 4, NULL, FILE_BEGIN), TRUE);
        T_EQ(rt_file_set_eof(rw), TRUE);
        T_EQ(host_size("d.bin"), 4);
        T_EQ(lseek(fd, 0, SEEK_CUR), 4);
        T_EQ(rt_file_seek(rw, 20, NULL, FILE_BEGIN), TRUE);
        T_EQ(rt_file_set_eof(rw), TRUE);
        T_EQ(host_size("d.bin"), 20);
        char b[64] = { 0 };
        T_EQ(host_read_all("d.bin", b, sizeof b), 20);
        unsigned nonzero = 0;
        for (int i = 4; i <= 19; i++) nonzero += b[i] != 0;
        T_EQ(nonzero, 0);
        T_EQ(rt_file_set_eof(ro), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(host_size("d.bin"), 20);
        T_EQ(rt_handle_close(rw), TRUE);
        T_EQ(rt_handle_close(ro), TRUE);
    }

    T_PLAN(18, "flush: TRUE on a handle that writes, ERROR_ACCESS_DENIED on one that cannot") {
        reset_d();
        HANDLE rw = open_d(RD | WR);
        HANDLE ro = open_d(RD);
        T_REQUIRE(rw != BAD && ro != BAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_flush(rw), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(rt_file_flush(ro), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(rt_handle_close(rw), TRUE);
        T_EQ(rt_handle_close(ro), TRUE);
    }
}

TEST(core, file_io_stops)
{
    T_PLAN(15, "seek, size, flush and set_eof on NULL: FALSE, ERROR_INVALID_HANDLE") {
        LONGLONG v = 77;
        T_EQ(rt_file_seek(NULL, 0, &v, FILE_BEGIN), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(rt_file_size(NULL, &v), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(rt_file_flush(NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(rt_file_set_eof(NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }

    T_PLAN(19, "the stops: seek, size, flush and set_eof on NUL, and every call on a directory") {
        mkdir(I("Dir"), 0755);
        HANDLE nul = cfa("NUL", RD | WR, SH, OPEN_EXISTING, 0);
        HANDLE dir = cfa("C:\\Titanfall2\\Dir", RD, SH, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
        T_REQUIRE(nul != BAD && dir != BAD);
        char buf[16] = "abcd";
        DWORD n = 0;
        LONGLONG v = 0;
        T_TRAPS(rt_file_seek(nul, 0, &v, FILE_BEGIN), "rt_file_seek: not implemented");
        T_TRAPS(rt_file_size(nul, &v), "rt_file_size: not implemented");
        T_TRAPS(rt_file_flush(nul), "rt_file_flush: not implemented");
        T_TRAPS(rt_file_set_eof(nul), "rt_file_set_eof: not implemented");
        T_TRAPS(rt_file_read(dir, buf, 4, &n, NULL), "rt_file_read: not implemented");
        T_TRAPS(rt_file_write(dir, buf, 4, &n, NULL), "rt_file_write: not implemented");
        T_TRAPS(rt_file_seek(dir, 0, &v, FILE_BEGIN), "rt_file_seek: not implemented");
        T_TRAPS(rt_file_size(dir, &v), "rt_file_size: not implemented");
        T_TRAPS(rt_file_flush(dir), "rt_file_flush: not implemented");
        T_TRAPS(rt_file_set_eof(dir), "rt_file_set_eof: not implemented");
        T_EQ(rt_handle_close(nul), TRUE);
        T_EQ(rt_handle_close(dir), TRUE);
    }

    T_PLAN(20, "no leak: a fresh handle, closed, drops the open count by one") {
        reset_d();
        const unsigned before = rt_file_test_open_count();
        HANDLE h = open_d(RD | WR);
        T_REQUIRE(h != BAD);
        T_EQ(rt_file_test_open_count(), before + 1);
        char buf[16] = { 0 };
        DWORD got = 77;
        T_EQ(rt_file_read(h, buf, 4, &got, NULL), TRUE);
        T_EQ(got, 4);
        LONGLONG sz = 0;
        T_EQ(rt_file_size(h, &sz), TRUE);
        T_EQ(sz, 10);
        T_EQ(rt_file_flush(h), TRUE);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_file_test_open_count(), before);
    }
}

/* ---- several threads -------------------------------------------------------------------------- */

#define NTHREAD 8
#define NREC    4000                    /* case 21: records of 4 bytes; record i holds i */
#define WPER    500                     /* case 22: records per writer thread */

static HANDLE rd_h, wr_h;              /* set by the main thread before the threads start */
static atomic_uint seen[NREC];
static atomic_uint short_reads, read_fail, out_of_range, wrote_bad;
static unsigned char wrec[NTHREAD * WPER * 4];

/* A file of count records, record i holding i in four bytes, little-endian. */
static void make_records(const char *rel, unsigned count) {
    unsigned char b[NREC * 4];
    for (unsigned i = 0; i < count; i++) {
        b[4 * i + 0] = (unsigned char)(i & 0xff);
        b[4 * i + 1] = (unsigned char)((i >> 8) & 0xff);
        b[4 * i + 2] = (unsigned char)((i >> 16) & 0xff);
        b[4 * i + 3] = (unsigned char)((i >> 24) & 0xff);
    }
    int fd = open(I(rel), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0 || write(fd, b, count * 4u) != (ssize_t)(count * 4u)) rt_test_fail(__FILE__, __LINE__, "make %s", rel);
    if (fd >= 0) close(fd);
}

static void *reader(void *arg) {
    (void)arg;
    for (;;) {
        unsigned char buf[4];
        DWORD got = 0;
        if (!rt_file_read(rd_h, buf, 4, &got, NULL)) { atomic_fetch_add(&read_fail, 1); break; }
        if (got == 0) break;                                   /* end of file */
        if (got != 4) { atomic_fetch_add(&short_reads, 1); continue; }
        const uint32_t v = (uint32_t)buf[0] | (uint32_t)buf[1] << 8 | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;
        if (v >= NREC) atomic_fetch_add(&out_of_range, 1);
        else atomic_fetch_add(&seen[v], 1);
    }
    return NULL;
}

static void *writer(void *arg) {
    const unsigned k = (unsigned)(uintptr_t)arg;               /* this thread's number */
    unsigned char rec[4] = { (unsigned char)k, 0, 0, 0 };
    for (unsigned n = 0; n < WPER; n++) {
        DWORD wrote = 0;
        if (!rt_file_write(wr_h, rec, 4, &wrote, NULL) || wrote != 4) atomic_fetch_add(&wrote_bad, 1);
    }
    return NULL;
}

TEST(core, file_io_threads_read)
{
    T_PLAN(21, "eight threads read 4-byte records through one handle: each record exactly once") {
        make_records("rec.bin", NREC);
        HANDLE h = cfa("C:\\Titanfall2\\rec.bin", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD);
        rd_h = h;
        pthread_t t[NTHREAD];
        for (int i = 0; i < NTHREAD; i++) T_REQUIRE(pthread_create(&t[i], NULL, reader, NULL) == 0);
        for (int i = 0; i < NTHREAD; i++) pthread_join(t[i], NULL);
        unsigned wrong = 0;
        for (unsigned k = 0; k < NREC; k++) wrong += atomic_load(&seen[k]) != 1;
        T_EQ(wrong, 0);
        T_EQ(atomic_load(&short_reads), 0);
        T_EQ(atomic_load(&read_fail), 0);
        T_EQ(atomic_load(&out_of_range), 0);
        T_EQ(rt_handle_close(h), TRUE);
    }
}

TEST(core, file_io_threads_write)
{
    T_PLAN(22, "eight threads write 500 records each through one handle: each number 500 times") {
        HANDLE h = cfa("C:\\Titanfall2\\wrec.bin", WR, SH, CREATE_ALWAYS, 0);
        T_REQUIRE(h != BAD);
        wr_h = h;
        pthread_t t[NTHREAD];
        for (int i = 0; i < NTHREAD; i++) T_REQUIRE(pthread_create(&t[i], NULL, writer, (void *)(uintptr_t)i) == 0);
        for (int i = 0; i < NTHREAD; i++) pthread_join(t[i], NULL);
        T_EQ(atomic_load(&wrote_bad), 0);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(host_size("wrec.bin"), NTHREAD * WPER * 4);
        T_EQ(host_read_all("wrec.bin", (char *)wrec, sizeof wrec), NTHREAD * WPER * 4);
        unsigned cnt[NTHREAD] = { 0 }, bad = 0;
        for (unsigned j = 0; j < NTHREAD * WPER; j++) {
            const unsigned v = wrec[4 * j];
            if (wrec[4 * j + 1] | wrec[4 * j + 2] | wrec[4 * j + 3]) bad++;
            else if (v < NTHREAD) cnt[v]++;
            else bad++;
        }
        T_EQ(bad, 0);
        unsigned wrong = 0;
        for (unsigned k = 0; k < NTHREAD; k++) wrong += cnt[k] != WPER;
        T_EQ(wrong, 0);
    }
}
