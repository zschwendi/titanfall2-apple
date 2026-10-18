/* rt_disk_space (core/file_dir.c; the specification is rt/file.h).
 *
 * Where the expected values come from: this file asks the host itself, with statfs on the
 * host directory that the mapping of docs/RUNTIME.md section 7.1 puts behind each guest
 * directory (C:\ is <state>/vfs, C:\Titanfall2 the install root, C:\Users\Player <state>/home).
 * The size of a volume does not change during a test and is compared exactly.  The free space
 * does, in both directions and between any two calls (other builds write to and delete from the
 * same disk), so one reading of the host cannot be the expected value of one call.  What is
 * required is that the two agree exactly in at least one of up to 2,000 tries, each a host
 * reading followed at once by the call: a wrong unit never agrees, and a busy disk still does
 * within a few tries.  Where the host's two free counts are equal, as on the volume this was
 * written on, the test cannot tell `avail` from `free`.  (A first version compared only when
 * the host gave the same number before and after the call; that failed once in the full suite,
 * because the number had moved and moved back in between.)  Which names succeed and which code
 * a failure has is Wine's kernel32/tests/drive.c (test_GetDiskFreeSpaceW).
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"

#define UNSET UINT64_C(0x1122334455667788)

static const WCHAR *W(const char *a) {
    static WCHAR buf[4][512];
    static unsigned k;
    WCHAR *b = buf[k++ & 3];
    size_t i = 0;
    for (; a[i]; i++) b[i] = (unsigned char)a[i];
    b[i] = 0;
    return b;
}
static const char *host_of(const char *base, const char *rel) {
    static char buf[4][2048];
    static unsigned k;
    char *b = buf[k++ & 3];
    snprintf(b, 2048, "%s/%s", base, rel);
    return b;
}

/* rt_disk_space(name) against statfs(host): the size exactly; the free space exactly in at
 * least one of many tries (see the top of the file). */
static void same_volume(RtName name, const char *host)
{
    struct statfs before;
    ULONGLONG avail = UNSET, total = UNSET, free_ = UNSET;
    bool exact = false;
    for (int tries = 0; tries < 2000 && !exact; tries++) {
        T_REQUIRE(statfs(host, &before) == 0);
        avail = total = free_ = UNSET;
        T_REQUIRE(rt_disk_space(name, &avail, &total, &free_));
        exact = avail == (uint64_t)before.f_bavail * before.f_bsize &&
                free_ == (uint64_t)before.f_bfree * before.f_bsize;
    }
    T_TRUE(exact);
    T_EQ(total, (uint64_t)before.f_blocks * before.f_bsize);
    T_TRUE(total > 0);
    T_TRUE(avail <= free_);
    T_TRUE(free_ <= total);
    T_TRUE(avail % before.f_bsize == 0 && free_ % before.f_bsize == 0);
}

static void fails_with(RtName name, DWORD code)
{
    ULONGLONG avail = UNSET, total = UNSET, free_ = UNSET;
    rt_set_last_error(T_CTX(), 0x5EA1);
    T_EQ(rt_disk_space(name, &avail, &total, &free_), FALSE);
    T_EQ(T_LAST_ERROR(), code);
    T_EQ(avail, UNSET);
    T_EQ(total, UNSET);
    T_EQ(free_, UNSET);
}

TEST(core, file_disk)
{
    Ctx *ctx = T_CTX();
    const char *vfs = host_of(rt_test_state_dir(), "vfs");
    rt_set_last_error(ctx, 0x5EA1);

    T_CASE("no name and the spellings of the root give the volume behind the root");
    same_volume((RtName){ NULL, NULL }, vfs);
    same_volume(RT_NAME_A("C:\\"), vfs);
    same_volume(RT_NAME_W(W("C:\\")), vfs);
    same_volume(RT_NAME_A("c:/"), vfs);
    same_volume(RT_NAME_A("\\"), vfs);
    same_volume(RT_NAME_W(W("/")), vfs);

    T_CASE("a directory that is not the root gives the volume that holds it");
    same_volume(RT_NAME_A("C:\\Titanfall2"), rt_test_install_root());
    same_volume(RT_NAME_W(W("C:\\Titanfall2\\")), rt_test_install_root());
    same_volume(RT_NAME_A("."), rt_test_install_root());              /* the current directory at start */
    same_volume(RT_NAME_A("C:\\Users\\Player\\Documents"), host_of(rt_test_state_dir(), "home/Documents"));
    same_volume(RT_NAME_W(W("C:\\Users\\Player\\AppData\\Local\\Temp")), host_of(rt_test_state_dir(), "tmp"));
    same_volume(RT_NAME_A("C:\\Windows\\system32"), host_of(rt_test_state_dir(), "vfs/Windows/system32"));

    T_CASE("each of the three pointers may be NULL");
    {
        ULONGLONG avail = UNSET, total = UNSET, free_ = UNSET, total2 = UNSET;
        T_TRUE(rt_disk_space(RT_NAME_A("C:\\"), NULL, NULL, NULL));
        T_TRUE(rt_disk_space(RT_NAME_A("C:\\"), &avail, NULL, NULL));
        T_TRUE(rt_disk_space(RT_NAME_A("C:\\"), NULL, &total, NULL));
        T_TRUE(rt_disk_space(RT_NAME_A("C:\\"), NULL, NULL, &free_));
        T_TRUE(avail != UNSET && total != UNSET && free_ != UNSET);
        T_TRUE(rt_disk_space(RT_NAME_A("C:\\"), NULL, &total2, NULL));
        T_EQ(total2, total);
    }

    T_CASE("success leaves last error alone");
    T_EQ(T_LAST_ERROR(), 0x5EA1);

    T_CASE("the empty string, another drive and a missing directory are ERROR_PATH_NOT_FOUND, nothing written");
    fails_with(RT_NAME_A(""), ERROR_PATH_NOT_FOUND);
    fails_with(RT_NAME_W(W("")), ERROR_PATH_NOT_FOUND);
    fails_with(RT_NAME_A("D:\\"), ERROR_PATH_NOT_FOUND);
    fails_with(RT_NAME_W(W("Z:\\")), ERROR_PATH_NOT_FOUND);
    fails_with(RT_NAME_A("C:\\Titanfall2\\no-such-directory"), ERROR_PATH_NOT_FOUND);
    fails_with(RT_NAME_A("C:\\no-such-directory\\deeper"), ERROR_PATH_NOT_FOUND);

    T_CASE("a file, and a name with both forms, stop");
    {
        ULONGLONG total = UNSET;
        const char *file = host_of(rt_test_install_root(), "disk.txt");
        const int fd = open(file, O_CREAT | O_WRONLY, 0644);
        T_REQUIRE(fd >= 0);
        close(fd);
        T_TRAPS(rt_disk_space(RT_NAME_A("C:\\Titanfall2\\disk.txt"), NULL, &total, NULL),
                "rt_disk_space: not implemented: a name that is not a directory");
        T_TRAPS(rt_disk_space((RtName){ W("C:\\"), "C:\\" }, NULL, &total, NULL), "both a wide and a narrow form");
        T_EQ(total, UNSET);
    }
}
