// Can a normal arm64 macOS process switch on FEAT_AFP (FPCR.AH / FPCR.NEP / FPCR.FIZ), and
// do native float instructions then give x86 SSE results for the cases where plain ARM differs?
//   AH  (bit 1): x86-style NaN propagation, default NaN sign, and FMIN/FMAX like MINSS/MAXSS
//   NEP (bit 2): scalar ops keep the upper lanes of the destination, like SSE scalar ops
//   FIZ (bit 0): flush inputs to zero (x86 DAZ)
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/sysctl.h>
#include <arm_neon.h>

static uint64_t rd_fpcr(void) { return __builtin_arm_rsr64("fpcr"); }
static void wr_fpcr(uint64_t v) { __builtin_arm_wsr64("fpcr", v); }
static float f(uint32_t b) { float x; memcpy(&x, &b, 4); return x; }
static uint32_t u(float x) { uint32_t b; memcpy(&b, &x, 4); return b; }

__attribute__((noinline)) static float op_sub(float a, float b) { float r; __asm__("fsub %s0, %s1, %s2" : [r0] "=w"(r) : [a1] "w"(a), [b2] "w"(b)); (void)0; return r; }
__attribute__((noinline)) static float op_mul(float a, float b) { float r; __asm__("fmul %s0, %s1, %s2" : "=w"(r) : "w"(a), "w"(b)); return r; }
__attribute__((noinline)) static float op_min(float a, float b) { float r; __asm__("fmin %s0, %s1, %s2" : "=w"(r) : "w"(a), "w"(b)); return r; }
__attribute__((noinline)) static float op_max(float a, float b) { float r; __asm__("fmax %s0, %s1, %s2" : "=w"(r) : "w"(a), "w"(b)); return r; }
// fadd s0, s0, s1 on a full vector: do lanes 1..3 of the destination survive?
__attribute__((noinline)) static float32x4_t op_add_merge(float32x4_t d, float32x4_t s) {
    __asm__("fadd %s0, %s0, %s1" : "+w"(d) : "w"(s));
    return d;
}

static void run(const char *label) {
    float inf = f(0x7F800000), qnan = f(0x7FC11111), snan = f(0x7F822222);
    float32x4_t d = {1.0f, 2.0f, 3.0f, 4.0f}, s = {10.0f, 20.0f, 30.0f, 40.0f};
    float32x4_t m = op_add_merge(d, s);
    printf("%-28s fpcr=%#llx\n", label, (unsigned long long)rd_fpcr());
    printf("  inf - inf            = %08x   (x86: ffc00000)\n", u(op_sub(inf, inf)));
    printf("  qnan(1) * snan(2)    = %08x   (x86: 7fc11111, first operand)\n", u(op_mul(qnan, snan)));
    printf("  min(qnan, 1.0)       = %08x   (x86 minss: 3f800000, second operand)\n", u(op_min(qnan, 1.0f)));
    printf("  min(1.0, qnan)       = %08x   (x86 minss: 7fc11111, second operand)\n", u(op_min(1.0f, qnan)));
    printf("  min(+0, -0)          = %08x   (x86 minss: 80000000, second operand)\n", u(op_min(0.0f, f(0x80000000))));
    printf("  max(-0, +0)          = %08x   (x86 maxss: 00000000, second operand)\n", u(op_max(f(0x80000000), 0.0f)));
    printf("  scalar add, lanes 1-3 = %g %g %g   (x86 addss keeps 2 3 4)\n", m[1], m[2], m[3]);
}

int main(void) {
    int v = 0; size_t n = sizeof v;
    sysctlbyname("hw.optional.arm.FEAT_AFP", &v, &n, NULL, 0);
    printf("hw.optional.arm.FEAT_AFP = %d\n", v);
    uint64_t base = rd_fpcr();
    run("default");
    wr_fpcr(base | 2);            run("AH");
    wr_fpcr(base | 2 | 4);        run("AH + NEP");
    wr_fpcr(base | 1 | 2 | 4);    printf("AH + NEP + FIZ reads back fpcr=%#llx\n", (unsigned long long)rd_fpcr());
    wr_fpcr(base);
    return 0;
}
