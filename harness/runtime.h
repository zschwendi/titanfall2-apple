/* Guest state, memory access, register access, condition codes, atomics and the control-transfer
 * macros that every lifted C file is written against.  The contract is docs/DESIGN.md sections
 * 4-7.  This file holds what several instruction families share; the C helpers that only one
 * family's translators call are in insns/<family>.h, and every generated C file includes this
 * file and then each of those (the lifter writes the include lines from the families it finds).
 *
 * Two builds:
 *   -DHARNESS_ISOLATED   isolated-function test: every way out of a lifted function records an
 *                        event and applies a deterministic stub effect (DESIGN.md section 8).
 *   (default)            the really-linked program: calls go to other lifted functions or to
 *                        the dispatch table.  Declared here, not yet exercised.
 *
 * Flag helpers follow the Intel SDM vol. 2 "Flags Affected" sections.  A flag an instruction
 * leaves undefined may be written with anything or left alone; a flag an instruction leaves
 * *unchanged* must never be written.
 */
#ifndef TF_RUNTIME_H
#define TF_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "fpmode.h"         /* the float mode lifted code runs in, and the two switches (arm64 only) */

/* An xmm register, seen as lanes of every element type the SSE instructions use.  Lane 0 is the
 * low end.  Unsigned lanes: b w d q.  Signed lanes: sb sw i l.  f: single, pd: double. */
typedef union {
    uint8_t  b[16];
    uint16_t w[8];
    uint32_t d[4];
    uint64_t q[2];
    int8_t   sb[16];
    int16_t  sw[8];
    int32_t  i[4];
    int64_t  l[2];
    float    f[4];
    double   pd[2];
} Xmm;

typedef struct Ctx {
    uint64_t r[16];            /* rax rcx rdx rbx rsp rbp rsi rdi r8..r15 (encoding order) */
    uint8_t  cf, pf, af, zf, sf, of, df;   /* each 0 or 1 */
    uint8_t  pad;
    uint32_t mxcsr;
    uint16_t fpcw;             /* x87 control word; starts as TF_FPCW_DEFAULT (insns/fp_x87cw.h) */
    Xmm      xmm[16];
    uint64_t gs_base;          /* guest TEB */
    void    *host;             /* runtime or harness private data */
} Ctx;

typedef void (*LiftedFn)(Ctx *);
/* THE REGISTER FILE DOES NOT ESCAPE.  Lifted code reaches the register file only through its
 * `ctx` argument, and nothing it calls keeps the pointer: every callee is a lifted function, a
 * shim or one of the rt_ functions below, all of which take `ctx` and use it during the call
 * only.  TF_NOESCAPE says so on each of those parameters (clang's `noescape`, LLVM's
 * `nocapture`), and the call macros of the linked build call through LiftedFnNE, the same type
 * with that word.  With it, and with the compiler's limit on the uses it follows raised
 * (RT_LIFT_CFLAGS: -mllvm -capture-tracking-max-uses-to-explore), the compiler knows that a
 * guest load or store, whose address is made from an integer, cannot be a field of the register
 * file.  Without it that knowledge (the `restrict` of lift.py) ended at the first call of a
 * function, and in a function with more than 100 uses of `ctx` it never began: registers were
 * reloaded after every guest store and no flag store that a guest load followed was deleted
 * (docs/findings/25-lifted-speed.md).  What must stay true: the guest never learns the address
 * of a Ctx, and no function called from lifted code without `ctx` reads or writes one.
 * -DTF_CTX_MAY_ESCAPE builds without this. */
#if !defined(TF_CTX_MAY_ESCAPE) && defined(__has_attribute)
#if __has_attribute(noescape)
#define TF_NOESCAPE __attribute__((noescape))
#endif
#endif
#ifndef TF_NOESCAPE
#define TF_NOESCAPE
#endif
typedef void (*LiftedFnNE)(Ctx *TF_NOESCAPE);
typedef struct { uint32_t rva; LiftedFn fn; } LiftedEntry;
/* A function the lifter skipped (<module>_skips of the lifted C, sorted by rva): why, in the
 * words of the first reason of its record in the lifter's report ("unsupported: movnti"),
 * and `at`, the rva of the instruction that reason is about.  <module>_table has a row for
 * the function all the same: a stub that stops the run with this reason (rt_trap_skipped). */
typedef struct { uint32_t rva, at; const char *why; } LiftedSkip;

/* ---- memory: guest address == host address ------------------------------------------- */

#define TF_INLINE static inline __attribute__((always_inline, unused))

/* TF_WATCH_ST64_ADDR and TF_WATCH_ST64_VALUE (a DIAGNOSTIC build: `make gate2-build
 * GUEST_WATCH=<address>:<value>`, runtime/rt.mk): lifted code that stores that 64-bit value at
 * that guest address stops in a trap instruction instead, so that the runtime's fatal report
 * names the lifted function that was about to make the store and its callers.  For "who wrote
 * this?" when a guest structure is found damaged; the image bases are fixed, so the address
 * of a global is the same in every run. */
#ifdef TF_WATCH_ST64_ADDR
#define TF_WATCH_ST64(a, v) \
    do { if (__builtin_expect((a) == (uint64_t)(TF_WATCH_ST64_ADDR) && (v) == (uint64_t)(TF_WATCH_ST64_VALUE), 0)) __builtin_trap(); } while (0)
#else
#define TF_WATCH_ST64(a, v) ((void)0)
#endif

#ifndef TF_GUEST_MEM_SEQCST

