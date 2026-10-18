/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/debugapi/nf-debugapi-outputdebugstringw
 * RT-CASES:  the text is logged on the guest.debug channel; one log line per line of text without the line ending; characters of code page 1252 are written as OutputDebugStringA writes them; NULL and the empty string log nothing; a line longer than the buffer is continued on the next log line; does not change last error
 * RT-TRAPS:  a character with no code point in code page 1252
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, void, OutputDebugStringW, LPCWSTR lpOutputString)
{
    /* Microsoft: the text is converted by the system locale to the ANSI code page and then
     * displayed through OutputDebugStringA (RT-DOC).  So each UTF-16 unit becomes its code
     * page 1252 byte (CP_ACP), and the bytes take exactly the rules of OutputDebugStringA:
     * printable ASCII and tab as they are, any other byte as a backslash, x and two hex
     * digits; the line endings; 1000 output characters to a line.  Characters that 1252 has
     * come out as the byte it has for them: U+00E9 as \xe9, U+20AC as \x80.  A character that
     * 1252 does not have stops the run: without WC_NO_BEST_FIT_CHARS the core would stop on it
     * too, and with that flag the default character '?' would be written, which is a guess.
     * Windows maps such characters by best fit (U+0100 becomes 'A'), and the runtime has no
     * best-fit table (RUNTIME 8.4). */
    enum { LINE_MAX_CHARS = 1000 };
    static const char hex[] = "0123456789abcdef";
    const WCHAR *p = lpOutputString;
    char line[1024];
    unsigned n = 0;                                     /* output characters in line[] */

    if (p == NULL) return;
    for (; *p != 0; p++) {
        WCHAR u = *p;
        if (u == '\n' || (u == '\r' && p[1] == '\n')) {          /* p[1] is at worst the terminator */
            if (u == '\r') p++;                                   /* the "\r\n" pair is one line ending */
            line[n] = 0;
            rt_log(RT_CH_GUEST_DEBUG, RT_LL_INFO, "%s", line);
            n = 0;
            continue;
        }
        char b = 0;
        BOOL used = FALSE;
        int wrote = rt_wide_to_mb(CP_ACP, WC_NO_BEST_FIT_CHARS, p, 1, &b, 1, NULL, &used);
        if (wrote != 1) rt_fatal("OutputDebugStringW: converting U+%04X to code page 1252 wrote %d bytes", (unsigned)u, wrote);
        if (used) RT_UNIMPLEMENTED(ctx, "a character with no code point in code page 1252 (U+%04X): Windows converts it by best fit, which the runtime has no table for", (unsigned)u);
        unsigned c = (unsigned char)b;
        unsigned width = (c == '\t' || (c >= 0x20 && c < 0x7F)) ? 1 : 4;
        if (n + width > LINE_MAX_CHARS) {                    /* full: continue on the next log line */
            line[n] = 0;
            rt_log(RT_CH_GUEST_DEBUG, RT_LL_INFO, "%s", line);
            n = 0;
        }
        if (width == 1) {
            line[n++] = (char)c;
        } else {
            line[n++] = 0x5C;                               /* backslash */
            line[n++] = 'x';
            line[n++] = hex[c >> 4];
            line[n++] = hex[c & 15];
        }
    }
    if (n > 0) {                                            /* a last line without a line ending */
        line[n] = 0;
        rt_log(RT_CH_GUEST_DEBUG, RT_LL_INFO, "%s", line);
    }
}
