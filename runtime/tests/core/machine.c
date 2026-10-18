/* The pretend machine as the guest sees it (docs/RUNTIME.md 8.5): GetSystemInfo, the processor
 * feature numbers, the description's start-up and RT_CPUS, and the consistency of every place
 * that states the processor count, the version or the features.  The CPUID answers themselves
 * are in machine_cpuid.c.
 *
 * The expected values are the Windows documentation's and the design's, written out by hand.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

#include "rt/test.h"
#include "rt/machine.h"
#include "rt/vm.h"

/* In runtime/core/machine.c: RT_CPUS parsing, not in rt/machine.h. */
uint32_t rt_machine_parse_cpus(const char *text);

/* The PF_* numbers, from the Windows documentation (winnt.h), written here independently of
 * the runtime's own list. */
#define T_PF_FLOATING_POINT_PRECISION_ERRATA       0
#define T_PF_FLOATING_POINT_EMULATED               1
#define T_PF_COMPARE_EXCHANGE_DOUBLE               2
#define T_PF_MMX_INSTRUCTIONS_AVAILABLE            3
#define T_PF_PPC_MOVEMEM_64BIT_OK                  4
#define T_PF_ALPHA_BYTE_INSTRUCTIONS               5
#define T_PF_XMMI_INSTRUCTIONS_AVAILABLE           6
#define T_PF_3DNOW_INSTRUCTIONS_AVAILABLE          7
#define T_PF_RDTSC_INSTRUCTION_AVAILABLE           8
#define T_PF_PAE_ENABLED                           9
#define T_PF_XMMI64_INSTRUCTIONS_AVAILABLE        10
#define T_PF_SSE_DAZ_MODE_AVAILABLE               11
#define T_PF_NX_ENABLED                           12
#define T_PF_SSE3_INSTRUCTIONS_AVAILABLE          13
#define T_PF_COMPARE_EXCHANGE128                  14
#define T_PF_COMPARE64_EXCHANGE128                15
#define T_PF_CHANNELS_ENABLED                     16
#define T_PF_XSAVE_ENABLED                        17
#define T_PF_SECOND_LEVEL_ADDRESS_TRANSLATION     20
#define T_PF_VIRT_FIRMWARE_ENABLED                21
#define T_PF_RDWRFSGSBASE_AVAILABLE               22
#define T_PF_FASTFAIL_AVAILABLE                   23
#define T_PF_RDRAND_INSTRUCTION_AVAILABLE         28
#define T_PF_RDTSCP_INSTRUCTION_AVAILABLE         32
#define T_PF_RDPID_INSTRUCTION_AVAILABLE          33
#define T_PF_MONITORX_INSTRUCTION_AVAILABLE       35
#define T_PF_SSSE3_INSTRUCTIONS_AVAILABLE         36
#define T_PF_SSE4_1_INSTRUCTIONS_AVAILABLE        37
#define T_PF_SSE4_2_INSTRUCTIONS_AVAILABLE        38
#define T_PF_AVX_INSTRUCTIONS_AVAILABLE           39
#define T_PF_AVX2_INSTRUCTIONS_AVAILABLE          40
#define T_PF_AVX512F_INSTRUCTIONS_AVAILABLE       41
#define T_PF_ERMS_AVAILABLE                       42

/* The processor count the machine reports, read from the description. */
static uint32_t count(void) { return rt_machine()->cpu_count; }

/* CPUID through a context of the test's own: leaf in rax, subleaf in rcx. */
static void cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4]) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.r[0] = leaf;
    c.r[1] = sub;
    rt_cpuid(&c);
    out[0] = (uint32_t)c.r[0];
    out[1] = (uint32_t)c.r[3];
    out[2] = (uint32_t)c.r[1];
    out[3] = (uint32_t)c.r[2];
}

/* ---- GetSystemInfo -------------------------------------------------------------------------- */

