/* The pretend machine (docs/RUNTIME.md section 8, in particular 8.5).
 *
 * One file, runtime/core/machine.c, holds the description of the machine the guest believes
 * it runs on.  GetSystemInfo, GetVersionEx, VerifyVersionInfo, IsProcessorFeaturePresent,
 * GetLogicalProcessorInformation, rt_cpuid, the process block, the environment and the
 * registry seed all read from it, so they cannot disagree.
 *
 * Built so far: the RtMachine description itself (rt_machine_init, rt_machine), which the
 * skeleton needs for the process block.  Everything marked [rt-core-machine] is declared
 * for a later unit and calls RT_CORE_UNIMPLEMENTED until then.
 */
#ifndef RT_MACHINE_H
#define RT_MACHINE_H

#include "runtime.h"
#include "rt/wintypes.h"
#include "rt/win/sysinfo.h"

typedef struct RtMachine {
    /* Windows 7 SP1 x64, workstation: the oldest system the game supports, so the smallest
     * set of optional paths. */
    uint32_t os_major;            /* 6 */
    uint32_t os_minor;            /* 1 */
    uint32_t os_build;            /* 7601 */
    uint16_t sp_major;            /* 1 */
    uint16_t sp_minor;            /* 0 */
    const char *sp_name;          /* "Service Pack 1" */

    /* Processor: x64, family 6, one package, no SMT. */
    uint32_t cpu_count;           /* the host's performance cores; RT_CPUS overrides (1 to 64) */
    uint32_t cpu_family;          /* 6 */
    uint64_t cpu_hz;              /* nominal 3,000,000,000: rt_rdtsc and the registry agree on it */

    /* Memory, as the guest is told (section 5.1). */
    uint32_t page_size;           /* 4096 */
    uint32_t granularity;         /* 65536 */
    uint64_t min_address;         /* 0x10000 */
    uint64_t max_address;         /* 0x7FFFFFEFFFF: the Windows 7 limit of 8 TB */
    uint64_t phys_total;          /* host physical memory */
    uint64_t phys_avail;          /* half of it */

    /* Host facts the runtime itself needs; never shown to the guest. */
    uint64_t host_page_size;      /* 16384 on Apple silicon; asked, never assumed */
    uint32_t host_perf_cores;
    uint32_t host_cores;

    /* Locale (section 8.4). */
    uint32_t lcid;                /* 0x0409 */
    uint32_t ansi_cp;             /* 1252 */
    uint32_t oem_cp;              /* 437 */
} RtMachine;

/* Build the description from constants, the host (sysctl) and the environment (RT_CPUS).
 * Called once by rt_init, after rt_vm_init. */
void rt_machine_init(void);

/* The description.  Read-only after rt_machine_init. */
const RtMachine *rt_machine(void);

/* ---- answers computed from the description (runtime/core/machine.c) ------------------------- */

/* GetSystemInfo / GetNativeSystemInfo: fill *out from the description.
 * wProcessorArchitecture AMD64, dwProcessorType 8664, dwActiveProcessorMask one bit per
 * reported processor, wProcessorLevel = family, wProcessorRevision = model << 8 | stepping
 * as rt_cpuid leaf 1 reports them. */
void rt_machine_system_info(SYSTEM_INFO *out);

/* IsProcessorFeaturePresent(feature): the PF_* answer that matches rt_cpuid.
 * PF_FASTFAIL_AVAILABLE (23) is FALSE.  An unknown feature number is FALSE. */
BOOL rt_machine_processor_feature(DWORD feature);

/* Random bytes for the guest (BCryptGenRandom with the system-preferred generator,
 * UuidCreate): fill buf[0 .. n-1] from the host's generator, arc4random_buf, which the host
 * seeds from its kernel and which cannot fail.  Exactly n bytes are written.  n == 0 writes
 * nothing, and buf may then be NULL.
 * The bytes are NOT reproducible: there is no seed to set and, unlike the clocks of
 * rt/time.h, no source to substitute, so a run that depends on them does not repeat.  A test
 * checks what the bytes are like, never their values.
 * Cannot fail and sets no last error; any thread, guest or not; takes no lock of the runtime
 * and works before rt_machine_init.  It is here because a shim may not include stdlib.h.
 * Fatal: buf NULL with n above 0. */
void rt_random_bytes(void *buf, SIZE_T n);

/* The lifted `cpuid` instruction (declared in harness/runtime.h as rt_cpuid): leaf in eax,
 * sub-leaf in ecx; results in eax, ebx, ecx, edx.  Reports MMX, SSE, SSE2 and SSE3 and nothing
 * later: no SSSE3, SSE4.1, SSE4.2, POPCNT, CMPXCHG16B, AES, AVX, AVX2, BMI, SHA,
 * RDRAND or XSAVE, and no SMT (docs/DESIGN.md section 7, docs/findings/16-cpu-features.md;
 * the bits are listed leaf by leaf in runtime/core/machine.c). */
/* void rt_cpuid(Ctx *ctx); */

/* One bit of what rt_cpuid answers for (leaf, sub): 1, 0, or -1 when there is no answer (a
 * leaf the runtime does not describe, a call before rt_machine_init, reg or bit out of
 * range).  reg: 0 eax, 1 ebx, 2 ecx, 3 edx.  For reports; nothing the guest sees. */
int rt_machine_cpuid_bit(uint32_t leaf, uint32_t sub, unsigned reg, unsigned bit);

#endif /* RT_MACHINE_H */
