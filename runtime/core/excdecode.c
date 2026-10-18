/* Text for an exception code, an EXCEPTION_RECORD and a CONTEXT, for the fatal reports of
 * core/exception.c.  Specification: core/report.h, "core/excdecode.c".  Built: every function
 * is written to that specification.
 *
 * No state.  No lock.  Reads guest memory only after rt_report_readable says it can.
 * Calls out of this file: rt_report_appendf, rt_report_regs, rt_report_readable
 * (core/report.h); rt_module_describe (rt/module.h); LD8, LD32, LD64 (harness/runtime.h).
 * Nothing else: no rt_log, no rt_fatal.
 */
#include <string.h>

#include "report.h"

const char *rt_exc_code_name(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:          return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_IN_PAGE_ERROR:             return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INVALID_HANDLE:            return "EXCEPTION_INVALID_HANDLE";
    case EXCEPTION_ILLEGAL_INSTRUCTION:       return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:  return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
    case EXCEPTION_INVALID_DISPOSITION:       return "EXCEPTION_INVALID_DISPOSITION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:     return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_FLT_DENORMAL_OPERAND:      return "EXCEPTION_FLT_DENORMAL_OPERAND";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:        return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INEXACT_RESULT:        return "EXCEPTION_FLT_INEXACT_RESULT";
    case EXCEPTION_FLT_INVALID_OPERATION:     return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_OVERFLOW:              return "EXCEPTION_FLT_OVERFLOW";
    case EXCEPTION_FLT_STACK_CHECK:           return "EXCEPTION_FLT_STACK_CHECK";
    case EXCEPTION_FLT_UNDERFLOW:             return "EXCEPTION_FLT_UNDERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:              return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_PRIV_INSTRUCTION:          return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_STACK_OVERFLOW:            return "EXCEPTION_STACK_OVERFLOW";
    case STATUS_STACK_BUFFER_OVERRUN:         return "STATUS_STACK_BUFFER_OVERRUN";
    case STATUS_INVALID_CRUNTIME_PARAMETER:   return "STATUS_INVALID_CRUNTIME_PARAMETER";
    case STATUS_FATAL_APP_EXIT:               return "STATUS_FATAL_APP_EXIT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:     return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_BREAKPOINT:                return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_SINGLE_STEP:               return "EXCEPTION_SINGLE_STEP";
    case EXCEPTION_GUARD_PAGE:                return "EXCEPTION_GUARD_PAGE";
    case DBG_PRINTEXCEPTION_C:                return "DBG_PRINTEXCEPTION_C";
    case DBG_PRINTEXCEPTION_WIDE_C:           return "DBG_PRINTEXCEPTION_WIDE_C";
    case RT_EXC_THREAD_NAME:                  return "thread name";
    case RT_EXC_CXX:                          return "C++ exception";
    case RT_EXC_DELAYLOAD_MOD_NOT_FOUND:      return "delay-load: module not found";
    case RT_EXC_DELAYLOAD_PROC_NOT_FOUND:     return "delay-load: procedure not found";
    case RT_EXC_DELAYLOAD_INVALID_PARAMETER:  return "delay-load: invalid parameter";
    default:                                  return NULL;
    }
}

/* Reads a guest string one byte at a time, each byte only after rt_report_readable says it can
 * be read; stops at a NUL, at the first unreadable byte, or after 127 bytes.  Copies the bytes
 * to out (NUL-terminated) and returns their count. */
static size_t read_string(uint64_t addr, char out[128]) {
    size_t k = 0;
    while (k < 127 && rt_report_readable(addr + k, 1)) {
        uint8_t c = (uint8_t)LD8(addr + k);
        if (c == 0) break;
        out[k++] = (char)c;
    }
    out[k] = 0;
    return k;
}

/* The ThrowInfo chain of a C++ throw (see the spec above).  B is the image base, T the
 * ThrowInfo.  True, with the type name in name, when every step can be read. */
static bool throw_type_name(uint64_t B, uint64_t T, char name[128]) {
    if (!rt_report_readable(T, 16)) return false;
    uint64_t A = B + LD32(T + 12);
    if (!rt_report_readable(A, 8)) return false;
    int32_t count = (int32_t)LD32(A);
    uint64_t C = B + LD32(A + 4);
    if (count < 1 || !rt_report_readable(C, 8)) return false;
    uint64_t D = B + LD32(C + 4);
    return read_string(D + 16, name) > 0;
}

static size_t describe_cxx(char *buf, size_t n, size_t at, DWORD nargs, const uint64_t *args) {
    if (nargs < 3 || (uint32_t)args[0] != RT_EXC_CXX_MAGIC)
        return rt_report_appendf(buf, n, at, "  C++ exception with unexpected parameters\n");
    if (args[1] == 0 && args[2] == 0)
        return rt_report_appendf(buf, n, at, "  C++ rethrow (throw;) with no exception object\n");
    uint64_t B = nargs >= 4 ? args[3] : 0;
    char name[128];
    if (throw_type_name(B, args[2], name))
        return rt_report_appendf(buf, n, at, "  C++ exception of type %s (object at 0x%llx)\n", name,
                                 (unsigned long long)args[1]);
    return rt_report_appendf(buf, n, at, "  C++ exception; the type cannot be read (ThrowInfo at 0x%llx)\n",
                             (unsigned long long)args[2]);
}

