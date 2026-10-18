// What do FNEG and FABS do to NaN bit patterns with and without FPCR.AH?  (explains the copysign finding)
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <arm_neon.h>
static uint64_t rd_fpcr(void) { return __builtin_arm_rsr64("fpcr"); }
static void wr_fpcr(uint64_t v) { __builtin_arm_wsr64("fpcr", v); }
__attribute__((noinline)) static uint64_t fneg_d(uint64_t b) { double x, r; memcpy(&x, &b, 8); __asm__("fneg %d0, %d1" : "=w"(r) : "w"(x)); uint64_t o; memcpy(&o, &r, 8); return o; }
__attribute__((noinline)) static uint64_t fabs_d(uint64_t b) { double x, r; memcpy(&x, &b, 8); __asm__("fabs %d0, %d1" : "=w"(r) : "w"(x)); uint64_t o; memcpy(&o, &r, 8); return o; }
__attribute__((noinline)) static uint32_t fneg_s(uint32_t b) { float x, r; memcpy(&x, &b, 4); __asm__("fneg %s0, %s1" : "=w"(r) : "w"(x)); uint32_t o; memcpy(&o, &r, 4); return o; }
__attribute__((noinline)) static uint32_t fabs_s(uint32_t b) { float x, r; memcpy(&x, &b, 4); __asm__("fabs %s0, %s1" : "=w"(r) : "w"(x)); uint32_t o; memcpy(&o, &r, 4); return o; }
__attribute__((noinline)) static uint64_t fneg_2d(uint64_t b) { uint64x2_t v = vdupq_n_u64(b); float64x2_t f = vreinterpretq_f64_u64(v), r; __asm__("fneg %0.2d, %1.2d" : "=w"(r) : "w"(f)); return vgetq_lane_u64(vreinterpretq_u64_f64(r), 0); }
__attribute__((noinline)) static uint32_t fneg_4s(uint32_t b) { uint32x4_t v = vdupq_n_u32(b); float32x4_t f = vreinterpretq_f32_u32(v), r; __asm__("fneg %0.4s, %1.4s" : "=w"(r) : "w"(f)); return vgetq_lane_u32(vreinterpretq_u32_f32(r), 0); }
__attribute__((noinline)) static uint64_t fabs_2d(uint64_t b) { uint64x2_t v = vdupq_n_u64(b); float64x2_t f = vreinterpretq_f64_u64(v), r; __asm__("fabs %0.2d, %1.2d" : "=w"(r) : "w"(f)); return vgetq_lane_u64(vreinterpretq_u64_f64(r), 0); }
__attribute__((noinline)) static double cs(double x, double y) { return __builtin_copysign(x, y); }
int main(void) {
    uint64_t base = rd_fpcr();
    const uint64_t d[] = {0xffffffffffffffffull, 0x7ff0000000000001ull, 0x7ff8000000000000ull, 0xfff8000000000000ull, 0x7ff8000000000123ull, 0x3ff0000000000000ull, 0x7ff0000000000000ull, 0};
    const uint32_t s[] = {0xffffffffu, 0x7f800001u, 0x7fc00000u, 0xffc00000u, 0x7fc00123u, 0x3f800000u, 0x7f800000u, 0};
    for (int m = 0; m < 2; m++) {
        wr_fpcr(base | (m ? 2 : 0));
        printf("FPCR.AH=%d\n", m);
        for (int i = 0; i < 8; i++) printf("  d in=%016llx  fneg=%016llx fabs=%016llx fneg.2d=%016llx fabs.2d=%016llx\n", (unsigned long long)d[i], (unsigned long long)fneg_d(d[i]), (unsigned long long)fabs_d(d[i]), (unsigned long long)fneg_2d(d[i]), (unsigned long long)fabs_2d(d[i]));
        for (int i = 0; i < 8; i++) printf("  s in=%08x  fneg=%08x fabs=%08x fneg.4s=%08x\n", s[i], fneg_s(s[i]), fabs_s(s[i]), fneg_4s(s[i]));
        double r = cs(-250.0, 5e10);
        uint64_t u; memcpy(&u, &r, 8);
        printf("  compiled copysign(-250.0, +5e10) = %g (%016llx)\n", r, (unsigned long long)u);
        wr_fpcr(base);
    }
    return 0;
}
