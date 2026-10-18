#include <string.h>

#include "rt/test.h"
#include "rt/machine.h"
#include "rt/win/sysinfo.h"

_Static_assert(sizeof(MEMORYSTATUS) == 56, "MEMORYSTATUS");

/* The buffer is the 56-byte structure followed by 8 bytes the shim must not touch (0xA5). */
typedef union {
    MEMORYSTATUS ms;
    unsigned char raw[64];
} MemBuf;

static void fill(MemBuf *b, unsigned char v)
{
    memset(b->raw, v, sizeof b->raw);
    b->ms.dwLength = 0;                 /* the caller need not set it: start from 0 */
}

TEST(kernel32, GlobalMemoryStatus)
{
    const RtMachine *m = rt_machine();
    uint64_t used = m->phys_total - m->phys_avail;              /* the memory in use */
    uint64_t total_virtual = m->max_address - m->min_address + 1;
    MemBuf b;
    unsigned bad;

    T_CASE("fills every member from the machine description");
    fill(&b, 0xA5);
    CALL(kernel32, GlobalMemoryStatus, &b.ms);
    T_EQ(b.ms.dwTotalPhys, m->phys_total);
    T_EQ(b.ms.dwAvailPhys, m->phys_avail);
    T_EQ(b.ms.dwTotalPageFile, m->phys_total);
    T_EQ(b.ms.dwAvailPageFile, m->phys_avail);
    T_EQ(b.ms.dwTotalVirtual, total_virtual);
    T_EQ(b.ms.dwAvailVirtual, total_virtual - used);
    T_TRUE(b.ms.dwAvailVirtual < b.ms.dwTotalVirtual);          /* below the total */

    T_CASE("dwMemoryLoad is the per cent of physical memory in use, from 0 to 100");
    T_EQ(b.ms.dwMemoryLoad, (DWORD)(used * 100 / m->phys_total));
    T_TRUE(b.ms.dwMemoryLoad <= 100);

    T_CASE("sets dwLength to 56, which the caller need not set");
    T_EQ(b.ms.dwLength, 56);

    T_CASE("writes only the structure, so the 8 bytes after it are not written");
    bad = 0;
    for (unsigned i = 56; i < 64; i++)
        if (b.raw[i] != 0xA5) bad++;
    T_EQ(bad, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    fill(&b, 0xA5);
    CALL(kernel32, GlobalMemoryStatus, &b.ms);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stop: no T_CASE of its own (RT-TRAPS). */
    T_TRAPS(CALL(kernel32, GlobalMemoryStatus, NULL), "NULL lpBuffer");
}
