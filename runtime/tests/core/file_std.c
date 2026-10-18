/* The standard handles and the console control handler (rt/file.h, the section "standard
 * handles" and rt_console_ctrl_handler): rt_std_get, rt_std_set, rt_file_type and the
 * handler list.  Each TEST is its own process; the cases of a TEST run in order.
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "../../core/thread_priv.h"

int      rt_file_test_fd(HANDLE h);
unsigned rt_file_test_open_count(void);
HANDLE   rt_file_std_slot(unsigned i);

#define RD   GENERIC_READ
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
/* A file type query: the value, and the last error it leaves (0xDEAD: untouched). */
static void want_type(HANDLE h, DWORD type, DWORD le) {
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_file_type(h), type);
    T_EQ(T_LAST_ERROR(), le);
}

TEST(core, file_std_handles)
{
    HANDLE h = NULL;

    T_PLAN(1, "the three slots are the objects of rt_file_init") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_std_get(STD_INPUT_HANDLE), rt_file_std_slot(0));
        T_EQ(rt_std_get(STD_OUTPUT_HANDLE), rt_file_std_slot(1));
        T_EQ(rt_std_get(STD_ERROR_HANDLE), rt_file_std_slot(2));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
    }

    T_PLAN(2, "any other value is an invalid handle") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_std_get(0), INVALID_HANDLE_VALUE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_std_get(1), INVALID_HANDLE_VALUE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_std_get((DWORD)-13), INVALID_HANDLE_VALUE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }

    T_PLAN(3, "getting makes no handle") {
        T_EQ(rt_std_get(STD_INPUT_HANDLE), rt_std_get(STD_INPUT_HANDLE));
        T_EQ(rt_std_get(STD_OUTPUT_HANDLE), rt_std_get(STD_OUTPUT_HANDLE));
        T_EQ(rt_std_get(STD_ERROR_HANDLE), rt_std_get(STD_ERROR_HANDLE));
        T_EQ(rt_file_test_open_count(), 3);
    }

    T_PLAN(4, "set the output; the old one stays open") {
        put("f.bin", "hello", 0644);
        h = cfa("C:\\Titanfall2\\f.bin", GENERIC_READ, 7, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD);
        const HANDLE old_out = rt_std_get(STD_OUTPUT_HANDLE);
        const HANDLE in_before = rt_std_get(STD_INPUT_HANDLE);
        const HANDLE err_before = rt_std_get(STD_ERROR_HANDLE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_std_set(STD_OUTPUT_HANDLE, h), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(rt_std_get(STD_OUTPUT_HANDLE), h);
        T_TRUE(rt_file_test_fd(old_out) != -1);
        T_EQ(rt_std_get(STD_INPUT_HANDLE), in_before);
        T_EQ(rt_std_get(STD_ERROR_HANDLE), err_before);
    }

    T_PLAN(5, "NULL and any value are stored as they are") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_std_set(STD_ERROR_HANDLE, NULL), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_std_get(STD_ERROR_HANDLE), NULL);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(rt_std_set(STD_ERROR_HANDLE, (HANDLE)0x12344), TRUE);
        T_EQ(rt_std_get(STD_ERROR_HANDLE), (HANDLE)0x12344);
    }

    T_PLAN(6, "another which is refused and changes nothing") {
        const HANDLE in_b = rt_std_get(STD_INPUT_HANDLE);
        const HANDLE out_b = rt_std_get(STD_OUTPUT_HANDLE);
        const HANDLE err_b = rt_std_get(STD_ERROR_HANDLE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_std_set((DWORD)5, h), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_std_set((DWORD)0, h), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        T_EQ(rt_std_get(STD_INPUT_HANDLE), in_b);
        T_EQ(rt_std_get(STD_OUTPUT_HANDLE), out_b);
        T_EQ(rt_std_get(STD_ERROR_HANDLE), err_b);
    }
}

