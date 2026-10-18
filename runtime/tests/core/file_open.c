/* rt_file_create (rt/file.h): dispositions, access, the errors, last error on success, NUL,
 * directories, and the stops.  Reading and writing are another piece's, so the tests look at
 * a file through the host: the descriptor behind a handle (rt_file_test_fd) and the install
 * root the harness made.
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"

int rt_file_test_fd(HANDLE h);

#define RD   GENERIC_READ
#define WR   GENERIC_WRITE
#define SH   (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define BAD  INVALID_HANDLE_VALUE

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
static long long size_of(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) ? -1 : (long long)st.st_size;
}
static unsigned mode_of(const char *rel) {
    struct stat st;
    return stat(I(rel), &st) ? 0 : (unsigned)(st.st_mode & 0777);
}
/* rt_file_create with last error set to a marker first, so that "set on success" shows. */
static HANDLE cf(const char *win, DWORD access, DWORD disp, DWORD fa) {
    rt_thread_set_last_error(0xDEAD);
    return rt_file_create(RT_NAME_W(W(win)), access, SH, disp, fa, NULL);
}
static int accmode(HANDLE h) { return fcntl(rt_file_test_fd(h), F_GETFL) & O_ACCMODE; }

TEST(core, file_open_dispositions)
{
    HANDLE h;

    T_CASE("OPEN_EXISTING");
    T_EQ(cf("C:\\Titanfall2\\none.bin", RD, OPEN_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    T_EQ(cf("C:\\Titanfall2\\nodir\\none.bin", RD, OPEN_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    put("data.bin", "hello", 0644);
    h = cf("C:\\Titanfall2\\data.bin", RD, OPEN_EXISTING, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);                        /* set on success, to 0 */
    char got[8] = { 0 };
    T_EQ(read(rt_file_test_fd(h), got, 8), 5);
    T_TRUE(memcmp(got, "hello", 5) == 0);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(rt_file_test_fd(h), -1);

    T_CASE("the name: A form, case, relative, NULL");
    rt_thread_set_last_error(0xDEAD);
    h = rt_file_create(RT_NAME_A("c:/TITANFALL2/DATA.BIN"), RD, SH, OPEN_EXISTING, 0, NULL);
    T_TRUE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("data.bin", RD, OPEN_EXISTING, 0);       /* the current directory is C:\Titanfall2 */
    T_TRUE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(rt_file_create((RtName){ NULL, NULL }, RD, SH, OPEN_EXISTING, 0, NULL), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("CREATE_NEW");
    h = cf("C:\\Titanfall2\\New.txt", WR, CREATE_NEW, FILE_ATTRIBUTE_NORMAL);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(size_of("New.txt"), 0);
    T_EQ(mode_of("New.txt"), 0644);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(cf("C:\\Titanfall2\\data.bin", WR, CREATE_NEW, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_EXISTS);
    T_EQ(size_of("data.bin"), 5);                   /* and it was not truncated */
    h = cf("C:\\Titanfall2\\NEW.TXT", RD, OPEN_EXISTING, 0);    /* visible to the next translation */
    T_TRUE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("CREATE_ALWAYS");
    h = cf("C:\\Titanfall2\\data.bin", WR, CREATE_ALWAYS, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);     /* a success that says the file was there */
    T_EQ(size_of("data.bin"), 0);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\ca.bin", RD, CREATE_ALWAYS, 0);     /* read access still creates */
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(size_of("ca.bin"), 0);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("OPEN_ALWAYS");
    put("data.bin", "hello", 0644);
    h = cf("C:\\Titanfall2\\data.bin", RD | WR, OPEN_ALWAYS, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
    T_EQ(size_of("data.bin"), 5);                   /* not truncated */
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\oa.bin", RD | WR, OPEN_ALWAYS, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(size_of("oa.bin"), 0);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("TRUNCATE_EXISTING");
    T_EQ(cf("C:\\Titanfall2\\data.bin", RD, TRUNCATE_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);  /* needs write access */
    T_EQ(size_of("data.bin"), 5);
    T_EQ(cf("C:\\Titanfall2\\none.bin", WR, TRUNCATE_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    T_EQ(size_of("none.bin"), -1);                  /* and nothing was created */
    h = cf("C:\\Titanfall2\\data.bin", WR, TRUNCATE_EXISTING, 0);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(size_of("data.bin"), 0);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("a disposition that is none");
    T_EQ(cf("C:\\Titanfall2\\data.bin", RD, 0, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(cf("C:\\Titanfall2\\data.bin", RD, 6, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
}

TEST(core, file_open_access_and_kinds)
{
    HANDLE h;
    put("data.bin", "hello", 0644);
    put("ro.bin", "fixed", 0444);
    mkdir(I("Sub"), 0755);

    T_CASE("access decides what the descriptor can do");
    h = cf("C:\\Titanfall2\\data.bin", RD, OPEN_EXISTING, 0);
    T_EQ(accmode(h), O_RDONLY);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\data.bin", WR, OPEN_EXISTING, 0);
    T_EQ(accmode(h), O_RDWR);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\data.bin", FILE_GENERIC_READ | FILE_GENERIC_WRITE, OPEN_EXISTING, 0);
    T_EQ(accmode(h), O_RDWR);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\data.bin", 0, OPEN_EXISTING, 0);    /* attributes only */
    T_REQUIRE(h != BAD);
    T_EQ(accmode(h), O_RDONLY);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("a read-only file");
    T_EQ(cf("C:\\Titanfall2\\ro.bin", WR, OPEN_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(cf("C:\\Titanfall2\\ro.bin", RD, CREATE_ALWAYS, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(size_of("ro.bin"), 5);
    h = cf("C:\\Titanfall2\\ro.bin", RD, OPEN_EXISTING, 0);
    T_TRUE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\made_ro.bin", WR, CREATE_NEW, FILE_ATTRIBUTE_READONLY);
    T_REQUIRE(h != BAD);                            /* the creating handle itself can write */
    T_EQ(mode_of("made_ro.bin"), 0444);
    T_EQ(write(rt_file_test_fd(h), "x", 1), 1);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("a directory");
    T_EQ(cf("C:\\Titanfall2\\Sub", RD, OPEN_EXISTING, 0), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    T_EQ(cf("C:\\Titanfall2\\Sub", WR, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS), BAD);
    T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    h = cf("C:\\Titanfall2\\Sub", RD, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
    T_REQUIRE(h != BAD);
    struct stat st;
    T_EQ(fstat(rt_file_test_fd(h), &st), 0);
    T_TRUE(S_ISDIR(st.st_mode));
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("NUL");
    h = cf("NUL", WR, OPEN_EXISTING, 0);
    T_REQUIRE(h != BAD);
    T_EQ(fstat(rt_file_test_fd(h), &st), 0);
    T_TRUE(S_ISCHR(st.st_mode));
    T_EQ(write(rt_file_test_fd(h), "gone", 4), 4);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\Sub\\nul", RD | WR, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE);
    T_REQUIRE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(rt_handle_close(h), TRUE);
    T_EQ(access("/dev/null", F_OK), 0);             /* delete-on-close does not reach the device */
    h = cf("nul", RD, OPEN_ALWAYS, 0);
    T_TRUE(h != BAD);
    T_EQ(T_LAST_ERROR(), 0);
    T_EQ(rt_handle_close(h), TRUE);

    T_CASE("flags that are hints change nothing visible");
    h = cf("C:\\Titanfall2\\data.bin", RD, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN |
           FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY);
    T_TRUE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);
    h = cf("C:\\Titanfall2\\data.bin", RD, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS);
    T_TRUE(h != BAD);
    T_EQ(rt_handle_close(h), TRUE);
}

TEST(core, file_open_stops)
{
    put("data.bin", "hello", 0644);
    const RtName n = RT_NAME_W(W("C:\\Titanfall2\\data.bin"));
    T_TRAPS(rt_file_create(n, RD, SH, OPEN_EXISTING, 0, (HANDLE)(uintptr_t)4), "rt_file_create: not implemented: a template file");
    T_TRAPS(rt_file_create(n, RD, 8, OPEN_EXISTING, 0, NULL), "rt_file_create: not implemented: share mode 0x8");
    T_TRAPS(rt_file_create(n, MAXIMUM_ALLOWED, SH, OPEN_EXISTING, 0, NULL), "rt_file_create: not implemented: access 0x2000000");
    T_TRAPS(rt_file_create(n, WRITE_DAC, SH, OPEN_EXISTING, 0, NULL), "rt_file_create: not implemented: access");
    T_TRAPS(rt_file_create(n, FILE_APPEND_DATA, SH, OPEN_EXISTING, 0, NULL), "rt_file_create: not implemented: an append-only handle");
    T_TRAPS(rt_file_create(n, RD, SH, OPEN_EXISTING, FILE_FLAG_POSIX_SEMANTICS, NULL), "rt_file_create: not implemented: flags and attributes 0x1000000");
    T_TRAPS(rt_file_create(n, RD, SH, OPEN_EXISTING, FILE_ATTRIBUTE_DIRECTORY, NULL), "rt_file_create: not implemented: flags and attributes");
    T_TRAPS(rt_file_create(RT_NAME_A("NUL"), WR, SH, CREATE_NEW, 0, NULL), "rt_file_create: not implemented: CREATE_NEW on NUL");
    T_TRAPS(rt_file_create((RtName){ W("a"), "a" }, RD, SH, OPEN_EXISTING, 0, NULL), "both a wide and a narrow form");
    /* the stops come before the argument errors and before the name is looked at */
    T_TRAPS(rt_file_create(RT_NAME_A("Q:\\none"), RD, 8, 99, 0, NULL), "rt_file_create: not implemented: share mode");
}