/* The delay-load helper's exceptions: the code does not change the text. */
static size_t describe_delayload(char *buf, size_t n, size_t at, DWORD nargs, const uint64_t *args) {
    if (nargs < 1 || !rt_report_readable(args[0], 72))
        return rt_report_appendf(buf, n, at, "  delay-load failure; the descriptor cannot be read\n");
    uint64_t I = args[0];
    char dll[128], proc[128];
    read_string(LD64(I + 24), dll);
    const char *d = dll[0] ? dll : "?";
    DWORD last = LD32(I + 64);
    if (LD32(I + 32)) {
        read_string(LD64(I + 40), proc);
        const char *p = proc[0] ? proc : "?";
        return rt_report_appendf(buf, n, at, "  delay-load of %s!%s failed, last error %u\n", d, p, (unsigned)last);
    }
    return rt_report_appendf(buf, n, at, "  delay-load of %s!#%u failed, last error %u\n", d,
                             (unsigned)LD32(I + 40), (unsigned)last);
}

size_t rt_exc_describe(char *buf, size_t n, size_t at, DWORD code, DWORD nargs, const uint64_t *args) {
    if (code == RT_EXC_CXX) return describe_cxx(buf, n, at, nargs, args);
    if (code == RT_EXC_DELAYLOAD_MOD_NOT_FOUND || code == RT_EXC_DELAYLOAD_PROC_NOT_FOUND ||
        code == RT_EXC_DELAYLOAD_INVALID_PARAMETER)
        return describe_delayload(buf, n, at, nargs, args);
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && nargs >= 2) {
        const char *what = args[0] == 0 ? "read" : args[0] == 1 ? "write" : args[0] == 8 ? "execute" : "access";
        return rt_report_appendf(buf, n, at, "  %s of address 0x%016llx\n", what, (unsigned long long)args[1]);
    }
    return at;
}

size_t rt_exc_format_record(char *buf, size_t n, size_t at, uint64_t record) {
    if (record == 0)
        return rt_report_appendf(buf, n, at, "  no exception record\n");
    if (!rt_report_readable(record, 152))
        return rt_report_appendf(buf, n, at, "  exception record at 0x%llx is not readable guest memory\n",
                                 (unsigned long long)record);
    DWORD code = LD32(record + 0), flags = LD32(record + 4);
    uint64_t chained = LD64(record + 8), addr = LD64(record + 16);
    DWORD N = LD32(record + 24);
    if (N > EXCEPTION_MAXIMUM_PARAMETERS) N = EXCEPTION_MAXIMUM_PARAMETERS;
    uint64_t params[EXCEPTION_MAXIMUM_PARAMETERS];
    for (DWORD i = 0; i < N; i++) params[i] = LD64(record + 32 + 8 * (uint64_t)i);
    const char *name = rt_exc_code_name(code);
    char where[160];
    rt_module_describe(addr, where, sizeof where);
    at = rt_report_appendf(buf, n, at, "  exception 0x%08x%s%s%s, flags 0x%x, at %s (0x%llx)\n", (unsigned)code,
                           name ? " (" : "", name ? name : "", name ? ")" : "", (unsigned)flags, where,
                           (unsigned long long)addr);
    at = rt_report_appendf(buf, n, at, "  %u parameters", (unsigned)N);
    if (N) at = rt_report_appendf(buf, n, at, ":");
    for (DWORD i = 0; i < N; i++)
        at = rt_report_appendf(buf, n, at, " 0x%llx", (unsigned long long)params[i]);
    at = rt_report_appendf(buf, n, at, "\n");
    at = rt_exc_describe(buf, n, at, code, N, params);
    if (chained)
        at = rt_report_appendf(buf, n, at, "  chained record at 0x%llx, not printed\n", (unsigned long long)chained);
    return at;
}

size_t rt_exc_format_context(char *buf, size_t n, size_t at, uint64_t context) {
    if (context == 0)
        return rt_report_appendf(buf, n, at, "  no context record\n");
    if (!rt_report_readable(context, 1232))
        return rt_report_appendf(buf, n, at, "  context record at 0x%llx is not readable guest memory\n",
                                 (unsigned long long)context);
    /* Rax, Rcx ... R15 are sixteen consecutive uint64_t from offset 0x78 (win/exception.h). */
    uint64_t r[16];
    for (int i = 0; i < 16; i++) r[i] = LD64(context + 0x78 + 8 * (uint64_t)i);
    uint64_t rip = LD64(context + 0xF8);
    char where[160];
    rt_module_describe(rip, where, sizeof where);
    at = rt_report_appendf(buf, n, at, "  context: rip=%s (0x%016llx) eflags=%08x mxcsr=%08x\n", where,
                           (unsigned long long)rip, (unsigned)LD32(context + 0x44), (unsigned)LD32(context + 0x34));
    return rt_report_regs(buf, n, at, r, "    ");
}
