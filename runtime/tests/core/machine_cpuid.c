/* The CPUID answers of the pretend machine (docs/RUNTIME.md 8.5, docs/DESIGN.md 7, finding 16):
 * every leaf the game queries, every leaf the enumeration loops reach, each bit the game
 * tests, the registers an instruction leaves alone, and the loud stop for a leaf that is not
 * described.  The expected values are written out here by hand, from the design and finding
 * 16, not computed from the table in runtime/core/machine.c, so a change to either shows. */
#include <string.h>

#include "rt/test.h"
#include "rt/machine.h"
#include "rt/vm.h"

/* The processor count is the host's, or RT_CPUS: each test reads it from the machine. */
static uint32_t count(void) { return rt_machine()->cpu_count; }

/* A lifted `cpuid` on a context of its own: the leaf in rax, the subleaf in rcx, both with
 * garbage in the upper halves, which CPUID ignores.  Returns rax, rbx, rcx, rdx whole. */
static void cpuid_full(uint32_t leaf, uint32_t sub, uint64_t r[4]) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.r[0] = (UINT64_C(0xCAFEBABE) << 32) | leaf;
    c.r[1] = (UINT64_C(0x5A5A5A5A) << 32) | sub;
    c.r[3] = UINT64_C(0xB0B0B0B0B0B0B0B0);      /* rbx and rdx are written whole too: CPUID clears */
    c.r[2] = UINT64_C(0xD0D0D0D0D0D0D0D0);      /* the upper half of all four registers */
    rt_cpuid(&c);
    r[0] = c.r[0];
    r[1] = c.r[3];
    r[2] = c.r[1];
    r[3] = c.r[2];
}

/* The 32-bit answers: eax, ebx, ecx, edx. */
static void cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4]) {
    uint64_t r[4];
    cpuid_full(leaf, sub, r);
    for (int i = 0; i < 4; i++)
        if (r[i] >> 32) rt_test_fail(__FILE__, __LINE__, "cpuid leaf %#x: the upper half of register %d is not zero", leaf, i);
    for (int i = 0; i < 4; i++) out[i] = (uint32_t)r[i];
}

/* A call on the test's own context, for the loud-stop cases. */
static void cpuid_at(uint32_t leaf, uint32_t sub) {
    Ctx *c = T_CTX();
    c->r[0] = leaf;
    c->r[1] = sub;
    rt_cpuid(c);
}

TEST(core, cpuid_vendor_and_max_leaf)
{
    /* Leaf 0: the highest basic leaf is 7; the vendor is "GenuineIntel", in ebx, edx, ecx. */
    uint32_t v[4];
    cpuid(0, 0, v);
    T_EQ(v[0], 7);
    char vendor[13];
    memcpy(vendor + 0, &v[1], 4);
    memcpy(vendor + 4, &v[3], 4);
    memcpy(vendor + 8, &v[2], 4);
    vendor[12] = 0;
    T_TRUE(!strcmp(vendor, "GenuineIntel"));
}

TEST(core, cpuid_signature_and_topology)
{
    /* Leaf 1 eax: family 6, model 13, stepping 8, no extended fields. */
    uint32_t v[4];
    cpuid(1, 0, v);
    T_EQ(v[0], 0x000006D8);
    T_EQ((v[0] >> 8) & 0xF, 6);
    T_EQ((v[0] >> 4) & 0xF, 0xD);
    T_EQ(v[0] & 0xF, 8);
    T_EQ((v[0] >> 16) & 0xF, 0);            /* extended model */
    T_EQ((v[0] >> 20) & 0xFF, 0);           /* extended family */

    /* Leaf 1 ebx: brand index 0, CLFLUSH line 8 units (64 bytes), the processor count, APIC ID 0. */
    T_EQ(v[1] & 0xFF, 0);
    T_EQ((v[1] >> 8) & 0xFF, 8);
    T_EQ((v[1] >> 16) & 0xFF, count());
    T_EQ(v[1] >> 24, 0);

    /* No SMT: HTT is clear (the count above is reported in every place, not because HTT says so). */
    T_EQ((v[3] >> 28) & 1, 0);
}

