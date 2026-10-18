/* Exceptions and process exit: the structures and constants of winnt.h and winbase.h that
 * RaiseException, UnhandledExceptionFilter, RtlCaptureContext and GetExitCodeProcess use.
 * Guest-visible: every layout here is the x64 one and is asserted.  The core functions over
 * them are in rt/crash.h.
 */
#ifndef RT_WIN_EXCEPTION_H
#define RT_WIN_EXCEPTION_H

#include <stddef.h>

#include "rt/wintypes.h"

#define EXCEPTION_MAXIMUM_PARAMETERS  15

/* EXCEPTION_RECORD.ExceptionFlags */
#define EXCEPTION_NONCONTINUABLE      0x00000001u

/* What an exception filter returns (a LONG). */
#define EXCEPTION_EXECUTE_HANDLER       1
#define EXCEPTION_CONTINUE_SEARCH       0
#define EXCEPTION_CONTINUE_EXECUTION  (-1)

/* GetExitCodeProcess and GetExitCodeThread: the object has not ended. */
#define STILL_ACTIVE                  259u

/* Exception codes (NTSTATUS values, here as DWORD).  rt_exc_code_name (runtime/core/report.h)
 * names exactly these. */
#define EXCEPTION_ACCESS_VIOLATION          0xC0000005u
#define EXCEPTION_IN_PAGE_ERROR             0xC0000006u
#define EXCEPTION_INVALID_HANDLE            0xC0000008u
#define EXCEPTION_ILLEGAL_INSTRUCTION       0xC000001Du
#define EXCEPTION_NONCONTINUABLE_EXCEPTION  0xC0000025u
#define EXCEPTION_INVALID_DISPOSITION       0xC0000026u
#define EXCEPTION_ARRAY_BOUNDS_EXCEEDED     0xC000008Cu
#define EXCEPTION_FLT_DENORMAL_OPERAND      0xC000008Du
#define EXCEPTION_FLT_DIVIDE_BY_ZERO        0xC000008Eu
#define EXCEPTION_FLT_INEXACT_RESULT        0xC000008Fu
#define EXCEPTION_FLT_INVALID_OPERATION     0xC0000090u
#define EXCEPTION_FLT_OVERFLOW              0xC0000091u
#define EXCEPTION_FLT_STACK_CHECK           0xC0000092u
#define EXCEPTION_FLT_UNDERFLOW             0xC0000093u
#define EXCEPTION_INT_DIVIDE_BY_ZERO        0xC0000094u
#define EXCEPTION_INT_OVERFLOW              0xC0000095u
#define EXCEPTION_PRIV_INSTRUCTION          0xC0000096u
#define EXCEPTION_STACK_OVERFLOW            0xC00000FDu
#define STATUS_STACK_BUFFER_OVERRUN         0xC0000409u     /* the C runtime's /GS failure */
#define STATUS_INVALID_CRUNTIME_PARAMETER   0xC0000417u     /* the C runtime's invalid-parameter handler */
#define STATUS_FATAL_APP_EXIT               0x40000015u     /* the C runtime's abort() */
#define EXCEPTION_DATATYPE_MISALIGNMENT     0x80000002u
#define EXCEPTION_BREAKPOINT                0x80000003u
#define EXCEPTION_SINGLE_STEP               0x80000004u
#define EXCEPTION_GUARD_PAGE                0x80000001u

/* Codes that are conventions between a program and a debugger, not failures
 * (docs/RUNTIME.md section 9.3: rt_raise_exception returns for these). */
#define DBG_PRINTEXCEPTION_C                0x40010006u     /* OutputDebugStringA's own raise */
#define DBG_PRINTEXCEPTION_WIDE_C           0x4001000Au     /* OutputDebugStringW's own raise */
#define RT_EXC_THREAD_NAME                  0x406D1388u     /* Visual Studio's "name this thread" */
#define RT_EXC_THREAD_NAME_TYPE             0x1000u         /* THREADNAME_INFO.dwType */

/* Codes the Visual C++ runtime raises. */
#define RT_EXC_CXX                          0xE06D7363u     /* a C++ throw: 0xE0000000 | 'msc' */
#define RT_EXC_CXX_MAGIC                    0x19930520u     /* ExceptionInformation[0] of a throw */
#define RT_EXC_DELAYLOAD_MOD_NOT_FOUND      0xC06D007Eu     /* delay-load helper: LoadLibrary failed */
#define RT_EXC_DELAYLOAD_PROC_NOT_FOUND     0xC06D007Fu     /* delay-load helper: GetProcAddress failed */
#define RT_EXC_DELAYLOAD_INVALID_PARAMETER  0xC06D0057u     /* delay-load helper: bad descriptor */

