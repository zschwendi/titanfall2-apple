/* The pretend machine (docs/RUNTIME.md section 8.5).  Interface: rt/machine.h.
 *
 * One file holds the description of the machine the guest believes it runs on.  Every answer
 * about the processor comes from the tables in this file: rt_cpuid returns them, the feature
 * numbers of rt_machine_processor_feature are read from the same bits, GetSystemInfo takes
 * the family and revision from the signature and the processor count from rt_machine.  So
 * the answers cannot disagree with one another, and the tests check that they do not.
 *
 * The processor, in one paragraph.  Vendor "GenuineIntel", family 6, model 13, stepping 8.
 * That signature belongs to a Pentium M ("Dothan"), a family 6 part of the feature level the
 * design asks for: "SSE2-era features only" (DESIGN.md section 7).  It has MMX, SSE, SSE2 and
 * SSE3, and it has no SSSE3 or later, no AVX, AES, SHA, BMI, POPCNT, CMPXCHG16B, RDRAND, XSAVE,
 * no SMT (leaf 1 EBX gives the count, HTT is clear) and no RDTSCP.  Long mode is on, because
 * the guest is 64-bit, and SSE3 is on (below); no Dothan had either, so the signature is a
 * stand-in and the feature and mode bits are the runtime's.  Finding 16 lists every leaf the
 * game asks for and the bits it tests; each of those has a deliberate answer below.
 *
 * SSE3 IS ON SINCE 2026-10-12, AND WHY.  Every 64-bit processor a game of 2016 ever met has
 * SSE3 (the first without it were the 2003 Athlon 64s), so "SSE2 without SSE3" in long mode
 * is a machine nobody tested on.  The game's sound decoder (binkawin64+0x359e) picks its
 * transform routines by this one bit: with SSE3 the table at .rdata+0x11b60, without it the
 * one at .rdata+0x11b10.  With the bit clear every decoded sound came out as four signals
 * interleaved in time (lag-1 correlation of the decoder's own output about 0, lag-4 0.5 to
 * 0.97, for mono, stereo and six-channel sources alike); with it set the output is smooth
 * (lag-1 0.95 to 1.00).  Whether the other table's routines are wrong in the original or in
 * our translation is not known (NOTES.md, 12 October).  The lifter translates the SSE3
 * instructions the game has (addsubps, haddps, movddup, movshdup, movsldup).
 *
 * Built: the description, rt_cpuid, rt_machine_system_info, rt_machine_processor_feature.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include "core.h"
#include "rt/machine.h"
#include "rt/vm.h"

static RtMachine machine;
static bool      ready;

static uint64_t host_number(const char *name) {
    uint64_t v = 0;
    size_t len = sizeof v;
    if (sysctlbyname(name, &v, &len, NULL, 0) != 0) return 0;
    return v;           /* little-endian host: a 4-byte answer lands in the low half */
}

/* RT_CPUS: a decimal number from 1 to 64 and nothing else.  Returns 0 for anything else, so
 * the caller can refuse it.  Not in rt/machine.h: the test for it calls it directly. */
uint32_t rt_machine_parse_cpus(const char *text) {
    uint32_t n = 0;
    if (!text || !*text) return 0;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
        n = n * 10 + (uint32_t)(*p - '0');
        if (n > 64) return 0;                   /* checked before each step, so no overflow */
    }
    return n;                                   /* 0 for "0" and for "000": not a processor count */
}

/* The processor's family and its model and stepping, the signature of leaf 1 (see the top). */
#define CPU_FAMILY       6u
#define CPU_MODEL        0xDu
#define CPU_STEPPING     8u

