// Compiled with -fmath-errno: clang then emits "fsqrt; if result is NaN call libm sqrtf" so errno
// can be set.  Exposes whether the libm fallback changes the NaN a lifted sqrtss returns.
#include <stdint.h>
#include <math.h>
typedef union { uint8_t b[16]; uint32_t d[4]; uint64_t q[2]; float f[4]; double pd[2]; int32_t i[4]; int64_t l[2]; } Xmm;
void sqrtss__errno(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->f[0]  = sqrtf(s->f[0]); }
void sqrtsd__errno(Xmm *d, const Xmm *s, uint64_t *g) { (void)g; d->pd[0] = sqrt(s->pd[0]); }