TEST(core, system_info_fields)
{
    /* Every field the Windows documentation names, from a buffer full of garbage. */
    const RtMachine *mc = rt_machine();
    SYSTEM_INFO si;
    memset(&si, 0xAB, sizeof si);
    rt_machine_system_info(&si);
    T_EQ(si.wProcessorArchitecture, PROCESSOR_ARCHITECTURE_AMD64);   /* 9 */
    T_EQ(si.wProcessorArchitecture, 9);
    T_EQ(si.wReserved, 0);
    T_EQ(si.dwOemId, 9);                                             /* the same bytes, in the union */
    T_EQ(si.dwPageSize, 4096);
    T_EQ((uint64_t)(uintptr_t)si.lpMinimumApplicationAddress, UINT64_C(0x10000));
    T_EQ((uint64_t)(uintptr_t)si.lpMaximumApplicationAddress, UINT64_C(0x7FFFFFEFFFF));
    T_EQ(si.dwNumberOfProcessors, mc->cpu_count);
    T_EQ(si.dwProcessorType, 8664);
    T_EQ(si.dwAllocationGranularity, 65536);
    T_EQ(si.wProcessorLevel, 6);
    T_EQ(si.wProcessorRevision, 0x0D08);                             /* model 13 << 8 | stepping 8 */
}

TEST(core, system_info_writes_only_its_structure)
{
    /* SYSTEM_INFO is 48 bytes; the bytes after it are not touched. */
    uint64_t buf[8];
    memset(buf, 0xCD, sizeof buf);
    SYSTEM_INFO *si = (SYSTEM_INFO *)buf;
    rt_machine_system_info(si);
    const uint8_t *tail = (const uint8_t *)buf + sizeof(SYSTEM_INFO);
    for (size_t i = 0; i < sizeof buf - sizeof(SYSTEM_INFO); i++) T_REQUIRE(tail[i] == 0xCD);
    T_EQ(si->dwPageSize, 4096);
}

TEST(core, system_info_null_stops)
{
    /* GetSystemInfo has no failure return; NULL is a guest bug, reported loudly. */
    T_TRAPS(rt_machine_system_info(NULL), "GetSystemInfo: the buffer is NULL");
}

TEST(core, system_info_active_mask_for_the_host_count)
{
    /* One bit per processor, from bit 0 up. */
    SYSTEM_INFO si;
    rt_machine_system_info(&si);
    uint32_t n = count();
    T_EQ(si.dwActiveProcessorMask, n == 64 ? ~UINT64_C(0) : (UINT64_C(1) << n) - 1);
    uint64_t m = si.dwActiveProcessorMask;
    unsigned bits = 0;
    while (m) { bits += (unsigned)(m & 1); m >>= 1; }
    T_EQ(bits, n);
}

TEST(core, processor_count_boundaries_in_every_place)
{
    /* The count is stated in SYSTEM_INFO (the count and the mask) and in CPUID (leaf 1 ebx
     * and leaf 0x80000008 ecx).  Each boundary of the APIC-ID width is tried: 1 and 2 (no
     * worker threads, and the smallest width), 3 and 4, 5 and 8 (the host's count), 9, and 63
     * and 64 (the mask's top bit and the largest count). */
    RtMachine *m = (RtMachine *)rt_machine();
    uint32_t saved = m->cpu_count;
    static const struct { uint32_t n, width; } cases[] = {
        { 1, 0 }, { 2, 1 }, { 3, 2 }, { 4, 2 }, { 5, 3 }, { 8, 3 }, { 9, 4 }, { 63, 6 }, { 64, 6 },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t n = cases[i].n;
        m->cpu_count = n;

        SYSTEM_INFO si;
        rt_machine_system_info(&si);
        T_EQ(si.dwNumberOfProcessors, n);
        T_EQ(si.dwActiveProcessorMask, n == 64 ? ~UINT64_C(0) : (UINT64_C(1) << n) - 1);

        uint32_t v[4];
        cpuid(1, 0, v);
        T_EQ((v[1] >> 16) & 0xFF, n);

        cpuid(0x80000008, 0, v);
        T_EQ(v[2] & 0xFF, n - 1);
        T_EQ((v[2] >> 12) & 0xF, cases[i].width);
    }
    m->cpu_count = saved;
}

TEST(core, processor_revision_and_level_come_from_leaf_1)
{
    /* wProcessorLevel is the family and wProcessorRevision is model << 8 | stepping, both as
     * leaf 1 eax states them: they cannot disagree. */
    uint32_t v[4];
    cpuid(1, 0, v);
    uint32_t family = (v[0] >> 8) & 0xF;
    uint32_t model = ((v[0] >> 4) & 0xF) | (((v[0] >> 16) & 0xF) << 4);
    uint32_t stepping = v[0] & 0xF;
    SYSTEM_INFO si;
    rt_machine_system_info(&si);
    T_EQ(si.wProcessorLevel, family);
    T_EQ(si.wProcessorRevision, (model << 8) | stepping);
}