void rt_machine_init(void) {
    if (ready) rt_fatal("rt_machine_init called twice");
    RtMachine *m = &machine;

    m->os_major = 6; m->os_minor = 1; m->os_build = 7601;       /* Windows 7 SP1 */
    m->sp_major = 1; m->sp_minor = 0; m->sp_name = "Service Pack 1";

    m->host_page_size = (uint64_t)sysconf(_SC_PAGESIZE);
    m->host_cores = (uint32_t)host_number("hw.physicalcpu");
    /* The performance cores, the lever on the engine's threading (section 8.5).  On Apple
     * silicon hw.perflevel0 is the performance class.  A host with no core classes has no
     * such sysctl, and then every logical processor counts. */
    m->host_perf_cores = (uint32_t)host_number("hw.perflevel0.logicalcpu");
    if (m->host_perf_cores == 0) m->host_perf_cores = (uint32_t)host_number("hw.logicalcpu");
    if (m->host_cores == 0 || m->host_perf_cores == 0) rt_fatal("cannot read the host's processor count");

    /* The processor count is reported everywhere the guest can read it: SYSTEM_INFO, the
     * process block, leaf 1 EBX and leaf 0x80000008 ECX.  RT_CPUS overrides it; two or fewer
     * means no worker threads at all (the tier0 rule of finding 29). */
    m->cpu_count = m->host_perf_cores;
    const char *env = getenv("RT_CPUS");
    if (env && *env) {
        uint32_t n = rt_machine_parse_cpus(env);
        if (!n) rt_fatal("RT_CPUS must be a number from 1 to 64, not '%s'", env);
        m->cpu_count = n;
    }
    m->cpu_family = CPU_FAMILY;
    m->cpu_hz = UINT64_C(3000000000);

    m->page_size = RT_GUEST_PAGE;
    m->granularity = RT_GUEST_GRANULARITY;
    m->min_address = UINT64_C(0x10000);
    m->max_address = UINT64_C(0x7FFFFFEFFFF);
    m->phys_total = host_number("hw.memsize");
    if (m->phys_total == 0) rt_fatal("cannot read the host's memory size");
    m->phys_avail = m->phys_total / 2;

    m->lcid = 0x0409; m->ansi_cp = 1252; m->oem_cp = 437;
    ready = true;
}

const RtMachine *rt_machine(void) {
    if (!ready) rt_fatal("rt_machine before rt_machine_init");
    return &machine;
}

/* ---- the processor: one table of bits, read by rt_cpuid and by the feature numbers ----------- */

/* Leaf 0: the highest basic leaf the runtime describes.  Leaves above it are a loud stop, not
 * zeros: nothing the game queries lies beyond it (finding 16), and a program that asks for
 * topology (0xB) or the hypervisor range should say so in the log. */
#define CPU_MAX_LEAF    7u
#define CPU_MAX_XLEAF  0x80000008u

/* Vendor "GenuineIntel".  The game queries the Intel cache leaves (2 and 4), and the
 * reference runs under Proton were on an Intel machine.  The vendor decides which branch of
 * amd_ags_x64 runs (finding 16, section 1); the Intel branch is the one chosen, not verified. */
#define VENDOR_EBX  0x756E6547u     /* "Genu" */
#define VENDOR_EDX  0x49656E69u     /* "ineI" */
#define VENDOR_ECX  0x6C65746Eu     /* "ntel" */

/* Leaf 1 EAX: family 6, model 13, stepping 8.  Leaf 1 EBX bits 15:8 give the CLFLUSH line size
 * in 8-byte units: 8 means 64-byte lines. */
#define CPU_SIGNATURE   ((CPU_FAMILY << 8) | (CPU_MODEL << 4) | CPU_STEPPING)      /* 0x6D8 */
#define CPU_REVISION    ((CPU_MODEL << 8) | CPU_STEPPING)                          /* 0x0D08 */
#define CPU_CLFLUSH_LINE_UNITS  8u

/* Leaf 1 EDX, bit by bit.  1 where a Dothan-class SSE2 processor has the feature and the
 * runtime has or does not need to refuse it; 0 for what the runtime does not provide.
 *   The SSE2 baseline the finding requires: MMX (23), SSE (25), SSE2 (26), and FXSR (24),
 *   which every SSE part has.  TSC (4) because rt_rdtsc is implemented.  CX8 (8) because
 *   CMPXCHG8B is the baseline of every x86-64 processor, though the lifter's cmpxchg path
 *   has not been checked against its 8-byte form.  CLFSH (19) is set because EBX gives the
 *   line size; the lifter does not translate CLFLUSH (none in finding 16's list).
 *   HTT (28) is 0: no SMT, so no software that honours the flag divides the count by threads
 *   per core.  VME (1), SEP (11), PSN (18), DS (21), ACPI (22), SS (27), TM (29), IA64 (30)
 *   and PBE (31) are 0 because the runtime provides none of them. */