TF_INLINE uint8_t  LD8(uint64_t a)  { uint8_t v;  __builtin_memcpy(&v, (const void *)(uintptr_t)a, 1); return v; }
TF_INLINE uint16_t LD16(uint64_t a) { uint16_t v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 2); return v; }
TF_INLINE uint32_t LD32(uint64_t a) { uint32_t v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 4); return v; }
TF_INLINE uint64_t LD64(uint64_t a) { uint64_t v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 8); return v; }
TF_INLINE Xmm      LDX(uint64_t a)  { Xmm v;      __builtin_memcpy(&v, (const void *)(uintptr_t)a, 16); return v; }
TF_INLINE void ST8(uint64_t a, uint8_t v)   { __builtin_memcpy((void *)(uintptr_t)a, &v, 1); }
TF_INLINE void ST16(uint64_t a, uint16_t v) { __builtin_memcpy((void *)(uintptr_t)a, &v, 2); }
TF_INLINE void ST32(uint64_t a, uint32_t v) { __builtin_memcpy((void *)(uintptr_t)a, &v, 4); }
TF_INLINE void ST64(uint64_t a, uint64_t v) { TF_WATCH_ST64(a, v); __builtin_memcpy((void *)(uintptr_t)a, &v, 8); }
TF_INLINE void STX(uint64_t a, Xmm v)       { __builtin_memcpy((void *)(uintptr_t)a, &v, 16); }

/* Scalar float operands in memory (Translator.fval). */
TF_INLINE float  LDF32(uint64_t a) { float v;  __builtin_memcpy(&v, (const void *)(uintptr_t)a, 4); return v; }
TF_INLINE double LDF64(uint64_t a) { double v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 8); return v; }

#else /* TF_GUEST_MEM_SEQCST */

/* A DIAGNOSTIC build of lifted code, never the shipped one: `make gate2-build GUEST_MEM=seqcst`
 * (runtime/rt.mk; it builds beside the normal one, in build/gate2-seqcst).  Every guest load and
 * store of 1, 2, 4 or 8 bytes at an address aligned to its size is a sequentially consistent
 * atomic (on arm64: ldar and stlr), and a 16-byte one at an 8-aligned address is two of them.
 * That is STRONGER than x86, whose ordering lets a store wait in the store buffer past later
 * loads of other addresses, and it also stops the compiler from merging two loads of one guest
 * address or keeping one across a loop, which plain loads allow.
 *
 * What it is for: lifted code compiled the normal way runs under arm64's weak ordering unless
 * the runtime gets the processor's x86 ordering mode for the thread, which a plain program is
 * refused (RUNTIME.md; issue 42).  A fault of multithreaded guest code that appears in the
 * normal build and never in this one is a fault of memory ordering, and the cure is that mode,
 * not a change to lifted code; a fault that appears in both is not.
 *
 * What it leaves out: an access that is not aligned to its size stays a plain copy (x86 allows
 * it; an atomic access on arm64 does not); the string instructions and the other helpers that
 * copy guest memory with memcpy; lock-prefixed instructions, which are atomics in both builds.
 *
 * The 16-byte loads and stores (LDX and STX: the vector moves) come three ways in this build
 * (`GUEST_X16=` of runtime/rt.mk, each in build directories of its own):
 *   (none)              "ordered": at an 8-aligned address two sequentially consistent 8-byte
 *                       accesses, low half first.  Ordered against every other guest access, but
 *                       another thread can change the 16 bytes between the two halves.
 *   TF_GUEST_X16_ATOMIC "ordered and atomic": at a 16-aligned address one sequentially consistent
 *                       128-bit atomic access.  Apple clang 21 makes the load `ldp` + `dmb ish` and
 *                       the store `dmb ish` + `stp` + `dmb ish`: no lock and no loop; it relies on
 *                       the processor's rule (FEAT_LSE2) that a pair access to 16 aligned bytes is
 *                       one access.  8-aligned but not 16-aligned: as "ordered".
 *   TF_GUEST_X16_PLAIN  a plain 16-byte copy as in the normal build, for measuring what the
 *                       ordering of the vector moves alone changes. */
/* TF_GUEST_MEM_TSO (`GUEST_MEM=tso`, with TF_GUEST_MEM_SEQCST): the same build with x86's own order
 * instead of a stronger one.  Loads are acquire (on Apple silicon `ldapr`, FEAT_LRCPC) and stores
 * release (`stlr`): a load is ordered before every later load and store, a store after every
 * earlier load and store, and a store may still wait past later loads of other addresses, which is
 * x86's store buffer.  What sequential consistency adds (a load waits until every earlier store is
 * visible: `ldar` after `stlr`) x86 does not have; it is the cost this variant leaves out.  This is
 * the order the build without the processor's x86 mode is meant to ship with (no Apple account). */
/* TF_GUEST_MEM_TSOB (`GUEST_MEM=tsob`): x86's order by barriers.  Loads acquire (`ldapr`) as in
 * TF_GUEST_MEM_TSO; a store is `dmb ishst` then a plain store.  The barrier orders the store after every
 * earlier store, the acquire loads order every later access after them, and nothing makes a load wait
 * for an earlier store: exactly x86's rules.  A release store (`stlr`) also waits for every earlier
 * LOAD to finish, which x86 never asks; measured on 16 October it is that wait that makes the tso and
 * seqcst builds six times slower (GUEST_DIAG=stonly: 20 frames a second; ldonly: 109; hardware: 120).
 * Checked on an M3 Pro with a message-passing litmus test under cache-line contention: plain stores
 * with acquire loads were reordered (95 to 472 million times in 5 s); with the barrier, 0. */
#ifdef TF_GUEST_MEM_TSOB
#define TF_GUEST_LOAD_ORDER  __ATOMIC_ACQUIRE
#define TF_GUEST_STORE_ORDER __ATOMIC_RELAXED
#define TF_ST_FENCE()        __builtin_arm_dmb(10)          /* dmb ishst */
#define TF_LD_FENCE()        __builtin_arm_dmb(9)           /* dmb ishld: after a load that is not atomic */
#else
#define TF_ST_FENCE()        ((void)0)
#define TF_LD_FENCE()        ((void)0)
#endif
#ifndef TF_GUEST_LOAD_ORDER            /* GUEST_DIAG= of runtime/rt.mk sets both, for measuring only */
#ifdef TF_GUEST_MEM_TSO
#define TF_GUEST_LOAD_ORDER  __ATOMIC_ACQUIRE
#define TF_GUEST_STORE_ORDER __ATOMIC_RELEASE
#else
#define TF_GUEST_LOAD_ORDER  __ATOMIC_SEQ_CST
#define TF_GUEST_STORE_ORDER __ATOMIC_SEQ_CST
#endif
#endif
#define TF_SC_LOAD(T, a)     __atomic_load_n((const T *)(uintptr_t)(a), TF_GUEST_LOAD_ORDER)
#define TF_SC_STORE(T, a, v) __atomic_store_n((T *)(uintptr_t)(a), (v), TF_GUEST_STORE_ORDER)

