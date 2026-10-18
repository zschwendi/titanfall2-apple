/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-expandenvironmentstringsa
 * RT-CASES:  expands a variable that is set and ignores the case of its name; a buffer that holds the result gets it with its terminator and the length of the result plus one is returned; a name that is not set is copied as it is; a percent sign with no later percent sign is copied as it is; a buffer too small or size 0 gets an empty string and the length plus two is returned; a NULL buffer with size 0 returns the length plus two; does not change last error also when the buffer is too small
 * RT-TRAPS:  core: NULL source; core: a byte of 0x80 or more in the source; core: a character of 0x80 or more in a value that is copied; core: a reference to no variable that is followed by the name of one; core: a NULL buffer when size holds the result; core: a source longer than 32767 bytes; core: a result longer than 32767 bytes
 */
#include "rt/shim.h"
#include "rt/env.h"

SHIM(kernel32, DWORD, ExpandEnvironmentStringsA, LPCSTR lpSrc, LPSTR lpDst, DWORD nSize)
{
    /* The core does all of it (rt_env_expand_a, rt/env.h): the expansion, the size rule (the length
     * plus one when the buffer holds the result, else the length plus two with dst[0] set to 0) and
     * the look-ups under the environment's lock.  It cannot fail and sets no last error, so the
     * shim sets none, on success or when the buffer is too small.
     *
     * The core's fatal cases are not guarded here: a NULL source, a byte of 0x80 or more in the
     * source, a character of 0x80 or more in a value that is copied, a reference that is followed
     * by the name of a variable, a NULL buffer when size holds the result, and a source or result
     * longer than 32767 bytes.
     *
     * Not checked: the page says lpDst cannot be the same buffer as lpSrc.  The core builds the
     * result before it writes lpDst and does not look for an overlap. */
    return rt_env_expand_a(lpSrc, lpDst, nSize);
}