#define EDX_FPU    (1u << 0)
#define EDX_DE     (1u << 2)
#define EDX_PSE    (1u << 3)
#define EDX_TSC    (1u << 4)
#define EDX_MSR    (1u << 5)
#define EDX_PAE    (1u << 6)
#define EDX_MCE    (1u << 7)
#define EDX_CX8    (1u << 8)
#define EDX_APIC   (1u << 9)
#define EDX_MTRR   (1u << 12)
#define EDX_PGE    (1u << 13)
#define EDX_MCA    (1u << 14)
#define EDX_CMOV   (1u << 15)
#define EDX_PAT    (1u << 16)
#define EDX_PSE36  (1u << 17)
#define EDX_CLFSH  (1u << 19)
#define EDX_MMX    (1u << 23)
#define EDX_FXSR   (1u << 24)
#define EDX_SSE    (1u << 25)
#define EDX_SSE2   (1u << 26)
#define LEAF1_EDX  (EDX_FPU | EDX_DE | EDX_PSE | EDX_TSC | EDX_MSR | EDX_PAE | EDX_MCE | EDX_CX8 | \
                    EDX_APIC | EDX_MTRR | EDX_PGE | EDX_MCA | EDX_CMOV | EDX_PAT | EDX_PSE36 |     \
                    EDX_CLFSH | EDX_MMX | EDX_FXSR | EDX_SSE | EDX_SSE2)

/* Leaf 1 ECX: SSE3 (0) and nothing else (the paragraph at the top says why SSE3).  SSSE3 (9),
 * SSE4.1 (19), SSE4.2 (20), PCLMULQDQ (1), FMA (12),
 * CMPXCHG16B (13), PCID (17), MOVBE (22), POPCNT (23), AESNI (25), XSAVE (26), OSXSAVE (27),
 * AVX (28), F16C (29), RDRAND (30) and the hypervisor bit (31) are all absent: finding 16
 * says the game tests several of them and that none should be set.  The rest of the word
 * (MONITOR, VMX, EIST, x2APIC, TSC-deadline ...) is absent for the same reason as VME above.
 * Named here only where a feature number below reads them. */
#define ECX_SSE3      (1u << 0)
#define ECX_SSSE3     (1u << 9)
#define ECX_CX16      (1u << 13)
#define ECX_SSE41     (1u << 19)
#define ECX_SSE42     (1u << 20)
#define ECX_VMX       (1u << 5)
#define ECX_RDRAND    (1u << 30)
#define ECX_XSAVE     (1u << 26)
#define ECX_AVX       (1u << 28)
#define LEAF1_ECX     ECX_SSE3

/* Leaf 7, subleaf 0: EAX (the highest subleaf) is 0 and the rest is all 0.  Absent: FSGSBASE
 * (0), BMI1 (3), AVX2 (5), BMI2 (8), ERMS (9), AVX512F (16), RDSEED (18), SHA (29).  The
 * game tests AVX2 and ERMS; both are absent.  Leaf 7 ECX and EDX are not tested by the game. */
#define EBX7_FSGSBASE  (1u << 0)
#define EBX7_AVX2      (1u << 5)
#define EBX7_ERMS      (1u << 9)
#define EBX7_AVX512F   (1u << 16)
#define EBX7_RDSEED    (1u << 18)
#define LEAF7_EBX      0u

/* Extended leaf 0x80000001.  ECX is all 0: LAHF64 (0), LZCNT (5), SSE4A (6), PREFETCHW (8),
 * XOP (11), SKINIT (12), FMA4 (16), TBM (21), MONITORX (29); finding 16 says LZCNT, XOP, FMA4
 * and PREFETCHW are 0.  EDX: LM (29) is set because the guest is 64-bit.  MMX (23) repeats leaf 1.
 * NX (20) is 0: the runtime does not enforce no-execute (VirtualAlloc's execute protections
 * are recorded and treated as read, RUNTIME.md 5.2), so the machine does not claim it.
 * SYSCALL (11) is 0: the lifter skips functions that contain syscall (DESIGN.md 7) and user
 * code on Windows does not execute it.  RDTSCP (27) is 0: the lifter does not translate it
 * and the game has none.  1GB pages (26), MMXEXT (22), FXSR_OPT (25), 3DNow (30, 31) are 0. */
#define X1_ECX_MONITORX  (1u << 29)
#define X1_EDX_NX        (1u << 20)
#define X1_EDX_RDTSCP    (1u << 27)
#define X1_EDX_MMX       (1u << 23)
#define X1_EDX_LM        (1u << 29)
#define LEAF_X1_ECX      0u
#define LEAF_X1_EDX     (X1_EDX_MMX | X1_EDX_LM)

