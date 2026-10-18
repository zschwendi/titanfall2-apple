/* The file object's lifetime (rt/file.h, core/file_priv.h): the three standard objects that
 * rt_file_init makes, references and handles, delete on close, the sharing log, the shared
 * helpers of core/file.c, and opens from several threads at once.  The workers are plain host
 * threads, as in path_threads.c: they only count what went wrong.
 */
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/time.h"

int      rt_file_test_fd(HANDLE h);
unsigned rt_file_test_open_count(void);
HANDLE   rt_file_std_slot(unsigned i);
HANDLE   rt_file_std_slot_swap(unsigned i, HANDLE h);
bool     rt_file_log_if_open(const char *host, const char *what);
void     rt_file_attr_from_stat(const struct stat *st, DWORD *attrs, FILETIME *creation,
                                FILETIME *access, FILETIME *write, ULONGLONG *size);

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
static bool exists(const char *rel) { struct stat st; return stat(I(rel), &st) == 0; }
static HANDLE cfa(const char *win, DWORD access, DWORD share, DWORD disp, DWORD fa) {
    return rt_file_create(RT_NAME_A(win), access, share, disp, fa, NULL);
}
static bool same_file(int a, int b) {
    struct stat x, y;
    return fstat(a, &x) == 0 && fstat(b, &y) == 0 && x.st_dev == y.st_dev && x.st_ino == y.st_ino && x.st_rdev == y.st_rdev;
}

