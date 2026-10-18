/* The overlapped piece of the file layer (rt/file.h, OVERLAPPED; core/file_ov.c): reads and
 * writes with an OVERLAPPED through rt_file_read and rt_file_write, rt_file_overlapped_result
 * and rt_file_read_ex.  The plan is the list of docs/handoff/rt-contract-file.md for
 * rt-file-ov, refined: cases 1 to 31, one T_PLAN block each; docs/handoff/rt-file-ov.md has
 * the table and says which wrong body each case was seen to catch.
 *
 * COMMON TO EVERY TEST FILE of the file layer (docs/handoff/rt-contract-file.md):
 * - The shape and the helpers I, put and cfa are those of file_lifetime.c.  Files are made on
 *   the host under rt_test_install_root() (which is C:\Titanfall2) and opened with
 *   rt_file_create.  The test hooks rt_file_test_fd and rt_file_test_open_count are declared
 *   by hand.
 * - Several TEST(core, file_ov_<group>) in the file; a numbered case before each group of
 *   checks (T_PLAN, which is T_CASE with the plan's number).  A block stands alone: what it
 *   needs it makes itself, so that RT_TEST_PLAN_CASE=<n> can run it without the others.
 * - "LE untouched" means: rt_thread_set_last_error(0xDEAD) before the call,
 *   T_EQ(T_LAST_ERROR(), 0xDEAD) after.
 * - "traps F" means T_TRAPS(call, "F: not implemented").
 * - Threads are plain host pthreads that only add to atomic_uint counters; the test thread
 *   checks the counters after pthread_join.  Nothing waits for a length of time: every thread
 *   does a fixed amount of work and ends.
 *
 * A completion routine is a guest function, so it can only run on a real guest thread: every
 * alertable wait here is made by the test's main thread (guest thread 1).  The one hand-made
 * guest thread (case 30, a Worker of wait_util.c) only queues.
 */
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include "wait_util.c"

#include "rt/file.h"
#include "rt/sync.h"

int      rt_file_test_fd(HANDLE h);
unsigned rt_file_test_open_count(void);

#define CTX  T_CTX()
#define ME   RT_THREAD(CTX)
#define RD   GENERIC_READ
#define WR   GENERIC_WRITE
#define SH   (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define BAD  INVALID_HANDLE_VALUE
#define OVL  FILE_FLAG_OVERLAPPED
#define JUNK 0x5A5A5A5Au                /* what Internal and InternalHigh hold before a call */
#define FAILED_STATUS 0xC0000001u       /* Internal after a call that failed outright (core/file_ov.c) */

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

/* o.bin holds "0123456789"; open_o opens it, with FILE_FLAG_OVERLAPPED or 0 as `flag`. */
static void reset_o(void) { put("o.bin", "0123456789", 0644); }
static HANDLE open_o(DWORD access, DWORD flag) { return cfa("C:\\Titanfall2\\o.bin", access, SH, OPEN_EXISTING, flag); }
static off_t pos(HANDLE h) { return lseek(rt_file_test_fd(h), 0, SEEK_CUR); }
static void set_pos(HANDLE h, off_t to) { lseek(rt_file_test_fd(h), to, SEEK_SET); }
static off_t host_size(const char *rel) { struct stat st; return stat(I(rel), &st) == 0 ? st.st_size : -1; }
static bool host_is(const char *rel, const void *want, size_t n) {
    char got[64];
    int fd = open(I(rel), O_RDONLY);
    if (fd < 0) return false;
    ssize_t r = read(fd, got, sizeof got);
    close(fd);
    return r == (ssize_t)n && memcmp(got, want, n) == 0;
}
/* An OVERLAPPED for a request at `offset`, with junk where the result goes. */
static OVERLAPPED at(uint64_t offset, HANDLE event) {
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.Internal = JUNK;
    ov.InternalHigh = JUNK;
    ov.Offset = (DWORD)offset;
    ov.OffsetHigh = (DWORD)(offset >> 32);
    ov.hEvent = event;
    return ov;
}
static HANDLE new_event(BOOL manual) { return rt_event_create(CTX, manual, FALSE, NULL); }

/* ---- reads with an OVERLAPPED ------------------------------------------------------------------ */