/* Extended leaf 0x80000007 EDX bit 8: invariant TSC.  rt_rdtsc is the monotonic clock scaled
 * to a constant 3.0 GHz (RUNTIME 8.1), so its rate does not change and the bit is true. */
#define X7_EDX_INVARIANT_TSC  (1u << 8)

/* Leaf 1 EBX bits 23:16 and extended leaf 0x80000008 ECX bits 7:0 both carry the processor
 * count.  Leaf 1 EBX documents that field only when HTT is set, and HTT is clear; it is
 * reported anyway so that the count is the same in every place.  0x80000008 ECX is the AMD
 * field "number of cores minus one", with ApicIdSize in bits 15:12; Intel reserves it. */
static uint32_t apic_id_bits(uint32_t n) {
    uint32_t b = 0;
    while ((UINT64_C(1) << b) < n) b++;             /* ceil(log2(n)) */
    return b;
}

/* The answer of cpuid(leaf, subleaf): eax, ebx, ecx, edx in out[0..3].  False for a leaf the
 * runtime does not describe.  Leaves that define no subleaves ignore the subleaf. */
static bool cpuid_answer(uint32_t leaf, uint32_t sub, uint32_t out[4]) {
    uint32_t a = 0, b = 0, c = 0, d = 0;
    (void)sub;
    switch (leaf) {
    case 0x0:
        a = CPU_MAX_LEAF; b = VENDOR_EBX; c = VENDOR_ECX; d = VENDOR_EDX;
        break;
    case 0x1: {
        uint32_t count = rt_machine()->cpu_count;
        a = CPU_SIGNATURE;
        b = (CPU_CLFLUSH_LINE_UNITS << 8) | (count << 16);          /* brand index 0, APIC ID 0 */
        c = LEAF1_ECX;
        d = LEAF1_EDX;
        break;
    }
    case 0x2:
        /* One call, and every descriptor byte is 0 (null).  Intel's "use leaf 4" byte is not
         * sent, so a reader that wants the caches finds none. */
        a = 0x01;
        break;
    case 0x3:
        /* Processor serial number: defined only when leaf 1 EDX.PSN is set, and it is not. */
        break;
    case 0x4:
        /* No caches described, at any subleaf: EAX type 0 ends every enumeration.  Nothing the
         * game queries depends on this (finding 16: "stored, not tested"). */
        break;
    case 0x5:
        /* MONITOR/MWAIT: not present (leaf 1 ECX bit 3 is 0), so no sizes. */
        break;
    case 0x6:
        /* Thermal and power management: none. */
        break;
    case 0x7:
        /* Subleaf 0 defines the feature word; the highest subleaf is 0 (EAX = 0). */
        break;
    case 0x80000000:
        a = CPU_MAX_XLEAF;
        break;
    case 0x80000001:
        c = LEAF_X1_ECX; d = LEAF_X1_EDX;
        break;
    case 0x80000002:
    case 0x80000003:
    case 0x80000004:
        /* No brand string.  The game never reads one, and a program that prints the
         * processor name gets an empty line.  A real part always has one: this is a deliberate
         * gap, so that the enumeration loop does not stop, and it is not known what Windows
         * would show for a part without one. */
        break;
    case 0x80000005:
    case 0x80000006:
        /* AMD's L1 and L2 cache leaves, and Intel's L2 field of 0x80000006: no caches, as leaf 4. */
        break;
    case 0x80000007:
        d = X7_EDX_INVARIANT_TSC;
        break;
    case 0x80000008: {
        uint32_t count = rt_machine()->cpu_count;
        a = (48u << 8) | 36u;                                       /* linear 48 bits, physical 36 */
        c = (apic_id_bits(count) << 12) | (count - 1);
        break;
    }
    default:
        return false;
    }
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
    return true;
}

/* One bit of those answers, for a report: 1 or 0, or -1 when there is no answer (the leaf is
 * not described, the description is not initialised yet, or reg or bit is out of range).
 * reg: 0 eax, 1 ebx, 2 ecx, 3 edx.  The stop at an instruction the lifter does not translate
 * asks this to say whether the processor described here would have executed the instruction
 * (rt_trap_untranslated, runtime/core/trap.c). */
int rt_machine_cpuid_bit(uint32_t leaf, uint32_t sub, unsigned reg, unsigned bit) {
    uint32_t out[4];
    if (!ready || reg > 3 || bit > 31 || !cpuid_answer(leaf, sub, out)) return -1;
    return (int)((out[reg] >> bit) & 1);
}

