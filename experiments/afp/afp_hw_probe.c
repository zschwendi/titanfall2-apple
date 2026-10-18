// afp_hw_probe.c - run on a REAL x86-64 CPU (not the Mac): settles the behaviours where Unicorn is
// known or suspected to differ from hardware.  Written but NOT run during the experiment: the only
// machines available there were an arm64 Mac (no Rosetta) and an emulator.
//   gcc -O1 afp_hw_probe.c -o afp_hw_probe && ./afp_hw_probe
// Each line: probe, what hardware returned, then what the emulator and the SDM-based model used by
// afp_lab.py predicted.  A line marked DIFFERS-FROM-ORACLE means afp_lab.py's oracle needs changing.
#include <stdio.h>
#include <stdint.h>

static const uint32_t MX_DEFAULT = 0x1f80, MX_FTZ = 0x1f80 | 0x8000, MX_DAZ = 0x1f80 | 0x40;

#define SS2(INSN, A, B, MX) ({ uint32_t a_ = (A), b_ = (B), r_, m_ = (MX), d_ = MX_DEFAULT; \
    __asm__ volatile("ldmxcsr %[m]\n\tmovd %[a], %%xmm0\n\tmovd %[b], %%xmm1\n\t" INSN " %%xmm1, %%xmm0\n\tmovd %%xmm0, %[r]\n\tldmxcsr %[d]" \
                     : [r] "=r"(r_) : [a] "r"(a_), [b] "r"(b_), [m] "m"(m_), [d] "m"(d_) : "xmm0", "xmm1", "memory"); r_; })
#define SS1(INSN, B, MX) SS2(INSN, 0, B, MX)

static int nbad;
static void probe(const char *name, uint32_t hw, uint32_t emu, uint32_t model) {
    int d = (hw != model);
    nbad += d;
    printf("%-46s hw=%08x  emulator=%08x  model=%08x  %s\n", name, hw, emu, model, d ? "DIFFERS-FROM-ORACLE" : (hw == emu ? "ok" : "ok (emulator differs)"));
}

int main(void) {
    // 1. approximations: the emulator computes the exact quotient
    probe("rcpss(1.0)", SS1("rcpss", 0x3f800000, MX_DEFAULT), 0x3f800000, 0x3f800000);
    probe("rcpss(3.0)", SS1("rcpss", 0x40400000, MX_DEFAULT), 0x3eaaaaab, 0x3eaaaaab);
    probe("rsqrtss(1.0)", SS1("rsqrtss", 0x3f800000, MX_DEFAULT), 0x3f800000, 0x3f800000);
    probe("rsqrtss(2.0)", SS1("rsqrtss", 0x40000000, MX_DEFAULT), 0x3f3504f3, 0x3f3504f3);
    // frecpe under AH on the M3 gives 3f7ff000 / 3f7ff000 for 1.0; compare hw above with those
    // 2. FTZ is ignored by Unicorn; model flushes tiny results (tininess after rounding)
    probe("mulss 2^-126*0.5, FTZ=1", SS2("mulss", 0x00800000, 0x3f000000, MX_FTZ), 0x00400000, 0x00000000);
    probe("mulss 1b6a9cc0*248bab31 (0.99999998*2^-126), FTZ=1", SS2("mulss", 0x1b6a9cc0, 0x248bab31, MX_FTZ), 0x00800000, 0x00800000);
    probe("mulss a1d199b3*1e1c55e9, FTZ=1", SS2("mulss", 0xa1d199b3, 0x1e1c55e9, MX_FTZ), 0x80800000, 0x80800000);
    probe("addss denormal+denormal, FTZ=1 (exact result)", SS2("addss", 0x00000001, 0x00000001, MX_FTZ), 0x00000002, 0x00000000);
    // 3. DAZ: minss returns the original denormal or a flushed zero?
    probe("minss(1e-45, +inf), DAZ=1", SS2("minss", 0x00000001, 0x7f800000, MX_DAZ), 0x00000001, 0x00000001);
    probe("minss(+inf, 1e-45), DAZ=1", SS2("minss", 0x7f800000, 0x00000001, MX_DAZ), 0x00000001, 0x00000001);
    probe("maxss(1e-45, -inf), DAZ=1", SS2("maxss", 0x00000001, 0xff800000, MX_DAZ), 0x00000001, 0x00000001);
    // 4. NaN selection with two NaN operands (SDM table 4-7: first source operand, quieted)
    probe("addss(sNaN 7f800001, qNaN 7fe27b08)", SS2("addss", 0x7f800001, 0x7fe27b08, MX_DEFAULT), 0x7fe27b08, 0x7fc00001);
    probe("addss(sNaN 7f800002, sNaN 7f800004)", SS2("addss", 0x7f800002, 0x7f800004, MX_DEFAULT), 0x7fc00004, 0x7fc00002);
    probe("mulss(qNaN 7fc00009, qNaN 7fc00005)", SS2("mulss", 0x7fc00009, 0x7fc00005, MX_DEFAULT), 0x7fc00009, 0x7fc00009);
    probe("addss(1.0, qNaN 7fc00005)", SS2("addss", 0x3f800000, 0x7fc00005, MX_DEFAULT), 0x7fc00005, 0x7fc00005);
    probe("minss(sNaN 7f800001, 1.0)  [returns 2nd operand]", SS2("minss", 0x7f800001, 0x3f800000, MX_DEFAULT), 0x3f800000, 0x3f800000);
    probe("minss(1.0, sNaN 7f800001)  [2nd operand, not quieted]", SS2("minss", 0x3f800000, 0x7f800001, MX_DEFAULT), 0x7f800001, 0x7f800001);
    // 5. default NaN (real indefinite)
    probe("sqrtss(-1.0) default NaN", SS1("sqrtss", 0xbf800000, MX_DEFAULT), 0xffc00000, 0xffc00000);
    printf("%d probe(s) differ from the oracle used by afp_lab.py\n", nbad);
    return nbad != 0;
}