/* EXCEPTION_RECORD: 152 bytes on x64. */
typedef struct EXCEPTION_RECORD {
    DWORD     ExceptionCode;
    DWORD     ExceptionFlags;
    struct EXCEPTION_RECORD *ExceptionRecord;       /* a chained record, or NULL */
    PVOID     ExceptionAddress;
    DWORD     NumberParameters;
    DWORD     rt_pad;
    ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS];
} EXCEPTION_RECORD;
typedef EXCEPTION_RECORD *PEXCEPTION_RECORD;

/* CONTEXT: 1232 bytes on x64, aligned to 16.  The sixteen integer registers are in the
 * order of Ctx.r[] (rax rcx rdx rbx rsp rbp rsi rdi r8..r15).  The floating-point save
 * area and the vector registers are kept as bytes: XMM_SAVE_AREA32 has MxCsr at +24 and
 * Xmm0..Xmm15 at +160, 16 bytes each. */
#define CONTEXT_AMD64            0x00100000u
#define CONTEXT_CONTROL          (CONTEXT_AMD64 | 0x01u)    /* SegSs Rsp SegCs Rip EFlags */
#define CONTEXT_INTEGER          (CONTEXT_AMD64 | 0x02u)    /* Rax Rcx Rdx Rbx Rbp Rsi Rdi R8-R15 */
#define CONTEXT_SEGMENTS         (CONTEXT_AMD64 | 0x04u)
#define CONTEXT_FLOATING_POINT   (CONTEXT_AMD64 | 0x08u)    /* FltSave (Xmm0-Xmm15, MxCsr) */
#define CONTEXT_FULL             (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)

typedef struct __attribute__((aligned(16))) CONTEXT {
    DWORD64 P1Home, P2Home, P3Home, P4Home, P5Home, P6Home;
    DWORD   ContextFlags;
    DWORD   MxCsr;
    WORD    SegCs, SegDs, SegEs, SegFs, SegGs, SegSs;
    DWORD   EFlags;
    DWORD64 Dr0, Dr1, Dr2, Dr3, Dr6, Dr7;
    DWORD64 Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi, R8, R9, R10, R11, R12, R13, R14, R15;
    DWORD64 Rip;
    BYTE    FltSave[512];
    BYTE    VectorRegister[26 * 16];
    DWORD64 VectorControl;
    DWORD64 DebugControl, LastBranchToRip, LastBranchFromRip, LastExceptionToRip, LastExceptionFromRip;
} CONTEXT;
typedef CONTEXT *PCONTEXT, *LPCONTEXT;

#define RT_CONTEXT_FLTSAVE_MXCSR  24u       /* offsets inside CONTEXT.FltSave */
#define RT_CONTEXT_FLTSAVE_XMM0   160u

typedef struct EXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord;
    PCONTEXT          ContextRecord;
} EXCEPTION_POINTERS;
typedef EXCEPTION_POINTERS *PEXCEPTION_POINTERS, *LPEXCEPTION_POINTERS;

_Static_assert(sizeof(EXCEPTION_RECORD) == 152 && offsetof(EXCEPTION_RECORD, ExceptionAddress) == 16 &&
               offsetof(EXCEPTION_RECORD, NumberParameters) == 24 &&
               offsetof(EXCEPTION_RECORD, ExceptionInformation) == 32, "EXCEPTION_RECORD");
_Static_assert(sizeof(CONTEXT) == 1232 && offsetof(CONTEXT, ContextFlags) == 0x30 &&
               offsetof(CONTEXT, MxCsr) == 0x34 && offsetof(CONTEXT, SegCs) == 0x38 &&
               offsetof(CONTEXT, EFlags) == 0x44 && offsetof(CONTEXT, Dr0) == 0x48 &&
               offsetof(CONTEXT, Rax) == 0x78 && offsetof(CONTEXT, Rsp) == 0x98 &&
               offsetof(CONTEXT, R15) == 0xF0 && offsetof(CONTEXT, Rip) == 0xF8 &&
               offsetof(CONTEXT, FltSave) == 0x100 && offsetof(CONTEXT, VectorRegister) == 0x300 &&
               offsetof(CONTEXT, VectorControl) == 0x4A0, "CONTEXT");