/* The lifted `cpuid` (harness/runtime.h).  Only the registers the instruction writes change:
 * rax, rbx, rcx and rdx, each with its upper half zeroed as a 32-bit write does.  The leaf is
 * eax and the subleaf is ecx, both read as 32 bits. */
void rt_cpuid(Ctx *ctx) {
    uint32_t leaf = (uint32_t)ctx->r[0];
    uint32_t sub = (uint32_t)ctx->r[1];
    uint32_t out[4];
    if (!cpuid_answer(leaf, sub, out))
        RT_UNIMPLEMENTED(ctx, "CPUID leaf 0x%x, subleaf 0x%x: the runtime does not describe this leaf (RUNTIME.md 8.5)",
                         leaf, sub);
    ctx->r[0] = out[0];
    ctx->r[3] = out[1];
    ctx->r[1] = out[2];
    ctx->r[2] = out[3];
}

/* ---- GetSystemInfo ------------------------------------------------------------------------- */

void rt_machine_system_info(SYSTEM_INFO *out) {
    /* GetSystemInfo has no failure return.  A NULL buffer is a guest bug that Windows would
     * fault on; here it stops the run with a report. */
    if (!out) rt_fatal("GetSystemInfo: the buffer is NULL");
    const RtMachine *m = rt_machine();

    memset(out, 0, sizeof *out);
    out->wProcessorArchitecture = PROCESSOR_ARCHITECTURE_AMD64;     /* wReserved stays 0 */
    out->dwPageSize = m->page_size;
    out->lpMinimumApplicationAddress = (LPVOID)(uintptr_t)m->min_address;
    out->lpMaximumApplicationAddress = (LPVOID)(uintptr_t)m->max_address;
    /* One bit per processor, from bit 0 up: 1 << 64 would be undefined, so 64 is a special case. */
    out->dwActiveProcessorMask = m->cpu_count >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << m->cpu_count) - 1;
    out->dwNumberOfProcessors = m->cpu_count;
    /* dwProcessorType is 8664 (PROCESSOR_AMD_X8664) for every 64-bit Windows, whatever the
     * vendor: that is what the x64 documentation says, and it is what we report. */
    out->dwProcessorType = PROCESSOR_AMD_X8664;
    out->dwAllocationGranularity = m->granularity;
    /* wProcessorLevel is the family; wProcessorRevision is model << 8 | stepping, the values
     * leaf 1 EAX carries.  Both come from the same constants as leaf 1. */
    out->wProcessorLevel = (WORD)m->cpu_family;
    out->wProcessorRevision = (WORD)CPU_REVISION;
}

/* ---- IsProcessorFeaturePresent ------------------------------------------------------------- */

/* The PF_* numbers of winnt.h.  Numbers 0 to 23 are the long-standing ones.  The rest are
 * from the current SDK as remembered, not checked against a header (none is on this machine):
 * a reviewer should check 24 to 45 against winnt.h.  Guarded so that a header that later
 * defines them with the same values does not clash. */