TEST(core, cpuid_leaf1_feature_bits)
{
    uint32_t v[4];
    cpuid(1, 0, v);

    /* ecx: SSE3 (bit 0) and nothing else.  SSSE3 and later, AES, PCLMULQDQ, FMA, CMPXCHG16B,
     * MOVBE, POPCNT, RDRAND, XSAVE, OSXSAVE, AVX, F16C, the hypervisor bit: all absent
     * (finding 16).  SSE3 is on because the game's sound decoder picks its transform routines
     * by it and the other set gives wrong sound (runtime/core/machine.c, the top). */
    T_EQ(v[2], 1u);

    /* edx: exactly the SSE2 baseline and the architectural set the table names. */
    T_EQ(v[3], 0x078BF3FD);
    static const struct { uint32_t bit; const char *name; } on[] = {
        { 0, "FPU" }, { 2, "DE" }, { 3, "PSE" }, { 4, "TSC" }, { 5, "MSR" }, { 6, "PAE" },
        { 7, "MCE" }, { 8, "CX8" }, { 9, "APIC" }, { 12, "MTRR" }, { 13, "PGE" }, { 14, "MCA" },
        { 15, "CMOV" }, { 16, "PAT" }, { 17, "PSE36" }, { 19, "CLFSH" }, { 23, "MMX" },
        { 24, "FXSR" }, { 25, "SSE" }, { 26, "SSE2" },
    };
    for (unsigned i = 0; i < sizeof on / sizeof on[0]; i++)
        T_REQUIRE(((v[3] >> on[i].bit) & 1) == 1);             /* see the names in the table */
    /* Off: VME, SEP, PSN, DS, ACPI, HTT, SS, TM, IA64, PBE, and the reserved bits. */
    static const uint32_t off[] = { 1, 10, 11, 18, 20, 21, 22, 27, 28, 29, 30, 31 };
    for (unsigned i = 0; i < sizeof off / sizeof off[0]; i++)
        T_REQUIRE(((v[3] >> off[i]) & 1) == 0);
}

TEST(core, cpuid_leaves_2_4_7)
{
    /* Leaf 2: one call, every descriptor byte null. */
    uint32_t v[4];
    cpuid(2, 0, v);
    T_EQ(v[0], 1);
    T_EQ(v[1], 0);
    T_EQ(v[2], 0);
    T_EQ(v[3], 0);

    /* Leaf 4: no caches, at every subleaf (type 0 ends an enumeration). */
    for (uint32_t sub = 0; sub < 4; sub++) {
        cpuid(4, sub, v);
        T_EQ(v[0] | v[1] | v[2] | v[3], 0);
    }

    /* Leaf 7, subleaf 0: the highest subleaf is 0, and every feature is absent.  The game
     * tests AVX2 (ebx bit 5) and ERMS (ebx bit 9): both clear. */
    cpuid(7, 0, v);
    T_EQ(v[0], 0);
    T_EQ(v[1], 0);
    T_EQ(v[2], 0);
    T_EQ(v[3], 0);
    T_EQ((v[1] >> 5) & 1, 0);
    T_EQ((v[1] >> 9) & 1, 0);
    cpuid(7, 1, v);                          /* past the highest subleaf: zeros */
    T_EQ(v[0] | v[1] | v[2] | v[3], 0);
}

TEST(core, cpuid_leaves_3_5_6)
{
    /* Serial number, MONITOR/MWAIT, thermal and power: none present, all zero. */
    uint32_t v[4];
    cpuid(3, 0, v);
    T_EQ(v[0] | v[1] | v[2] | v[3], 0);
    cpuid(5, 0, v);
    T_EQ(v[0] | v[1] | v[2] | v[3], 0);
    cpuid(6, 0, v);
    T_EQ(v[0] | v[1] | v[2] | v[3], 0);
}