TF_INLINE uint8_t  LD8(uint64_t a)  { return TF_SC_LOAD(uint8_t, a); }
TF_INLINE uint16_t LD16(uint64_t a) {
    if (a & 1) { uint16_t v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 2); TF_LD_FENCE(); return v; }
    return TF_SC_LOAD(uint16_t, a); }
TF_INLINE uint32_t LD32(uint64_t a) {
    if (a & 3) { uint32_t v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 4); TF_LD_FENCE(); return v; }
    return TF_SC_LOAD(uint32_t, a); }
TF_INLINE uint64_t LD64(uint64_t a) {
    if (a & 7) { uint64_t v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 8); TF_LD_FENCE(); return v; }
    return TF_SC_LOAD(uint64_t, a); }
#if defined(TF_GUEST_X16_PLAIN) && defined(TF_GUEST_X16_ATOMIC)
#error "TF_GUEST_X16_PLAIN and TF_GUEST_X16_ATOMIC exclude each other"
#endif
#ifdef TF_GUEST_X16_PLAIN
#define TF_X16_SPLIT(a) 0
#else
#define TF_X16_SPLIT(a) (!((a) & 7))
#endif
TF_INLINE Xmm      LDX(uint64_t a)  {
    Xmm v;
#ifdef TF_GUEST_X16_ATOMIC
    if (!(a & 15)) {
        unsigned __int128 u = TF_SC_LOAD(unsigned __int128, a);
        __builtin_memcpy(&v, &u, 16); return v; }
#endif
    if (!TF_X16_SPLIT(a)) { __builtin_memcpy(&v, (const void *)(uintptr_t)a, 16); TF_LD_FENCE(); return v; }
    v.q[0] = TF_SC_LOAD(uint64_t, a); v.q[1] = TF_SC_LOAD(uint64_t, a + 8); return v; }
TF_INLINE void ST8(uint64_t a, uint8_t v)   { TF_ST_FENCE(); TF_SC_STORE(uint8_t, a, v); }
TF_INLINE void ST16(uint64_t a, uint16_t v) {
    TF_ST_FENCE();
    if (a & 1) { __builtin_memcpy((void *)(uintptr_t)a, &v, 2); return; }
    TF_SC_STORE(uint16_t, a, v); }
TF_INLINE void ST32(uint64_t a, uint32_t v) {
    TF_ST_FENCE();
    if (a & 3) { __builtin_memcpy((void *)(uintptr_t)a, &v, 4); return; }
    TF_SC_STORE(uint32_t, a, v); }
TF_INLINE void ST64(uint64_t a, uint64_t v) {
    TF_WATCH_ST64(a, v);
    TF_ST_FENCE();
    if (a & 7) { __builtin_memcpy((void *)(uintptr_t)a, &v, 8); return; }
    TF_SC_STORE(uint64_t, a, v); }
TF_INLINE void STX(uint64_t a, Xmm v)       {
    TF_ST_FENCE();
#ifdef TF_GUEST_X16_ATOMIC
    if (!(a & 15)) {
        unsigned __int128 u; __builtin_memcpy(&u, &v, 16);
        TF_SC_STORE(unsigned __int128, a, u); return; }
#endif
    if (!TF_X16_SPLIT(a)) { __builtin_memcpy((void *)(uintptr_t)a, &v, 16); return; }
    TF_SC_STORE(uint64_t, a, v.q[0]); TF_SC_STORE(uint64_t, a + 8, v.q[1]); }

/* Scalar float operands in memory (Translator.fval). */
TF_INLINE float  LDF32(uint64_t a) { uint32_t u = LD32(a); float v;  __builtin_memcpy(&v, &u, 4); return v; }
TF_INLINE double LDF64(uint64_t a) { uint64_t u = LD64(a); double v; __builtin_memcpy(&v, &u, 8); return v; }

#endif /* TF_GUEST_MEM_SEQCST */

/* Stack variants of the guest memory macros.  The lifter names an access whose operand is [rsp]
 * (no index, no segment override) with these, and so do push, pop, leave, pushfq, popfq, call,
 * ret and the return-address checks.  Without TF_GUEST_STACK_PLAIN each is its normal macro, so
 * the build is the same as before.  With it (`GUEST_STACK=plain`, with GUEST_MEM=seqcst or tso,
 * runtime/rt.mk) each is a plain copy, as in the first block of this file: a guest stack access
 * then costs no ordering.  The one risk: a stack object that another thread reads or writes,
 * when the owner's accesses to it are rsp-based and the other side relies on their order (a flag
 * or a lock kept on the stack, whose address was given to another thread). */
#ifdef TF_GUEST_STACK_PLAIN

TF_INLINE uint8_t  LDS8(uint64_t a)  { uint8_t v;  __builtin_memcpy(&v, (const void *)(uintptr_t)a, 1); return v; }
TF_INLINE uint16_t LDS16(uint64_t a) { uint16_t v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 2); return v; }
TF_INLINE uint32_t LDS32(uint64_t a) { uint32_t v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 4); return v; }
TF_INLINE uint64_t LDS64(uint64_t a) { uint64_t v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 8); return v; }
TF_INLINE Xmm      LDSX(uint64_t a)  { Xmm v;      __builtin_memcpy(&v, (const void *)(uintptr_t)a, 16); return v; }
TF_INLINE void STS8(uint64_t a, uint8_t v)   { __builtin_memcpy((void *)(uintptr_t)a, &v, 1); }
TF_INLINE void STS16(uint64_t a, uint16_t v) { __builtin_memcpy((void *)(uintptr_t)a, &v, 2); }
TF_INLINE void STS32(uint64_t a, uint32_t v) { __builtin_memcpy((void *)(uintptr_t)a, &v, 4); }
TF_INLINE void STS64(uint64_t a, uint64_t v) { TF_WATCH_ST64(a, v); __builtin_memcpy((void *)(uintptr_t)a, &v, 8); }
TF_INLINE void STSX(uint64_t a, Xmm v)       { __builtin_memcpy((void *)(uintptr_t)a, &v, 16); }
TF_INLINE float  LDSF32(uint64_t a) { float v;  __builtin_memcpy(&v, (const void *)(uintptr_t)a, 4); return v; }
TF_INLINE double LDSF64(uint64_t a) { double v; __builtin_memcpy(&v, (const void *)(uintptr_t)a, 8); return v; }

