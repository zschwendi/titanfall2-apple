/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/debugapi/nf-debugapi-outputdebugstringa
 * RT-CASES:  the text is logged on the guest.debug channel; one log line per line of text without the line ending; bytes outside printable ASCII are escaped; NULL and the empty string log nothing; a line longer than the buffer is continued on the next log line; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(kernel32, void, OutputDebugStringA, LPCSTR lpOutputString)
{
    /* One rt_log call per line of text.  A line ends at "\n" or "\r\n" (not printed); the
     * last line needs no ending but a line ending at the very end adds no empty line.
     * Bytes are code page 1252: anything but printable ASCII and tab is written as the four
     * characters backslash, x and two lower-case hex digits, so the log stays printable.
     * A backslash in the text is printed as is.  The buffer holds at most 1000 output
     * characters; a longer line continues on the next log line, and an escape is never
     * split. */
    enum { LINE_MAX_CHARS = 1000 };
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p = (const unsigned char *)lpOutputString;
    char line[1024];
    unsigned n = 0;                                 /* output characters in line[] */

    if (p == NULL) return;
    for (; *p != 0; p++) {
        unsigned c = *p;
        if (c == '\n' || (c == '\r' && p[1] == '\n')) {     /* p[1] is at worst the terminator */
            if (c == '\r') p++;                             /* the "\r\n" pair is one line ending */
            line[n] = 0;
            rt_log(RT_CH_GUEST_DEBUG, RT_LL_INFO, "%s", line);
            n = 0;
            continue;
        }
        unsigned width = (c == '\t' || (c >= 0x20 && c < 0x7F)) ? 1 : 4;
        if (n + width > LINE_MAX_CHARS) {                   /* full: continue on the next log line */
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
