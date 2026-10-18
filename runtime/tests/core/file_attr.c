/* Attributes and times (rt/file.h, core/file_priv.h): rt_file_get_attributes, _get_attributes_ex,
 * _set_attributes, _info_by_handle and _set_times.  The plan's cases are numbered 1 to 15; each is
 * one T_PLAN block.  Files are made on the host under rt_test_install_root() with put(); the
 * functions under test are called through the public names and checked against the host's stat.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/time.h"

int      rt_file_test_fd(HANDLE h);
unsigned rt_file_test_open_count(void);
HANDLE   rt_file_std_slot(unsigned i);

#define RD   GENERIC_READ
#define WR   GENERIC_WRITE
#define SH   (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define BAD  INVALID_HANDLE_VALUE
#define N(x) RT_NAME_W(W(x))

/* W: the wide form of an ASCII literal, for N().  (file_lifetime.c has no W; this one is local.) */
static const WCHAR *W(const char *s) {
    static WCHAR buf[4][256];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    size_t i = 0;
    for (; s[i] && i < 255; i++) b[i] = (WCHAR)(unsigned char)s[i];
    b[i] = 0;
    return b;
}
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
static uint64_t ft(FILETIME f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

TEST(core, file_attr_get)
{
    put("f.bin", "hello", 0644);
    put("ro.bin", "abc", 0444);
    mkdir(I("Dir"), 0755);

    T_PLAN(1, "attributes of files, directories and a mixed-case A name") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2\\f.bin")), 0x20);
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2\\ro.bin")), 0x21);
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2\\Dir")), 0x10);
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2")), 0x10);
        T_EQ(rt_file_get_attributes(RT_NAME_A("c:/titanfall2/F.BIN")), 0x20);
    }

    T_PLAN(2, "missing names: the error of the translation or of stat") {
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2\\missing.bin")), INVALID_FILE_ATTRIBUTES);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2\\no\\x")), INVALID_FILE_ATTRIBUTES);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_get_attributes(N("D:\\x")), INVALID_FILE_ATTRIBUTES);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        rt_thread_set_last_error(0xDEAD);
        T_EQ(rt_file_get_attributes((RtName){0}), INVALID_FILE_ATTRIBUTES);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    }

    T_PLAN(3, "NUL stops the function") {
        T_TRAPS(rt_file_get_attributes(N("NUL")), "rt_file_get_attributes: not implemented");
    }
}

TEST(core, file_attr_get_ex)
{
    put("f.bin", "hello", 0644);
    mkdir(I("Dir"), 0755);

    T_PLAN(4, "level 0 fills every field of a file and of a directory") {
        WIN32_FILE_ATTRIBUTE_DATA d;
        memset(&d, 0, sizeof d);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_file_get_attributes_ex(N("C:\\Titanfall2\\f.bin"), 0, &d));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(d.dwFileAttributes, 0x20);
        T_EQ(d.nFileSizeLow, 5);
        T_EQ(d.nFileSizeHigh, 0);
        struct stat st;
        T_EQ(stat(I("f.bin"), &st), 0);
        T_EQ(ft(d.ftCreationTime), rt_time_filetime_from_unix(st.st_birthtimespec.tv_sec, st.st_birthtimespec.tv_nsec));
        T_EQ(ft(d.ftLastAccessTime), rt_time_filetime_from_unix(st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec));
        T_EQ(ft(d.ftLastWriteTime), rt_time_filetime_from_unix(st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec));

        memset(&d, 0, sizeof d);
        T_TRUE(rt_file_get_attributes_ex(N("C:\\Titanfall2\\Dir"), 0, &d));
        T_EQ(d.dwFileAttributes, 0x10);
        T_EQ(d.nFileSizeLow, 0);
        T_EQ(d.nFileSizeHigh, 0);
    }

    T_PLAN(5, "other levels and missing names fail untouched; out NULL stops") {
        WIN32_FILE_ATTRIBUTE_DATA d, aa;
        memset(&aa, 0xAA, sizeof aa);

        memcpy(&d, &aa, sizeof d);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(!rt_file_get_attributes_ex(N("C:\\Titanfall2\\f.bin"), 1, &d));
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_TRUE(memcmp(&d, &aa, sizeof d) == 0);

        memcpy(&d, &aa, sizeof d);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(!rt_file_get_attributes_ex(N("C:\\Titanfall2\\missing.bin"), 1, &d));
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_TRUE(memcmp(&d, &aa, sizeof d) == 0);

        memcpy(&d, &aa, sizeof d);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(!rt_file_get_attributes_ex(N("C:\\Titanfall2\\missing.bin"), 0, &d));
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        T_TRUE(memcmp(&d, &aa, sizeof d) == 0);

        T_TRAPS(rt_file_get_attributes_ex(N("C:\\Titanfall2\\f.bin"), 0, NULL), "rt_file_get_attributes_ex: not implemented");
    }
}