/* ---- IsProcessorFeaturePresent -------------------------------------------------------------- */

TEST(core, processor_feature_answers)
{
    /* Every feature number the runtime knows, against the Windows meaning of each on the
     * processor this machine describes (an SSE3 part with no SSSE3, no AVX, and no RDTSCP). */
    static const DWORD yes[] = {
        T_PF_COMPARE_EXCHANGE_DOUBLE, T_PF_MMX_INSTRUCTIONS_AVAILABLE, T_PF_XMMI_INSTRUCTIONS_AVAILABLE,
        T_PF_RDTSC_INSTRUCTION_AVAILABLE, T_PF_PAE_ENABLED, T_PF_XMMI64_INSTRUCTIONS_AVAILABLE,
        T_PF_SSE_DAZ_MODE_AVAILABLE, T_PF_SSE3_INSTRUCTIONS_AVAILABLE,
    };
    static const DWORD no[] = {
        T_PF_FLOATING_POINT_PRECISION_ERRATA, T_PF_FLOATING_POINT_EMULATED, T_PF_PPC_MOVEMEM_64BIT_OK,
        T_PF_ALPHA_BYTE_INSTRUCTIONS, T_PF_3DNOW_INSTRUCTIONS_AVAILABLE, T_PF_NX_ENABLED,
        T_PF_COMPARE_EXCHANGE128, T_PF_COMPARE64_EXCHANGE128,
        T_PF_CHANNELS_ENABLED, T_PF_XSAVE_ENABLED, T_PF_SECOND_LEVEL_ADDRESS_TRANSLATION,
        T_PF_VIRT_FIRMWARE_ENABLED, T_PF_RDWRFSGSBASE_AVAILABLE, T_PF_FASTFAIL_AVAILABLE,
        T_PF_RDRAND_INSTRUCTION_AVAILABLE, T_PF_RDTSCP_INSTRUCTION_AVAILABLE,
        T_PF_RDPID_INSTRUCTION_AVAILABLE, T_PF_MONITORX_INSTRUCTION_AVAILABLE,
        T_PF_SSSE3_INSTRUCTIONS_AVAILABLE, T_PF_SSE4_1_INSTRUCTIONS_AVAILABLE,
        T_PF_SSE4_2_INSTRUCTIONS_AVAILABLE, T_PF_AVX_INSTRUCTIONS_AVAILABLE,
        T_PF_AVX2_INSTRUCTIONS_AVAILABLE, T_PF_AVX512F_INSTRUCTIONS_AVAILABLE,
        T_PF_ERMS_AVAILABLE,
    };
    for (unsigned i = 0; i < sizeof yes / sizeof yes[0]; i++) {
        T_REQUIRE(rt_machine_processor_feature(yes[i]) == TRUE);
    }
    for (unsigned i = 0; i < sizeof no / sizeof no[0]; i++) {
        T_REQUIRE(rt_machine_processor_feature(no[i]) == FALSE);
    }
    /* Explicit, one by one, for the two the design names. */
    T_EQ(rt_machine_processor_feature(T_PF_FASTFAIL_AVAILABLE), FALSE);
    T_EQ(rt_machine_processor_feature(T_PF_SSE3_INSTRUCTIONS_AVAILABLE), TRUE);
    T_EQ(rt_machine_processor_feature(T_PF_SSSE3_INSTRUCTIONS_AVAILABLE), FALSE);
}

TEST(core, processor_feature_arm_numbers_are_false_without_a_log)
{
    /* The ARM and newer numbers the x64 machine does not have are FALSE, and that is a
     * known answer, so nothing is logged. */
    unsigned before = T_LOG_COUNT(RT_CH_IMPORT);
    for (DWORD pf = 18; pf <= 31; pf++) T_REQUIRE(rt_machine_processor_feature(pf) == FALSE);
    T_EQ(rt_machine_processor_feature(34), FALSE);
    for (DWORD pf = 43; pf <= 45; pf++) T_REQUIRE(rt_machine_processor_feature(pf) == FALSE);
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before);
}

