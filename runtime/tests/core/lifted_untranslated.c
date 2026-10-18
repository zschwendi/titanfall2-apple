/* The stop at an instruction the lifter does not translate (harness/runtime.h, UNTRANSLATED;
 * lifter/stops.py; docs/DESIGN.md section 3).  A function that holds such an instruction is
 * lifted, and the lifted code calls rt_trap_untranslated when it arrives there.  What the stop
 * means depends on what this runtime's CPUID answers for the feature the instruction needs:
 * the report must say "a real processor answering like this would fault here" exactly when a
 * needed feature is reported absent, and "a gap of the lifter" otherwise.
 *
 * Where the expected answers come from.  That OSXSAVE, AVX and AES are absent and MMX is
 * present is the runtime's stated description (rt/machine.h: "Reports MMX, SSE, SSE2 and SSE3 and
 * nothing later"; docs/findings/16-cpu-features.md).  That the report agrees with CPUID is
 * checked against rt_cpuid itself, run on a context as the guest would run it. */
#include <stdio.h>
#include <string.h>

#include "rt/machine.h"
#include "rt/test_pe.h"
#include "../../core/loader.h"

#define CORPUS_REL "bin/x64_retail/corpus_O1.dll"

#define FIRST "FATAL lifted code reached an instruction the lifter does not translate: "
#define FAULT "\n  a processor that answers CPUID as this runtime does raises an invalid-opcode fault (#UD) at this instruction"
#define GAP   "the stop is a gap of the lifter (the instruction has no translation), not a fault of the guest\n"

TEST(core, lifted_untranslated_report)
{
    RtModule *m = rt_module_find("rttest.dll");
    T_REQUIRE(m != NULL);
    uint64_t a = m->base + 0x777;
    char first[256];

    /* A feature the runtime reports absent: the stop stands for the processor's own fault. */
    snprintf(first, sizeof first, FIRST "xgetbv at rttest.dll+0x777 (%#llx)\n"
             "  it needs OSXSAVE (CPUID leaf 0x1, ecx bit 27: reported absent)" FAULT, (unsigned long long)a);
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "xgetbv", "OSXSAVE=1.ecx.27"), first);
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "xgetbv", "OSXSAVE=1.ecx.27"),
            "the stop stands where that fault would be, and the guest took a path that a test of the feature should have closed\n");
    /* The lines of any trap follow; inside a function there is no return address to name. */
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "xgetbv", "OSXSAVE=1.ecx.27"),
            "\n  called from (unknown: no return address on the guest stack)");
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "xgetbv", "OSXSAVE=1.ecx.27"), "\n  rcx=");

    /* A feature the runtime reports present: a real processor would execute the instruction. */
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "movq (opcode f6f /0 is not a form the lifter translates)", "MMX=1.edx.23"),
            FIRST "movq (opcode f6f /0 is not a form the lifter translates) at rttest.dll+0x777");
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "movq (opcode f6f /0 is not a form the lifter translates)", "MMX=1.edx.23"),
            "\n  it needs MMX (CPUID leaf 0x1, edx bit 23: reported present)\n"
            "  a processor that answers CPUID as this runtime does executes this instruction: " GAP);

    /* Two features, leaf 7, an extended leaf.  One absent feature is enough for the fault. */
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "vaesenc", "AVX=1.ecx.28 AES=1.ecx.25"),
            "\n  it needs AVX (CPUID leaf 0x1, ecx bit 28: reported absent) and AES (CPUID leaf 0x1, ecx bit 25: reported absent)" FAULT);
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "an invented pair", "SSE2=1.edx.26 AVX2=7.ebx.5"),
            "\n  it needs SSE2 (CPUID leaf 0x1, edx bit 26: reported present) and AVX2 (CPUID leaf 0x7, ebx bit 5: reported absent)" FAULT);
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "vprotd", "XOP=80000001.ecx.11"),
            "\n  it needs XOP (CPUID leaf 0x80000001, ecx bit 11: reported absent)" FAULT);

    /* No feature known to the lifter: taken to be executable, so the lifter's gap. */
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "lock neg", ""),
            FIRST "lock neg at rttest.dll+0x777");
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "lock neg", ""),
            "\n  the lifter knows of no CPUID feature bit that this instruction waits for, so a real processor is taken to "
            "execute it: " GAP);
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "lock neg", NULL), "the lifter knows of no CPUID feature bit");

    /* What the runtime cannot answer is said, not guessed: neither "fault" nor "gap". */
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "x", "FUTURE=40000000.eax.0"),
            "\n  it needs FUTURE (CPUID leaf 0x40000000, eax bit 0: no answer from this runtime)\n"
            "  whether a processor answering CPUID as this runtime does would execute it cannot be said from that");
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "x", "not a feature item"), "a feature this report cannot read (\"not a feature item\")");
    T_TRAPS(rt_trap_untranslated(T_CTX(), a, "x", "not a feature item"), "cannot be said from that");
    T_TRAPS(rt_trap_untranslated(NULL, a, NULL, NULL), "does not translate: (not named) at rttest.dll+0x777");
    T_TRAPS(rt_trap_untranslated(NULL, a, NULL, NULL), "\n  (no guest thread)\n");
}