#ifndef PF_FLOATING_POINT_PRECISION_ERRATA
#define PF_FLOATING_POINT_PRECISION_ERRATA       0
#define PF_FLOATING_POINT_EMULATED               1
#define PF_COMPARE_EXCHANGE_DOUBLE               2
#define PF_MMX_INSTRUCTIONS_AVAILABLE            3
#define PF_PPC_MOVEMEM_64BIT_OK                  4
#define PF_ALPHA_BYTE_INSTRUCTIONS               5
#define PF_XMMI_INSTRUCTIONS_AVAILABLE           6
#define PF_3DNOW_INSTRUCTIONS_AVAILABLE          7
#define PF_RDTSC_INSTRUCTION_AVAILABLE           8
#define PF_PAE_ENABLED                           9
#define PF_XMMI64_INSTRUCTIONS_AVAILABLE        10
#define PF_SSE_DAZ_MODE_AVAILABLE               11
#define PF_NX_ENABLED                           12
#define PF_SSE3_INSTRUCTIONS_AVAILABLE          13
#define PF_COMPARE_EXCHANGE128                  14
#define PF_COMPARE64_EXCHANGE128                15
#define PF_CHANNELS_ENABLED                     16
#define PF_XSAVE_ENABLED                        17
#define PF_ARM_VFP_32_REGISTERS_AVAILABLE       18
#define PF_ARM_NEON_INSTRUCTIONS_AVAILABLE      19
#define PF_SECOND_LEVEL_ADDRESS_TRANSLATION     20
#define PF_VIRT_FIRMWARE_ENABLED                21
#define PF_RDWRFSGSBASE_AVAILABLE               22
#define PF_FASTFAIL_AVAILABLE                   23
#define PF_ARM_DIVIDE_INSTRUCTION_AVAILABLE     24
#define PF_ARM_64BIT_LOADSTORE_ATOMIC           25
#define PF_ARM_EXTERNAL_CACHE_AVAILABLE         26
#define PF_ARM_FMAC_INSTRUCTIONS_AVAILABLE      27
#define PF_RDRAND_INSTRUCTION_AVAILABLE         28
#define PF_ARM_V8_INSTRUCTIONS_AVAILABLE        29
#define PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE 30
#define PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE  31
#define PF_RDTSCP_INSTRUCTION_AVAILABLE         32
#define PF_RDPID_INSTRUCTION_AVAILABLE          33
#define PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE 34
#define PF_MONITORX_INSTRUCTION_AVAILABLE       35
#define PF_SSSE3_INSTRUCTIONS_AVAILABLE         36
#define PF_SSE4_1_INSTRUCTIONS_AVAILABLE        37
#define PF_SSE4_2_INSTRUCTIONS_AVAILABLE        38
#define PF_AVX_INSTRUCTIONS_AVAILABLE           39
#define PF_AVX2_INSTRUCTIONS_AVAILABLE          40
#define PF_AVX512F_INSTRUCTIONS_AVAILABLE       41
#define PF_ERMS_AVAILABLE                       42
#define PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE    43
#define PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE 44
#define PF_ARM_V83_LRCPC_INSTRUCTIONS_AVAILABLE 45
#endif

static BOOL on(bool b) { return b ? TRUE : FALSE; }

