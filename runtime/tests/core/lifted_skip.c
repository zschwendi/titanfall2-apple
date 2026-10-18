/* A function the lifter skipped (docs/RUNTIME.md appendix B item 2): the module's table has
 * a row for it, a stub that stops the run with the lifter's reason, so a call by address (a
 * vtable slot, an export, another module's import slot) ends in the same report as a direct
 * call and not in a dispatch miss.  rt/module.h (RtLiftedCode) says how a reader tells the
 * rows apart; runtime/core/trap.c has the report. */
#include <stdio.h>
#include <string.h>

#include "rt/test_pe.h"
#include "../../core/loader.h"

#define CORPUS_REL "bin/x64_retail/corpus_O1.dll"

TEST(core, lifted_skip_report)
{
    /* What every such stub ends in (harness/runtime.h): the function, that the lifter
     * skipped it, why and where, on the first line; then the lines of any trap. */
    RtModule *m = rt_module_find("rttest.dll");
    T_REQUIRE(m != NULL);
    char first[256];
    snprintf(first, sizeof first, "FATAL call to rttest.dll+0x777 (%#llx), a function the lifter skipped: "
             "unsupported: movnti at rttest.dll+0x780\n  the function has no translation",
             (unsigned long long)(m->base + 0x777));
    T_TRAPS(rt_trap_skipped(T_CTX(), m->base + 0x777, "unsupported: movnti", m->base + 0x780), first);
    T_TRAPS(rt_trap_skipped(T_CTX(), m->base + 0x777, "unsupported: movnti", m->base + 0x780), "\n  rcx=");
    T_TRAPS(rt_trap_skipped(NULL, m->base + 0x777, NULL, 0), "a function the lifter skipped: (no reason given) at 0\n");
    T_TRAPS(rt_trap_skipped(NULL, m->base + 0x777, NULL, 0), "\n  (no guest thread)\n");
}

TEST_SETUP(core, lifted_skip_corpus) { rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL); }
TEST(core, lifted_skip_corpus)
{
    RtModule *m = rt_module_find("corpus_O1.dll");
    T_REQUIRE(m != NULL && !m->native);
    RtLdrModule *lm = rt_ldr_module(m);
    const RtLiftedCode *code = lm->code;
    rt_ldr_image_map(lm);
    rt_ldr_image_protect(lm);

    /* The two numbers: *count rows, of which *nskips are stubs.  Every skipped function has
     * a row, and nothing else is reported as skipped. */
    T_TRUE(*code->nskips < *code->count && *code->count - *code->nskips > 100);
    size_t stubs = 0;
    for (size_t i = 0; i < *code->count; i++) stubs += rt_lifted_skip(code, code->table[i].rva) != NULL;
    T_EQ(stubs, *code->nskips);
    T_TRUE(rt_lifted_skip(code, 0) == NULL && rt_lifted_skip(code, (uint32_t)m->size) == NULL);
    uint64_t fib = rt_loader_export(m, "fib", 0);
    T_REQUIRE(fib != 0);
    T_TRUE(rt_lifted_skip(code, (uint32_t)(fib - m->base)) == NULL);            /* lifted: loader_corpus_call runs it */

    /* An export the lifter skipped, called by address.  `dispatch` (tests/corpus/calls.c)
     * ends in a jump through a table of code addresses, which the lifter does not resolve;
     * should it learn to, any other skipped function of the corpus serves. */
    uint64_t target = rt_loader_export(m, "dispatch", 0);
    T_REQUIRE(target != 0);
    const LiftedSkip *skip = rt_lifted_skip(code, (uint32_t)(target - m->base));
    if (!skip && *code->nskips) {
        skip = &code->skips[0];
        target = m->base + skip->rva;
    }
    T_REQUIRE(skip != NULL);                 /* the corpus has no skipped function left: this test needs another subject */
    T_TRUE(skip->why != NULL && strchr(skip->why, ':') != NULL && skip->at >= skip->rva && skip->at < m->size);
    T_TRUE(rt_disp_find(target) != NULL);    /* a dispatch key like any function of the table */

    char first[512];
    snprintf(first, sizeof first, "FATAL call to corpus_o1.dll+%#x (%#llx), a function the lifter skipped: %s at corpus_o1.dll+%#x\n",
             skip->rva, (unsigned long long)target, skip->why, skip->at);
    const char *detail = NULL;
    RtTestArg a[3] = { { RT_AC_U32, 4, { .u = 1 } }, { RT_AC_U32, 4, { .u = 20 } }, { RT_AC_U32, 4, { .u = 22 } } };
    T_TRAPS(rt_test_invoke(target, 3, a, &detail), first);
    T_TRAPS(rt_test_invoke(target, 3, a, &detail), "  called from rttest.dll!rt_test_return   thread 0x");
    T_TRAPS(rt_test_invoke(target, 3, a, &detail), "\"\n  rcx=");                 /* the registers, as in any trap */
    /* A tail jump through a slot reaches the same report. */
    T_TRAPS(rt_dispatch(T_CTX(), target), first);
    /* An address inside the function is no function start: still a dispatch miss. */
    T_TRAPS(rt_dispatch(T_CTX(), target + 1), "FATAL dispatch miss: call to guest address");
    T_TRAPS(rt_dispatch(T_CTX(), target + 1), "it is not the start of a function the lifter found");
}