/* CPUID as the guest runs it: leaf in rax, subleaf in rcx; eax ebx ecx edx in out[0..3]. */
static void guest_cpuid(uint32_t leaf, uint32_t out[4]) {
    Ctx c;
    memset(&c, 0, sizeof c);
    c.r[0] = leaf;
    rt_cpuid(&c);
    out[0] = (uint32_t)c.r[0];
    out[1] = (uint32_t)c.r[3];
    out[2] = (uint32_t)c.r[1];
    out[3] = (uint32_t)c.r[2];
}

TEST(core, lifted_untranslated_agrees_with_cpuid)
{
    /* Every bit the lifter's table names (lifter/stops.py, CPUID_BITS): the report says
     * "present" exactly when the guest's own cpuid shows the bit set, and draws the fault
     * conclusion exactly when it is clear.  Were a bit to change in runtime/core/machine.c,
     * the report would follow; this test is what holds the two together. */
    static const struct { const char *name; uint32_t leaf; unsigned reg, bit; } bits[] = {
        {"MMX", 1, 3, 23}, {"SSE", 1, 3, 25}, {"SSE2", 1, 3, 26},
        {"SSE3", 1, 2, 0}, {"PCLMULQDQ", 1, 2, 1}, {"SSSE3", 1, 2, 9}, {"FMA", 1, 2, 12}, {"SSE4.1", 1, 2, 19},
        {"SSE4.2", 1, 2, 20}, {"MOVBE", 1, 2, 22}, {"POPCNT", 1, 2, 23}, {"AES", 1, 2, 25}, {"OSXSAVE", 1, 2, 27},
        {"AVX", 1, 2, 28}, {"F16C", 1, 2, 29}, {"RDRAND", 1, 2, 30},
        {"BMI1", 7, 1, 3}, {"AVX2", 7, 1, 5}, {"BMI2", 7, 1, 8}, {"AVX512F", 7, 1, 16}, {"RDSEED", 7, 1, 18},
        {"ADX", 7, 1, 19}, {"SHA", 7, 1, 29},
        {"LAHF-SAHF", 0x80000001u, 2, 0},
        {"SSE4A", 0x80000001u, 2, 6}, {"XOP", 0x80000001u, 2, 11}, {"FMA4", 0x80000001u, 2, 16},
        {"TBM", 0x80000001u, 2, 21}, {"3DNow", 0x80000001u, 3, 31},
    };
    static const char *const regs[4] = { "eax", "ebx", "ecx", "edx" };
    RtModule *m = rt_module_find("rttest.dll");
    T_REQUIRE(m != NULL);
    unsigned present = 0;
    for (size_t i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        uint32_t out[4];
        char needs[64], want[160];
        guest_cpuid(bits[i].leaf, out);
        int set = (out[bits[i].reg] >> bits[i].bit) & 1;
        T_EQ(rt_machine_cpuid_bit(bits[i].leaf, 0, bits[i].reg, bits[i].bit), set);
        snprintf(needs, sizeof needs, "%s=%x.%s.%u", bits[i].name, bits[i].leaf, regs[bits[i].reg], bits[i].bit);
        snprintf(want, sizeof want, "  it needs %s (CPUID leaf %#x, %s bit %u: reported %s)\n  a processor that answers CPUID "
                 "as this runtime does %s", bits[i].name, bits[i].leaf, regs[bits[i].reg], bits[i].bit,
                 set ? "present" : "absent", set ? "executes this instruction" : "raises an invalid-opcode fault (#UD)");
        T_TRAPS(rt_trap_untranslated(T_CTX(), m->base + 0x777, "x", needs), want);
        present += (unsigned)set;
    }
    /* The description of record: MMX, SSE, SSE2 and SSE3 and nothing later. */
    T_EQ(present, 4u);
    T_EQ(rt_machine_cpuid_bit(1, 0, 2, 0), 1);
    T_EQ(rt_machine_cpuid_bit(1, 0, 2, 9), 0);
    T_EQ(rt_machine_cpuid_bit(1, 0, 3, 23), 1);
    T_EQ(rt_machine_cpuid_bit(1, 0, 2, 27), 0);
    /* No answer: a leaf the runtime does not describe, a register or bit that does not exist. */
    T_EQ(rt_machine_cpuid_bit(0x40000000u, 0, 0, 0), -1);
    T_EQ(rt_machine_cpuid_bit(1, 0, 4, 0), -1);
    T_EQ(rt_machine_cpuid_bit(1, 0, 3, 32), -1);
}