#else

#define LDS8(a)    LD8(a)
#define LDS16(a)   LD16(a)
#define LDS32(a)   LD32(a)
#define LDS64(a)   LD64(a)
#define LDSX(a)    LDX(a)
#define STS8(a, v)  ST8((a), (v))
#define STS16(a, v) ST16((a), (v))
#define STS32(a, v) ST32((a), (v))
#define STS64(a, v) ST64((a), (v))
#define STSX(a, v)  STX((a), (v))
#define LDSF32(a)  LDF32(a)
#define LDSF64(a)  LDF64(a)

#endif /* TF_GUEST_STACK_PLAIN */

/* ---- float operations whose operand order matters ------------------------------------- */
/* With two NaN operands SSE returns the first source operand, quieted (SDM vol. 1 table 4-7), and
 * so does the host instruction in the guest float mode: fadd Rd, Rn, Rm returns Rn's NaN.  But
 * the compiler may swap the operands of a commutative C operation (it turned `*p + x` into
 * `x + *p`, docs/findings/11-float-mode.md trap 4), which changes which NaN comes back.  So every
 * commutative float operation of lifted code goes through one of these: the instruction is
 * written out and the operands stay where the translator put them (a = the x86 destination).
 * `volatile` because the result also depends on FPCR, which the compiler does not know about:
 * two of them on either side of an ldmxcsr must not be merged.  Non-commutative operations
 * (subtraction, division) are exact as plain C; see lifter/insns/README.md. */
#define TF_FP_ORDERED(NAME, INSN)                                                              \
TF_INLINE float tf_##NAME##32(float a, float b) {                                              \
    float r; __asm__ volatile(INSN " %s0, %s1, %s2" : "=w"(r) : "w"(a), "w"(b)); return r; }   \
TF_INLINE double tf_##NAME##64(double a, double b) {                                           \
    double r; __asm__ volatile(INSN " %d0, %d1, %d2" : "=w"(r) : "w"(a), "w"(b)); return r; }
TF_FP_ORDERED(fadd, "fadd")
TF_FP_ORDERED(fmul, "fmul")
/* min and max: SSE returns the second operand when either is a NaN or both are zeros, and under
 * MXCSR.DAZ a zero where the operand selected is a denormal.  The host's fmin and fmax do exactly
 * that in the guest float mode (FPCR.AH, FIZ = DAZ): 12,400 of 12,400 lanes against a Core
 * i5-7600K (harness/oraclefix.py).  The C form `a < b ? a : b` is wrong under DAZ, and libm's
 * fmin/fmax are another function altogether.  a = the x86 destination, b = the source. */
TF_FP_ORDERED(fmin, "fmin")
TF_FP_ORDERED(fmax, "fmax")
/* Square root: one operand, so there is no order to keep, but the instruction is written out for
 * the other reason: libm's sqrtf/sqrt become the host's fsqrt only as long as the build inlines
 * them (-fmath-errno or -fno-builtin make them calls), and host library code must not run in the
 * guest float mode (fpmode.h).  fsqrt there is exact against the CPU (docs/findings/11-float-mode.md). */
TF_INLINE float tf_fsqrt32(float a) {
    float r; __asm__ volatile("fsqrt %s0, %s1" : "=w"(r) : "w"(a)); return r; }
TF_INLINE double tf_fsqrt64(double a) {
    double r; __asm__ volatile("fsqrt %d0, %d1" : "=w"(r) : "w"(a)); return r; }
/* Round to an integral value in the current rounding mode (FPCR.RMode, which the guest mode
 * keeps equal to MXCSR.RC): what libm's rintf/rint are, written out for the same reason as the
 * square root.  cvtss2si and cvtsd2si round with it before their range test. */
TF_INLINE float tf_frint32(float a) {
    float r; __asm__ volatile("frintx %s0, %s1" : "=w"(r) : "w"(a)); return r; }
TF_INLINE double tf_frint64(double a) {
    double r; __asm__ volatile("frintx %d0, %d1" : "=w"(r) : "w"(a)); return r; }

/* rcpss/rcpps and rsqrtss/rsqrtps are not 1/x and 1/sqrt(x): the CPU looks the result up in a
 * table (2^11 entries, 12 bits of result), and the table is the vendor's.  The one reproduced
 * here is INTEL's: an AMD processor returns other bits for the same operand, and a game that ran
 * on AMD saw those.  The host's frecpe / frsqrte in the guest float mode (FPCR.AH) is that Intel
 * function exactly: all 2^32 operands under all 16 MXCSR settings against a Core i5-7600K
 * (tools/hworacle/sweep.py), and again case by case through these helpers
 * (harness/recip_sweep.py), 0 differences, so there is no correction to make.  Without AH the
 * same instruction is Arm's 8-bit estimate, another function.  Neither is `1.0f / a`, which is
 * what the emulator computes: only the real CPU judges these (harness/oraclefix.py,
 * HARDWARE_ONLY).  `volatile` as above: the result depends on FPCR. */
TF_INLINE float tf_frecpe32(float a) {
    float r; __asm__ volatile("frecpe %s0, %s1" : "=w"(r) : "w"(a)); return r; }
TF_INLINE float tf_frsqrte32(float a) {
    float r; __asm__ volatile("frsqrte %s0, %s1" : "=w"(r) : "w"(a)); return r; }