TEST(core, processor_feature_unknown_numbers_are_false_and_logged_once)
{
    /* Numbers past the table, and the largest DWORD: FALSE, and each one is logged once. */
    unsigned before = T_LOG_COUNT(RT_CH_IMPORT);
    T_EQ(rt_machine_processor_feature(46), FALSE);
    T_EQ(rt_machine_processor_feature(47), FALSE);
    T_EQ(rt_machine_processor_feature(1000), FALSE);
    T_EQ(rt_machine_processor_feature((DWORD)0xFFFFFFFFu), FALSE);
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 4);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "IsProcessorFeaturePresent(46): not a feature the runtime reports"));
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "IsProcessorFeaturePresent(4294967295)"));
    T_EQ(rt_machine_processor_feature(46), FALSE);
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 4);                     /* once, not again */
}

TEST(core, processor_feature_matches_cpuid_bit_for_bit)
{
    /* For each feature with a CPUID bit, IsProcessorFeaturePresent is TRUE exactly when the
     * bit is set in the leaf the game reads.  reg: 0 eax, 1 ebx, 2 ecx, 3 edx. */
    static const struct { DWORD pf; uint32_t leaf, sub; int reg; uint32_t bit; } map[] = {
        { T_PF_COMPARE_EXCHANGE_DOUBLE,        1,          0, 3,  8 },   /* CX8 */
        { T_PF_MMX_INSTRUCTIONS_AVAILABLE,     1,          0, 3, 23 },   /* MMX */
        { T_PF_XMMI_INSTRUCTIONS_AVAILABLE,    1,          0, 3, 25 },   /* SSE */
        { T_PF_XMMI64_INSTRUCTIONS_AVAILABLE,  1,          0, 3, 26 },   /* SSE2 */
        { T_PF_RDTSC_INSTRUCTION_AVAILABLE,    1,          0, 3,  4 },   /* TSC */
        { T_PF_PAE_ENABLED,                    1,          0, 3,  6 },   /* PAE */
        { T_PF_SSE3_INSTRUCTIONS_AVAILABLE,    1,          0, 2,  0 },   /* SSE3 */
        { T_PF_SSSE3_INSTRUCTIONS_AVAILABLE,   1,          0, 2,  9 },   /* SSSE3 */
        { T_PF_SSE4_1_INSTRUCTIONS_AVAILABLE,  1,          0, 2, 19 },   /* SSE4.1 */
        { T_PF_SSE4_2_INSTRUCTIONS_AVAILABLE,  1,          0, 2, 20 },   /* SSE4.2 */
        { T_PF_AVX_INSTRUCTIONS_AVAILABLE,     1,          0, 2, 28 },   /* AVX */
        { T_PF_COMPARE_EXCHANGE128,            1,          0, 2, 13 },   /* CMPXCHG16B */
        { T_PF_COMPARE64_EXCHANGE128,          1,          0, 2, 13 },
        { T_PF_XSAVE_ENABLED,                  1,          0, 2, 26 },   /* XSAVE */
        { T_PF_VIRT_FIRMWARE_ENABLED,          1,          0, 2,  5 },   /* VMX */
        { T_PF_RDRAND_INSTRUCTION_AVAILABLE,   1,          0, 2, 30 },   /* RDRAND */
        { T_PF_RDWRFSGSBASE_AVAILABLE,         7,          0, 1,  0 },   /* FSGSBASE */
        { T_PF_AVX2_INSTRUCTIONS_AVAILABLE,    7,          0, 1,  5 },   /* AVX2 */
        { T_PF_AVX512F_INSTRUCTIONS_AVAILABLE, 7,          0, 1, 16 },   /* AVX512F */
        { T_PF_ERMS_AVAILABLE,                 7,          0, 1,  9 },   /* ERMS */
        { T_PF_RDPID_INSTRUCTION_AVAILABLE,    7,          0, 2, 22 },   /* RDPID */
        { T_PF_NX_ENABLED,                     0x80000001, 0, 3, 20 },   /* NX */
        { T_PF_RDTSCP_INSTRUCTION_AVAILABLE,   0x80000001, 0, 3, 27 },   /* RDTSCP */
        { T_PF_MONITORX_INSTRUCTION_AVAILABLE, 0x80000001, 0, 2, 29 },   /* MONITORX */
    };
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++) {
        uint32_t v[4];
        cpuid(map[i].leaf, map[i].sub, v);
        BOOL bit = (v[map[i].reg] >> map[i].bit) & 1 ? TRUE : FALSE;
        if (rt_machine_processor_feature(map[i].pf) != bit)
            rt_test_fail(__FILE__, __LINE__, "IsProcessorFeaturePresent(%u) disagrees with CPUID leaf %#x, reg %d, bit %u, which is %s",
                         (unsigned)map[i].pf, (unsigned)map[i].leaf, map[i].reg, (unsigned)map[i].bit,
                         bit ? "set" : "clear");
    }
    /* DAZ has no CPUID bit: the float mode provides it, so it is TRUE. */
    T_EQ(rt_machine_processor_feature(T_PF_SSE_DAZ_MODE_AVAILABLE), TRUE);
}

