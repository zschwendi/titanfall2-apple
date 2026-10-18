/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
 * RT-CASES:  NUL-terminated input with the terminator converted and counted; explicit length with no terminator written; size query with cchWideChar 0 writes nothing; code page 1252 maps high bytes; code page 437 maps high bytes; CP_ACP is code page 1252; CP_UTF8 decodes multi-byte sequences and supplementary characters; buffer one too small gives ERROR_INSUFFICIENT_BUFFER; MB_ERR_INVALID_CHARS on invalid UTF-8 gives ERROR_NO_UNICODE_TRANSLATION; bad flags give ERROR_INVALID_FLAGS; code page 20127 converts ASCII and fails on a high byte with MB_ERR_INVALID_CHARS; invalid parameters give ERROR_INVALID_PARAMETER
 * RT-TRAPS:  core: MB_COMPOSITE; core: MB_USEGLYPHCHARS; core: 20127 high byte without MB_ERR_INVALID_CHARS; core: partially overlapping buffers
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, int, MultiByteToWideChar, UINT CodePage, DWORD dwFlags, LPCCH lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar)
{
    /* The core checks the arguments in the order the documentation gives, sets the last error
     * when it fails and leaves it alone when it succeeds.  It stops (fatal) on the cases in
     * RT-TRAPS, which the documentation settles but the core does not implement, or does not
     * settle. */
    return rt_mb_to_wide(CodePage, dwFlags, lpMultiByteStr, cbMultiByte, lpWideCharStr, cchWideChar);
}