_Static_assert(sizeof(EXCEPTION_POINTERS) == 16, "EXCEPTION_POINTERS");

/* ---- table-based unwinding: what RtlLookupFunctionEntry and RtlVirtualUnwind use.  The
 * layouts are those of "x64 exception handling" (learn.microsoft.com/cpp/build/
 * exception-handling-x64); the core functions over them are in rt/unwind.h. ------------------- */

/* One row of a module's exception directory (.pdata): 12 bytes, three RVAs, rows sorted by
 * BeginAddress.  UnwindData is the RVA of an UNWIND_INFO, which is read as bytes:
 *     +0  version (bits 0-2: 1, or 2) and flags (bits 3-7: UNW_FLAG_*)
 *     +1  size of the prologue in bytes
 *     +2  count of unwind code slots
 *     +3  frame register (bits 0-3; 0 = none) and frame offset / 16 (bits 4-7)
 *     +4  the slots, 2 bytes each: offset in the prologue; operation (bits 0-3), info (bits 4-7)
 *     +4 + 2 * ((count + 1) & ~1)   the handler's RVA and its data (EHANDLER, UHANDLER), or the
 *                                   RUNTIME_FUNCTION of the parent (CHAININFO) */
typedef struct RUNTIME_FUNCTION {
    DWORD BeginAddress, EndAddress, UnwindData;
} RUNTIME_FUNCTION;
typedef RUNTIME_FUNCTION *PRUNTIME_FUNCTION;

#define UNW_FLAG_NHANDLER   0x0u
#define UNW_FLAG_EHANDLER   0x1u
#define UNW_FLAG_UHANDLER   0x2u
#define UNW_FLAG_CHAININFO  0x4u

#define UWOP_PUSH_NONVOL      0     /* 1 slot;  info = register */
#define UWOP_ALLOC_LARGE      1     /* 2 slots (info 0: size / 8) or 3 (info 1: size, 32 bits) */
#define UWOP_ALLOC_SMALL      2     /* 1 slot;  size = info * 8 + 8 */
#define UWOP_SET_FPREG        3     /* 1 slot */
#define UWOP_SAVE_NONVOL      4     /* 2 slots; info = register, offset / 8 */
#define UWOP_SAVE_NONVOL_FAR  5     /* 3 slots; info = register, offset in 32 bits */
#define UWOP_EPILOG           6     /* version 2 only; NOT in the published documentation */
#define UWOP_SPARE_CODE       7     /* version 2 only; NOT in the published documentation */
#define UWOP_SAVE_XMM128      8     /* 2 slots; info = xmm register, offset / 16 */
#define UWOP_SAVE_XMM128_FAR  9     /* 3 slots; info = xmm register, offset in 32 bits */
#define UWOP_PUSH_MACHFRAME   10    /* 1 slot;  info 1 = with an error code */

/* RtlVirtualUnwind's optional last argument: where each non-volatile register the frame
 * saved was read from.  Only the entries of registers that were restored are written. */
typedef struct KNONVOLATILE_CONTEXT_POINTERS {
    PVOID    FloatingContext[16];       /* Xmm0..Xmm15: the address of the 16 saved bytes */
    DWORD64 *IntegerContext[16];        /* in the order of Ctx.r[] */
} KNONVOLATILE_CONTEXT_POINTERS;
typedef KNONVOLATILE_CONTEXT_POINTERS *PKNONVOLATILE_CONTEXT_POINTERS;

/* The remaining types of the Rtl* unwind functions' prototypes.  PEXCEPTION_ROUTINE is a guest
 * code address (a language-specific handler).  The history table is a cache a caller may
 * pass to RtlLookupFunctionEntry and RtlUnwindEx; the runtime never reads or writes one, so
 * it stays an incomplete type. */
typedef DWORD64 *PDWORD64;
typedef GUESTFN  PEXCEPTION_ROUTINE;
typedef struct UNWIND_HISTORY_TABLE *PUNWIND_HISTORY_TABLE;

_Static_assert(sizeof(RUNTIME_FUNCTION) == 12, "RUNTIME_FUNCTION");
_Static_assert(sizeof(KNONVOLATILE_CONTEXT_POINTERS) == 256, "KNONVOLATILE_CONTEXT_POINTERS");

#endif /* RT_WIN_EXCEPTION_H */
