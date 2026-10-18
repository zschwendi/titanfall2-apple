/* Float mode for lifted code.  Decision of issue #11; measurements in docs/findings/11-float-mode.md.
 *
 * Lifted float instructions are plain C on the Xmm union.  They match SSE bit for bit only while
 * FPCR has AH set and its rounding and flush bits mirror the guest's MXCSR.  AH must not stay on
 * while host code runs: it changes FNEG and FABS on NaN bit patterns, which breaks libm and masks
 * the compiler builds with fneg.  So every crossing between lifted code and host code switches:
 * the harness driver, the runtime's shim thunks, calls from host code into the guest, and the
 * lifted ldmxcsr.  This header is the one place the mapping lives; arm64 only.
 */
#ifndef TF_FPMODE_H
#define TF_FPMODE_H

#include <stdint.h>

#define TF_FPCR_FIZ   (UINT64_C(1) << 0)     /* flush denormal inputs to zero: MXCSR.DAZ */
#define TF_FPCR_AH    (UINT64_C(1) << 1)     /* x86 rules for NaN results, NaN operand choice, flushing */
#define TF_FPCR_FZ    (UINT64_C(1) << 24)    /* flush tiny results to zero: MXCSR.FTZ (only with AH) */
#define TF_FPCR_RMODE_SHIFT 22
#define TF_FPCR_HOST  UINT64_C(0)            /* what macOS gives every new thread */

#define TF_MXCSR_DAZ      (1u << 6)
#define TF_MXCSR_RC_SHIFT 13
#define TF_MXCSR_FTZ      (1u << 15)
#define TF_MXCSR_DEFAULT  0x1F80u
#define TF_FPCW_DEFAULT   0x027Fu     /* x87 control word of a new Windows x64 thread */

static inline uint64_t tf_fpcr_read(void)        { return __builtin_arm_rsr64("fpcr"); }
static inline void     tf_fpcr_write(uint64_t v) { __builtin_arm_wsr64("fpcr", v); }

/* The FPCR value under which plain C reproduces SSE for this MXCSR.  NEP stays off. */
static inline uint64_t tf_fpcr_for_mxcsr(uint32_t mxcsr) {
    static const uint8_t rmode[4] = { 0, 2, 1, 3 };     /* x86 RC: nearest, down, up, toward zero */
    uint64_t v = TF_FPCR_AH | ((uint64_t)rmode[(mxcsr >> TF_MXCSR_RC_SHIFT) & 3] << TF_FPCR_RMODE_SHIFT);
    if (mxcsr & TF_MXCSR_DAZ) v |= TF_FPCR_FIZ;
    if (mxcsr & TF_MXCSR_FTZ) v |= TF_FPCR_FZ;
    return v;
}

static inline void tf_fp_enter_guest(uint32_t mxcsr) { tf_fpcr_write(tf_fpcr_for_mxcsr(mxcsr)); }
static inline void tf_fp_enter_host(void)            { tf_fpcr_write(TF_FPCR_HOST); }

#endif
