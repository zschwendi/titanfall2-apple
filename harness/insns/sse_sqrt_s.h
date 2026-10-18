/* SSE scalar square root (sqrtss, sqrtsd). No helpers of its own: the translation is tf_fsqrt32
   and tf_fsqrt64 of runtime.h, the host's fsqrt written out (no libm, so no <math.h>). */
#ifndef TF_INSNS_SSE_SQRT_S_H
#define TF_INSNS_SSE_SQRT_S_H

#include "../runtime.h"

#endif
