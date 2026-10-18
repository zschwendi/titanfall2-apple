/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getfullpathnamew
 * RT-CASES:  a relative name gives the current directory, a backslash and the name, and the part points at the name; a name with dot-dot is normalised and the part points at its last component; a name that ends in a separator gives a NULL part; a character outside code page 1252 comes back as it is; lpFilePart NULL is accepted: the length is returned and the buffer is written; the size query with a zero buffer returns the length plus one and writes nothing; a buffer one WCHAR too small returns the length plus one and writes neither the buffer nor the part; NULL, an empty name and a name of spaces only fail with ERROR_INVALID_NAME and write nothing; success leaves last error alone
 * RT-TRAPS:  lpBuffer NULL with a nonzero nBufferLength; core: the bare prefix of a device or namespace path; core: two separators with no server name after them; core: a device name with a dot or colon after it; core: an unpaired surrogate in the name
 */
#include "rt/shim.h"
#include "rt/path.h"

SHIM(kernel32, DWORD, GetFullPathNameW, LPCWSTR lpFileName, DWORD nBufferLength, LPWSTR lpBuffer, LPWSTR *lpFilePart)
{
    /* The core does the path (rt_path_full_w, rt/path.h): the forms of the name, the
     * normalisation, the sizes, the file part and the error code.  The name and the result are
     * UTF-16 already, so the shim passes them through.  The size query (NULL buffer with length
     * 0) is answered; a NULL buffer with a length is not settled by the documentation, so it is
     * refused, as RT-TRAPS says. */
    DWORD len = 0, part = RT_PATH_NO_PART, e;

    if (lpBuffer == NULL && nBufferLength != 0)
        RT_UNIMPLEMENTED(ctx, "GetFullPathNameW with lpBuffer NULL with a nonzero nBufferLength (%u)", (unsigned)nBufferLength);

    e = rt_path_full_w(lpFileName, lpBuffer, nBufferLength, &len, &part);
    if (e) return FAIL(ctx, e, 0);

    /* The core writes the result only when it fits with its terminator, that is when len <
     * nBufferLength; then the file part is stored, or NULL if the result has none.  Otherwise
     * len is the size needed with the terminator, and lpFilePart is not written (planner's
     * choice, as in the A form). */
    if (len < nBufferLength && lpFilePart != NULL)
        *lpFilePart = (part == RT_PATH_NO_PART) ? NULL : lpBuffer + part;

    /* Last error is left alone on success. */
    return len;
}
