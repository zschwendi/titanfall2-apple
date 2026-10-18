/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-gettemppatha
 * RT-CASES:  returns the temporary directory with its trailing backslash, and the length without the terminator; a buffer one byte too small returns the length plus one and keeps every byte; a size query, a size of 0 with a NULL buffer, returns the length plus one; a buffer of exactly the length plus one holds the text; does not change last error
 * RT-TRAPS:  core: no buffer
 */
#include "rt/shim.h"
#include "rt/path.h"

/* The core answers every size: a buffer too small is left alone and the size is returned.
 * A NULL buffer that would hold the text is fatal in the core (rt_path_known_a); the shim
 * adds no guard for it, so that case is `core: no buffer` in RT-TRAPS. */
SHIM(kernel32, DWORD, GetTempPathA, DWORD nBufferLength, LPSTR lpBuffer)
{
    return rt_path_known_a(RT_KNOWN_TEMP, lpBuffer, nBufferLength);
}
