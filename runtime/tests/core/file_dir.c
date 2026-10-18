/* The delete, move and directory functions of the file layer (rt/file.h, core/file_dir.c):
 * DeleteFile, MoveFileEx, CreateDirectory, RemoveDirectory, GetDriveType, by the numbered plan
 * (swarm brief core-file-dir-test).  The helpers are those of file_lifetime.c.  Each TEST runs
 * in its own process; a case that needs a spoiled state starts a new TEST.
 */
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/path.h"

int rt_file_test_fd(HANDLE h);

#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 0x1u
#endif

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
static bool exists_host(const char *path) { struct stat st; return stat(path, &st) == 0; }
static bool exists(const char *rel) { return exists_host(I(rel)); }
static bool content_is(const char *rel, const char *text) {
    char buf[256] = {0};
    int fd = open(I(rel), O_RDONLY);
    if (fd < 0) return false;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    return n >= 0 && strcmp(buf, text) == 0;
}
static bool has_entry(const char *rel_dir, const char *name) {
    DIR *d = opendir(I(rel_dir));
    bool found = false;
    if (!d) return false;
    for (struct dirent *e = readdir(d); e; e = readdir(d)) if (strcmp(e->d_name, name) == 0) found = true;
    closedir(d);
    return found;
}
static HANDLE cfa(const char *win, DWORD access, DWORD share, DWORD disp, DWORD fa) {
    return rt_file_create(RT_NAME_A(win), access, share, disp, fa, NULL);
}
/* A name as the guest gives it: narrow, or wide (UTF-16 built here from ASCII). */
static RtName na(const char *win) { return RT_NAME_A(win); }
static RtName nw(const char *win) {
    static WCHAR buf[4][256];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    unsigned i = 0;
    for (; win[i] && i < 255; i++) b[i] = (WCHAR)(unsigned char)win[i];
    b[i] = 0;
    return RT_NAME_W(b);
}

