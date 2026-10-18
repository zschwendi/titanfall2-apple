/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-getstringtypew
 * RT-CASES:  CT_CTYPE1 for a letter of each case, a digit, a space and a control character; CT_CTYPE2 and CT_CTYPE3 for one character each; CT_CTYPE3 of a surrogate unit gives the surrogate flags; a length of -1 writes the NUL and nothing after it; a length given writes that many words and no more; ERROR_INVALID_FLAGS for a bit outside the three; ERROR_INVALID_PARAMETER for none or two of the three bits, NULL pointers, length 0 and out equal to src; does not change last error on success
 * RT-TRAPS:  surrogate code unit in CT_CTYPE1 or CT_CTYPE2; code unit the core does not type
 */
#include "rt/shim.h"
#include "rt/locale.h"

/* The core types a unit that rt_wchar_type_known accepts.  CT_CTYPE3 also types a surrogate
 * unit.  Any other unit is fatal in the core, so the shim finds it first and names it. */
static int untyped(DWORD info_type, WCHAR c)
{
    if (rt_wchar_type_known(c))
        return 0;
    if (info_type == CT_CTYPE3 && c >= 0xD800 && c <= 0xDFFF)
        return 0;
    return 1;
}

SHIM(kernel32, BOOL, GetStringTypeW, DWORD dwInfoType, LPCWCH lpSrcStr, int cchSrc, LPWORD lpCharType)
{
    /* Only a call the core answers is scanned.  The documented failures (bad flags, bad
     * parameters) are the core's to return, so they are not preempted here. */
    if ((dwInfoType == CT_CTYPE1 || dwInfoType == CT_CTYPE2 || dwInfoType == CT_CTYPE3) &&
        lpSrcStr && lpCharType && cchSrc != 0 && (const void *)lpSrcStr != (const void *)lpCharType) {
        /* The units counted by cchSrc, or up to and including the NUL when cchSrc is negative. */
        for (int i = 0; cchSrc < 0 || i < cchSrc; i++) {
            WCHAR c = lpSrcStr[i];
            if (untyped(dwInfoType, c)) {
                if (c >= 0xD800 && c <= 0xDFFF)
                    RT_UNIMPLEMENTED(ctx, "GetStringTypeW surrogate code unit in CT_CTYPE1 or CT_CTYPE2 (unit 0x%x)", (unsigned)c);
                else
                    RT_UNIMPLEMENTED(ctx, "GetStringTypeW code unit the core does not type (unit 0x%x)", (unsigned)c);
            }
            if (cchSrc < 0 && c == 0)
                break;
        }
    }
    return rt_string_type(dwInfoType, lpSrcStr, cchSrc, lpCharType);
}