/* ---- start-up and RT_CPUS -------------------------------------------------------------------- */

TEST(core, machine_init_twice_stops)
{
    T_TRAPS(rt_machine_init(), "rt_machine_init called twice");
}

TEST(core, parse_cpus_accepts_1_to_64_only)
{
    /* RT_CPUS: decimal digits only, a value from 1 to 64.  0 means refused. */
    T_EQ(rt_machine_parse_cpus("1"), 1);
    T_EQ(rt_machine_parse_cpus("2"), 2);
    T_EQ(rt_machine_parse_cpus("63"), 63);
    T_EQ(rt_machine_parse_cpus("64"), 64);
    T_EQ(rt_machine_parse_cpus("6"), 6);
    T_EQ(rt_machine_parse_cpus("0064"), 64);                         /* leading zeros are digits */
    T_EQ(rt_machine_parse_cpus("00000000000000000000000000007"), 7);
    T_EQ(rt_machine_parse_cpus("0"), 0);
    T_EQ(rt_machine_parse_cpus("00"), 0);
    T_EQ(rt_machine_parse_cpus("65"), 0);
    T_EQ(rt_machine_parse_cpus("065"), 0);
    T_EQ(rt_machine_parse_cpus("100"), 0);
    T_EQ(rt_machine_parse_cpus("99999999999999999999999"), 0);        /* no overflow */
    T_EQ(rt_machine_parse_cpus(""), 0);
    T_EQ(rt_machine_parse_cpus(NULL), 0);
    T_EQ(rt_machine_parse_cpus("-1"), 0);
    T_EQ(rt_machine_parse_cpus("+5"), 0);
    T_EQ(rt_machine_parse_cpus(" 5"), 0);
    T_EQ(rt_machine_parse_cpus("5 "), 0);
    T_EQ(rt_machine_parse_cpus("5x"), 0);
    T_EQ(rt_machine_parse_cpus("x5"), 0);
    T_EQ(rt_machine_parse_cpus("0x5"), 0);
    T_EQ(rt_machine_parse_cpus("1.0"), 0);
    T_EQ(rt_machine_parse_cpus("1e1"), 0);
    T_EQ(rt_machine_parse_cpus("64 "), 0);
}

TEST(core, host_facts_and_the_count)
{
    /* The count is the host's performance cores (hw.perflevel0.logicalcpu), unless RT_CPUS
     * says otherwise; the memory is the host's, and half of it is available. */
    const RtMachine *mc = rt_machine();
    uint64_t memsize = 0;
    size_t len = sizeof memsize;
    T_REQUIRE(sysctlbyname("hw.memsize", &memsize, &len, NULL, 0) == 0);
    T_EQ(mc->phys_total, memsize);
    T_EQ(mc->phys_avail, memsize / 2);

    int32_t perf = 0;
    len = sizeof perf;
    if (sysctlbyname("hw.perflevel0.logicalcpu", &perf, &len, NULL, 0) == 0) {
        T_EQ(mc->host_perf_cores, (uint32_t)perf);
    }
    T_TRUE(mc->host_perf_cores >= 1 && mc->host_perf_cores <= mc->host_cores);
    T_EQ(mc->host_page_size, rt_vm_host_page());

    const char *env = getenv("RT_CPUS");
    if (env && *env) T_EQ(mc->cpu_count, rt_machine_parse_cpus(env));
    else             T_EQ(mc->cpu_count, mc->host_perf_cores);
}

