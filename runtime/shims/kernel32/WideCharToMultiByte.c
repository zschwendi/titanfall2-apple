/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
 * RT-CASES:  -1 and explicit lengths; size query; buffer one too small; default character and used-default flag; UTF-8 with default character pointers fails; bad flag; UTF-8 surrogate pair becomes four bytes; UTF-8 unpaired surrogate becomes U+FFFD; WC_ERR_INVALID_CHARS fails on an unpaired surrogate; invalid arguments fail with ERROR_INVALID_PARAMETER; success leaves last error alone
 * RT-TRAPS:  core: a character a code page other than 1252 lacks, without WC_NO_BEST_FIT_CHARS; core: WC_DISCARDNS, WC_SEPCHARS or WC_DEFAULTCHAR; core: WC_COMPOSITECHECK with a combining mark; core: partially overlapping buffers
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, int, WideCharToMultiByte, UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar, LPBOOL lpUsedDefaultChar)
{
    /* The core's comment is the specification: it checks the arguments, converts, and sets
     * the last error itself on failure. */
    return rt_wide_to_mb(CodePage, dwFlags, lpWideCharStr, cchWideChar, lpMultiByteStr, cbMultiByte,
                         lpDefaultChar, lpUsedDefaultChar);
}
