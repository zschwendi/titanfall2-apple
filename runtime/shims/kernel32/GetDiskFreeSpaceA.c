/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getdiskfreespacea
 * RT-CASES:  8 sectors per cluster and 512 bytes per sector for C root and NULL root; total clusters are the core total over 4096, capped at 0xFFFFFFFF, for C root and NULL root; free clusters are written and not above the total clusters; D root, empty root and a missing directory fail with ERROR_PATH_NOT_FOUND and write nothing; success leaves a preset last error alone
 * RT-TRAPS:  a NULL lpSectorsPerCluster; a NULL lpBytesPerSector; a NULL lpNumberOfFreeClusters; a NULL lpTotalNumberOfClusters; core: a name that is not a directory
 */
#include "rt/shim.h"
#include "rt/file.h"

#define SECTORS_PER_CLUSTER 8
#define BYTES_PER_SECTOR    512

/* Clusters that hold the given bytes, capped at 0xFFFFFFFF (planner's choice: the page does
 * not say what a volume beyond 16 TB reports). */
static DWORD clusters(ULONGLONG bytes)
{
    ULONGLONG n = bytes / (SECTORS_PER_CLUSTER * BYTES_PER_SECTOR);
    return n > 0xFFFFFFFFu ? 0xFFFFFFFFu : (DWORD)n;
}

SHIM(kernel32, BOOL, GetDiskFreeSpaceA, LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters)
{
    /* The page does not say that an out-pointer may be NULL: each NULL one is a trap, and
     * they are checked before anything else. */
    if (!lpSectorsPerCluster) RT_UNIMPLEMENTED(ctx, "GetDiskFreeSpaceA with a NULL lpSectorsPerCluster");
    if (!lpBytesPerSector) RT_UNIMPLEMENTED(ctx, "GetDiskFreeSpaceA with a NULL lpBytesPerSector");
    if (!lpNumberOfFreeClusters) RT_UNIMPLEMENTED(ctx, "GetDiskFreeSpaceA with a NULL lpNumberOfFreeClusters");
    if (!lpTotalNumberOfClusters) RT_UNIMPLEMENTED(ctx, "GetDiskFreeSpaceA with a NULL lpTotalNumberOfClusters");

    ULONGLONG avail = 0, total = 0;
    if (!rt_disk_space(RT_NAME_A(lpRootPathName), &avail, &total, NULL)) return FALSE;

    /* The core's numbers in clusters: the free ones are those available to the caller. */
    *lpSectorsPerCluster = SECTORS_PER_CLUSTER;
    *lpBytesPerSector = BYTES_PER_SECTOR;
    *lpNumberOfFreeClusters = clusters(avail);
    *lpTotalNumberOfClusters = clusters(total);
    return TRUE;
}
