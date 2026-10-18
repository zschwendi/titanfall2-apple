/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-getfullpathnamea
 * RT-CASES:  a relative name gives the current directory, a separator and the name, and the part points at the name; a name with dot-dot is normalised and the part points at its last component; a name that ends in a separator gives a NULL part; a code page 1252 character comes back as the same byte; lpFilePart NULL is accepted: the length is returned and the buffer is written; a name of 259 characters and a result of 259 characters are accepted; the size query with a zero buffer returns the length plus one and writes nothing; a buffer one byte too small returns the length plus one and writes neither the buffer nor the part; NULL, an empty name and a name of spaces only fail with ERROR_INVALID_NAME and write nothing; success leaves last error alone
 * RT-TRAPS:  a name of MAX_PATH characters or more; a result of MAX_PATH characters or more; lpBuffer NULL with room for the result; a character outside code page 1252 in the result; core: the bare prefix of a device or namespace path; core: two separators with no server name after them; core: a device name with a dot or colon after it
 */
#include "rt/shim.h"
#include "rt/path.h"
#include "rt/locale.h"

SHIM(kernel32, DWORD, GetFullPathNameA, LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR *lpFilePart)
{
    /* The core does the path (rt_path_full_w, rt/path.h): the forms of the name, the
     * normalisation, the sizes, the file part and the error code.  It works in UTF-16 and has
     * no A form (docs/handoff/rt-contract-file.md), so this shim converts the name to UTF-16
     * and the result back to code page 1252, copies it to the guest's buffer and stores the
     * file part.  Code page 1252 is one byte per WCHAR, so lengths and indexes are the same in
     * both forms.
     *
     * A name, or a result, of MAX_PATH characters or more does not fit the buffers here: the
     * shim refuses it (RT_UNIMPLEMENTED), as RT-TRAPS says.  The core would accept a longer one. */
    WCHAR in[MAX_PATH];                 /* the name, NUL included */
    WCHAR out[MAX_PATH];                /* the result, NUL included */
    char narrow[MAX_PATH];              /* the result in code page 1252, NUL included */
    DWORD len = 0, part = RT_PATH_NO_PART, e, i;
    BOOL used = FALSE;
    int n;

    /* A NULL name goes to the core as NULL, which fails it with ERROR_INVALID_NAME. */
    if (lpFileName != NULL) {
        /* A result of 0 means the name, with its NUL, does not fit in MAX_PATH WCHARs. */
        n = rt_mb_to_wide(CP_ACP, 0, lpFileName, -1, in, MAX_PATH);
        if (n == 0) RT_UNIMPLEMENTED(ctx, "GetFullPathNameA with a name of MAX_PATH characters or more: \"%s\"", lpFileName);
    }
    e = rt_path_full_w(lpFileName != NULL ? in : NULL, out, MAX_PATH, &len, &part);
    if (e) return FAIL(ctx, e, 0);

    /* The core writes the result only when it fits out with its NUL: then len is the length
     * without the NUL, at most MAX_PATH - 1.  Otherwise len is the length with the NUL, and
     * the result has MAX_PATH characters or more. */
    if (len > MAX_PATH) RT_UNIMPLEMENTED(ctx, "GetFullPathNameA with a result of MAX_PATH characters or more: %u characters", (len - 1));

    /* The result, NUL included, in code page 1252.  WC_NO_BEST_FIT_CHARS makes a character the
     * page lacks come out as the default character with used set, so it is not written as a
     * look-alike. */
    n = rt_wide_to_mb(CP_ACP, WC_NO_BEST_FIT_CHARS, out, (int)len + 1, narrow, MAX_PATH, NULL, &used);
    if (n == 0 || used) RT_UNIMPLEMENTED(ctx, "GetFullPathNameA with a character outside code page 1252 in the result");

    /* The size query, and a buffer too small: the size with the NUL, and nothing is written. */
    if (nBufferLength < len + 1) return len + 1;

    if (lpBuffer == NULL) RT_UNIMPLEMENTED(ctx, "GetFullPathNameA with lpBuffer NULL with room for the result");
    for (i = 0; i <= len; i++)
        lpBuffer[i] = narrow[i];
    if (lpFilePart != NULL)
        *lpFilePart = (part == RT_PATH_NO_PART) ? NULL : lpBuffer + part;

    /* Last error is left alone on success. */
    return len;
}
