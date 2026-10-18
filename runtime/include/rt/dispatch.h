/* Dispatch: guest code address to host function (docs/RUNTIME.md section 2).
 *
 * One global open-addressing hash table keyed by guest address, filled during rt_init and
 * then made read-only, so lookups need no lock.  Keys are: every lifted function
 * (base + rva), every shim stub, every trap stub, every other native stub.  All of them
 * have the type void (Ctx *).
 *
 * Lifted code reaches this through CALL_INDIRECT / TAIL_INDIRECT in harness/runtime.h,
 * which look the target up with TF_LOOKUP (by default rt_dispatch_fn, below) and call the
 * result; TAIL_INDIRECT makes that a musttail call (issue 49).
 */
#ifndef RT_DISPATCH_H
#define RT_DISPATCH_H

#include "rt/thread.h"

/* 16 bytes.  guest == 0 marks an empty slot.  An empty slot's fn is never NULL: it is a
 * function that reports a call to guest address 0, because rt_lookup(0) matches the first
 * empty slot it probes. */
typedef struct { uint64_t guest; LiftedFn fn; } RtDispEntry;

#define RT_DISP_BITS  19                                          /* 2^19 entries, 8 MB */

extern const RtDispEntry *g_disp;
extern unsigned           g_disp_shift;                           /* 64 - RT_DISP_BITS */
extern uint64_t           g_disp_mask;                            /* 2^RT_DISP_BITS - 1 */

/* The single miss hook.  Reports RT_THREAD(ctx)->miss_target as module+rva, says whether it
 * is inside an image, the VM arena (the game generated code) or neither, and aborts. */
void rt_dispatch_miss(Ctx *ctx);

/* The host function for guest address `target`.  Never NULL: a miss returns
 * rt_dispatch_miss after recording the target (for target 0, a function that does the
 * same), so the caller just calls the result. */
static inline __attribute__((always_inline, unused)) LiftedFn rt_lookup(Ctx *ctx, uint64_t target) {
    uint64_t i = (target * UINT64_C(0x9E3779B97F4A7C15)) >> g_disp_shift;
    for (;; i = (i + 1) & g_disp_mask) {
        const RtDispEntry *e = &g_disp[i];
        if (__builtin_expect(e->guest == target, 1)) return e->fn;
        if (e->guest == 0) { RT_THREAD(ctx)->miss_target = target; return rt_dispatch_miss; }
    }
}

/* rt_lookup as a function that harness/runtime.h declares without this header, for the
 * linked-build macros (RUNTIME.md appendix B item 1, issue 49):
 *
 *     #define TF_LOOKUP(ctx, target) rt_dispatch_fn((ctx), (target))      (unless already defined)
 *     #define TAIL_INDIRECT(target) \
 *         do { __attribute__((musttail)) return TF_LOOKUP(ctx, (target))(ctx); } while (0)
 *
 * Lifted C compiled with `-include rt/dispatch.h -DTF_LOOKUP=rt_lookup` gets the inline
 * lookup instead (not done: RT_LIFT_CFLAGS in rt.mk does not, and nothing measures it yet).
 *
 * Returns exactly what rt_lookup returns: the host function registered for `guest_target`,
 * never NULL; for an address that is not in the table (0 included) it records the target in
 * RT_THREAD(ctx)->miss_target and returns the function that makes the dispatch-miss report.
 * So the caller always calls the result, and because every such function has the type
 * void (Ctx *) the call can be a musttail call: a guest tail call through a register or a
 * slot then leaves the host stack as it found it.  It does not push, pop or check anything:
 * the guest return address is already on the guest stack.  Takes no lock (the table is
 * read-only after rt_init); `ctx` must be the calling thread's. */
LiftedFn rt_dispatch_fn(Ctx *ctx, uint64_t guest_target);

/* Look `guest_target` up and call it: rt_dispatch_fn(ctx, guest_target)(ctx).  For native
 * code that calls into the guest (tests, shims making a callback).  Lifted code does not use
 * it any more: as a nested call it cannot be a tail call. */
void rt_dispatch(Ctx *ctx, uint64_t guest_target);

/* ---- building the table (rt_init only, single-threaded) --------------------------------- */

/* Allocate the empty table.  Called once by rt_init. */
void rt_disp_init(void);

/* Map `guest` to `fn`.  Fatal if the table is sealed, if `guest` is 0, if `guest` is
 * already present with a different function, or if the table would pass half full. */
void rt_disp_add(uint64_t guest, LiftedFn fn);

/* Add every entry of a lifted module's <module>_table.c at base + rva. */
void rt_disp_add_table(uint64_t base, const LiftedEntry *entries, size_t count);

/* Make the table read-only.  After this rt_disp_add is a fatal error. */
void rt_disp_seal(void);
bool rt_disp_sealed(void);

/* Number of keys in the table. */
size_t rt_disp_count(void);

/* The function registered for `guest`, or NULL.  For diagnostics and tests; the hot path
 * is rt_lookup. */
LiftedFn rt_disp_find(uint64_t guest);

#endif /* RT_DISPATCH_H */