BOOL rt_machine_processor_feature(DWORD feature) {
    switch (feature) {
    case PF_FLOATING_POINT_PRECISION_ERRATA:    return FALSE;   /* no FDIV erratum in this part */
    case PF_FLOATING_POINT_EMULATED:            return FALSE;   /* x87 is in hardware */
    case PF_COMPARE_EXCHANGE_DOUBLE:            return on(LEAF1_EDX & EDX_CX8);
    case PF_MMX_INSTRUCTIONS_AVAILABLE:         return on(LEAF1_EDX & EDX_MMX);
    case PF_PPC_MOVEMEM_64BIT_OK:               return FALSE;   /* PowerPC */
    case PF_ALPHA_BYTE_INSTRUCTIONS:            return FALSE;   /* Alpha */
    case PF_XMMI_INSTRUCTIONS_AVAILABLE:        return on(LEAF1_EDX & EDX_SSE);
    case PF_3DNOW_INSTRUCTIONS_AVAILABLE:       return FALSE;   /* 3DNow: 0x80000001 EDX bit 31 is 0 */
    case PF_RDTSC_INSTRUCTION_AVAILABLE:        return on(LEAF1_EDX & EDX_TSC);
    /* Windows documents PF_PAE_ENABLED as the PAE state of the processor.  x64 Windows always
     * pages with PAE, so TRUE follows the leaf 1 PAE bit; not checked against a Windows 7 x64 run. */
    case PF_PAE_ENABLED:                        return on(LEAF1_EDX & EDX_PAE);
    case PF_XMMI64_INSTRUCTIONS_AVAILABLE:      return on(LEAF1_EDX & EDX_SSE2);
    /* DAZ is not a CPUID bit: the float mode maps MXCSR.DAZ to FPCR.FIZ and MXCSR.FTZ to
     * FPCR.FZ (harness/fpmode.h), so the mode is there whenever the runtime runs. */
    case PF_SSE_DAZ_MODE_AVAILABLE:             return TRUE;
    /* A real NX part on Windows 7 x64 reports TRUE here.  The runtime does not enforce no-execute,
     * so the machine answers FALSE, which is the one deliberate departure in this table. */
    case PF_NX_ENABLED:                         return on(LEAF_X1_EDX & X1_EDX_NX);
    case PF_SSE3_INSTRUCTIONS_AVAILABLE:        return on(LEAF1_ECX & ECX_SSE3);
    case PF_COMPARE_EXCHANGE128:                return on(LEAF1_ECX & ECX_CX16);
    case PF_COMPARE64_EXCHANGE128:              return on(LEAF1_ECX & ECX_CX16);
    /* Not a CPUID feature, and not known to me what Windows 7 x64 answers for it.  FALSE is a
     * guess, not verified: a reviewer with the Windows documentation should check it. */
    case PF_CHANNELS_ENABLED:                   return FALSE;
    case PF_XSAVE_ENABLED:                      return on(LEAF1_ECX & ECX_XSAVE);
    case PF_ARM_VFP_32_REGISTERS_AVAILABLE:     return FALSE;
    case PF_ARM_NEON_INSTRUCTIONS_AVAILABLE:    return FALSE;
    case PF_SECOND_LEVEL_ADDRESS_TRANSLATION:   return FALSE;   /* no hypervisor */
    case PF_VIRT_FIRMWARE_ENABLED:              return on(LEAF1_ECX & ECX_VMX);
    case PF_RDWRFSGSBASE_AVAILABLE:             return on(LEAF7_EBX & EBX7_FSGSBASE);
    case PF_FASTFAIL_AVAILABLE:                 return FALSE;   /* int 0x29 is not a fast-fail here (RUNTIME 8.5) */
    case PF_ARM_DIVIDE_INSTRUCTION_AVAILABLE:   return FALSE;
    case PF_ARM_64BIT_LOADSTORE_ATOMIC:         return FALSE;
    case PF_ARM_EXTERNAL_CACHE_AVAILABLE:       return FALSE;
    case PF_ARM_FMAC_INSTRUCTIONS_AVAILABLE:    return FALSE;
    case PF_RDRAND_INSTRUCTION_AVAILABLE:       return on(LEAF1_ECX & ECX_RDRAND);
    case PF_ARM_V8_INSTRUCTIONS_AVAILABLE:      return FALSE;
    case PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE: return FALSE;
    case PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE:  return FALSE;
    case PF_RDTSCP_INSTRUCTION_AVAILABLE:       return on(LEAF_X1_EDX & X1_EDX_RDTSCP);
    case PF_RDPID_INSTRUCTION_AVAILABLE:        return FALSE;   /* leaf 7 ECX is 0 */
    case PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE: return FALSE;
    case PF_MONITORX_INSTRUCTION_AVAILABLE:     return on(LEAF_X1_ECX & X1_ECX_MONITORX);
    case PF_SSSE3_INSTRUCTIONS_AVAILABLE:       return on(LEAF1_ECX & ECX_SSSE3);
    case PF_SSE4_1_INSTRUCTIONS_AVAILABLE:      return on(LEAF1_ECX & ECX_SSE41);
    case PF_SSE4_2_INSTRUCTIONS_AVAILABLE:      return on(LEAF1_ECX & ECX_SSE42);
    case PF_AVX_INSTRUCTIONS_AVAILABLE:         return on(LEAF1_ECX & ECX_AVX);
    case PF_AVX2_INSTRUCTIONS_AVAILABLE:        return on(LEAF7_EBX & EBX7_AVX2);
    case PF_AVX512F_INSTRUCTIONS_AVAILABLE:     return on(LEAF7_EBX & EBX7_AVX512F);
    case PF_ERMS_AVAILABLE:                     return on(LEAF7_EBX & EBX7_ERMS);
    case PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE:  return FALSE;
    case PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE: return FALSE;
    case PF_ARM_V83_LRCPC_INSTRUCTIONS_AVAILABLE: return FALSE;
    default: {
        /* The header's contract: an unknown number is FALSE, as Windows answers for a feature
         * it does not know.  Logged once, so that a feature the game asks for and the runtime
         * does not know shows up in the first run and is not taken for an answer. */
        char key[32];
        snprintf(key, sizeof key, "PF %u", (unsigned)feature);
        rt_log_once(RT_CH_IMPORT, RT_LL_WARN, key,
                    "IsProcessorFeaturePresent(%u): not a feature the runtime reports; answered FALSE",
                    (unsigned)feature);
        return FALSE;
    }
    }
}

/* ---- random bytes ----------------------------------------------------------------------------- */

void rt_random_bytes(void *buf, SIZE_T n) {
    if (n == 0) return;
    if (!buf) rt_fatal("rt_random_bytes: NULL buffer for %llu bytes", (unsigned long long)n);
    arc4random_buf(buf, (size_t)n);
}