TEST(core, file_std_type)
{
    HANDLE h = NULL;

    T_PLAN(7, "the three original standard handles are character devices") {
        want_type(rt_std_get(STD_INPUT_HANDLE), FILE_TYPE_CHAR, 0xDEAD);
        want_type(rt_std_get(STD_OUTPUT_HANDLE), FILE_TYPE_CHAR, 0xDEAD);
        want_type(rt_std_get(STD_ERROR_HANDLE), FILE_TYPE_CHAR, 0xDEAD);
    }

    T_PLAN(8, "a file and a directory are disk; NUL is a character device") {
        put("f.bin", "hello", 0644);
        h = cfa("C:\\Titanfall2\\f.bin", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD);
        want_type(h, FILE_TYPE_DISK, 0xDEAD);
        mkdir(I("dir"), 0755);
        HANDLE dir = cfa("C:\\Titanfall2\\dir", RD, SH, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
        T_REQUIRE(dir != BAD);
        want_type(dir, FILE_TYPE_DISK, 0xDEAD);
        HANDLE nul = cfa("NUL", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(nul != BAD);
        want_type(nul, FILE_TYPE_CHAR, 0xDEAD);
    }

    T_PLAN(9, "what is not an open file is unknown and invalid") {
        put("g.bin", "hello", 0644);
        HANDLE closed = cfa("C:\\Titanfall2\\g.bin", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(closed != BAD);
        T_EQ(rt_handle_close(closed), TRUE);
        HANDLE ev = rt_event_create(T_CTX(), TRUE, FALSE, NULL);
        T_REQUIRE(ev != NULL);
        want_type(NULL, FILE_TYPE_UNKNOWN, ERROR_INVALID_HANDLE);
        want_type(INVALID_HANDLE_VALUE, FILE_TYPE_UNKNOWN, ERROR_INVALID_HANDLE);
        want_type(closed, FILE_TYPE_UNKNOWN, ERROR_INVALID_HANDLE);
        want_type(ev, FILE_TYPE_UNKNOWN, ERROR_INVALID_HANDLE);
    }

    T_PLAN(10, "a type query takes no reference that a close does not drop") {
        const unsigned before = rt_file_test_open_count();
        (void)rt_file_type(h);
        (void)rt_file_type(h);
        (void)rt_file_type(h);
        (void)rt_handle_close(h);
        T_EQ(rt_file_test_open_count(), before - 1);
    }
}

TEST(core, file_std_close)
{
    T_PLAN(11, "closing the original output keeps the stale value and the host descriptor") {
        const HANDLE out = rt_std_get(STD_OUTPUT_HANDLE);
        T_EQ(rt_handle_close(out), TRUE);
        T_EQ(rt_std_get(STD_OUTPUT_HANDLE), out);
        T_EQ(rt_file_type(out), FILE_TYPE_UNKNOWN);
        T_TRUE(fcntl(1, F_GETFD) != -1);
    }
}

/* One SetConsoleCtrlHandler call that must give TRUE and leave the last error untouched. */
static void ctl(GUESTFN routine, BOOL add) {
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_console_ctrl_handler(routine, add), TRUE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
}

TEST(core, file_std_ctrl)
{
    T_PLAN(12, "the handler list: adds, removes, and the Ctrl+C flag") {
        ctl((GUESTFN)0x1000, TRUE);
        ctl((GUESTFN)0x2000, TRUE);
        ctl((GUESTFN)0x1000, TRUE);
        ctl((GUESTFN)0x1000, FALSE);
        ctl((GUESTFN)0x1000, FALSE);
        T_TRAPS(rt_console_ctrl_handler((GUESTFN)0x1000, FALSE), "rt_console_ctrl_handler: not implemented");
        ctl((GUESTFN)0x2000, FALSE);
        ctl((GUESTFN)0, TRUE);
        ctl((GUESTFN)0, FALSE);
    }
}

TEST(core, file_std_ctrl_limit)
{
    T_PLAN(13, "sixty-four entries are kept; the sixty-fifth stops") {
        for (uint64_t i = 0; i < 64; i++) T_EQ(rt_console_ctrl_handler((GUESTFN)(0x20000 + i), TRUE), TRUE);
        T_TRAPS(rt_console_ctrl_handler((GUESTFN)0x30000, TRUE), "rt_console_ctrl_handler: not implemented");
        for (uint64_t i = 0; i < 64; i++) T_EQ(rt_console_ctrl_handler((GUESTFN)(0x20000 + i), FALSE), TRUE);
    }
}

/* Eight host threads, each adding and removing eight routines of its own, 200 times. */
static atomic_uint bad, go;

static void *adder(void *arg) {
    const uint64_t t = (uint64_t)(uintptr_t)arg;
    while (!atomic_load(&go)) { }
    for (unsigned k = 0; k < 200; k++) {
        for (uint64_t i = 0; i < 8; i++)
            if (!rt_console_ctrl_handler((GUESTFN)(0x10000 * (t + 1) + i), TRUE)) atomic_fetch_add(&bad, 1);
        for (uint64_t i = 0; i < 8; i++)
            if (!rt_console_ctrl_handler((GUESTFN)(0x10000 * (t + 1) + i), FALSE)) atomic_fetch_add(&bad, 1);
    }
    return NULL;
}

TEST(core, file_std_ctrl_threads)
{
    T_PLAN(14, "eight threads add and remove; nothing is left over") {
        pthread_t th[8];
        for (uint64_t t = 0; t < 8; t++) T_REQUIRE(pthread_create(&th[t], NULL, adder, (void *)(uintptr_t)t) == 0);
        atomic_store(&go, 1);
        for (int t = 0; t < 8; t++) pthread_join(th[t], NULL);
        T_EQ(atomic_load(&bad), 0);
        unsigned ok = 0;
        for (uint64_t i = 0; i < 64; i++)
            if (rt_console_ctrl_handler((GUESTFN)(0x40000 + i), TRUE)) ok++;
        T_EQ(ok, 64);
    }
}