TEST(core, file_attr_set_mode)
{
    put("f.bin", "hello", 0644);
    put("g.bin", "hello", 0640);

    T_PLAN(6, "READONLY removes the write bits, NORMAL gives the owner's write bit back") {
        struct stat st;
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_file_set_attributes(N("C:\\Titanfall2\\f.bin"), FILE_ATTRIBUTE_READONLY));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(stat(I("f.bin"), &st), 0);
        T_EQ(st.st_mode & 0222, 0);
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2\\f.bin")), 0x21);

        T_TRUE(rt_file_set_attributes(N("C:\\Titanfall2\\f.bin"), FILE_ATTRIBUTE_NORMAL));
        T_EQ(stat(I("f.bin"), &st), 0);
        T_TRUE((st.st_mode & 0200) != 0);
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2\\f.bin")), 0x20);
    }

    T_PLAN(7, "a mode of 0640 keeps its group and other bits through READONLY and NORMAL") {
        struct stat st;
        T_TRUE(rt_file_set_attributes(N("C:\\Titanfall2\\g.bin"), FILE_ATTRIBUTE_READONLY));
        T_EQ(stat(I("g.bin"), &st), 0);
        T_EQ(st.st_mode & 0777, 0440);

        T_TRUE(rt_file_set_attributes(N("C:\\Titanfall2\\g.bin"), FILE_ATTRIBUTE_NORMAL));
        T_EQ(stat(I("g.bin"), &st), 0);
        T_EQ(st.st_mode & 0777, 0640);
    }
}

