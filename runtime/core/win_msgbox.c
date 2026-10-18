/* MessageBoxA/W with no screen (specification: rt_message_box in rt/window.h).  BUILT.
 * Tests: tests/core/win_msgbox.c and the two shim tests.
 *
 * The text is turned into UTF-8 once (convert), logged line by line, then printed in the
 * fatal report.  Nothing here takes a lock of its own; the log lines are written before
 * the report is claimed, because rt_log may not be called inside one (core/report.h).
 */
#include <stdio.h>
#include <string.h>

#include "report.h"
#include "win_priv.h"

#define MB_TEXT_MAX     16000       /* bytes of converted text that are logged */
#define MB_REPORT_MAX   2000        /* bytes of it that the report repeats */

/* Code page 1252, bytes 0x80 to 0x9F; 0 where the code page defines no character. */
static const uint16_t cp1252_high[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
};

static size_t put_escape(char *out, size_t at, const char *fmt, unsigned v) {
    return at + (size_t)snprintf(out + at, 8, fmt, v);
}

/* Append code point `cp` as UTF-8.  The caller keeps 8 bytes free. */
static size_t put_utf8(char *out, size_t at, uint32_t cp) {
    if (cp < 0x80) { out[at++] = (char)cp; }
    else if (cp < 0x800) { out[at++] = (char)(0xC0 | (cp >> 6)); out[at++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out[at++] = (char)(0xE0 | (cp >> 12)); out[at++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[at++] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[at++] = (char)(0xF0 | (cp >> 18)); out[at++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[at++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[at++] = (char)(0x80 | (cp & 0x3F));
    }
    return at;
}

/* The guest's text as UTF-8 in out[0..cap), NUL-terminated, cut when full: "\r\n" and "\n"
 * become "\n"; a control character other than tab becomes \xNN, a byte code page 1252 does
 * not define likewise, an unpaired surrogate \uNNNN.  NULL text is "(null)". */
static size_t convert(RtText t, char *out, size_t cap) {
    size_t at = 0;
    if (!t.w && !t.a) return (size_t)snprintf(out, cap, "(null)");
    for (size_t i = 0; at + 8 < cap; i++) {
        uint32_t c = t.w ? t.w[i] : (unsigned char)t.a[i];
        uint32_t next = c ? (t.w ? t.w[i + 1] : (unsigned char)t.a[i + 1]) : 0;
        if (c == 0) break;
        if (c == '\r' && next == '\n') continue;
        if (c == '\n' || c == '\t') { out[at++] = (char)c; continue; }
        if (c < 0x20 || c == 0x7F) { at = put_escape(out, at, "\\x%02x", c); continue; }
        if (t.a && c >= 0x80 && c < 0xA0) {
            if (!cp1252_high[c - 0x80]) { at = put_escape(out, at, "\\x%02x", c); continue; }
            c = cp1252_high[c - 0x80];
        } else if (t.w && c >= 0xD800 && c < 0xDC00 && next >= 0xDC00 && next < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (next - 0xDC00);
            i++;
        } else if (t.w && c >= 0xD800 && c < 0xE000) {
            at = put_escape(out, at, "\\u%04x", c);
            continue;
        }
        at = put_utf8(out, at, c);
    }
    out[at] = 0;
    return at;
}

_Noreturn void rt_message_box(Ctx *ctx, HWND owner, RtText text, RtText caption, UINT type) {
    char cap[512], body[MB_TEXT_MAX + 16];
    convert(caption, cap, sizeof cap);
    size_t n = convert(text, body, sizeof body);
    for (char *p = cap; *p; p++) if (*p == '\n') *p = ' ';             /* a caption is one line */

    /* 1. The log: the caption, then one line per line of text. */
    rt_log(RT_CH_TRAP, RT_LL_ERROR, "MessageBox caption: %s", cap);
    for (size_t a = 0; a <= n; ) {
        size_t b = a;
        while (b < n && body[b] != '\n') b++;
        if (b < n || b > a || a == 0)                                   /* no empty line after a final "\n" */
            rt_log(RT_CH_TRAP, RT_LL_ERROR, "MessageBox text: %.*s", (int)(b - a), body + a);
        a = b + 1;
    }

    /* 2. The report. */
    if (rt_report_begin() == RT_REPORT_NESTED) rt_report_abort();
    size_t size;
    char *buf = rt_report_buffer(&size);
    size_t at = rt_report_appendf(buf, size, 0, "FATAL guest called MessageBox: %s\n  text: ", cap);
    for (size_t i = 0; i < n && i < MB_REPORT_MAX; i++)
        at = body[i] == '\n' ? rt_report_appendf(buf, size, at, "\n    ")
                             : rt_report_appendf(buf, size, at, "%c", body[i]);
    if (n > MB_REPORT_MAX) at = rt_report_appendf(buf, size, at, " ... (%u more bytes are in the log)", (unsigned)(n - MB_REPORT_MAX));
    at = rt_report_appendf(buf, size, at, "\n  uType 0x%x, owner window 0x%llx\n", (unsigned)type,
                           (unsigned long long)(uintptr_t)owner);
    RtThread *t = ctx ? RT_THREAD(ctx) : NULL;
    at += rt_report_context(buf + at, size - at, ctx, t && t->shim ? t->shim->entry_rsp : 0);
    rt_log_flush_try();
    rt_log_raw(buf, at);
    rt_report_abort();
}