TEST(core, windows7_description)
{
    /* The version and the locale as the guest is told them (8.4, 8.5). */
    const RtMachine *mc = rt_machine();
    T_EQ(mc->os_major, 6);
    T_EQ(mc->os_minor, 1);
    T_EQ(mc->os_build, 7601);
    T_EQ(mc->sp_major, 1);
    T_EQ(mc->sp_minor, 0);
    T_TRUE(!strcmp(mc->sp_name, "Service Pack 1"));
    T_EQ(mc->page_size, 4096);
    T_EQ(mc->granularity, 65536);
    T_EQ(mc->min_address, UINT64_C(0x10000));
    T_EQ(mc->max_address, UINT64_C(0x7FFFFFEFFFF));
    T_EQ(mc->cpu_family, 6);
    T_EQ(mc->cpu_hz, UINT64_C(3000000000));
    T_EQ(mc->lcid, 0x0409);
    T_EQ(mc->ansi_cp, 1252);
    T_EQ(mc->oem_cp, 437);
}

TEST(core, peb_agrees_with_the_machine)
{
    /* The process block carries the count and the version; they are the same values. */
    const RtMachine *mc = rt_machine();
    uint64_t peb = LD64(T_CTX()->gs_base + 0x60);
    T_EQ(LD32(peb + 0xB8), mc->cpu_count);
    T_EQ(LD32(peb + 0xB8), count());
    T_EQ(LD32(peb + 0x118), mc->os_major);
    T_EQ(LD32(peb + 0x11C), mc->os_minor);
    T_EQ(LD16(peb + 0x120), mc->os_build);
    T_EQ(LD16(peb + 0x122), (mc->sp_major << 8) | mc->sp_minor);

    SYSTEM_INFO si;
    rt_machine_system_info(&si);
    T_EQ(si.dwNumberOfProcessors, LD32(peb + 0xB8));
}

/* ---- concurrency ------------------------------------------------------------------------------ */

/* Several host threads read the description and the CPUID table at once.  Nothing here writes
 * anything, so every answer must equal the one the test thread got before the threads ran. */
#define READERS      4
#define ROUNDS       2000
#define FEATURES     46

struct reader {
    uint32_t feat[FEATURES];
    uint32_t leaf1[4], leaf8[4];
    uint32_t si_count;
    uint32_t bad, good;
};

static struct reader expect;
static struct reader results[READERS];

static void *reader_main(void *p) {
    struct reader *r = p;
    for (int round = 0; round < ROUNDS; round++) {
        uint32_t v[4];
        cpuid(1, 0, v);
        if (memcmp(v, expect.leaf1, sizeof v)) r->bad++;
        cpuid(0x80000008, 0, v);
        if (memcmp(v, expect.leaf8, sizeof v)) r->bad++;
        SYSTEM_INFO si;
        rt_machine_system_info(&si);
        if (si.dwNumberOfProcessors != expect.si_count) r->bad++;
        for (DWORD pf = 0; pf < FEATURES; pf++)
            if ((uint32_t)rt_machine_processor_feature(pf) != r->feat[pf]) r->bad++;
        r->good++;
    }
    return NULL;
}

TEST(core, concurrent_readers_see_one_machine)
{
    /* The expected answers, taken on this thread first. */
    cpuid(1, 0, expect.leaf1);
    cpuid(0x80000008, 0, expect.leaf8);
    SYSTEM_INFO si;
    rt_machine_system_info(&si);
    expect.si_count = si.dwNumberOfProcessors;
    for (DWORD pf = 0; pf < FEATURES; pf++) expect.feat[pf] = (uint32_t)rt_machine_processor_feature(pf);
    for (int i = 0; i < READERS; i++) memcpy(results[i].feat, expect.feat, sizeof expect.feat);

    pthread_t th[READERS];
    for (int i = 0; i < READERS; i++) {
        T_REQUIRE(pthread_create(&th[i], NULL, reader_main, &results[i]) == 0);
    }
    for (int i = 0; i < READERS; i++) pthread_join(th[i], NULL);

    uint32_t bad = 0, good = 0;
    for (int i = 0; i < READERS; i++) { bad += results[i].bad; good += results[i].good; }
    T_EQ(bad, 0);
    T_EQ(good, (uint32_t)(READERS * ROUNDS));
}
