#include <string.h>

#include "rt/test.h"
#include "rt/machine.h"
#include "rt/win/sysinfo.h"

/* The buffer is the 64-byte structure followed by 8 bytes the shim must not touch (0xA5). */
typedef union {
    MEMORYSTATUSEX ms;
    unsigned char raw[72];
} MemBuf;

static void fill(MemBuf *b, unsigned char v)
{
    memset(b->raw, v, sizeof b->raw);
    b->ms.dwLength = 64;
}

TEST(kernel32, GlobalMemoryStatusEx)
{
    const RtMachine *m = rt_machine();
    uint64_t used = m->phys_total - m->phys_avail;              /* the memory in use */
    uint64_t total_virtual = m->max_address - m->min_address + 1;
    MemBuf b;
    unsigned char first[64];
    unsigned bad;

    T_CASE("fills every member from the machine description");
    fill(&b, 0xA5);
    T_EQ(CALL(kernel32, GlobalMemoryStatusEx, &b.ms), TRUE);
    T_EQ(b.ms.ullTotalPhys, m->phys_total);
    T_EQ(b.ms.ullAvailPhys, m->phys_avail);
    T_EQ(b.ms.ullTotalPageFile, m->phys_total);
    T_EQ(b.ms.ullAvailPageFile, m->phys_avail);
    T_EQ(b.ms.ullTotalVirtual, total_virtual);
    T_EQ(b.ms.ullAvailVirtual, total_virtual - used);
    T_TRUE(b.ms.ullAvailVirtual < b.ms.ullTotalVirtual);        /* below the total */
    T_EQ(b.ms.ullAvailExtendedVirtual, 0);                      /* reserved: always 0 */

    T_CASE("dwMemoryLoad is the per cent of physical memory in use, from 0 to 100");
    T_EQ(b.ms.dwMemoryLoad, (DWORD)(used * 100 / m->phys_total));
    T_TRUE(b.ms.dwMemoryLoad <= 100);

    T_CASE("writes only the structure, so dwLength stays 64 and the 8 bytes after it are not written");
    T_EQ(b.ms.dwLength, 64);
    bad = 0;
    for (unsigned i = 64; i < 72; i++)
        if (b.raw[i] != 0xA5) bad++;
    T_EQ(bad, 0);

    T_CASE("returns TRUE and the same answer on every call");
    fill(&b, 0xA5);
    T_EQ(CALL(kernel32, GlobalMemoryStatusEx, &b.ms), TRUE);
    memcpy(first, b.raw, sizeof first);
    fill(&b, 0x5A);
    T_EQ(CALL(kernel32, GlobalMemoryStatusEx, &b.ms), TRUE);
    T_EQ(memcmp(first, b.raw, sizeof first), 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    fill(&b, 0xA5);
    CALL(kernel32, GlobalMemoryStatusEx, &b.ms);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stops: no T_CASE of their own (RT-TRAPS). */
    T_TRAPS(CALL(kernel32, GlobalMemoryStatusEx, NULL), "NULL lpBuffer");
    fill(&b, 0xA5);
    b.ms.dwLength = 0;
    T_TRAPS(CALL(kernel32, GlobalMemoryStatusEx, &b.ms), "dwLength other than 64");
    b.ms.dwLength = 32;
    T_TRAPS(CALL(kernel32, GlobalMemoryStatusEx, &b.ms), "dwLength other than 64");
}
