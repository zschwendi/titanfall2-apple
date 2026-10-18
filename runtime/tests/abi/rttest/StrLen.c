/* RT-STATUS: full
 * RT-DOC:    none
 * RT-CASES:  string arguments are ordinary pointers; the call trace prints them
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(rttest, DWORD, StrLen, LPCSTR s, LPCWSTR w)
{
    DWORD n = s ? (DWORD)strlen(s) : 0;
    if (w) while (*w++) n += 100;
    return n;
}
