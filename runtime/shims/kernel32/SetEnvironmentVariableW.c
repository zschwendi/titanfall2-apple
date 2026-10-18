/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-setenvironmentvariablew
 * RT-CASES:  sets a new variable and reads it back with its value; replaces the value of an existing variable, matching the name without regard to case; keeps the spelling and the place of an existing variable and adds a new one at the end of the block; a NULL value deletes the variable; deleting a variable that does not exist fails with ERROR_ENVVAR_NOT_FOUND; an empty value sets the variable to the empty string and it still exists; a value of RT_ENV_VALUE_MAX characters is accepted and one more fails with ERROR_INVALID_PARAMETER; a value of any text, non-ASCII included, is kept exactly; an empty name with a value fails with ERROR_INVALID_PARAMETER; an empty name with NULL fails with ERROR_ENVVAR_NOT_FOUND; a name beginning with '=' is stored like any other; success does not change last error
 * RT-TRAPS:  NULL name; '=' after the first character of the name; a name that is not ASCII
 */
#include "rt/shim.h"
#include "rt/env.h"

SHIM(kernel32, BOOL, SetEnvironmentVariableW, LPCWSTR lpName, LPCWSTR lpValue)
{
    /* The documentation does not say what Windows does with a NULL name, a name with '=' after its
     * first character, or a name that is not ASCII (its case folding is the locale unit's business),
     * so the shim refuses them here.  A name may begin with '=' (the per-drive directory variables).
     * A value is any text and is kept exactly. */
    const WCHAR *p;
    DWORD err = 0;

    if (lpName == NULL) RT_UNIMPLEMENTED(ctx, "SetEnvironmentVariableW with a NULL name");
    for (p = lpName; *p; p++) {
        if (*p >= 0x80)
            RT_UNIMPLEMENTED(ctx, "SetEnvironmentVariableW with a name that is not ASCII: %#x", (unsigned)*p);
        if (*p == '=' && p != lpName)
            RT_UNIMPLEMENTED(ctx, "SetEnvironmentVariableW with '=' after the first character of the name: index %u",
                             (unsigned)(p - lpName));
    }

    /* The core returns the Win32 error it gives in err; the shim sets it only on failure. */
    if (rt_env_set_w(lpName, lpValue, &err)) return TRUE;
    return FAIL(ctx, err, FALSE);
}