/* The whole way: lifted code of the test corpus arrives at an instruction without a
 * translation.  The functions are tests/corpus/intrare.c's, each one instruction in a wrapper:
 *   ir_movbe32   `movbe`, which waits for a feature this runtime reports absent
 *   ir_rcl1      `bt` (translated, and executed) and then `rcl`, for which the lifter knows no
 *                feature bit
 * Should the lifter learn one of them the call returns and T_TRAPS says so: take another
 * export of that file whose instruction the lift log lists under "lifted with stops". */
TEST_SETUP(core, lifted_untranslated_corpus) { rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL); }
TEST(core, lifted_untranslated_corpus)
{
    RtModule *m = rt_module_find("corpus_O1.dll");
    T_REQUIRE(m != NULL && !m->native);
    RtLdrModule *lm = rt_ldr_module(m);
    const RtLiftedCode *code = lm->code;
    rt_ldr_image_map(lm);
    rt_ldr_image_protect(lm);

    uint64_t movbe = rt_loader_export(m, "ir_movbe32", 0), rcl = rt_loader_export(m, "ir_rcl1", 0);
    T_REQUIRE(movbe != 0 && rcl != 0);
    /* Both are lifted functions with rows of their own, not stubs of skipped ones. */
    T_TRUE(rt_lifted_skip(code, (uint32_t)(movbe - m->base)) == NULL);
    T_TRUE(rt_lifted_skip(code, (uint32_t)(rcl - m->base)) == NULL);

    static uint32_t word = 0x11223344;
    const char *detail = NULL;
    char first[256];
    RtTestArg p[1] = { { RT_AC_PTR, 8, { .u = (uint64_t)(uintptr_t)&word } } };
    /* movbe is the function's first instruction (it has nothing to set up). */
    snprintf(first, sizeof first, FIRST "movbe at corpus_o1.dll+%#llx (%#llx)\n"
             "  it needs MOVBE (CPUID leaf 0x1, ecx bit 22: reported absent)" FAULT,
             (unsigned long long)(movbe - m->base), (unsigned long long)movbe);
    T_TRAPS(rt_test_invoke(movbe, 1, p, &detail), first);
    T_TRAPS(rt_test_invoke(movbe, 1, p, &detail), "\n  called from (unknown: no return address on the guest stack)   thread 0x");
    T_TRAPS(rt_test_invoke(movbe, 1, p, &detail), "\n  rcx=");
    /* The same through a tail jump to the function. */
    T_TRAPS(rt_dispatch(T_CTX(), movbe), first);

    RtTestArg a[2] = { { RT_AC_U32, 4, { .u = 0x80000001u } }, { RT_AC_U32, 4, { .u = 1 } } };
    T_TRAPS(rt_test_invoke(rcl, 2, a, &detail), FIRST "rcl at corpus_o1.dll+");
    T_TRAPS(rt_test_invoke(rcl, 2, a, &detail),
            "\n  the lifter knows of no CPUID feature bit that this instruction waits for, so a real processor is taken to "
            "execute it: " GAP);
}