/* THE PACKED FORMS: one host vector instruction for one packed x86 operation.
 * A packed float instruction is the scalar one in every lane, and the host's vector instruction
 * is its scalar one in every lane under the same FPCR (rounding, AH, FIZ, FZ), so everything
 * said above about the scalar helpers holds lane by lane: the operand order (a = the x86
 * destination, b = the source), `volatile`, the instruction written out.  The lifter's packed
 * families call these on whole registers: `ctx->xmm[d] = tf_fadd_ps(ctx->xmm[d], s);`.
 * The copies to and from the vector type are register moves once inlined (measured: two addps and
 * a store are 8 host instructions; lane by lane they were 33).  The family's own test decides
 * (the expected lanes come from the real CPU under seven MXCSR settings, harness/floatcase.py). */
typedef float  tf_v4f __attribute__((vector_size(16)));
typedef double tf_v2d __attribute__((vector_size(16)));
#define TF_FP_PACKED2(NAME, INSN)                                                              \
TF_INLINE Xmm tf_##NAME##_ps(Xmm a, Xmm b) {                                                   \
    tf_v4f x, y, r; __builtin_memcpy(&x, &a, 16); __builtin_memcpy(&y, &b, 16);                \
    __asm__ volatile(INSN " %0.4s, %1.4s, %2.4s" : "=w"(r) : "w"(x), "w"(y));                  \
    Xmm o; __builtin_memcpy(&o, &r, 16); return o; }                                           \
TF_INLINE Xmm tf_##NAME##_pd(Xmm a, Xmm b) {                                                   \
    tf_v2d x, y, r; __builtin_memcpy(&x, &a, 16); __builtin_memcpy(&y, &b, 16);                \
    __asm__ volatile(INSN " %0.2d, %1.2d, %2.2d" : "=w"(r) : "w"(x), "w"(y));                  \
    Xmm o; __builtin_memcpy(&o, &r, 16); return o; }
#define TF_FP_PACKED1(NAME, INSN)                                                              \
TF_INLINE Xmm tf_##NAME##_ps(Xmm a) {                                                          \
    tf_v4f x, r; __builtin_memcpy(&x, &a, 16);                                                 \
    __asm__ volatile(INSN " %0.4s, %1.4s" : "=w"(r) : "w"(x));                                 \
    Xmm o; __builtin_memcpy(&o, &r, 16); return o; }                                           \
TF_INLINE Xmm tf_##NAME##_pd(Xmm a) {                                                          \
    tf_v2d x, r; __builtin_memcpy(&x, &a, 16);                                                 \
    __asm__ volatile(INSN " %0.2d, %1.2d" : "=w"(r) : "w"(x));                                 \
    Xmm o; __builtin_memcpy(&o, &r, 16); return o; }
TF_FP_PACKED2(fadd, "fadd")
TF_FP_PACKED2(fsub, "fsub")
TF_FP_PACKED2(fmul, "fmul")
TF_FP_PACKED2(fdiv, "fdiv")
TF_FP_PACKED2(fmin, "fmin")
TF_FP_PACKED2(fmax, "fmax")
TF_FP_PACKED1(fsqrt, "fsqrt")
TF_FP_PACKED1(frecpe, "frecpe")
TF_FP_PACKED1(frsqrte, "frsqrte")

/* Lane shuffles of whole registers, with the lanes chosen when the code is translated (the
 * lifter knows the immediate): one or two host instructions instead of four lane moves through
 * memory.  TF_SHUF4(A, B, i0, i1, i2, i3): 32-bit lanes; an index 0 to 3 takes a lane of A, 4 to 7
 * a lane of B (index - 4).  TF_SHUF2(A, B, i0, i1): 64-bit lanes, 0 to 1 of A, 2 to 3 of B.
 * A and B are Xmm values (read the source into a local first, as everywhere); the result is an
 * Xmm.  Moves of bits only: no float mode is involved, NaNs pass unchanged. */
typedef uint32_t tf_v4u __attribute__((vector_size(16)));
typedef uint64_t tf_v2q __attribute__((vector_size(16)));
#define TF_SHUF4(A, B, i0, i1, i2, i3) __extension__ ({                                        \
    tf_v4u x_, y_; Xmm a_ = (A), b_ = (B), o_;                                                 \
    __builtin_memcpy(&x_, &a_, 16); __builtin_memcpy(&y_, &b_, 16);                            \
    tf_v4u r_ = __builtin_shufflevector(x_, y_, (i0), (i1), (i2), (i3));                       \
    __builtin_memcpy(&o_, &r_, 16); o_; })
#define TF_SHUF2(A, B, i0, i1) __extension__ ({                                                \
    tf_v2q x_, y_; Xmm a_ = (A), b_ = (B), o_;                                                 \
    __builtin_memcpy(&x_, &a_, 16); __builtin_memcpy(&y_, &b_, 16);                            \
    tf_v2q r_ = __builtin_shufflevector(x_, y_, (i0), (i1));                                   \
    __builtin_memcpy(&o_, &r_, 16); o_; })

/* Pointer for the __atomic builtins (lock-prefixed instructions, xchg with memory). */
#define AP(T, a) ((T *)(uintptr_t)(a))

/* ---- atomics (DESIGN.md section 7) ---------------------------------------------------- */
/* Every lock-prefixed instruction and every xchg with memory goes through x_atomic_*.
 *
 * x86 allows a locked operand at any address.  The host's atomic instructions take any
 * address too, as long as the operand lies inside one 16-byte block (FEAT_LSE2, which every
 * Apple Silicon CPU has), and raise SIGBUS when it straddles a 16-byte boundary.  For a
 * straddling operand the helpers take one process-wide lock and do a plain
 * read-modify-write.  That excludes other straddling operations, but NOT an aligned atomic
 * operation that touches the same bytes at the same time: such a pair is not atomic with
 * respect to each other here, where on x86 it is.  Compilers do not generate straddling
 * locked operands, so this is a correctness-preserving fallback for code that does not
 * race, not an implementation of split locks.
 *
 * tf_straddle_lock is defined once by whoever links the lifted code (harness/driver.c). */
extern uint8_t tf_straddle_lock;