TEST(core, file_ov_read)
{
    char buf[128];
    DWORD got;

    T_PLAN(1, "a handle without the flag: the read is at the offset and the position follows it") {
        reset_o();
        HANDLE h = open_o(RD | WR, 0);
        T_REQUIRE(h != BAD);
        set_pos(h, 1);
        OVERLAPPED ov = at(4, NULL);
        got = 77;
        memset(buf, 0, sizeof buf);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_read(h, buf, 3, &got, &ov), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);                       /* LE untouched */
        T_EQ(got, 3);
        T_TRUE(memcmp(buf, "456", 4) == 0);                 /* three bytes and no more */
        T_EQ(ov.Internal, STATUS_SUCCESS);
        T_EQ(ov.InternalHigh, 3);
        T_EQ(pos(h), 7);                                    /* offset + bytes */
        T_EQ(ov.Offset, 4);                                 /* "the system will not modify these values" */
        T_EQ(ov.OffsetHigh, 0);
        T_EQ(rt_file_read(h, buf, 2, &got, NULL), TRUE);    /* and a plain read goes on from there */
        T_TRUE(got == 2 && memcmp(buf, "78", 2) == 0);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_file_test_open_count(), 3);
    }

    T_PLAN(2, "a handle with the flag keeps its position") {
        reset_o();
        HANDLE h = open_o(RD | WR, OVL);
        T_REQUIRE(h != BAD);
        set_pos(h, 2);
        OVERLAPPED ov = at(4, NULL);
        got = 77;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_read(h, buf, 3, &got, &ov), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(got, 3);
        T_TRUE(memcmp(buf, "456", 3) == 0);
        T_EQ(ov.Internal, STATUS_SUCCESS);
        T_EQ(ov.InternalHigh, 3);
        T_EQ(pos(h), 2);
        ov = at(0, NULL);
        T_EQ(rt_file_read(h, buf, 10, &got, &ov), TRUE);    /* the whole file, from an offset before the position */
        T_TRUE(got == 10 && memcmp(buf, "0123456789", 10) == 0);
        T_EQ(pos(h), 2);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(3, "hEvent, reset before, is set afterwards") {
        reset_o();
        HANDLE h = open_o(RD, OVL), s = open_o(RD, 0);
        HANDLE manual = new_event(TRUE), autor = new_event(FALSE);
        T_REQUIRE(h != BAD && s != BAD && manual && autor);
        OVERLAPPED ov = at(1, manual);
        T_EQ(rt_file_read(h, buf, 2, &got, &ov), TRUE);
        T_EQ(wu_state(manual).set, true);
        T_EQ(ov.hEvent, manual);
        ov = at(1, autor);
        T_EQ(rt_file_read(h, buf, 2, &got, &ov), TRUE);
        T_EQ(wu_state(autor).set, true);
        T_EQ(rt_wait_handle(CTX, autor, 0, FALSE), WAIT_OBJECT_0);     /* the one wait it satisfies */
        rt_event_reset(CTX, manual);
        ov = at(1, manual);
        T_EQ(rt_file_read(s, buf, 2, &got, &ov), TRUE);                 /* a handle without the flag too */
        T_EQ(wu_state(manual).set, true);
        T_EQ(rt_handle_close(manual), TRUE);                            /* no reference to the event is kept */
        T_EQ(rt_handle_close(autor), TRUE);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(s), TRUE);
    }

    T_PLAN(4, "a read that reaches the end: TRUE with the bytes there are") {
        reset_o();
        HANDLE h = open_o(RD, OVL);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(8, NULL);
        got = 77;
        memset(buf, 0, sizeof buf);
        T_EQ(rt_file_read(h, buf, 100, &got, &ov), TRUE);
        T_EQ(got, 2);
        T_TRUE(memcmp(buf, "89", 3) == 0);
        T_EQ(ov.Internal, STATUS_SUCCESS);
        T_EQ(ov.InternalHigh, 2);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(5, "a read that begins at or beyond the end: FALSE, ERROR_HANDLE_EOF, the event set") {
        reset_o();
        HANDLE h = open_o(RD, OVL), e = new_event(TRUE);
        T_REQUIRE(h != BAD && e);
        set_pos(h, 3);
        static const uint64_t where[3] = { 10, 1000, UINT64_C(0x100000004) };  /* the last: OffsetHigh counts */
        for (unsigned i = 0; i < 3; i++) {
            OVERLAPPED ov = at(where[i], e);
            rt_event_reset(CTX, e);
            got = 77;
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_file_read(h, buf, 5, &got, &ov), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_HANDLE_EOF);
            T_EQ(got, 0);
            T_EQ(ov.Internal, STATUS_END_OF_FILE);
            T_EQ(ov.InternalHigh, 0);
            T_EQ(wu_state(e).set, true);
        }
        T_EQ(pos(h), 3);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(e), TRUE);
    }

    T_PLAN(6, "got may be NULL: the result is in the OVERLAPPED") {
        reset_o();
        HANDLE h = open_o(RD, OVL);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(6, NULL);
        T_EQ(rt_file_read(h, buf, 4, NULL, &ov), TRUE);
        T_TRUE(memcmp(buf, "6789", 4) == 0);
        T_EQ(ov.Internal, STATUS_SUCCESS);
        T_EQ(ov.InternalHigh, 4);
        ov = at(10, NULL);
        T_EQ(rt_file_read(h, buf, 4, NULL, &ov), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_HANDLE_EOF);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(7, "len 0: TRUE with nothing read, at the end of the file too") {
        reset_o();
        HANDLE h = open_o(RD, OVL), s = open_o(RD, 0), e = new_event(TRUE);
        T_REQUIRE(h != BAD && s != BAD && e);
        static const uint64_t where[2] = { 3, 10 };
        for (unsigned i = 0; i < 2; i++) {
            OVERLAPPED ov = at(where[i], e);
            rt_event_reset(CTX, e);
            got = 77;
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_file_read(h, NULL, 0, &got, &ov), TRUE);
            T_EQ(T_LAST_ERROR(), 0xDEAD);
            T_EQ(got, 0);
            T_EQ(ov.Internal, STATUS_SUCCESS);
            T_EQ(ov.InternalHigh, 0);
            T_EQ(wu_state(e).set, true);
        }
        OVERLAPPED ov = at(6, NULL);
        T_EQ(rt_file_read(s, NULL, 0, &got, &ov), TRUE);    /* without the flag: the position is offset + 0 */
        T_EQ(pos(s), 6);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(s), TRUE);
        T_EQ(rt_handle_close(e), TRUE);
    }

    T_PLAN(8, "a call that fails outright leaves no old result in the OVERLAPPED") {
        reset_o();
        HANDLE wo = open_o(WR, OVL), ro = open_o(RD, OVL), e = new_event(TRUE);
        T_REQUIRE(wo != BAD && ro != BAD && e);
        OVERLAPPED ov = at(0, e);
        ov.Internal = STATUS_SUCCESS;                       /* as an earlier request left it */
        ov.InternalHigh = 99;
        got = 77;
        T_EQ(rt_file_read(wo, buf, 4, &got, &ov), FALSE);  /* the handle cannot read */
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(got, 0);
        T_EQ(ov.Internal, FAILED_STATUS);
        T_EQ(ov.InternalHigh, 0);
        T_EQ(wu_state(e).set, true);
        ov = at(2, NULL);
        ov.Internal = STATUS_SUCCESS;
        got = 77;
        T_EQ(rt_file_read(ro, NULL, 4, &got, &ov), FALSE); /* a buffer the host refuses */
        T_EQ(T_LAST_ERROR(), ERROR_NOACCESS);
        T_EQ(got, 0);
        T_EQ(ov.Internal, FAILED_STATUS);
        T_EQ(rt_handle_close(wo), TRUE);
        T_EQ(rt_handle_close(ro), TRUE);
        T_EQ(rt_handle_close(e), TRUE);
    }

    T_PLAN(9, "the stops of a read with an OVERLAPPED") {
        reset_o();
        mkdir(I("OvDir"), 0755);
        HANDLE h = open_o(RD, OVL), s = open_o(RD, 0);
        HANDLE nul = cfa("NUL", RD | WR, SH, OPEN_EXISTING, 0);
        HANDLE dir = cfa("C:\\Titanfall2\\OvDir", 0, SH, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
        T_REQUIRE(h != BAD && s != BAD && nul != BAD && dir != BAD);
        OVERLAPPED ov = at(0, s);                           /* hEvent is a file handle */
        T_TRAPS(rt_file_read(h, buf, 4, &got, &ov), "rt_file_read: not implemented");
        ov = at(0, (HANDLE)(uintptr_t)0x12344);             /* ... is no handle at all */
        T_TRAPS(rt_file_read(h, buf, 4, &got, &ov), "rt_file_read: not implemented");
        ov = at(UINT64_MAX, NULL);                          /* 0xFFFFFFFF:0xFFFFFFFF */
        T_TRAPS(rt_file_read(h, buf, 4, &got, &ov), "rt_file_read: not implemented");
        ov = at(UINT64_C(0x8000000000000000), NULL);
        T_TRAPS(rt_file_read(h, buf, 4, &got, &ov), "rt_file_read: not implemented");
        ov = at(0, NULL);
        T_TRAPS(rt_file_read(nul, buf, 4, &got, &ov), "rt_file_read: not implemented");
        T_TRAPS(rt_file_read(dir, buf, 4, &got, &ov), "rt_file_read: not implemented");
        /* The end of the file on a handle without the flag is no stop: FALSE with
         * ERROR_HANDLE_EOF, the result in the OVERLAPPED, the position where it was. */
        ov = at(10, NULL);
        set_pos(s, 2);
        got = 77;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_read(s, buf, 4, &got, &ov), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_HANDLE_EOF);
        T_EQ(got, 0);
        T_EQ(ov.Internal, STATUS_END_OF_FILE);
        T_EQ(ov.InternalHigh, 0);
        T_EQ(pos(s), 2);
        ov = at(9, NULL);                                   /* one byte before it is an ordinary read */
        T_EQ(rt_file_read(s, buf, 4, &got, &ov), TRUE);
        T_EQ(got, 1);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(s), TRUE);
        T_EQ(rt_handle_close(nul), TRUE);
        T_EQ(rt_handle_close(dir), TRUE);
    }
    T_EQ(rt_file_test_open_count(), 3);
}

