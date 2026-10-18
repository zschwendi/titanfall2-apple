/* Minimal host for lifted C built WITHOUT -DHARNESS_ISOLATED (the "really linked" macro set
 * of harness/runtime.h), used by tests/run_selftests.py.
 *
 *   linked_main <ecx>
 *
 * Calls the lifted function ENTRY (-DENTRY=<module>_<rva>) on a small private guest stack
 * with ecx = <ecx> and prints "<ecx afterwards> <rsp afterwards - rsp before>".  Anything
 * the lifted code is not expected to do here (cpuid, a trap) ends the process with a non-zero
 * status; a call to a function the lifter skipped ends it with status 7 and the reason; an
 * instruction the lifter does not translate ends it with status 8, its name and what it needs;
 * a call that comes back with rsp or its return address changed ends it with status 6
 * (unless built with -DTF_NO_RSP_CHECK, which removes that check from the macros).  So does
 * an indirect call or jump, unless the program is built with -DWITH_TABLE: then it goes
 * through the dispatch table of the module (-DMODULE=<module>), and only a target that has
 * no entry there ends the process (status 5).  With -DSLOTS=<rva>,<rva>,...
 * (needs WITH_TABLE) rdx points at an array holding the guest addresses of those functions,
 * for code that calls or jumps through a slot.  With -DSLOTS_BEHIND=<offset> as well, rdx points
 * at an object instead whose field at that offset points at the array (a table of function
 * pointers that hangs off an object), and a slot written as 0 stays 0 (no function there).
 *
 * With -DIMAGE_FILE="<path>" the bytes of that file are the module's image: they are read into
 * memory and the module's base is their address, for code that reads its own data (a jump
 * table); the stop at an untranslated instruction then reports an RVA.  Without it the base is 0
 * and lifted code must not read the image.
 *
 *   linked_main table       (WITH_TABLE) prints "<rows of the table> rows, <how many are stubs
 *                           of skipped functions> skipped" and one line per skipped function.
 */
#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t tf_straddle_lock;

#ifdef IMAGE_FILE
#define IB_CAT_(a, b) a##b
#define IB_CAT(a, b) IB_CAT_(a, b)
#define IMAGE_BASE_VAR IB_CAT(g_base_, MODULE)
extern uint64_t IMAGE_BASE_VAR;
#define GUEST(a) ((a) - IMAGE_BASE_VAR)     /* the image is wherever the host put it: report RVAs */
#else
#define GUEST(a) (a)
#endif

void rt_cpuid(Ctx *ctx) { (void)ctx; exit(4); }
void rt_rdtsc(Ctx *ctx) { (void)ctx; exit(4); }

#ifdef WITH_TABLE
#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
extern const LiftedEntry CAT(MODULE, _table)[];
extern const size_t CAT(MODULE, _table_count);
extern const LiftedSkip CAT(MODULE, _skips)[];
extern const size_t CAT(MODULE, _skips_count);
extern uint64_t CAT(g_base_, MODULE);

static LiftedFn lookup(uint64_t guest_target) {
    for (size_t i = 0; i < CAT(MODULE, _table_count); i++)
        if (CAT(g_base_, MODULE) + CAT(MODULE, _table)[i].rva == guest_target) return CAT(MODULE, _table)[i].fn;
    fprintf(stderr, "dispatch miss: guest address 0x%llx\n", (unsigned long long)guest_target);
    exit(5);
}
LiftedFn rt_dispatch_fn(Ctx *ctx, uint64_t guest_target) { (void)ctx; return lookup(guest_target); }
#else
static void no_table(Ctx *ctx) { (void)ctx; exit(5); }
LiftedFn rt_dispatch_fn(Ctx *ctx, uint64_t guest_target) { (void)ctx; (void)guest_target; return no_table; }
#endif