TEST(core, cpuid_extended_leaves)
{
    uint32_t v[4];

    /* 0x80000000: the highest extended leaf is 0x80000008. */
    cpuid(0x80000000, 0, v);
    T_EQ(v[0], 0x80000008);
    T_EQ(v[1] | v[2] | v[3], 0);

    /* 0x80000001: long mode and MMX, and nothing else: no NX, SYSCALL, RDTSCP, 1 GB pages,
     * LZCNT, PREFETCHW, XOP, FMA4, 3DNow. */
    cpuid(0x80000001, 0, v);
    T_EQ(v[0], 0);
    T_EQ(v[1], 0);
    T_EQ(v[2], 0);
    T_EQ(v[3], 0x20800000);
    T_EQ((v[3] >> 29) & 1, 1);               /* LM */
    T_EQ((v[3] >> 23) & 1, 1);               /* MMX */
    T_EQ((v[3] >> 20) & 1, 0);               /* NX */
    T_EQ((v[3] >> 27) & 1, 0);               /* RDTSCP */
    T_EQ((v[3] >> 11) & 1, 0);               /* SYSCALL */
    T_EQ((v[3] >> 26) & 1, 0);               /* 1 GB pages */

    /* 0x80000002 to 0x80000004: no brand string.  0x80000005 and 0x80000006: no caches. */
    for (uint32_t leaf = 0x80000002; leaf <= 0x80000006; leaf++) {
        cpuid(leaf, 0, v);
        T_EQ(v[0] | v[1] | v[2] | v[3], 0);
    }

    /* 0x80000007: invariant TSC (edx bit 8) and nothing else. */
    cpuid(0x80000007, 0, v);
    T_EQ(v[0] | v[1] | v[2], 0);
    T_EQ(v[3], 0x100);

    /* 0x80000008: 36 physical and 48 linear address bits; ecx carries the processor count
     * as core count minus one, and the APIC ID width needed for it. */
    cpuid(0x80000008, 0, v);
    T_EQ(v[0], 0x3024);
    T_EQ(v[1], 0);
    uint32_t n = count(), bits = 0;
    while ((UINT32_C(1) << bits) < n) bits++;
    T_EQ(v[2] & 0xFF, n - 1);
    T_EQ((v[2] >> 12) & 0xF, bits);
    T_EQ(v[2] & 0xFFFF0F00u, 0);             /* nothing else in ecx */
    T_EQ(v[3], 0);
}

TEST(core, cpuid_leaves_that_ignore_the_subleaf)
{
    /* For these, ecx is not an input: the same answer whatever it holds. */
    static const uint32_t leaves[] = { 0, 1, 2, 3, 5, 6, 0x80000000, 0x80000001, 0x80000007, 0x80000008 };
    for (unsigned i = 0; i < sizeof leaves / sizeof leaves[0]; i++) {
        uint32_t a[4], b[4], c[4];
        cpuid(leaves[i], 0, a);
        cpuid(leaves[i], 0x7FFFFFFF, b);
        cpuid(leaves[i], 7, c);
        T_REQUIRE(!memcmp(a, b, sizeof a) && !memcmp(a, c, sizeof a));
    }
}

TEST(core, cpuid_outputs_are_32_bit)
{
    /* CPUID writes 32-bit registers: each result has a zero upper half, for every leaf. */
    static const uint32_t leaves[] = { 0, 1, 2, 3, 4, 5, 6, 7, 0x80000000, 0x80000001, 0x80000002,
                                       0x80000003, 0x80000004, 0x80000005, 0x80000006, 0x80000007,
                                       0x80000008 };
    for (unsigned i = 0; i < sizeof leaves / sizeof leaves[0]; i++) {
        uint64_t r[4];
        cpuid_full(leaves[i], 0, r);
        for (int k = 0; k < 4; k++) T_REQUIRE((r[k] >> 32) == 0);
    }
}

