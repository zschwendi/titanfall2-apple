/* The call trace (docs/RUNTIME.md section 9.5).
 *
 * When the `trace` channel is on, every generated thunk prints one line on entry and one on
 * return, with the arguments formatted from the SHIM declaration and the caller as
 * module+rva.  The shape follows Wine's relay log so the two can be compared after
 * normalising addresses:
 *
 *     trace: 1004:Call kernel32.SetLastError(00000006) ret=tier0.dll+0x1a40
 *     trace: 1004:Ret  kernel32.SetLastError() retval=void ret=tier0.dll+0x1a40
 *
 * Integers narrower than 64 bits are printed at their declared width (the rest of the
 * register is undefined), strings are printed after their address.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core.h"

#define STR_MAX 64      /* characters of a string argument that are printed */

static size_t put(char *buf, size_t n, size_t at, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static size_t put(char *buf, size_t n, size_t at, const char *fmt, ...) {
    if (at >= n) return at;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + at, n - at, fmt, ap);
    va_end(ap);
    if (w < 0) return at;
    return at + (size_t)w < n ? at + (size_t)w : n - 1;
}

static size_t put_char(char *buf, size_t n, size_t at, unsigned c) {
    if (c == '"' || c == '\\') return put(buf, n, at, "\\%c", (int)c);
    if (c >= 0x20 && c < 0x7F) return put(buf, n, at, "%c", (int)c);
    if (c == '\n') return put(buf, n, at, "\\n");
    if (c == '\r') return put(buf, n, at, "\\r");
    if (c == '\t') return put(buf, n, at, "\\t");
    return c < 0x100 ? put(buf, n, at, "\\x%02x", c) : put(buf, n, at, "\\u%04x", c);
}

static size_t put_value(char *buf, size_t n, size_t at, uint8_t cls, uint64_t bits) {
    switch (cls) {
    case RT_AC_I8: case RT_AC_U8:   return put(buf, n, at, "%02x", (unsigned)(uint8_t)bits);
    case RT_AC_I16: case RT_AC_U16: return put(buf, n, at, "%04x", (unsigned)(uint16_t)bits);
    case RT_AC_I32: case RT_AC_U32: return put(buf, n, at, "%08x", (uint32_t)bits);
    case RT_AC_F32: { float f; uint32_t b = (uint32_t)bits; memcpy(&f, &b, 4); return put(buf, n, at, "%g", (double)f); }
    case RT_AC_F64: { double d; memcpy(&d, &bits, 8); return put(buf, n, at, "%g", d); }
    case RT_AC_STR:
        at = put(buf, n, at, "%08llx", (unsigned long long)bits);
        if (bits) {
            const uint8_t *s = (const uint8_t *)(uintptr_t)bits;
            at = put(buf, n, at, " \"");
            int i = 0;
            for (; i < STR_MAX && s[i]; i++) at = put_char(buf, n, at, s[i]);
            at = put(buf, n, at, s[i] ? "\"..." : "\"");
        }
        return at;
    case RT_AC_WSTR:
        at = put(buf, n, at, "%08llx", (unsigned long long)bits);
        if (bits) {
            at = put(buf, n, at, " L\"");
            int i = 0;
            for (; i < STR_MAX && LD16(bits + 2 * (uint64_t)i); i++) at = put_char(buf, n, at, LD16(bits + 2 * (uint64_t)i));
            at = put(buf, n, at, LD16(bits + 2 * (uint64_t)i) ? "\"..." : "\"");
        }
        return at;
    default:                        return put(buf, n, at, "%08llx", (unsigned long long)bits);
    }
}

/* The bits of argument i as the callee receives them (section 3.1). */
static uint64_t arg_bits(Ctx *ctx, const RtShimFrame *f, uint8_t cls, unsigned i) {
    if (i < 4 && (cls == RT_AC_F32 || cls == RT_AC_F64)) return ctx->xmm[i].q[0];
    static const uint8_t reg_of[4] = { 1, 2, 8, 9 };
    if (i < 4) return ctx->r[reg_of[i]];
    return LD64(f->entry_rsp + 0x28 + 8 * (uint64_t)(i - 4));
}

void rt_trace_call(Ctx *ctx, const RtShimFrame *f) {
    char buf[2048], where[160];
    const RtShimEntry *e = f->shim;
    size_t at = put(buf, sizeof buf, 0, "%04x:Call %s.%s(", RT_THREAD(ctx)->id, e->lib, e->name);
    if (e->raw) {
        at = put(buf, sizeof buf, at, "rcx=%08llx,rdx=%08llx,r8=%08llx,r9=%08llx,...",
                 (unsigned long long)ctx->r[1], (unsigned long long)ctx->r[2],
                 (unsigned long long)ctx->r[8], (unsigned long long)ctx->r[9]);
    } else {
        unsigned shift = e->ret == RT_AC_STRUCT ? 1 : 0;        /* hidden struct-return pointer */
        for (unsigned i = 0; i < e->nargs; i++) {
            if (i) at = put(buf, sizeof buf, at, ",");
            at = put_value(buf, sizeof buf, at, e->args[i], arg_bits(ctx, f, e->args[i], i + shift));
        }
    }
    put(buf, sizeof buf, at, ") ret=%s", rt_module_describe(LD64(f->entry_rsp), where, sizeof where));
    rt_log(RT_CH_TRACE, RT_LL_DEBUG, "%s", buf);
}

void rt_trace_ret(Ctx *ctx, const RtShimFrame *f) {
    char buf[512], where[160];
    const RtShimEntry *e = f->shim;
    size_t at = put(buf, sizeof buf, 0, "%04x:Ret  %s.%s() retval=", RT_THREAD(ctx)->id, e->lib, e->name);
    if (e->ret == RT_AC_VOID && !e->raw) at = put(buf, sizeof buf, at, "void");
    else if (e->ret == RT_AC_F32 || e->ret == RT_AC_F64) at = put_value(buf, sizeof buf, at, e->ret, ctx->xmm[0].q[0]);
    else at = put_value(buf, sizeof buf, at, e->raw ? RT_AC_U64 : (e->ret == RT_AC_STR || e->ret == RT_AC_WSTR ? RT_AC_PTR : e->ret), ctx->r[0]);
    put(buf, sizeof buf, at, " ret=%s", rt_module_describe(LD64(f->entry_rsp), where, sizeof where));
    rt_log(RT_CH_TRACE, RT_LL_DEBUG, "%s", buf);
}
