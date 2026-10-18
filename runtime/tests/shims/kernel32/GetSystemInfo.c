#include "rt/test.h"
#include "rt/machine.h"

/* CPUID leaf 1 on a context of its own (as machine_cpuid does): eax and ebx, whole. */
static void cpuid_leaf1(uint32_t *eax, uint32_t *ebx) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.r[0] = 1;
    rt_cpuid(&c);
    *eax = (uint32_t)c.r[0];
    *ebx = (uint32_t)c.r[3];
}

TEST(kernel32, GetSystemInfo)
{
    const RtMachine *m = rt_machine();
    SYSTEM_INFO si;
    uint64_t want_mask = m->cpu_count >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << m->cpu_count) - 1;

    T_CASE("fills every field from the machine description");
    memset(&si, 0xA5, sizeof si);                       /* garbage: every field must be written */
    CALL(kernel32, GetSystemInfo, &si);
    T_EQ(si.wProcessorArchitecture, PROCESSOR_ARCHITECTURE_AMD64);   /* 9: x64 */
    T_EQ(si.wReserved, 0);
    T_EQ(si.dwPageSize, m->page_size);
    T_EQ(si.dwPageSize, 4096);                          /* the guest page (machine.h) */
    T_EQ(si.dwAllocationGranularity, m->granularity);
    T_EQ(si.dwAllocationGranularity, 65536);
    T_EQ((uint64_t)(uintptr_t)si.lpMinimumApplicationAddress, m->min_address);
    T_EQ((uint64_t)(uintptr_t)si.lpMinimumApplicationAddress, UINT64_C(0x10000));
    T_EQ((uint64_t)(uintptr_t)si.lpMaximumApplicationAddress, m->max_address);
    T_EQ((uint64_t)(uintptr_t)si.lpMaximumApplicationAddress, UINT64_C(0x7FFFFFEFFFF));
    T_EQ(si.dwProcessorType, PROCESSOR_AMD_X8664);      /* 8664 */

    T_CASE("the processor count and mask agree with CPUID leaf 1");
    uint32_t eax, ebx;
    cpuid_leaf1(&eax, &ebx);
    T_EQ(si.dwNumberOfProcessors, m->cpu_count);
    T_EQ(si.dwActiveProcessorMask, want_mask);
    T_EQ((ebx >> 16) & 0xFF, si.dwNumberOfProcessors);  /* leaf 1 EBX bits 23:16 carry the count */

    T_CASE("the level and revision agree with CPUID leaf 1");
    T_EQ(si.wProcessorLevel, (eax >> 8) & 0xF);         /* the family */
    T_EQ(si.wProcessorRevision, ((eax >> 4) & 0xF) << 8 | (eax & 0xF));   /* model << 8 | stepping */
    T_EQ(si.wProcessorRevision, 0x0D08);                /* model 13, stepping 8 (machine.c) */

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetSystemInfo, &si);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Not a documented case: a NULL buffer is a guest bug, which Windows would fault on.  The
     * core stops the run with a report instead of writing anywhere. */
    T_TRAPS(CALL(kernel32, GetSystemInfo, NULL), "GetSystemInfo: the buffer is NULL");
}