TEST(core, file_lifetime_objects)
{
    T_CASE("the three standard objects");
    T_EQ(rt_file_test_open_count(), 3);
    HANDLE in = rt_file_std_slot(0), out = rt_file_std_slot(1), err = rt_file_std_slot(2);
    T_TRUE(in && out && err && in != out && out != err && in != err);
    T_TRUE(same_file(rt_file_test_fd(out), 1));     /* output: the host's descriptor 1 */
    T_TRUE(same_file(rt_file_test_fd(err), 2));
    T_TRUE(rt_file_test_fd(out) > 2 && rt_file_test_fd(err) > 2);      /* a dup, not the descriptor itself */
    int null_fd = open("/dev/null", O_RDONLY);
    T_TRUE(same_file(rt_file_test_fd(in), null_fd));
    close(null_fd);
    T_EQ(fcntl(rt_file_test_fd(in), F_GETFL) & O_ACCMODE, O_RDONLY);
    T_EQ(rt_file_std_slot_swap(1, (HANDLE)(uintptr_t)0x1234), out);    /* the slot holds any value */
    T_EQ(rt_file_std_slot(1), (HANDLE)(uintptr_t)0x1234);
    T_EQ(rt_file_std_slot_swap(1, out), (HANDLE)(uintptr_t)0x1234);
    T_TRAPS(rt_file_std_slot(3), "standard handle slot 3 does not exist");
    T_TRAPS(rt_file_init(), "rt_file_init called twice");

    T_CASE("the object lives as long as a handle or a reference does");
    put("a.bin", "0123456789", 0644);
    HANDLE h = cfa("C:\\Titanfall2\\a.bin", RD, SH, OPEN_EXISTING, 0);
    T_REQUIRE(h != BAD);
    T_EQ(rt_file_test_open_count(), 4);
    const int fd = rt_file_test_fd(h);
    HANDLE d = rt_handle_dup(h);
    T_REQUIRE(d != NULL);
    T_EQ(rt_file_test_fd(d), fd);                   /* the same object: one descriptor, one position */
    T_EQ(rt_file_test_open_count(), 4);
    T_EQ(rt_handle_close(h), TRUE);
    T_TRUE(fcntl(fd, F_GETFD) != -1);               /* still open through the duplicate */
    RtObject *o = rt_handle_ref(d, RT_K_FILE);
    T_REQUIRE(o != NULL);
    T_EQ(rt_handle_close(d), TRUE);
    T_EQ(rt_file_test_open_count(), 4);             /* an operation in flight keeps it */
    T_TRUE(fcntl(fd, F_GETFD) != -1);
    rt_object_unref(o);
    T_EQ(rt_file_test_open_count(), 3);
    T_EQ(fcntl(fd, F_GETFD), -1);                   /* the descriptor went with the object */

    T_CASE("two opens are two objects with their own positions");
    HANDLE a = cfa("C:\\Titanfall2\\a.bin", RD, SH, OPEN_EXISTING, 0);
    HANDLE b = cfa("C:\\Titanfall2\\a.bin", RD, SH, OPEN_EXISTING, 0);
    T_REQUIRE(a != BAD && b != BAD);
    T_TRUE(rt_file_test_fd(a) != rt_file_test_fd(b));
    T_EQ(lseek(rt_file_test_fd(a), 7, SEEK_SET), 7);
    T_EQ(lseek(rt_file_test_fd(b), 0, SEEK_CUR), 0);
    T_EQ(rt_handle_close(a), TRUE);
    T_EQ(rt_handle_close(b), TRUE);
    T_EQ(rt_handle_close(b), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    T_CASE("delete on close");
    h = cfa("C:\\Titanfall2\\tmp1.bin", RD | WR, SH, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE);
    T_REQUIRE(h != BAD);
    d = rt_handle_dup(h);
    T_TRUE(exists("tmp1.bin"));
    T_EQ(rt_handle_close(h), TRUE);
    T_TRUE(exists("tmp1.bin"));                     /* not until the last handle */
    T_EQ(rt_handle_close(d), TRUE);
    T_TRUE(!exists("tmp1.bin"));
    h = cfa("C:\\Titanfall2\\tmp1.bin", RD, SH, OPEN_EXISTING, 0);     /* and the next translation sees that */
    T_EQ(h, BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    h = cfa("C:\\Titanfall2\\a.bin", RD, SH, OPEN_EXISTING, 0);        /* an ordinary close deletes nothing */
    T_EQ(rt_handle_close(h), TRUE);
    T_TRUE(exists("a.bin"));
    T_EQ(rt_file_test_open_count(), 3);

    T_CASE("attributes, times and size from a host stat");
    put("ro.bin", "abc", 0444);
    mkdir(I("Dir"), 0755);
    struct stat st;
    DWORD attrs = 0;
    ULONGLONG size = 99;
    FILETIME cr, ac, wr;
    T_EQ(stat(I("a.bin"), &st), 0);
    rt_file_attr_from_stat(&st, &attrs, &cr, &ac, &wr, &size);
    T_EQ(attrs, FILE_ATTRIBUTE_ARCHIVE);
    T_EQ(size, 10);
    uint64_t want = rt_time_filetime_from_unix(st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec);
    T_EQ(((uint64_t)wr.dwHighDateTime << 32) | wr.dwLowDateTime, want);
    want = rt_time_filetime_from_unix(st.st_birthtimespec.tv_sec, st.st_birthtimespec.tv_nsec);
    T_EQ(((uint64_t)cr.dwHighDateTime << 32) | cr.dwLowDateTime, want);
    want = rt_time_filetime_from_unix(st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec);
    T_EQ(((uint64_t)ac.dwHighDateTime << 32) | ac.dwLowDateTime, want);
    T_EQ(stat(I("ro.bin"), &st), 0);
    rt_file_attr_from_stat(&st, &attrs, NULL, NULL, NULL, NULL);
    T_EQ(attrs, FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY);
    T_EQ(stat(I("Dir"), &st), 0);
    rt_file_attr_from_stat(&st, &attrs, NULL, NULL, NULL, &size);
    T_EQ(attrs, FILE_ATTRIBUTE_DIRECTORY);
    T_EQ(size, 0);
    T_EQ(stat("/dev/null", &st), 0);
    T_TRAPS(rt_file_attr_from_stat(&st, &attrs, NULL, NULL, NULL, NULL), "rt_file_attr_from_stat: not implemented");
}

TEST(core, file_lifetime_sharing_log)
{
    put("s.bin", "x", 0644);
    put("t.bin", "x", 0644);

    T_CASE("opens that Windows allows log nothing");
    HANDLE a = cfa("C:\\Titanfall2\\s.bin", RD, FILE_SHARE_READ, OPEN_EXISTING, 0);
    HANDLE b = cfa("C:\\Titanfall2\\s.bin", RD, FILE_SHARE_READ, OPEN_EXISTING, 0);
    HANDLE c = cfa("C:\\Titanfall2\\s.bin", 0, 0, OPEN_EXISTING, 0);   /* no access: never a conflict */
    HANDLE o = cfa("C:\\Titanfall2\\t.bin", RD | WR, 0, OPEN_EXISTING, 0);     /* another file */
    T_TRUE(a != BAD && b != BAD && c != BAD && o != BAD);
    T_TRUE(!T_LOGGED(RT_CH_FILE, "sharing violation not enforced"));
    T_TRUE(!rt_file_log_if_open(I("none.bin"), "DeleteFile"));          /* nothing there */
    T_TRUE(!T_LOGGED(RT_CH_FILE, "sharing violation not enforced"));

    T_CASE("an open that Windows refuses succeeds and is logged");
    HANDLE w = cfa("C:\\Titanfall2\\s.bin", WR, SH, OPEN_EXISTING, 0);  /* a and b do not share write */
    T_TRUE(w != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_TRUE(T_LOGGED(RT_CH_FILE, "sharing violation not enforced"));
    T_TRUE(T_LOGGED(RT_CH_FILE, "s.bin opened with access 0x40000000 share 0x7"));

    T_CASE("the log for a delete or a move");
    T_TRUE(rt_file_log_if_open(I("s.bin"), "DeleteFile"));              /* a has no FILE_SHARE_DELETE */
    T_TRUE(T_LOGGED(RT_CH_FILE, "sharing violation not enforced: DeleteFile of"));
    T_EQ(rt_handle_close(a), TRUE);
    T_EQ(rt_handle_close(b), TRUE);
    T_TRUE(!rt_file_log_if_open(I("s.bin"), "MoveFile"));               /* w shares delete; c has no access */
    T_TRUE(!T_LOGGED(RT_CH_FILE, "MoveFile of"));
    T_EQ(rt_handle_close(c), TRUE);
    T_EQ(rt_handle_close(w), TRUE);
    T_EQ(rt_handle_close(o), TRUE);
    T_TRUE(!rt_file_log_if_open(I("t.bin"), "MoveFile"));               /* closed: not on the list any more */
}

/* ---- several threads -------------------------------------------------------------------------- */

#define NTHREAD 8
#define NNAME   150
static atomic_uint made[NNAME], fresh[NNAME], opened[NNAME], bad;
static atomic_uint go;

static void *racer(void *arg) {
    (void)arg;
    while (!atomic_load(&go)) { }
    for (unsigned k = 0; k < NNAME; k++) {
        char name[64];
        snprintf(name, sizeof name, "C:\\Titanfall2\\race\\new%u.tmp", k);
        HANDLE h = cfa(name, WR, SH, CREATE_NEW, 0);
        if (h != BAD) atomic_fetch_add(&made[k], 1);
        else if (rt_thread_get_last_error() != ERROR_FILE_EXISTS) atomic_fetch_add(&bad, 1);
        if (h != BAD && !rt_handle_close(h)) atomic_fetch_add(&bad, 1);

        snprintf(name, sizeof name, "C:\\Titanfall2\\race\\always%u.tmp", k);
        rt_thread_set_last_error(0xDEAD);
        h = cfa(name, RD | WR, SH, k & 1 ? OPEN_ALWAYS : CREATE_ALWAYS, 0);
        if (h == BAD) { atomic_fetch_add(&bad, 1); continue; }
        atomic_fetch_add(&opened[k], 1);
        const DWORD e = rt_thread_get_last_error();
        if (e == 0) atomic_fetch_add(&fresh[k], 1);
        else if (e != ERROR_ALREADY_EXISTS) atomic_fetch_add(&bad, 1);
        if (!rt_handle_close(h)) atomic_fetch_add(&bad, 1);
    }
    return NULL;
}

TEST(core, file_lifetime_threads)
{
    T_CASE("one name, eight threads: CREATE_NEW has one winner, the others are told who made the file");
    mkdir(I("race"), 0755);
    pthread_t t[NTHREAD];
    for (int i = 0; i < NTHREAD; i++) T_REQUIRE(pthread_create(&t[i], NULL, racer, NULL) == 0);
    atomic_store(&go, 1);
    for (int i = 0; i < NTHREAD; i++) pthread_join(t[i], NULL);
    unsigned wrong_new = 0, wrong_fresh = 0, wrong_open = 0;
    for (unsigned k = 0; k < NNAME; k++) {
        wrong_new   += atomic_load(&made[k]) != 1;
        wrong_fresh += atomic_load(&fresh[k]) != 1;         /* exactly one thread created each name */
        wrong_open  += atomic_load(&opened[k]) != NTHREAD;
    }
    T_EQ(wrong_new, 0);
    T_EQ(wrong_fresh, 0);
    T_EQ(wrong_open, 0);
    T_EQ(atomic_load(&bad), 0);
    T_EQ(rt_file_test_open_count(), 3);
}