/* ---- writes with an OVERLAPPED ----------------------------------------------------------------- */

TEST(core, file_ov_write)
{
    DWORD n;

    T_PLAN(10, "a write at the offset; a handle with the flag keeps its position") {
        reset_o();
        HANDLE h = open_o(RD | WR, OVL), e = new_event(TRUE);
        T_REQUIRE(h != BAD && e);
        set_pos(h, 9);
        OVERLAPPED ov = at(3, e);
        n = 77;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_write(h, "ab", 2, &n, &ov), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(n, 2);
        T_TRUE(host_is("o.bin", "012ab56789", 10));
        T_EQ(ov.Internal, STATUS_SUCCESS);
        T_EQ(ov.InternalHigh, 2);
        T_EQ(ov.Offset, 3);
        T_EQ(wu_state(e).set, true);
        T_EQ(pos(h), 9);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(e), TRUE);
    }

    T_PLAN(11, "a handle without the flag: the position follows the write") {
        reset_o();
        HANDLE h = open_o(RD | WR, 0);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(5, NULL);
        T_EQ(rt_file_write(h, "XYZ", 3, &n, &ov), TRUE);
        T_EQ(n, 3);
        T_EQ(pos(h), 8);
        T_EQ(rt_file_write(h, "!", 1, &n, NULL), TRUE);     /* a plain write goes on from there */
        T_TRUE(host_is("o.bin", "01234XYZ!9", 10));
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(12, "a write at an offset beyond the end extends the file with zeros") {
        reset_o();
        HANDLE h = open_o(RD | WR, OVL);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(14, NULL);
        T_EQ(rt_file_write(h, "ZZ", 2, NULL, &ov), TRUE);   /* put may be NULL */
        T_EQ(ov.InternalHigh, 2);
        T_TRUE(host_is("o.bin", "0123456789\0\0\0\0ZZ", 16));
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(13, "len 0 writes nothing and extends nothing") {
        reset_o();
        HANDLE h = open_o(RD | WR, OVL);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(100, NULL);
        n = 77;
        T_EQ(rt_file_write(h, NULL, 0, &n, &ov), TRUE);
        T_EQ(n, 0);
        T_EQ(ov.Internal, STATUS_SUCCESS);
        T_EQ(ov.InternalHigh, 0);
        T_EQ(host_size("o.bin"), 10);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(14, "a handle that cannot write: FALSE, ERROR_ACCESS_DENIED, the file unchanged") {
        reset_o();
        HANDLE ro = open_o(RD, OVL), e = new_event(TRUE);
        T_REQUIRE(ro != BAD && e);
        OVERLAPPED ov = at(0, e);
        ov.Internal = STATUS_SUCCESS;
        n = 77;
        T_EQ(rt_file_write(ro, "x", 1, &n, &ov), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(n, 0);
        T_EQ(ov.Internal, FAILED_STATUS);
        T_EQ(ov.InternalHigh, 0);
        T_EQ(wu_state(e).set, true);
        T_TRUE(host_is("o.bin", "0123456789", 10));
        T_EQ(rt_handle_close(ro), TRUE);
        T_EQ(rt_handle_close(e), TRUE);
    }

    T_PLAN(15, "the stops of a write with an OVERLAPPED") {
        reset_o();
        HANDLE h = open_o(RD | WR, OVL);
        HANDLE nul = cfa("NUL", RD | WR, SH, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD && nul != BAD);
        OVERLAPPED ov = at(UINT64_MAX, NULL);               /* the documented "write at the end of the file" */
        T_TRAPS(rt_file_write(h, "x", 1, &n, &ov), "rt_file_write: not implemented");
        ov = at(0, h);                                      /* hEvent is a file handle */
        T_TRAPS(rt_file_write(h, "x", 1, &n, &ov), "rt_file_write: not implemented");
        ov = at(0, NULL);
        T_TRAPS(rt_file_write(nul, "x", 1, &n, &ov), "rt_file_write: not implemented");
        T_TRUE(host_is("o.bin", "0123456789", 10));
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(nul), TRUE);
    }

    T_PLAN(16, "OffsetHigh is the high half of the offset") {
        /* A write 4 GB into a file: the host keeps the gap sparse.  Only on a file system
         * that does (APFS): anywhere else the gap would be written out. */
        struct statfs fs;
        T_REQUIRE(statfs(rt_test_install_root(), &fs) == 0);
        if (strcmp(fs.f_fstypename, "apfs") != 0) {
            T_TRUE(fs.f_fstypename[0] != 0);                /* nothing to check here */
        } else {
            HANDLE h = cfa("C:\\Titanfall2\\big.bin", RD | WR, SH, CREATE_ALWAYS, OVL);
            T_REQUIRE(h != BAD);
            const uint64_t far = UINT64_C(0x100000008);
            OVERLAPPED ov = at(far, NULL);
            T_EQ(rt_file_write(h, "HIGH", 4, &n, &ov), TRUE);
            T_EQ(host_size("big.bin"), (off_t)(far + 4));
            struct stat st;
            T_EQ(stat(I("big.bin"), &st), 0);
            T_TRUE(st.st_blocks < 4096);                    /* under 2 MB on disk */
            char buf[8] = { 0 };
            ov = at(far + 1, NULL);
            n = 0;
            T_EQ(rt_file_read(h, buf, 8, &n, &ov), TRUE);
            T_EQ(n, 3);
            T_TRUE(memcmp(buf, "IGH", 3) == 0);
            ov = at(8, NULL);                               /* the low half alone is the gap */
            T_EQ(rt_file_read(h, buf, 4, &n, &ov), TRUE);
            T_TRUE(n == 4 && memcmp(buf, "\0\0\0\0", 4) == 0);
            T_EQ(rt_handle_close(h), TRUE);
            unlink(I("big.bin"));
        }
    }
    T_EQ(rt_file_test_open_count(), 3);
}

/* ---- GetOverlappedResult ------------------------------------------------------------------------- */

TEST(core, file_ov_result)
{
    char buf[16];
    DWORD n;

    T_PLAN(17, "the result of a request that succeeded, as often as it is asked for") {
        reset_o();
        HANDLE h = open_o(RD | WR, OVL);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(7, NULL);
        T_EQ(rt_file_read(h, buf, 9, NULL, &ov), TRUE);
        for (int wait = 0; wait < 2; wait++) {
            n = 77;
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_file_overlapped_result(h, &ov, &n, wait), TRUE);
            T_EQ(T_LAST_ERROR(), 0xDEAD);
            T_EQ(n, 3);
        }
        ov = at(0, NULL);
        T_EQ(rt_file_write(h, "abcde", 5, NULL, &ov), TRUE);
        n = 77;
        T_EQ(rt_file_overlapped_result(h, &ov, &n, TRUE), TRUE);
        T_EQ(n, 5);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(18, "the result of a read at the end of the file: FALSE, ERROR_HANDLE_EOF, 0 bytes") {
        reset_o();
        HANDLE h = open_o(RD, OVL);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(10, NULL);
        T_EQ(rt_file_read(h, buf, 4, NULL, &ov), FALSE);
        for (int wait = 0; wait < 2; wait++) {
            n = 77;
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_file_overlapped_result(h, &ov, &n, wait), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_HANDLE_EOF);
            T_EQ(n, 0);
        }
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(19, "STATUS_PENDING, which only the program stores: incomplete, or the stop if it would wait") {
        reset_o();
        HANDLE h = open_o(RD, OVL);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(0, NULL);
        ov.Internal = STATUS_PENDING;
        n = 77;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_overlapped_result(h, &ov, &n, FALSE), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_IO_INCOMPLETE);
        T_EQ(n, 77);
        T_TRAPS(rt_file_overlapped_result(h, &ov, &n, TRUE), "rt_file_overlapped_result: not implemented");
        ov.Internal = UINT64_C(0xFFFFFFFF00000000) | STATUS_PENDING;   /* the status is the low half */
        T_EQ(rt_file_overlapped_result(h, &ov, &n, FALSE), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_IO_INCOMPLETE);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(20, "any other status, and missing arguments: the stop") {
        reset_o();
        HANDLE h = open_o(RD, OVL), wo = open_o(WR, OVL);
        T_REQUIRE(h != BAD && wo != BAD);
        OVERLAPPED ov = at(0, NULL);
        T_EQ(rt_file_read(h, buf, 4, NULL, &ov), TRUE);    /* a result of 4 bytes ... */
        T_EQ(rt_file_read(wo, buf, 4, NULL, &ov), FALSE);  /* ... then a call that fails with the same OVERLAPPED */
        T_TRAPS(rt_file_overlapped_result(wo, &ov, &n, FALSE), "rt_file_overlapped_result: not implemented");
        T_TRAPS(rt_file_overlapped_result(wo, &ov, &n, TRUE), "rt_file_overlapped_result: not implemented");
        ov.Internal = 0xC0000005u;
        T_TRAPS(rt_file_overlapped_result(h, &ov, &n, FALSE), "rt_file_overlapped_result: not implemented");
        ov.Internal = STATUS_SUCCESS;
        T_TRAPS(rt_file_overlapped_result(h, NULL, &n, FALSE), "rt_file_overlapped_result: not implemented");
        T_TRAPS(rt_file_overlapped_result(h, &ov, NULL, FALSE), "rt_file_overlapped_result: not implemented");
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(wo), TRUE);
    }

    T_PLAN(21, "a handle that is not an open file: FALSE, ERROR_INVALID_HANDLE") {
        reset_o();
        HANDLE closed = open_o(RD, OVL), e = new_event(TRUE);
        T_REQUIRE(closed != BAD && e);
        T_EQ(rt_handle_close(closed), TRUE);
        OVERLAPPED ov = at(0, NULL);
        ov.Internal = STATUS_SUCCESS;
        ov.InternalHigh = 4;
        const HANDLE bad[4] = { NULL, INVALID_HANDLE_VALUE, closed, e };
        for (unsigned i = 0; i < 4; i++) {
            n = 77;
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_file_overlapped_result(bad[i], &ov, &n, FALSE), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
            T_EQ(n, 77);
        }
        T_EQ(rt_handle_close(e), TRUE);
    }
    T_EQ(rt_file_test_open_count(), 3);
}

/* ---- ReadFileEx ---------------------------------------------------------------------------------- */

static struct { uint64_t error, bytes, ov; } done_log[16];
static unsigned done_n;
static int      done_wrong_thread, done_saw_lock;

static void done_note(Ctx *ctx) {
    if (done_n < 16) {
        done_log[done_n].error = ctx->r[1];
        done_log[done_n].bytes = ctx->r[2];
        done_log[done_n].ov = ctx->r[8];
    }
    done_n++;
    if (RT_THREAD(ctx) != ME || rt_thread_self() != ME) done_wrong_thread = 1;
    if (rt_dispatcher_held()) done_saw_lock = 1;
}

/* VOID CALLBACK on_done(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped) */
GUEST_FN(on_done)
{
    done_note(ctx);
    GUEST_RET(ctx);
}

/* A completion routine that asks for the next piece, as a loader's does: it reads on from
 * where its request ended until the file ends. */
static HANDLE chain_h;
static char   chain_buf[32];
static DWORD  chain_last_error;
GUEST_FN(on_done_chain)
{
    done_note(ctx);
    OVERLAPPED *ov = (OVERLAPPED *)(uintptr_t)ctx->r[8];
    if (ctx->r[1]) {                                        /* the end of the file: the chain stops */
        chain_last_error = (DWORD)ctx->r[1];
        GUEST_RET(ctx);
    }
    ov->Offset += (DWORD)ctx->r[2];
    if (!rt_file_read_ex(ctx, chain_h, chain_buf + ov->Offset, 4, ov, T_GUEST_ADDR(on_done_chain)))
        chain_last_error = 0xBAD;                           /* no call of the chain fails */
    GUEST_RET(ctx);
}

static void worker_read_ex(Worker *w) {
    static char buf[8];
    static OVERLAPPED ov;
    ov = at(2, NULL);
    HANDLE h = w->arg;
    W_CHECK(w, rt_file_read_ex(W_CTX(w), h, buf, 4, &ov, T_GUEST_ADDR(on_done)) == TRUE);
    W_CHECK(w, memcmp(buf, "2345", 4) == 0);
    W_CHECK(w, ov.Internal == STATUS_SUCCESS && ov.InternalHigh == 4);
}

TEST(core, file_ov_readex)
{
    char buf[32];

    T_PLAN(22, "the read is done in the call; the routine is not run by it") {
        reset_o();
        HANDLE h = open_o(RD, OVL), e = new_event(TRUE);
        T_REQUIRE(h != BAD && e);
        set_pos(h, 1);
        OVERLAPPED ov = at(3, e);
        memset(buf, 0, sizeof buf);
        const unsigned before = done_n;
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_read_ex(CTX, h, buf, 4, &ov, T_GUEST_ADDR(on_done)), TRUE);
        T_EQ(T_LAST_ERROR(), ERROR_SUCCESS);                /* documented: ERROR_SUCCESS after a success */
        T_TRUE(memcmp(buf, "3456", 5) == 0);
        T_EQ(ov.Internal, STATUS_SUCCESS);
        T_EQ(ov.InternalHigh, 4);
        T_EQ(pos(h), 1);
        T_EQ(done_n, before);
        T_EQ(wu_state(e).set, false);                       /* hEvent is ignored ... */
        ov = at(3, (HANDLE)(uintptr_t)0x12344);             /* ... whatever it holds */
        T_EQ(rt_file_read_ex(CTX, h, buf, 4, &ov, T_GUEST_ADDR(on_done)), TRUE);
        T_EQ(done_n, before);
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);   /* (both run here, before `ov` goes) */
        T_EQ(done_n, before + 2);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(e), TRUE);
    }

    T_PLAN(23, "no wait that is not alertable runs the routine") {
        reset_o();
        HANDLE h = open_o(RD, OVL), e = new_event(FALSE);
        T_REQUIRE(h != BAD && e);
        OVERLAPPED ov = at(0, NULL);
        const unsigned before = done_n;
        T_EQ(rt_file_read_ex(CTX, h, buf, 4, &ov, T_GUEST_ADDR(on_done)), TRUE);
        T_EQ(rt_sleep_ex(CTX, 0, FALSE), 0);
        T_EQ(rt_sleep_ex(CTX, 5, FALSE), 0);
        T_EQ(rt_wait_handle(CTX, e, 0, FALSE), WAIT_TIMEOUT);
        T_EQ(rt_wait_handle(CTX, e, 5, FALSE), WAIT_TIMEOUT);
        DWORD n = 0;
        T_EQ(rt_file_overlapped_result(h, &ov, &n, TRUE), TRUE);       /* nor does asking for the result */
        T_EQ(n, 4);
        T_EQ(done_n, before);
        T_EQ(rt_handle_close(h), TRUE);                     /* the file may be closed before the routine runs */
        T_EQ(rt_wait_handle(CTX, e, 0, TRUE), WAIT_IO_COMPLETION);
        T_EQ(done_n, before + 1);
        T_EQ(rt_handle_close(e), TRUE);
    }

    T_PLAN(24, "the next alertable wait runs it once, on the caller's thread, with (0, bytes, lpOverlapped)") {
        reset_o();
        HANDLE h = open_o(RD, OVL);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(7, NULL);
        const unsigned before = done_n;
        T_EQ(rt_file_read_ex(CTX, h, buf, 9, &ov, T_GUEST_ADDR(on_done)), TRUE);      /* reaches the end: 3 bytes */
        T_EQ(done_n, before);
        const uint64_t rsp = CTX->r[4];
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);
        T_EQ(CTX->r[4], rsp);
        T_EQ(done_n, before + 1);
        T_REQUIRE(before < 16);
        T_EQ(done_log[before].error, 0);
        T_EQ(done_log[before].bytes, 3);
        T_EQ(done_log[before].ov, (uint64_t)(uintptr_t)&ov);
        T_EQ(done_wrong_thread, 0);
        T_EQ(done_saw_lock, 0);
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);                 /* nothing is left: it does not run twice */
        T_EQ(rt_sleep_ex(CTX, 5, TRUE), 0);
        T_EQ(done_n, before + 1);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(25, "three requests: three routines, each with its own OVERLAPPED, in the order of the calls") {
        reset_o();
        HANDLE h = open_o(RD, OVL);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov[3] = { at(0, NULL), at(8, NULL), at(4, NULL) };
        char b[3][8];
        static const DWORD len[3] = { 1, 8, 0 };
        static const uint64_t bytes[3] = { 1, 2, 0 };
        const unsigned before = done_n;
        for (unsigned i = 0; i < 3; i++) T_EQ(rt_file_read_ex(CTX, h, b[i], len[i], &ov[i], T_GUEST_ADDR(on_done)), TRUE);
        T_EQ(done_n, before);
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);
        T_EQ(done_n, before + 3);
        T_REQUIRE(before + 3 <= 16);
        for (unsigned i = 0; i < 3; i++) {
            T_EQ(done_log[before + i].error, 0);
            T_EQ(done_log[before + i].bytes, bytes[i]);
            T_EQ(done_log[before + i].ov, (uint64_t)(uintptr_t)&ov[i]);
            T_EQ(ov[i].InternalHigh, bytes[i]);
        }
        T_TRUE(b[0][0] == '0' && memcmp(b[1], "89", 2) == 0);
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(26, "the end of the file: TRUE, and the routine gets ERROR_HANDLE_EOF and 0 bytes") {
        reset_o();
        HANDLE h = open_o(RD, OVL);
        T_REQUIRE(h != BAD);
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);                 /* nothing is queued before the call */
        static const uint64_t where[2] = { 10, 1000 };      /* at the end, and beyond it */
        for (unsigned i = 0; i < 2; i++) {
            const unsigned before = done_n;
            OVERLAPPED ov = at(where[i], NULL);
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_file_read_ex(CTX, h, buf, 4, &ov, T_GUEST_ADDR(on_done)), TRUE);
            T_EQ(T_LAST_ERROR(), 0);
            T_EQ(ov.Internal, STATUS_END_OF_FILE);
            T_EQ(ov.InternalHigh, 0);
            T_EQ(done_n, before);                           /* not before the alertable wait */
            T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);
            T_EQ(done_n, before + 1);
            T_REQUIRE(before < 16);
            T_EQ(done_log[before].error, ERROR_HANDLE_EOF);
            T_EQ(done_log[before].bytes, 0);
            T_EQ(done_log[before].ov, (uint64_t)(uintptr_t)&ov);
            DWORD n = 77;                                   /* and the result says the same */
            T_EQ(rt_file_overlapped_result(h, &ov, &n, FALSE), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_HANDLE_EOF);
            T_EQ(n, 0);
        }
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(27, "a routine that asks for the next piece: the whole chain runs in one alertable wait") {
        reset_o();
        chain_h = open_o(RD, OVL);
        T_REQUIRE(chain_h != BAD);
        static OVERLAPPED ov;
        ov = at(0, NULL);
        memset(chain_buf, 0, sizeof chain_buf);
        chain_last_error = 0;
        const unsigned before = done_n;
        T_EQ(rt_file_read_ex(CTX, chain_h, chain_buf, 4, &ov, T_GUEST_ADDR(on_done_chain)), TRUE);
        T_EQ(done_n, before);
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), WAIT_IO_COMPLETION);
        T_EQ(done_n, before + 4);                           /* 4 + 4 + 2 bytes, then the end: 0 bytes */
        T_EQ(chain_last_error, ERROR_HANDLE_EOF);
        T_TRUE(memcmp(chain_buf, "0123456789", 11) == 0);
        T_EQ(ov.Offset, 10);
        T_EQ(done_wrong_thread, 0);
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);
        T_EQ(rt_handle_close(chain_h), TRUE);
    }

    T_PLAN(28, "failures that queue nothing") {
        reset_o();
        HANDLE wo = open_o(WR, OVL), e = new_event(TRUE);
        T_REQUIRE(wo != BAD && e);
        const unsigned before = done_n;
        OVERLAPPED ov = at(0, NULL);
        ov.Internal = STATUS_SUCCESS;
        T_EQ(rt_file_read_ex(CTX, wo, buf, 4, &ov, T_GUEST_ADDR(on_done)), FALSE);    /* the handle cannot read */
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(ov.Internal, FAILED_STATUS);
        const HANDLE bad[3] = { NULL, INVALID_HANDLE_VALUE, e };
        for (unsigned i = 0; i < 3; i++) {
            rt_thread_set_last_error(0xDEAD);
            T_EQ(rt_file_read_ex(CTX, bad[i], buf, 4, &ov, T_GUEST_ADDR(on_done)), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        }
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);
        T_EQ(done_n, before);
        T_EQ(rt_handle_close(wo), TRUE);
        T_EQ(rt_handle_close(e), TRUE);
    }

    T_PLAN(29, "the stops of rt_file_read_ex") {
        reset_o();
        HANDLE h = open_o(RD, OVL), s = open_o(RD, 0);
        T_REQUIRE(h != BAD && s != BAD);
        OVERLAPPED ov = at(0, NULL);
        T_TRAPS(rt_file_read_ex(CTX, s, buf, 4, &ov, T_GUEST_ADDR(on_done)), "rt_file_read_ex: not implemented");
        T_TRAPS(rt_file_read_ex(CTX, h, buf, 4, NULL, T_GUEST_ADDR(on_done)), "rt_file_read_ex: not implemented");
        T_TRAPS(rt_file_read_ex(CTX, h, buf, 4, &ov, 0), "rt_file_read_ex: not implemented");
        ov = at(UINT64_MAX, NULL);
        T_TRAPS(rt_file_read_ex(CTX, h, buf, 4, &ov, T_GUEST_ADDR(on_done)), "rt_file_read_ex: not implemented");
        ov = at(0, NULL);
        T_TRAPS(rt_file_read_ex(NULL, h, buf, 4, &ov, T_GUEST_ADDR(on_done)), "rt_file_read_ex: no ctx");
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_handle_close(s), TRUE);
    }

    T_PLAN(30, "the routine belongs to the thread that called: no other thread's wait runs it") {
        reset_o();
        HANDLE h = open_o(RD, OVL);
        T_REQUIRE(h != BAD);
        static Worker w;
        const unsigned before = done_n;
        wu_start_one(&w, 0, worker_read_ex, h);
        if (!wu_finish(&w, 1, "ReadFileEx on another thread")) return;
        T_EQ(rt_sleep_ex(CTX, 0, TRUE), 0);                 /* this thread has nothing queued */
        T_EQ(rt_sleep_ex(CTX, 5, TRUE), 0);
        T_EQ(done_n, before);
        rt_wait_thread_exit(&w.t);                          /* it sat in the caller's queue, once */
        T_TRUE(T_LOGGED(RT_CH_THREAD, "exited with 1 APC still queued: dropped"));
        T_EQ(done_n, before);
        T_EQ(rt_handle_close(h), TRUE);
    }
    T_EQ(rt_file_test_open_count(), 3);
}

