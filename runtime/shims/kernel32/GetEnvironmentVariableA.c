/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-getenvironmentvariablea
 * RT-CASES:  a value is copied and its length returned, last error left alone; the name in another case finds it; the size query (NULL, 0) and a buffer one byte too small return the length plus one and leave a preset buffer alone, last error left alone; a variable that does not exist (0, ERROR_ENVVAR_NOT_FOUND, buffer untouched); a variable set to the empty string (0 and last error left alone, after a preset of 0x1234)
 * RT-TRAPS:  core: the variable name is NULL; core: contains '=' after its first character; core: which is not ASCII; core: NULL buffer of 16 bytes for a value of 5
 */
#include "rt/shim.h"
#include "rt/env.h"

SHIM(kernel32, DWORD, GetEnvironmentVariableA, LPCSTR lpName, LPSTR lpBuffer, DWORD nSize)
{
    /* The core stops by itself on a NULL name, a '=' after the first character of the name, a non-ASCII
     * byte in the name or in the value found, and a NULL buffer when the value fits; the shim adds no guard
     * for these.  The core gives the Win32 error in err; the shim sets it only on failure. */
    DWORD err = 0;
    DWORD n = rt_env_get_a(lpName, lpBuffer, nSize, &err);

    if (err) return FAIL(ctx, err, 0);
    return n;
}
