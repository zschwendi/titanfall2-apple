/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemdirectorya
 * RT-CASES:  returns the length without the terminator and writes the text with its terminator; a buffer one unit too small returns the length plus one and keeps every unit; a NULL buffer with size 0 returns the length plus one; a buffer of exactly the length plus one holds the text; does not change last error
 * RT-TRAPS:  core: no buffer
 */
#include "rt/shim.h"
#include "rt/path.h"

/* The core answers the size query (NULL with 0) and a buffer that is too small, and writes
 * nothing for either. A NULL buffer that would hold the text is fatal in the core; no guard
 * is added here (RT-TRAPS). */
SHIM(kernel32, UINT, GetSystemDirectoryA, LPSTR lpBuffer, UINT uSize)
{
    return rt_path_known_a(RT_KNOWN_SYSTEM, lpBuffer, uSize);
}