TEST(core, file_dir_delete)
{
    T_PLAN(1, "delete a file: TRUE, LE untouched, gone; a following OPEN_EXISTING fails with ERROR_FILE_NOT_FOUND; a wide name works") {
        put("x.bin", "0123456789", 0644);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete(na("C:\\Titanfall2\\x.bin")), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(!exists("x.bin"));
        T_EQ(cfa("C:\\Titanfall2\\x.bin", RD, SH, OPEN_EXISTING, 0), BAD);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        put("y.bin", "abc", 0644);
        T_EQ(rt_file_delete(nw("C:\\Titanfall2\\y.bin")), TRUE);
        T_TRUE(!exists("y.bin"));
    }

    T_PLAN(2, "delete what is not there: FALSE, ERROR_FILE_NOT_FOUND; in a missing directory: ERROR_PATH_NOT_FOUND") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete(na("C:\\Titanfall2\\nothere.bin")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete(na("C:\\Titanfall2\\nodir\\x.bin")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        /* A NULL name is ERROR_PATH_NOT_FOUND for these two, and so is the empty one. */
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete((RtName){ 0 }), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_create((RtName){ 0 }), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete(na("")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    }

    T_PLAN(3, "a file of mode 0444: FALSE, ERROR_ACCESS_DENIED, the file is still there") {
        put("ro3.bin", "keep", 0444);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete(na("C:\\Titanfall2\\ro3.bin")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_TRUE(exists("ro3.bin"));
    }

    T_PLAN(4, "a directory, empty or not, C:\\Titanfall2 and NUL: FALSE, ERROR_ACCESS_DENIED, still there") {
        mkdir(I("d4e"), 0755);
        mkdir(I("d4f"), 0755);
        put("d4f/f.bin", "in", 0644);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete(na("C:\\Titanfall2\\d4e")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_TRUE(exists("d4e"));
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete(na("C:\\Titanfall2\\d4f")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_TRUE(exists("d4f"));
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete(na("C:\\Titanfall2")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_TRUE(exists_host(rt_test_install_root()));
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_delete(na("NUL")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    }
}

TEST(core, file_dir_share)
{
    /* Fresh process: the share-7 open must find nothing logged, the second open logs one line. */
    T_PLAN(5, "a file open without FILE_SHARE_DELETE: TRUE, logged, gone, still readable through its handle; share 7 open: TRUE, nothing logged") {
        put("s5a.bin", "share seven", 0644);
        HANDLE a = cfa("C:\\Titanfall2\\s5a.bin", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(a != BAD);
        T_EQ(rt_file_delete(na("C:\\Titanfall2\\s5a.bin")), TRUE);
        T_TRUE(!T_LOGGED(RT_CH_FILE, "sharing violation not enforced"));
        T_EQ(rt_handle_close(a), TRUE);

        put("s5b.bin", "no share delete", 0644);
        HANDLE b = cfa("C:\\Titanfall2\\s5b.bin", RD, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, 0);
        T_REQUIRE(b != BAD);
        const int fd = rt_file_test_fd(b);
        T_EQ(rt_file_delete(na("C:\\Titanfall2\\s5b.bin")), TRUE);
        T_TRUE(T_LOGGED(RT_CH_FILE, "sharing violation not enforced: DeleteFile of"));
        T_TRUE(!exists("s5b.bin"));
        char buf[64] = {0};
        T_EQ(pread(fd, buf, sizeof buf - 1, 0), (ssize_t)strlen("no share delete"));
        T_TRUE(strcmp(buf, "no share delete") == 0);
        T_EQ(rt_handle_close(b), TRUE);
    }
}

TEST(core, file_dir_move)
{
    T_PLAN(6, "a.bin to b.bin, flags 0: TRUE, LE untouched, a gone, b has the content; OPEN_EXISTING of a fails, of b works") {
        put("a.bin", "alpha", 0644);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\a.bin"), na("C:\\Titanfall2\\b.bin"), 0), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_TRUE(!exists("a.bin"));
        T_TRUE(content_is("b.bin", "alpha"));
        T_EQ(cfa("C:\\Titanfall2\\a.bin", RD, SH, OPEN_EXISTING, 0), BAD);
        HANDLE b = cfa("C:\\Titanfall2\\b.bin", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(b != BAD);
        T_EQ(rt_handle_close(b), TRUE);
    }

    T_PLAN(7, "onto an existing c.bin, flags 0: FALSE, ERROR_ALREADY_EXISTS, both files intact; with MOVEFILE_REPLACE_EXISTING: TRUE, c.bin has the source's content, the source gone") {
        put("a.bin", "alpha", 0644);
        put("c.bin", "gamma", 0644);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\a.bin"), na("C:\\Titanfall2\\c.bin"), 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
        T_TRUE(content_is("a.bin", "alpha"));
        T_TRUE(content_is("c.bin", "gamma"));
        T_EQ(rt_file_move(na("C:\\Titanfall2\\a.bin"), na("C:\\Titanfall2\\c.bin"), MOVEFILE_REPLACE_EXISTING), TRUE);
        T_TRUE(content_is("c.bin", "alpha"));
        T_TRUE(!exists("a.bin"));
    }

    T_PLAN(8, "missing source: ERROR_FILE_NOT_FOUND; target in a missing directory: ERROR_PATH_NOT_FOUND; both bad: the source's error") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\nosrc.bin"), na("C:\\Titanfall2\\t8.bin"), 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        put("s8.bin", "eight", 0644);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\s8.bin"), na("C:\\Titanfall2\\nodir\\t.bin"), 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\nosrc.bin"), na("C:\\Titanfall2\\nodir\\t.bin"), 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    }

    T_PLAN(9, "c.bin to C.BIN, flags 0: TRUE (the same file is not 'exists'); a name onto itself: TRUE") {
        put("c.bin", "nine", 0644);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\c.bin"), na("C:\\Titanfall2\\C.BIN"), 0), TRUE);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\C.BIN"), na("C:\\Titanfall2\\C.BIN"), 0), TRUE);
    }

    T_PLAN(10, "a directory with a file inside moves, flags 0: TRUE; the file is under the new name") {
        mkdir(I("dir10"), 0755);
        put("dir10/f.bin", "ten", 0644);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\dir10"), na("C:\\Titanfall2\\dir10b"), 0), TRUE);
        T_TRUE(content_is("dir10b/f.bin", "ten"));
    }

    T_PLAN(11, "REPLACE with an existing directory as target, or a directory as source onto an existing file: FALSE, ERROR_ACCESS_DENIED; REPLACE onto a 0444 file traps rt_file_move") {
        put("f11.bin", "f", 0644);
        mkdir(I("d11t"), 0755);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\f11.bin"), na("C:\\Titanfall2\\d11t"), MOVEFILE_REPLACE_EXISTING), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        mkdir(I("d11s"), 0755);
        put("e11.bin", "e", 0644);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\d11s"), na("C:\\Titanfall2\\e11.bin"), MOVEFILE_REPLACE_EXISTING), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        put("ro11.bin", "r", 0444);
        RtName s = na("C:\\Titanfall2\\f11.bin"), t = na("C:\\Titanfall2\\ro11.bin");
        T_TRAPS(rt_file_move(s, t, MOVEFILE_REPLACE_EXISTING), "rt_file_move: not implemented");
    }

    T_PLAN(12, "flags 4 and 0x10, and a `to` of (RtName){0}: STOP (rt_file_move: not implemented)") {
        put("src12.bin", "twelve", 0644);
        RtName s = na("C:\\Titanfall2\\src12.bin"), t = na("C:\\Titanfall2\\t12.bin");
        T_TRAPS(rt_file_move(s, t, 4), "rt_file_move: not implemented");
        T_TRAPS(rt_file_move(s, t, 0x10), "rt_file_move: not implemented");
        T_TRAPS(rt_file_move(s, (RtName){0}, 0), "rt_file_move: not implemented");
    }

    T_PLAN(13, "C:\\Windows as source and as target: FALSE, ERROR_ACCESS_DENIED") {
        put("w13.bin", "w", 0644);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_move(na("C:\\Windows"), na("C:\\Titanfall2\\w13d.bin"), 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\w13.bin"), na("C:\\Windows"), 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
    }

    T_PLAN(14, "C:\\Titanfall2\\m.bin to C:\\Users\\Player\\m.bin, flags 0: TRUE; the file is there under the new name") {
        put("m.bin", "fourteen", 0644);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\m.bin"), na("C:\\Users\\Player\\m.bin"), 0), TRUE);
        HANDLE h = cfa("C:\\Users\\Player\\m.bin", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD);
        char buf[32] = {0};
        T_EQ(pread(rt_file_test_fd(h), buf, sizeof buf - 1, 0), (ssize_t)strlen("fourteen"));
        T_TRUE(strcmp(buf, "fourteen") == 0);
        T_EQ(rt_handle_close(h), TRUE);
    }
}

TEST(core, file_dir_move_share)
{
    /* Fresh process: the move's log line is the only one. */
    T_PLAN(15, "source open without FILE_SHARE_DELETE: TRUE and 'sharing violation not enforced: MoveFile of' logged") {
        put("s15.bin", "fifteen", 0644);
        HANDLE h = cfa("C:\\Titanfall2\\s15.bin", RD, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD);
        T_EQ(rt_file_move(na("C:\\Titanfall2\\s15.bin"), na("C:\\Titanfall2\\t15.bin"), 0), TRUE);
        T_TRUE(T_LOGGED(RT_CH_FILE, "sharing violation not enforced: MoveFile of"));
        T_EQ(rt_handle_close(h), TRUE);
    }
}

TEST(core, file_dir_dirs)
{
    T_PLAN(16, "rt_dir_create of a new name: TRUE, LE untouched, a host directory of mode 0755 with the spelling given; again, and on an existing file's name: ERROR_ALREADY_EXISTS; missing parent and D:\\x: ERROR_PATH_NOT_FOUND; a wide name works and a file can be created inside it at once") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_create(na("C:\\Titanfall2\\Dir16")), TRUE);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        struct stat st;
        T_REQUIRE(stat(I("Dir16"), &st) == 0);
        T_TRUE(S_ISDIR(st.st_mode));
        T_EQ(st.st_mode & 0777, 0755);
        T_TRUE(has_entry("", "Dir16"));
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_create(na("C:\\Titanfall2\\Dir16")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
        put("f16.bin", "file", 0644);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_create(na("C:\\Titanfall2\\f16.bin")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ALREADY_EXISTS);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_create(na("C:\\Titanfall2\\nodir16\\sub")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_create(na("D:\\x")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        T_EQ(rt_dir_create(nw("C:\\Titanfall2\\Wide16")), TRUE);
        HANDLE h = cfa("C:\\Titanfall2\\Wide16\\inside.bin", WR, SH, CREATE_NEW, 0);
        T_REQUIRE(h != BAD);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(17, "rt_dir_remove: an empty directory TRUE, gone; not empty FALSE, ERROR_DIR_NOT_EMPTY, still there; missing ERROR_PATH_NOT_FOUND; a file traps rt_dir_remove; C:\\Titanfall2 (current directory C:\\) and C:\\Users\\Player\\AppData: ERROR_ACCESS_DENIED, still there") {
        mkdir(I("e17"), 0755);
        T_EQ(rt_dir_remove(na("C:\\Titanfall2\\e17")), TRUE);
        T_TRUE(!exists("e17"));
        mkdir(I("n17"), 0755);
        put("n17/x.bin", "x", 0644);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_remove(na("C:\\Titanfall2\\n17")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_DIR_NOT_EMPTY);
        T_TRUE(exists("n17"));
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_remove(na("C:\\Titanfall2\\missing17")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        put("f17.bin", "f", 0644);
        RtName f = na("C:\\Titanfall2\\f17.bin");
        T_TRAPS(rt_dir_remove(f), "rt_dir_remove: not implemented");
        /* The process starts in C:\Titanfall2 (rt_path_init), so the install root is the current
         * directory until it is changed: Windows gives ERROR_SHARING_VIOLATION there (review X10,
         * Wine kernel32/tests/directory.c test_RemoveDirectoryW; file_dir_current below).  The
         * skeleton refusal is looked at from C:\ here. */
        T_EQ(rt_path_set_cwd_w(nw("C:\\").w), 0);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_remove(na("C:\\Titanfall2")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_TRUE(exists_host(rt_test_install_root()));
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_dir_remove(na("C:\\Users\\Player\\AppData")), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_ACCESS_DENIED);
        T_EQ(rt_dir_create(na("C:\\Users\\Player\\AppData")), FALSE);
    }
}

/* RemoveDirectoryW of the current directory (review X10; Wine kernel32/tests/directory.c
 * test_RemoveDirectoryA and test_RemoveDirectoryW): after SetCurrentDirectoryW(dir),
 * RemoveDirectoryW(dir) fails with ERROR_SHARING_VIOLATION and the directory stays; after
 * SetCurrentDirectoryW to its parent it succeeds.  The current directory is the runtime's own
 * (rt/path.h), not the host's, so the check must not read the host's. */
TEST(core, file_dir_current)
{
    T_TRUE(mkdir(I("cur33"), 0755) == 0);
    T_TRUE(mkdir(I("oth33"), 0755) == 0);
    T_EQ(rt_path_set_cwd_w(nw("C:\\Titanfall2\\cur33").w), 0);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_dir_remove(nw("C:\\Titanfall2\\cur33")), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_SHARING_VIOLATION);
    T_TRUE(exists("cur33"));
    /* the same directory, named the narrow way, and named relative to the current one */
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_dir_remove(na("C:\\Titanfall2\\cur33")), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_SHARING_VIOLATION);
    T_TRUE(exists("cur33"));
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_dir_remove(na("..\\cur33")), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_SHARING_VIOLATION);
    T_TRUE(exists("cur33"));
    /* another directory is removed as usual while this one is current; last error untouched */
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_dir_remove(na("C:\\Titanfall2\\oth33")), TRUE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_TRUE(!exists("oth33"));
    /* the parent is the current directory now: the directory goes, last error untouched */
    T_EQ(rt_path_set_cwd_w(nw("C:\\Titanfall2").w), 0);
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_dir_remove(nw("C:\\Titanfall2\\cur33")), TRUE);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_TRUE(!exists("cur33"));
    /* the install root is the start directory, so it is the current directory here: the same
     * answer; it stays (the skeleton refusal is tested in file_dir_dirs, from C:\) */
    rt_thread_set_last_error(0xDEAD);
    T_EQ(rt_dir_remove(na("C:\\Titanfall2")), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_SHARING_VIOLATION);
    T_TRUE(exists_host(rt_test_install_root()));
}

TEST(core, file_dir_drive)
{
    T_PLAN(18, "rt_drive_type: (RtName){0}, C:\\, c:\\, C:/ and the A form of C:\\: 3; D:\\ and Z:\\: 1; LE untouched; C: 3, D: 1 (no slash, as X:\\); C, c, C?, c?, ? and the empty string: 1; C:\\Titanfall2 and \\\\server\\share\\ trap") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_drive_type((RtName){0}), 3);
        T_EQ(rt_drive_type(nw("C:\\")), 3);
        T_EQ(rt_drive_type(nw("c:\\")), 3);
        T_EQ(rt_drive_type(nw("C:/")), 3);
        T_EQ(rt_drive_type(na("C:\\")), 3);
        T_EQ(rt_drive_type(na("D:\\")), 1);
        T_EQ(rt_drive_type(nw("Z:\\")), 1);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        /* Windows answers "X:" as "X:\" does, and the letter-only texts DRIVE_NO_ROOT_DIR
         * (review X9, Wine kernel32/tests/drive.c).  Last error is not asserted for these. */
        T_EQ(rt_drive_type(nw("C:")), 3);
        T_EQ(rt_drive_type(na("C:")), 3);
        T_EQ(rt_drive_type(nw("D:")), 1);
        T_EQ(rt_drive_type(nw("C")), 1);
        T_EQ(rt_drive_type(nw("c")), 1);
        T_EQ(rt_drive_type(nw("C?")), 1);
        T_EQ(rt_drive_type(nw("c?")), 1);
        T_EQ(rt_drive_type(nw("?")), 1);
        T_EQ(rt_drive_type(nw("")), 1);
        T_EQ(rt_drive_type(na("")), 1);
        T_TRAPS(rt_drive_type(na("C:\\Titanfall2")), "rt_drive_type: not implemented");
        T_TRAPS(rt_drive_type(na("\\\\server\\share\\")), "rt_drive_type: not implemented");
    }
}

/* ---- threads ---------------------------------------------------------------------------------- */

#define NTHREAD 8
#define NNAME   100
#define NFILE   50
static atomic_uint made[NNAME], bad, go;

static void *racer19(void *arg) {
    const unsigned t = (unsigned)(uintptr_t)arg;
    while (!atomic_load(&go)) { }
    for (unsigned k = 0; k < NNAME; k++) {
        char name[64];
        snprintf(name, sizeof name, "C:\\Titanfall2\\race19_%u", k);
        rt_thread_set_last_error(0xDEAD);
        if (rt_dir_create(na(name))) atomic_fetch_add(&made[k], 1);
        else if (rt_thread_get_last_error() != ERROR_ALREADY_EXISTS) atomic_fetch_add(&bad, 1);
    }
    char dir[64];
    snprintf(dir, sizeof dir, "C:\\Titanfall2\\own19_%u", t);
    if (!rt_dir_create(na(dir))) atomic_fetch_add(&bad, 1);
    for (unsigned f = 0; f < NFILE; f++) {
        char file[96];
        snprintf(file, sizeof file, "%s\\f%u.tmp", dir, f);
        HANDLE h = cfa(file, WR, SH, CREATE_NEW, 0);
        if (h == BAD || !rt_handle_close(h)) atomic_fetch_add(&bad, 1);
    }
    for (unsigned f = 0; f < NFILE; f++) {
        char file[96];
        snprintf(file, sizeof file, "%s\\f%u.tmp", dir, f);
        if (!rt_file_delete(na(file))) atomic_fetch_add(&bad, 1);
    }
    if (!rt_dir_remove(na(dir))) atomic_fetch_add(&bad, 1);
    return NULL;
}

TEST(core, file_dir_threads)
{
    T_PLAN(19, "8 threads race rt_dir_create on the same 100 names: exactly one TRUE per name, every other failure ERROR_ALREADY_EXISTS; each thread then creates 50 files in its own directory, deletes them and removes the directory: no failure") {
        pthread_t t[NTHREAD];
        for (unsigned i = 0; i < NTHREAD; i++) T_REQUIRE(pthread_create(&t[i], NULL, racer19, (void *)(uintptr_t)i) == 0);
        atomic_store(&go, 1);
        for (unsigned i = 0; i < NTHREAD; i++) pthread_join(t[i], NULL);
        unsigned wrong_new = 0;
        for (unsigned k = 0; k < NNAME; k++) wrong_new += atomic_load(&made[k]) != 1;
        T_EQ(wrong_new, 0);
        T_EQ(atomic_load(&bad), 0);
    }
}