TEST(core, cpuid_writes_only_the_registers_it_defines)
{
    /* rsp, the other general registers and the segment base are left as they were; rax, rbx,
     * rcx and rdx are the answer. */
    Ctx c;
    memset(&c, 0, sizeof c);
    for (int i = 0; i < 16; i++) c.r[i] = UINT64_C(0x1000) + (uint64_t)i * 0x111;
    c.r[0] = 1;
    c.gs_base = UINT64_C(0x7000000000);
    uint64_t before[16];
    memcpy(before, c.r, sizeof before);
    rt_cpuid(&c);
    /* rbx, rcx and rdx are written (r[3], r[1], r[2]); r[0] is the answer.  rsp and above are not. */
    for (int i = 4; i < 16; i++) T_REQUIRE(c.r[i] == before[i]);
    T_EQ(c.r[0], 0x6D8);
    T_EQ(c.gs_base, UINT64_C(0x7000000000));
    T_EQ(c.host, NULL);
}

TEST(core, cpuid_every_leaf_the_game_or_the_loops_reach_is_answered)
{
    /* The game queries 0, 1, 2, 4, 7, 0x80000000, 0x80000001 and 0x80000008; the enumeration
     * loops reach 1..max and 0x80000001..max, with max taken from leaves 0 and 0x80000000.
     * Every one of them answers (a stop would end this test), and the loops visit exactly the
     * leaves they should: 7 basic and 8 extended. */
    uint32_t v[4];
    unsigned reached = 0;
    cpuid(0, 0, v);
    uint32_t max_basic = v[0];
    for (uint32_t leaf = 1; leaf <= max_basic; leaf++) { cpuid(leaf, 0, v); reached++; }
    cpuid(0x80000000, 0, v);
    uint32_t max_ext = v[0];
    for (uint32_t leaf = 0x80000001; leaf <= max_ext; leaf++) { cpuid(leaf, 0, v); reached++; }
    T_EQ(max_basic, 7);
    T_EQ(max_ext, 0x80000008);
    T_EQ(reached, 7u + 8u);
    cpuid(2, 0, v);
    cpuid(4, 0, v);
    cpuid(7, 0, v);
}

TEST(core, cpuid_leaves_not_described_stop_the_run)
{
    /* Leaves above the highest described one are a loud stop that names the leaf and subleaf.
     * Topology (0xB), extended states (0xD), the TSC and core crystal leaves (0x15, 0x16), the
     * hypervisor range, and the extended leaves past 0x80000008. */
    T_TRAPS(cpuid_at(0x8, 0), "FATAL unimplemented: CPUID leaf 0x8, subleaf 0x0");
    T_TRAPS(cpuid_at(0xB, 0), "FATAL unimplemented: CPUID leaf 0xb, subleaf 0x0");
    T_TRAPS(cpuid_at(0xB, 1), "CPUID leaf 0xb, subleaf 0x1");
    T_TRAPS(cpuid_at(0xD, 0), "CPUID leaf 0xd");
    T_TRAPS(cpuid_at(0x15, 0), "CPUID leaf 0x15");
    T_TRAPS(cpuid_at(0x16, 0), "CPUID leaf 0x16");
    T_TRAPS(cpuid_at(0x40000000, 0), "CPUID leaf 0x40000000");
    T_TRAPS(cpuid_at(0x80000009, 0), "CPUID leaf 0x80000009, subleaf 0x0");
    T_TRAPS(cpuid_at(0x80000010, 0), "CPUID leaf 0x80000010");
    T_TRAPS(cpuid_at(0xFFFFFFFFu, 0), "CPUID leaf 0xffffffff");
}

TEST(core, cpuid_reports_the_stop_with_the_caller)
{
    /* The stop prints where the lifted code asked, as every fatal report does. */
    T_TRAPS(cpuid_at(0x42, 0), "  at runtime/core/machine.c:");
}