/* ---- several threads, one handle ---------------------------------------------------------------- */

#define NTHREAD   8
#define NWORD     65536u                /* t.bin: NWORD 32-bit words, word i holds i */
#define WREC      400                   /* records each thread writes */
#define NREAD     (WREC * 8)            /* requests per thread; every eighth also writes a record */

static HANDLE      th;                  /* set by the main thread before the threads start */
static atomic_uint go, call_failed, wrong_count, wrong_bytes, wrong_result;

static void *ov_racer(void *arg) {
    const unsigned t = (unsigned)(uintptr_t)arg;
    uint64_t rng = 0x9E3779B97F4A7C15ull * (t + 1);
    static _Thread_local uint32_t buf[256];
    while (!atomic_load(&go)) { }
    for (unsigned k = 0; k < NREAD; k++) {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        const unsigned words = 1 + (unsigned)(rng & 255);
        const unsigned first = (unsigned)((rng >> 8) % (NWORD - words + 1));
        OVERLAPPED ov;
        memset(&ov, 0, sizeof ov);
        ov.Internal = JUNK;
        ov.Offset = first * 4;
        DWORD got = 0;
        if (!rt_file_read(th, buf, words * 4, &got, &ov)) { atomic_fetch_add(&call_failed, 1); continue; }
        if (got != words * 4) atomic_fetch_add(&wrong_count, 1);
        if (ov.Internal != STATUS_SUCCESS || ov.InternalHigh != words * 4) atomic_fetch_add(&wrong_result, 1);
        for (unsigned i = 0; i < words && i * 4 < got; i++)
            if (buf[i] != first + i) { atomic_fetch_add(&wrong_bytes, 1); break; }
        /* Every eighth request also writes a record of this thread's own, interleaved with
         * the other threads' records beyond the words. */
        if ((k & 7) == 0) {
            const uint32_t rec[2] = { t, k / 8 };
            ov.Offset = NWORD * 4 + ((k / 8) * NTHREAD + t) * 8;
            if (!rt_file_write(th, rec, 8, &got, &ov) || got != 8) atomic_fetch_add(&call_failed, 1);
        }
    }
    return NULL;
}

