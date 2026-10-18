#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"

#define SENT ((DWORD)0x5A5A5A5Au)

/* I and put of runtime/tests/core/file_open.c: a host path under the install root, and a file
 * made there (the guest's C:\Titanfall2\<rel>). */
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

TEST(kernel32, GetDiskFreeSpaceA)
{
    DWORD spc, bps, nfree, ntot;
    ULONGLONG want_total = 0, want_clusters;

    T_CASE("8 sectors per cluster and 512 bytes per sector for C root and NULL root");
    spc = bps = 0;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, "C:\\", &spc, &bps, &nfree, &ntot), TRUE);
    T_EQ(spc, 8);
    T_EQ(bps, 512);
    spc = bps = 0;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, NULL, &spc, &bps, &nfree, &ntot), TRUE);
    T_EQ(spc, 8);
    T_EQ(bps, 512);

    T_CASE("total clusters are the core total over 4096, capped at 0xFFFFFFFF, for C root and NULL root");
    /* The oracle: the core's total of the C root, asked by the test itself. */
    T_REQUIRE(rt_disk_space(RT_NAME_A("C:\\"), NULL, &want_total, NULL));
    want_clusters = want_total / (8 * 512);
    if (want_clusters > 0xFFFFFFFFu) want_clusters = 0xFFFFFFFFu;
    ntot = 0;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, "C:\\", &spc, &bps, &nfree, &ntot), TRUE);
    T_EQ((ULONGLONG)ntot, want_clusters);
    ntot = 0;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, NULL, &spc, &bps, &nfree, &ntot), TRUE);
    T_EQ((ULONGLONG)ntot, want_clusters);

    T_CASE("free clusters are written and not above the total clusters");
    nfree = SENT;
    ntot = 0;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, "C:\\", &spc, &bps, &nfree, &ntot), TRUE);
    T_TRUE(nfree != SENT);
    T_TRUE(nfree <= ntot);

    T_CASE("D root, empty root and a missing directory fail with ERROR_PATH_NOT_FOUND and write nothing");
    CALL(kernel32, SetLastError, 0x1234);
    spc = bps = nfree = ntot = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, "D:\\", &spc, &bps, &nfree, &ntot), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    T_EQ(spc, SENT);
    T_EQ(bps, SENT);
    T_EQ(nfree, SENT);
    T_EQ(ntot, SENT);
    CALL(kernel32, SetLastError, 0x1234);
    spc = bps = nfree = ntot = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, "", &spc, &bps, &nfree, &ntot), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    T_EQ(spc, SENT);
    T_EQ(bps, SENT);
    T_EQ(nfree, SENT);
    T_EQ(ntot, SENT);
    CALL(kernel32, SetLastError, 0x1234);
    spc = bps = nfree = ntot = SENT;
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, "C:\\Titanfall2\\no_such_dir", &spc, &bps, &nfree, &ntot), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
    T_EQ(spc, SENT);
    T_EQ(bps, SENT);
    T_EQ(nfree, SENT);
    T_EQ(ntot, SENT);

    T_CASE("success leaves a preset last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, "C:\\", &spc, &bps, &nfree, &ntot), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetDiskFreeSpaceA, NULL, &spc, &bps, &nfree, &ntot), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The traps: the NULL out-pointers, checked first by the shim, and the stop of the core
     * (a name that leads to a file).  Listed in RT-TRAPS; they come after the last case. */
    put("disk_space_file.txt", "x", 0644);
    T_TRAPS(CALL(kernel32, GetDiskFreeSpaceA, "C:\\", NULL, &bps, &nfree, &ntot),
            "GetDiskFreeSpaceA with a NULL lpSectorsPerCluster");
    T_TRAPS(CALL(kernel32, GetDiskFreeSpaceA, "C:\\", &spc, NULL, &nfree, &ntot),
            "GetDiskFreeSpaceA with a NULL lpBytesPerSector");
    T_TRAPS(CALL(kernel32, GetDiskFreeSpaceA, "C:\\", &spc, &bps, NULL, &ntot),
            "GetDiskFreeSpaceA with a NULL lpNumberOfFreeClusters");
    T_TRAPS(CALL(kernel32, GetDiskFreeSpaceA, "C:\\", &spc, &bps, &nfree, NULL),
            "GetDiskFreeSpaceA with a NULL lpTotalNumberOfClusters");
    T_TRAPS(CALL(kernel32, GetDiskFreeSpaceA, "C:\\Titanfall2\\disk_space_file.txt", &spc, &bps, &nfree, &ntot),
            "rt_disk_space: not implemented: a name that is not a directory");
}
