#include "rt/test.h"
#include "rt/win/exception.h"

/* True if the n bytes at p all hold v. */
static int all_bytes(const void *p, size_t n, uint8_t v)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i] != v) return 0;
    return 1;
}

/* What the harness does around every CALL (runtime/tests/harness.c, rt_test_invoke): it puts
 * garbage in the volatile registers, the flags and xmm0-5 and in the non-volatile registers
 * (rbx rbp rsi rdi r12-r15) and xmm6-15, and restores the whole Ctx afterwards.  So the values
 * the shim sees there cannot be set by the test or read back by it, and the checks below use
 * only what the core writes from values the test can fix: MxCsr and the control word (not
 * touched by the harness), the segments, ContextFlags and the fields the core leaves alone. */
TEST(kernel32, RtlCaptureContext)
{
    Ctx *ctx = T_CTX();
    CONTEXT c;

    T_CASE("Rip and Rsp are set for the caller and Rcx is the ContextRecord pointer");
    memset(&c, 0xAA, sizeof c);
    CALL(kernel32, RtlCaptureContext, &c);
    T_TRUE(c.Rip != 0 && c.Rip != UINT64_C(0xAAAAAAAAAAAAAAAA));
    T_TRUE(c.Rsp != 0 && c.Rsp != UINT64_C(0xAAAAAAAAAAAAAAAA));
    T_EQ(c.Rcx, (uint64_t)(uintptr_t)&c);          /* rcx is the ContextRecord pointer itself */

    T_CASE("EFlags, segments, ContextFlags, MxCsr and the save area are written as the core specifies, and the fields it does not write keep the caller's bytes");
    ctx->mxcsr = 0x1FA0;
    ctx->fpcw = 0x027F;
    memset(&c, 0xAA, sizeof c);
    CALL(kernel32, RtlCaptureContext, &c);
    /* EFlags: bit 1 and IF are always set, and the only other bits are CF PF AF ZF SF DF OF. */
    T_TRUE((c.EFlags & 0x202u) == 0x202u);
    T_TRUE((c.EFlags & ~0xED7u) == 0);
    T_EQ(c.SegCs, 0x33);
    T_EQ(c.SegDs, 0x2B);
    T_EQ(c.SegEs, 0x2B);
    T_EQ(c.SegFs, 0x53);
    T_EQ(c.SegGs, 0x2B);
    T_EQ(c.SegSs, 0x2B);
    T_EQ(c.ContextFlags, CONTEXT_FULL | CONTEXT_SEGMENTS);
    T_EQ(c.MxCsr, 0x1FA0u);
    {
        /* The save area: control word, MxCsr, MxCsr mask 0xFFFF; the x87 bytes and the reserved
         * bytes are 0 (the core clears them).  Xmm0-15 (bytes 160 to 415) are not checked here. */
        uint16_t cw;
        uint32_t mx, mask;
        memcpy(&cw, c.FltSave, 2);
        memcpy(&mx, c.FltSave + RT_CONTEXT_FLTSAVE_MXCSR, 4);
        memcpy(&mask, c.FltSave + RT_CONTEXT_FLTSAVE_MXCSR + 4, 4);
        T_EQ(cw, 0x027Fu);
        T_EQ(mx, 0x1FA0u);
        T_EQ(mask, 0xFFFFu);
        T_TRUE(all_bytes(c.FltSave + 2, 22, 0));
        T_TRUE(all_bytes(c.FltSave + 32, 128, 0));
        T_TRUE(all_bytes(c.FltSave + RT_CONTEXT_FLTSAVE_XMM0 + 256, sizeof c.FltSave - RT_CONTEXT_FLTSAVE_XMM0 - 256, 0));
    }
    /* Not written, as on Windows: P1Home to P6Home, Dr0 to Dr7, and everything from
     * VectorRegister on (VectorControl and the debug-control fields). */
    T_TRUE(all_bytes(&c, offsetof(CONTEXT, ContextFlags), 0xAA));
    T_TRUE(all_bytes((uint8_t *)&c + offsetof(CONTEXT, Dr0), offsetof(CONTEXT, Rax) - offsetof(CONTEXT, Dr0), 0xAA));
    T_TRUE(all_bytes((uint8_t *)&c + offsetof(CONTEXT, VectorRegister), sizeof c - offsetof(CONTEXT, VectorRegister), 0xAA));

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    memset(&c, 0xAA, sizeof c);
    CALL(kernel32, RtlCaptureContext, &c);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(kernel32, RtlCaptureContext, NULL), "RtlCaptureContext with NULL ContextRecord");
}