TEST(core, file_ov_threads)
{
    T_PLAN(31, "eight threads, one handle, different offsets: each gets its own bytes and the position stays") {
        static uint32_t words[NWORD];
        for (unsigned i = 0; i < NWORD; i++) words[i] = i;
        int fd = open(I("t.bin"), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        T_REQUIRE(fd >= 0 && write(fd, words, sizeof words) == (ssize_t)sizeof words);
        close(fd);
        th = cfa("C:\\Titanfall2\\t.bin", RD | WR, SH, OPEN_EXISTING, OVL);
        T_REQUIRE(th != BAD);
        set_pos(th, 12345);
        pthread_t t[NTHREAD];
        for (uintptr_t i = 0; i < NTHREAD; i++) T_REQUIRE(pthread_create(&t[i], NULL, ov_racer, (void *)i) == 0);
        atomic_store(&go, 1);
        for (int i = 0; i < NTHREAD; i++) pthread_join(t[i], NULL);
        T_EQ(atomic_load(&call_failed), 0);
        T_EQ(atomic_load(&wrong_count), 0);
        T_EQ(atomic_load(&wrong_bytes), 0);
        T_EQ(atomic_load(&wrong_result), 0);
        T_EQ(pos(th), 12345);
        T_EQ(rt_handle_close(th), TRUE);

        /* The file: the words untouched, then every thread's records where it put them. */
        static uint32_t back[NWORD + WREC * NTHREAD * 2];
        fd = open(I("t.bin"), O_RDONLY);
        T_REQUIRE(fd >= 0);
        T_EQ(read(fd, back, sizeof back), (ssize_t)sizeof back);
        close(fd);
        T_EQ(host_size("t.bin"), (off_t)sizeof back);
        unsigned bad_word = 0, bad_rec = 0;
        for (unsigned i = 0; i < NWORD; i++) bad_word += back[i] != i;
        for (unsigned k = 0; k < WREC; k++)
            for (unsigned i = 0; i < NTHREAD; i++) {
                const uint32_t *rec = &back[NWORD + (k * NTHREAD + i) * 2];
                bad_rec += rec[0] != i || rec[1] != k;
            }
        T_EQ(bad_word, 0);
        T_EQ(bad_rec, 0);
    }
    T_EQ(rt_file_test_open_count(), 3);
}