_Noreturn void rt_trap(Ctx *ctx, uint64_t guest_address) {
    (void)ctx;
    fprintf(stderr, "trap at guest address 0x%llx\n", (unsigned long long)guest_address);
    exit(3);
}
/* TRAP without -DTF_EXACT_TRAPS (harness/runtime.h): the same stop, no register file passed */
_Noreturn void rt_trap_lazy(uint64_t guest_address) {
    fprintf(stderr, "trap at guest address 0x%llx\n", (unsigned long long)guest_address);
    exit(3);
}

/* The stub of a function the lifter skipped, reached by a direct call or through the table. */
_Noreturn void rt_trap_skipped(Ctx *ctx, uint64_t guest_address, const char *why, uint64_t why_address) {
    (void)ctx;
    fprintf(stderr, "skipped by the lifter: the function at guest address 0x%llx (%s at 0x%llx)\n",
            (unsigned long long)guest_address, why, (unsigned long long)why_address);
    exit(7);
}

/* An instruction the lifter does not translate, inside a lifted function.  This host answers
 * no CPUID, so it only repeats what the lifted code says the instruction needs. */
_Noreturn void rt_trap_untranslated(Ctx *ctx, uint64_t guest_address, const char *what, const char *needs) {
    (void)ctx;
    fprintf(stderr, "untranslated instruction at guest address 0x%llx: %s (needs: %s)\n",
            (unsigned long long)GUEST(guest_address), what, *needs ? needs : "no feature bit known");
    exit(8);
}

#ifndef TF_NO_RSP_CHECK
_Noreturn void rt_rsp_mismatch(Ctx *ctx, uint64_t return_address, uint64_t expected_rsp) {
    if (ctx->r[4] != expected_rsp)
        fprintf(stderr, "return check: the call returning to 0x%llx came back with rsp off by %lld\n",
                (unsigned long long)return_address, (long long)(ctx->r[4] - expected_rsp));
    else
        fprintf(stderr, "return check: the return address 0x%llx was overwritten with 0x%llx\n",
                (unsigned long long)return_address, (unsigned long long)LD64(expected_rsp - 8));
    exit(6);
}
#endif

void ENTRY(Ctx *ctx);

int main(int argc, char **argv) {
    static uint64_t stack[64];
    static Ctx ctx;
    if (argc != 2) return 2;
#ifdef WITH_TABLE
    if (!strcmp(argv[1], "table")) {             /* what the lifted C says about itself */
        printf("%zu rows, %zu skipped\n", CAT(MODULE, _table_count), CAT(MODULE, _skips_count));
        for (size_t i = 0; i < CAT(MODULE, _skips_count); i++)
            printf("0x%x: %s at 0x%x\n", CAT(MODULE, _skips)[i].rva, CAT(MODULE, _skips)[i].why, CAT(MODULE, _skips)[i].at);
        return 0;
    }
#endif
#ifdef IMAGE_FILE
    {
        FILE *f = fopen(IMAGE_FILE, "rb");
        static uint8_t image[0x10000];
        if (!f || fread(image, 1, sizeof image, f) == 0) return 2;
        fclose(f);
        IMAGE_BASE_VAR = (uint64_t)(uintptr_t)image;
    }
#endif
    uint64_t rsp0 = (uint64_t)(uintptr_t)&stack[32];
    ctx.r[4] = rsp0;
    ctx.r[1] = strtoull(argv[1], 0, 0);
#ifdef SLOTS
    static uint64_t slots[] = { SLOTS };
#ifdef SLOTS_BEHIND
    static uint64_t object[8];
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++) if (slots[i]) slots[i] += CAT(g_base_, MODULE);
    object[(SLOTS_BEHIND) / 8] = (uint64_t)(uintptr_t)slots;
    ctx.r[2] = (uint64_t)(uintptr_t)object;
#else
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++) slots[i] += CAT(g_base_, MODULE);
    ctx.r[2] = (uint64_t)(uintptr_t)slots;
#endif
#endif
    ENTRY(&ctx);
    printf("%llu %lld\n", (unsigned long long)(uint32_t)ctx.r[1], (long long)(ctx.r[4] - rsp0));
    return 0;
}
