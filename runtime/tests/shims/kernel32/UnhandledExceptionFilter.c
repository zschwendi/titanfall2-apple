#include "rt/test.h"
#include "rt/win/exception.h"

/* The structures the guest passes, in one block of guest memory. */
typedef struct {
    EXCEPTION_POINTERS ep;
    EXCEPTION_RECORD rec;
    CONTEXT c;
} UefFrame;

TEST(kernel32, UnhandledExceptionFilter)
{
    UefFrame *f;

    T_CASE("a NULL EXCEPTION_POINTERS gives the fatal report with no record");
    T_TRAPS(CALL(kernel32, UnhandledExceptionFilter, NULL), "FATAL guest called UnhandledExceptionFilter");

    T_CASE("an EXCEPTION_POINTERS gives the record and context in the fatal report, and never returns");
    /* Guest memory (VirtualAlloc), so the core finds the structures readable. */
    f = (UefFrame *)CALL(kernel32, VirtualAlloc, NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    T_REQUIRE(f != NULL);
    memset(f, 0, sizeof *f);
    f->rec.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    f->ep.ExceptionRecord = &f->rec;
    f->ep.ContextRecord = &f->c;
    T_TRAPS(CALL(kernel32, UnhandledExceptionFilter, &f->ep), "FATAL guest called UnhandledExceptionFilter");
}
