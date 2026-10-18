#include <string.h>
#include <stdint.h>
#include "rt/test.h"
#include "rt/win/exception.h"

/* Fresh inputs for one step: the stack holds the return address and three zeros, and the
 * context is at pc = base + 0x104, inside the function. */
static void reset(CONTEXT *c, uint64_t *stack, DWORD64 base)
{
    stack[0] = 0x1234567890;
    stack[1] = 0;
    stack[2] = 0;
    stack[3] = 0;
    memset(c, 0, sizeof *c);
    c->Rsp = (DWORD64)(uintptr_t)stack;
    c->Rip = base + 0x104;
}

/* The "image" is zeroed memory.  The function is [0x100, 0x110): sixteen nops at 0x100 (not an
 * epilogue), and version 1 unwind information at 0x200 with no prologue, no codes and no frame
 * register.  The test only reads these bytes; the core writes the context and the frame. */
TEST(kernel32, RtlVirtualUnwind)
{
    static uint8_t img[0x300];
    static const uint8_t unwind[4] = { 0x01, 0x00, 0x00, 0x00 };
    DWORD64 base = (DWORD64)(uintptr_t)img;
    RUNTIME_FUNCTION rf = { 0x100, 0x110, 0x200 };
    uint64_t stack[4];
    CONTEXT c;
    KNONVOLATILE_CONTEXT_POINTERS ptrs, zero;
    PVOID hd, mark = (PVOID)(uintptr_t)0x5A5A;
    DWORD64 ef;
    uint64_t r;
    uint8_t before[0x300];
    unsigned t;

    memset(img + 0x100, 0x90, 16);
    memcpy(img + 0x200, unwind, sizeof unwind);
    memset(&zero, 0, sizeof zero);

    T_CASE("a function with no frame register and no allocation: Rip is the return address, Rsp moves past it, the establisher frame is the Rsp before the step, and the result is NULL");
    reset(&c, stack, base);
    hd = mark;
    ef = 0xEF;
    r = CALL(kernel32, RtlVirtualUnwind, 0, base, c.Rip, &rf, &c, &hd, &ef, NULL);
    T_TRUE(r == 0);
    T_EQ(c.Rip, 0x1234567890);
    T_EQ(c.Rsp, (DWORD64)(uintptr_t)(stack + 1));
    T_EQ(ef, (DWORD64)(uintptr_t)stack);
    T_TRUE(hd == mark);

    T_CASE("the rest of the context and the handler data are not written");
    reset(&c, stack, base);
    memcpy(before, img, sizeof img);
    CONTEXT saved = c;
    hd = mark;
    ef = 0xEF;
    r = CALL(kernel32, RtlVirtualUnwind, 0, base, c.Rip, &rf, &c, &hd, &ef, NULL);
    T_TRUE(r == 0);
    saved.Rip = 0x1234567890;
    saved.Rsp = (DWORD64)(uintptr_t)(stack + 1);
    T_TRUE(memcmp(&saved, &c, sizeof c) == 0);
    T_TRUE(hd == mark);
    T_EQ(stack[1], 0);
    T_EQ(stack[2], 0);
    T_EQ(stack[3], 0);
    T_TRUE(memcmp(before, img, sizeof img) == 0);

    T_CASE("HandlerType 0, 1 and 2 are all accepted");
    for (t = 0; t <= 2; t++) {
        reset(&c, stack, base);
        hd = mark;
        ef = 0xEF;
        r = CALL(kernel32, RtlVirtualUnwind, t, base, c.Rip, &rf, &c, &hd, &ef, NULL);
        T_TRUE(r == 0);                             /* the entry has no handler of any kind */
        T_EQ(c.Rip, 0x1234567890);
        T_EQ(c.Rsp, (DWORD64)(uintptr_t)(stack + 1));
        T_TRUE(hd == mark);
    }

    T_CASE("ContextPointers may be NULL, and when given none of its entries is written");
    reset(&c, stack, base);
    hd = mark;
    ef = 0xEF;
    r = CALL(kernel32, RtlVirtualUnwind, 0, base, c.Rip, &rf, &c, &hd, &ef, NULL);
    T_TRUE(r == 0);
    T_EQ(c.Rip, 0x1234567890);
    memset(&ptrs, 0, sizeof ptrs);
    reset(&c, stack, base);
    hd = mark;
    ef = 0xEF;
    r = CALL(kernel32, RtlVirtualUnwind, 0, base, c.Rip, &rf, &c, &hd, &ef, &ptrs);
    T_TRUE(r == 0);
    T_EQ(c.Rip, 0x1234567890);
    T_TRUE(memcmp(&ptrs, &zero, sizeof ptrs) == 0);

    T_CASE("does not change last error");
    reset(&c, stack, base);
    hd = mark;
    ef = 0xEF;
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(kernel32, RtlVirtualUnwind, 0, base, c.Rip, &rf, &c, &hd, &ef, NULL);
    T_TRUE(r == 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stops: each with the other arguments valid. */
    reset(&c, stack, base);
    hd = mark;
    ef = 0xEF;
    T_TRAPS(CALL(kernel32, RtlVirtualUnwind, 0, base, c.Rip, NULL, &c, &hd, &ef, NULL), "NULL FunctionEntry");
    T_TRAPS(CALL(kernel32, RtlVirtualUnwind, 0, base, c.Rip, &rf, NULL, &hd, &ef, NULL), "NULL ContextRecord");
    T_TRAPS(CALL(kernel32, RtlVirtualUnwind, 0, base, c.Rip, &rf, &c, NULL, &ef, NULL), "NULL HandlerData");
    T_TRAPS(CALL(kernel32, RtlVirtualUnwind, 0, base, c.Rip, &rf, &c, &hd, NULL, NULL), "NULL EstablisherFrame");
    T_TRAPS(CALL(kernel32, RtlVirtualUnwind, 3, base, c.Rip, &rf, &c, &hd, &ef, NULL), "HandlerType above 2");
}