TF_INLINE int tf_straddles(uint64_t a, unsigned size) { return ((unsigned)a & 15u) + size > 16u; }
TF_INLINE void tf_straddle_enter(void) {
    while (__atomic_test_and_set(&tf_straddle_lock, __ATOMIC_ACQUIRE)) { }
}
TF_INLINE void tf_straddle_leave(void) { __atomic_clear(&tf_straddle_lock, __ATOMIC_RELEASE); }

/* Each returns the value the operand held before the operation. */
#define TF_ATOMIC_RMW(N, T, NAME, BUILTIN, EXPR)                                               \
TF_INLINE T x_atomic_##NAME##N(uint64_t a, T v) {                                              \
    if (!tf_straddles(a, N / 8)) return BUILTIN(AP(T, a), v, __ATOMIC_SEQ_CST);                \
    tf_straddle_enter();                                                                       \
    T old = LD##N(a);                                                                          \
    ST##N(a, (T)(EXPR));                                                                       \
    tf_straddle_leave();                                                                       \
    return old;                                                                                \
}
#define TF_ATOMIC(N, T)                                                                        \
TF_ATOMIC_RMW(N, T, xchg, __atomic_exchange_n, v)                                              \
TF_ATOMIC_RMW(N, T, add, __atomic_fetch_add, old + v)                                          \
TF_ATOMIC_RMW(N, T, sub, __atomic_fetch_sub, old - v)                                          \
TF_ATOMIC_RMW(N, T, and, __atomic_fetch_and, old & v)                                          \
TF_ATOMIC_RMW(N, T, or,  __atomic_fetch_or,  old | v)                                          \
TF_ATOMIC_RMW(N, T, xor, __atomic_fetch_xor, old ^ v)                                          \
/* Compare-exchange: stores `desired` if the operand equals `expected`; returns what the   */  \
/* operand held, so the exchange happened exactly when the result equals `expected`.       */  \
TF_INLINE T x_atomic_cmpxchg##N(uint64_t a, T expected, T desired) {                           \
    if (!tf_straddles(a, N / 8)) {                                                             \
        (void)__atomic_compare_exchange_n(AP(T, a), &expected, desired, 0,                     \
                                          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);                 \
        return expected;                                                                       \
    }                                                                                          \
    tf_straddle_enter();                                                                       \
    T old = LD##N(a);                                                                          \
    if (old == expected) ST##N(a, desired);                                                    \
    tf_straddle_leave();                                                                       \
    return old;                                                                                \
}
TF_ATOMIC(8,  uint8_t)
TF_ATOMIC(16, uint16_t)
TF_ATOMIC(32, uint32_t)
TF_ATOMIC(64, uint64_t)

/* lock cmpxchg16b.  The address is 16-byte aligned: the translator emits a trap for any
 * other address, because x86 raises #GP there (see t_cmpxchg16b in lifter/insns/atomics.py). */
TF_INLINE int x_atomic_cmpxchg128(uint64_t a, unsigned __int128 *expected, unsigned __int128 desired) {
    return __atomic_compare_exchange_n(AP(unsigned __int128, a), expected, desired, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

/* ---- general registers --------------------------------------------------------------- */
/* 32-bit writes zero-extend; 8- and 16-bit writes leave the other bits alone;
 * ah/ch/dh/bh are bits 8-15 of r[0..3]. */

#define R64(i)  (ctx->r[i])
#define R32(i)  ((uint32_t)ctx->r[i])
#define R16(i)  ((uint16_t)ctx->r[i])
#define R8(i)   ((uint8_t)ctx->r[i])
#define R8H(i)  ((uint8_t)(ctx->r[i] >> 8))
#define W64(i, v) (ctx->r[i] = (uint64_t)(v))
#define W32(i, v) (ctx->r[i] = (uint64_t)(uint32_t)(v))
#define W16(i, v) (ctx->r[i] = (ctx->r[i] & ~UINT64_C(0xFFFF)) | (uint64_t)(uint16_t)(v))
#define W8(i, v)  (ctx->r[i] = (ctx->r[i] & ~UINT64_C(0xFF)) | (uint64_t)(uint8_t)(v))
#define W8H(i, v) (ctx->r[i] = (ctx->r[i] & ~UINT64_C(0xFF00)) | ((uint64_t)(uint8_t)(v) << 8))

/* ---- condition codes (Intel SDM vol. 1, appendix B) ---------------------------------- */

#define CC_O   (ctx->of)
#define CC_NO  (!ctx->of)
#define CC_B   (ctx->cf)
#define CC_AE  (!ctx->cf)
#define CC_E   (ctx->zf)
#define CC_NE  (!ctx->zf)
#define CC_BE  (ctx->cf | ctx->zf)
#define CC_A   (!(ctx->cf | ctx->zf))
#define CC_S   (ctx->sf)
#define CC_NS  (!ctx->sf)
#define CC_P   (ctx->pf)
#define CC_NP  (!ctx->pf)
#define CC_L   (ctx->sf != ctx->of)
#define CC_GE  (ctx->sf == ctx->of)
#define CC_LE  (ctx->zf | (ctx->sf != ctx->of))
#define CC_G   (!ctx->zf & (ctx->sf == ctx->of))

/* ---- flag helpers that several families use, one set per operand size ------------------- */
/* N = bits, T = unsigned type.  The helpers that only one family's translators call are in
 * insns/<family>.h: the rest of the arithmetic in insns/int_arith.h, the shifts in
 * insns/shift_bit.h.  x_add and x_sub are here because xadd, cmpxchg and the string compares
 * compute their flags with them too. */

#define TF_FLAGS(N, T)                                                                         \
TF_INLINE void x_szp##N(Ctx *c, T r) {                                                         \
    c->zf = (uint8_t)(r == 0);                                                                 \
    c->sf = (uint8_t)((r >> (N - 1)) & 1);                                                     \
    c->pf = (uint8_t)!__builtin_parity((uint8_t)r);   /* PF: low byte only */                  \
}                                                                                              \
TF_INLINE T x_add##N(Ctx *c, T a, T b) {                                                       \
    T r = (T)(a + b);                                                                          \
    c->cf = (uint8_t)(r < a);                                                                  \
    c->of = (uint8_t)((((T)((a ^ r) & (b ^ r))) >> (N - 1)) & 1);                              \
    c->af = (uint8_t)(((a ^ b ^ r) >> 4) & 1);                                                 \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}                                                                                              \