TEST(core, file_attr_set_bits)
{
    put("f.bin", "hello", 0644);
    mkdir(I("Dir"), 0755);
    chmod(I("Dir"), 0755);

    T_PLAN(8, "HIDDEN and SYSTEM are not stored; READONLY and DIRECTORY on a directory change nothing") {
        struct stat st;
        T_TRUE(rt_file_set_attributes(N("C:\\Titanfall2\\f.bin"), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2\\f.bin")), 0x20);

        T_TRUE(rt_file_set_attributes(N("C:\\Titanfall2\\Dir"), FILE_ATTRIBUTE_READONLY));
        T_EQ(stat(I("Dir"), &st), 0);
        T_EQ(st.st_mode & 0777, 0755);

        T_TRUE(rt_file_set_attributes(N("C:\\Titanfall2\\Dir"), FILE_ATTRIBUTE_DIRECTORY));
        T_TRUE(rt_file_set_attributes(N("C:\\Titanfall2\\Dir"), FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY));
        T_EQ(stat(I("Dir"), &st), 0);
        T_EQ(st.st_mode & 0777, 0755);
        T_EQ(rt_file_get_attributes(N("C:\\Titanfall2\\Dir")), 0x10);
    }

    T_PLAN(9, "a directory bit on a file or on a missing name stops; a missing file is ERROR_FILE_NOT_FOUND") {
        T_TRAPS(rt_file_set_attributes(N("C:\\Titanfall2\\f.bin"), FILE_ATTRIBUTE_DIRECTORY), "rt_file_set_attributes: not implemented");
        T_TRAPS(rt_file_set_attributes(N("C:\\Titanfall2\\missing.bin"), FILE_ATTRIBUTE_DIRECTORY), "rt_file_set_attributes: not implemented");
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(!rt_file_set_attributes(N("C:\\Titanfall2\\missing.bin"), FILE_ATTRIBUTE_READONLY));
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
    }
}

static FILETIME mkft(uint64_t t) {
    FILETIME f;
    f.dwLowDateTime = (DWORD)(t & 0xFFFFFFFFu);
    f.dwHighDateTime = (DWORD)(t >> 32);
    return f;
}

/* Cases 10 to 15 share one handle, as the plan does: h is opened in case 10, outlives the
 * unlink of every name, and is closed in case 15. */
TEST(core, file_attr_times)
{
    put("f.bin", "hello", 0644);
    mkdir(I("Dir"), 0755);
    chmod(I("Dir"), 0755);
    HANDLE h = BAD;

    T_PLAN(10, "a file handle: fields from fstat; links follow the host's links; still TRUE after unlink") {
        BY_HANDLE_FILE_INFORMATION bi;
        memset(&bi, 0, sizeof bi);
        h = cfa("C:\\Titanfall2\\f.bin", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD);
        struct stat st;
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);

        rt_thread_set_last_error(0xDEAD);
        T_TRUE(rt_file_info_by_handle(h, &bi));
        T_EQ(T_LAST_ERROR(), 0xDEAD);
        T_EQ(bi.dwFileAttributes, 0x20);
        T_EQ(bi.nFileSizeLow, 5);
        T_EQ(bi.nFileSizeHigh, 0);
        T_EQ(bi.dwVolumeSerialNumber, RT_VOLUME_SERIAL);
        T_EQ(bi.nNumberOfLinks, 1);
        T_EQ(bi.nFileIndexHigh, (DWORD)((uint64_t)st.st_ino >> 32));
        T_EQ(bi.nFileIndexLow, (DWORD)((uint64_t)st.st_ino & 0xFFFFFFFFu));
        T_EQ(ft(bi.ftCreationTime), rt_time_filetime_from_unix(st.st_birthtimespec.tv_sec, st.st_birthtimespec.tv_nsec));
        T_EQ(ft(bi.ftLastAccessTime), rt_time_filetime_from_unix(st.st_atimespec.tv_sec, st.st_atimespec.tv_nsec));
        T_EQ(ft(bi.ftLastWriteTime), rt_time_filetime_from_unix(st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec));

        T_EQ(link(I("f.bin"), I("f2.bin")), 0);
        T_TRUE(rt_file_info_by_handle(h, &bi));
        T_EQ(bi.nNumberOfLinks, 2);

        T_EQ(unlink(I("f.bin")), 0);
        T_EQ(unlink(I("f2.bin")), 0);
        T_TRUE(rt_file_info_by_handle(h, &bi));
    }

    T_PLAN(11, "a directory handle; NULL fails; out NULL, a NUL handle and a standard handle stop") {
        BY_HANDLE_FILE_INFORMATION bi;
        memset(&bi, 0, sizeof bi);
        HANDLE dh = cfa("C:\\Titanfall2\\Dir", RD, SH, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS);
        T_REQUIRE(dh != BAD);
        T_TRUE(rt_file_info_by_handle(dh, &bi));
        T_EQ(bi.dwFileAttributes, 0x10);
        T_EQ(bi.nNumberOfLinks, 1);
        T_EQ(bi.nFileSizeLow, 0);
        T_EQ(bi.nFileSizeHigh, 0);

        rt_thread_set_last_error(0xDEAD);
        T_TRUE(!rt_file_info_by_handle(NULL, &bi));
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

        HANDLE nul = cfa("NUL", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(nul != BAD);
        T_TRAPS(rt_file_info_by_handle(nul, &bi), "rt_file_info_by_handle: not implemented");
        T_EQ(rt_handle_close(nul), TRUE);

        T_TRAPS(rt_file_info_by_handle(rt_file_std_slot(1), &bi), "rt_file_info_by_handle: not implemented");
        T_TRAPS(rt_file_info_by_handle(dh, NULL), "rt_file_info_by_handle: not implemented");
        T_EQ(rt_handle_close(dh), TRUE);
    }

    T_PLAN(12, "the write time alone: mtime is set, atime stays; then one half second") {
        struct stat st;
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        const struct timespec a0 = st.st_atimespec;

        FILETIME w = mkft(126444736000000000ULL);
        T_TRUE(rt_file_set_times(h, NULL, NULL, &w));
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        T_EQ(st.st_mtimespec.tv_sec, 1000000000);
        T_EQ(st.st_atimespec.tv_sec, a0.tv_sec);
        T_EQ(st.st_atimespec.tv_nsec, a0.tv_nsec);

        w = mkft(126444736000000000ULL + 5000000);
        T_TRUE(rt_file_set_times(h, NULL, NULL, &w));
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        T_EQ(st.st_mtimespec.tv_sec, 1000000000);
        T_EQ(st.st_mtimespec.tv_nsec, 500000000);
    }

    T_PLAN(13, "access alone; all NULL; a zero write time; a zero creation time with a write time") {
        struct stat st;
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        const struct timespec m0 = st.st_mtimespec;

        FILETIME a = mkft(127444736000000000ULL);
        T_TRUE(rt_file_set_times(h, NULL, &a, NULL));
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        T_EQ(st.st_atimespec.tv_sec, 1100000000);
        T_EQ(st.st_mtimespec.tv_sec, m0.tv_sec);
        T_EQ(st.st_mtimespec.tv_nsec, m0.tv_nsec);

        T_TRUE(rt_file_set_times(h, NULL, NULL, NULL));
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        T_EQ(st.st_atimespec.tv_sec, 1100000000);
        T_EQ(st.st_mtimespec.tv_sec, m0.tv_sec);
        T_EQ(st.st_mtimespec.tv_nsec, m0.tv_nsec);

        FILETIME zero = { 0, 0 };
        T_TRUE(rt_file_set_times(h, NULL, NULL, &zero));
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        T_EQ(st.st_mtimespec.tv_sec, m0.tv_sec);
        T_EQ(st.st_mtimespec.tv_nsec, m0.tv_nsec);

        FILETIME w = mkft(126444736000000000ULL);
        T_TRUE(rt_file_set_times(h, &zero, NULL, &w));
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        T_EQ(st.st_mtimespec.tv_sec, 1000000000);
        T_EQ(st.st_mtimespec.tv_nsec, 0);
    }

    T_PLAN(14, "a creation time is stored as the birth time, after a write time; 0xFFFFFFFF:0xFFFFFFFF, FILETIME 1 and a NUL handle stop; NULL fails") {
        struct stat st;
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        const struct timespec m0 = st.st_mtimespec;

        FILETIME c = mkft(127444736000000000ULL);                 /* unix 1100000000 */
        T_TRUE(rt_file_set_times(h, &c, NULL, NULL));
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        T_EQ(st.st_birthtimespec.tv_sec, 1100000000);
        T_EQ(st.st_birthtimespec.tv_nsec, 0);
        T_EQ(st.st_mtimespec.tv_sec, m0.tv_sec);
        T_EQ(st.st_mtimespec.tv_nsec, m0.tv_nsec);

        /* The write time (unix 1050000000) is earlier than the birth time just set, which the host
         * lowers to it: the creation time, unix 1200000000.5, is stored after the write time. */
        FILETIME w = mkft(126944736000000000ULL);
        FILETIME c2 = mkft(128444736005000000ULL);                /* unix 1200000000.5 */
        T_TRUE(rt_file_set_times(h, &c2, NULL, &w));
        T_EQ(fstat(rt_file_test_fd(h), &st), 0);
        T_EQ(st.st_birthtimespec.tv_sec, 1200000000);
        T_EQ(st.st_birthtimespec.tv_nsec, 500000000);
        T_EQ(st.st_mtimespec.tv_sec, 1050000000);
        T_EQ(st.st_mtimespec.tv_nsec, 0);

        FILETIME cmax = { 0xFFFFFFFFu, 0xFFFFFFFFu };
        FILETIME c1s = mkft(1);
        T_TRAPS(rt_file_set_times(h, &cmax, NULL, &w), "a creation time 0xffffffffffffffff");
        T_TRAPS(rt_file_set_times(h, &c1s, NULL, &w), "a creation time 0x1");
        HANDLE nul = cfa("NUL", RD, SH, OPEN_EXISTING, 0);
        T_REQUIRE(nul != BAD);
        T_TRAPS(rt_file_set_times(nul, NULL, NULL, &w), "rt_file_set_times: not implemented");
        T_EQ(rt_handle_close(nul), TRUE);
        rt_thread_set_last_error(0xDEAD);
        T_TRUE(!rt_file_set_times(NULL, NULL, NULL, &w));
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }

    T_PLAN(15, "closing the handle after cases 10 to 14 drops the open count by one") {
        const unsigned before = rt_file_test_open_count();
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_file_test_open_count(), before - 1);
    }
}