TF_INLINE T x_sub##N(Ctx *c, T a, T b) {                                                       \
    T r = (T)(a - b);                                                                          \
    c->cf = (uint8_t)(a < b);                                                                  \
    c->of = (uint8_t)((((T)((a ^ b) & (a ^ r))) >> (N - 1)) & 1);                              \
    c->af = (uint8_t)(((a ^ b ^ r) >> 4) & 1);                                                 \
    x_szp##N(c, r);                                                                            \
    return r;                                                                                  \
}

TF_FLAGS(8,  uint8_t)
TF_FLAGS(16, uint16_t)
TF_FLAGS(32, uint32_t)
TF_FLAGS(64, uint64_t)

/* ---- leaving a lifted function (DESIGN.md section 6) ---------------------------------- */

enum { TF_CALL_DIRECT = 1, TF_CALL_INDIRECT = 2, TF_TAIL_DIRECT = 3, TF_TAIL_INDIRECT = 4,
       TF_CPUID = 5, TF_RDTSC = 6 };

void rt_cpuid(Ctx *TF_NOESCAPE ctx);
void rt_rdtsc(Ctx *TF_NOESCAPE ctx);

#ifdef HARNESS_ISOLATED

/* Record (kind, target, rsp, rcx, rdx, r8, r9, rax, r10, r11, the 8 bytes at rsp,
 * xmm0-xmm5), apply the stub effect, pop the return address.  For a tail call the popped
 * address is remembered as the function's final return address. */
void h_stub(Ctx *ctx, int kind, uint64_t target);
void h_ret(Ctx *ctx, uint64_t return_address);
_Noreturn void h_trap(Ctx *ctx, uint32_t rva);
extern uint64_t h_sentinel;

#define TF_PUSH64(v) do { ctx->r[4] -= 8; STS64(ctx->r[4], (v)); } while (0)

#define CALL_DIRECT(fn, R, ret_rva) \
    do { TF_PUSH64(BASE + (ret_rva)); h_stub(ctx, TF_CALL_DIRECT, BASE + (R)); } while (0)
#define CALL_INDIRECT(target, ret_rva) \
    do { uint64_t t_ = (target); TF_PUSH64(BASE + (ret_rva)); h_stub(ctx, TF_CALL_INDIRECT, t_); } while (0)
#define TAIL_DIRECT(fn, R) \
    do { h_stub(ctx, TF_TAIL_DIRECT, BASE + (R)); return; } while (0)
#define TAIL_INDIRECT(target) \
    do { h_stub(ctx, TF_TAIL_INDIRECT, (target)); return; } while (0)
#define RET(n) \
    do { uint64_t ra_ = LDS64(ctx->r[4]); ctx->r[4] += 8 + (n); h_ret(ctx, ra_); return; } while (0)
#define TRAP(rva) \
    do { h_trap(ctx, (rva)); } while (0)
/* An instruction the lifter does not translate, inside a function it lifted (see the linked
 * form below).  In the function test the case ends there like at a trap, and the oracle,
 * which is told where the stops are, ends it at the same instruction: the state before the
 * instruction is compared (harness/difftest.py, UNTRANSLATED_WHAT). */
#define UNTRANSLATED(rva, what, needs) \
    do { h_trap(ctx, (rva)); } while (0)
/* Instruction test only: the synthetic end of a one-instruction function. */
#define INSN_END() \
    do { h_ret(ctx, h_sentinel); return; } while (0)

#else /* really linked */

/* The host function for a guest code address: a lifted function, a shim, a trap stub.  Never
 * NULL: for an address that has no function it returns one that reports the miss and stops
 * (or it stops itself), so the caller always calls the result.  It only looks up: the guest
 * return address is already on the guest stack.  Defined by whoever links the lifted code:
 * the runtime (runtime/core/dispatch.c), tests/linked_main.c, bench/driver.c. */
LiftedFn rt_dispatch_fn(Ctx *TF_NOESCAPE ctx, uint64_t guest_target);
_Noreturn void rt_trap(Ctx *TF_NOESCAPE ctx, uint64_t guest_address);
/* Lifted code called, directly or through the table, the function at `guest_address`, which
 * the lifter skipped: `why` is the reason (LiftedSkip.why) and `why_address` the guest address
 * of the instruction it is about.  The first line of the report names all three.  Entered
 * like any function: the guest return address is on top of the guest stack. */
_Noreturn void rt_trap_skipped(Ctx *TF_NOESCAPE ctx, uint64_t guest_address, const char *why, uint64_t why_address);
/* Lifted code arrived at the instruction at `guest_address`, which the lifter decoded and
 * has no translation for; the function it is in was lifted all the same (lifter/stops.py,
 * DESIGN.md section 3).  The instruction has not run and nothing after it will: this is a
 * stop.  `what` is the lifter's name for the instruction and why it has no translation
 * ("xgetbv", "movq (opcode f6f /0 is not a form the lifter translates)").  `needs` lists the
 * CPUID feature bits a processor must report to execute it, as `NAME=LEAF.REG.BIT` items
 * separated by spaces (LEAF in hex; "OSXSAVE=1.ecx.27"), or "" if the lifter knows of none:
 * whoever links the lifted code and answers CPUID can tell from it whether a processor
 * answering alike would have raised #UD here (then the stop stands for that fault) or would
 * have executed the instruction (then the stop is the lifter's gap). */
_Noreturn void rt_trap_untranslated(Ctx *TF_NOESCAPE ctx, uint64_t guest_address, const char *what, const char *needs);

/* What the two indirect macros look a target up with.  A build that has a faster lookup with
 * the same contract (the runtime's inline rt_lookup) defines TF_LOOKUP before this header. */
#ifndef TF_LOOKUP
#define TF_LOOKUP(ctx, target) rt_dispatch_fn((ctx), (target))
#endif

#define TF_PUSH64(v) do { ctx->r[4] -= 8; STS64(ctx->r[4], (v)); } while (0)

/* RET returns to the host caller whatever the guest return address is, so a call site checks
 * that the callee came back the way a guest `ret` to that site does (issue 49, RUNTIME.md
 * section 2): rsp has the value it had before the return address was pushed, and the slot
 * under it still holds that address.  The first fails after a longjmp, an unwind, a stack
 * switch, a `ret n` the caller did not expect, or a `push addr; ret`; the second when the
 * callee overwrote its return address in place.  Either way the guest would have gone
 * somewhere this host return did not, and rt_rsp_mismatch reports the call site and stops.
 * Nothing may write the guest stack under rsp between the callee's return and this check; a
 * shim keeps to that because its thunk pops the return address last (rt/shim.h).  A tail
 * call pushes nothing and checks nothing: it returns to the call site that started the chain.
 * This is the default; -DTF_NO_RSP_CHECK builds the unchecked macros. */
#ifndef TF_NO_RSP_CHECK
_Noreturn void rt_rsp_mismatch(Ctx *TF_NOESCAPE ctx, uint64_t return_address, uint64_t expected_rsp);
#define RT_SAVE_RSP() const uint64_t rsp_ = ctx->r[4]
#define RT_CHECK_RSP(ret_rva)                                                                  \
    do {                                                                                       \
        if (__builtin_expect(ctx->r[4] != rsp_ || LDS64(rsp_ - 8) != BASE + (ret_rva), 0))      \
            rt_rsp_mismatch(ctx, BASE + (ret_rva), rsp_);                                      \
    } while (0)
#else
#define RT_SAVE_RSP() do { } while (0)
#define RT_CHECK_RSP(ret_rva) do { } while (0)
#endif

/* FLAGS NOBODY READS.  Just before every call, tail call and return the six arithmetic flags are
 * overwritten with zeros.  By the calling convention no code reads a flag that was set on the
 * other side of a call or return (checked on the game's own bytes: 0 functions read a flag at
 * their entry before setting it, and the 2 sites that read one after a call are data decoded as
 * code: docs/scout/flags-across-calls.md), so nothing can see the zeros; and with them the
 * compiler can delete every flag computation that no instruction of the same function reads
 * (docs/findings/25-lifted-speed.md: 2.87 to 2.48 times native on the corpus, with `restrict`).
 * The direction flag is state, not a result: it is left alone.  The isolated build of the
 * function test (above) keeps exact flags at its end, where the oracle is compared.
 * -DTF_KEEP_FLAGS_AT_CALLS builds without this. */
#ifndef TF_KEEP_FLAGS_AT_CALLS
#define TF_KILL_FLAGS() do { __builtin_memset(&ctx->cf, 0, 6); } while (0)
#else
#define TF_KILL_FLAGS() do { } while (0)
#endif

#define CALL_DIRECT(fn, R, ret_rva) \
    do { RT_SAVE_RSP(); TF_PUSH64(BASE + (ret_rva)); TF_KILL_FLAGS(); ((LiftedFnNE)(fn))(ctx); RT_CHECK_RSP(ret_rva); } while (0)
#define CALL_INDIRECT(target, ret_rva) \
    do { uint64_t t_ = (target); RT_SAVE_RSP(); TF_PUSH64(BASE + (ret_rva)); TF_KILL_FLAGS(); ((LiftedFnNE)TF_LOOKUP(ctx, t_))(ctx); \
         RT_CHECK_RSP(ret_rva); } while (0)
/* A guest tail call must not grow the host stack, or a guest loop made of tail calls would
 * overflow it: musttail makes the compiler emit a jump at every optimisation level, and
 * refuses to compile where it cannot.  It can always be honoured because everything a guest
 * address resolves to has the type void (Ctx *) (issue 49). */
#define TAIL_DIRECT(fn, R) \
    do { TF_KILL_FLAGS(); __attribute__((musttail)) return ((LiftedFnNE)(fn))(ctx); } while (0)
#ifndef TF_MUTANT_NESTED_TAIL_INDIRECT
#define TAIL_INDIRECT(target) \
    do { uint64_t tt_ = (target); TF_KILL_FLAGS(); __attribute__((musttail)) return ((LiftedFnNE)TF_LOOKUP(ctx, tt_))(ctx); } while (0)
#else   /* tests/run_selftests.py only: the nested host call this macro used to be */
#define TAIL_INDIRECT(target) \
    do { uint64_t tt_ = (target); TF_KILL_FLAGS(); ((LiftedFnNE)TF_LOOKUP(ctx, tt_))(ctx); return; } while (0)
#endif
#define RET(n) \
    do { ctx->r[4] += 8 + (n); TF_KILL_FLAGS(); return; } while (0)
/* A TRAP DOES NOT READ THE REGISTER FILE.  A trap is a stop: the guest's own instruction would
 * have faulted (a misaligned operand, a divide error, int3, ud2) and the run ends with a report.
 * Called as rt_trap(ctx, ...) it could read every register and flag, so before each of the many
 * places that can trap (every aligned vector load and store has one) the compiler had to have
 * the whole register file in memory, on the path that does not trap too.  rt_trap_lazy takes no
 * `ctx`: whoever links the lifted code finds the calling thread's register file itself.  The
 * price is in the report of such a stop only: registers and flags written since the last call or
 * return may not be in the Ctx yet, and the report says so.  -DTF_EXACT_TRAPS builds the exact
 * form (rebuild with it to study a trap).  Measured on client_0063.c: 55,332 to 51,507
 * instructions (docs/findings/25-lifted-speed.md). */
#ifndef TF_EXACT_TRAPS
_Noreturn void rt_trap_lazy(uint64_t guest_address);
#define TRAP(rva) \
    do { rt_trap_lazy(BASE + (rva)); } while (0)
#else
#define TRAP(rva) \
    do { rt_trap(ctx, BASE + (rva)); } while (0)
#endif
#define UNTRANSLATED(rva, what, needs) \
    do { rt_trap_untranslated(ctx, BASE + (rva), (what), (needs)); } while (0)
#define INSN_END() \
    do { return; } while (0)

#endif

#endif /* TF_RUNTIME_H */
