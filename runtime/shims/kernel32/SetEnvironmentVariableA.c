/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processenv/nf-processenv-setenvironmentvariablea
 * RT-CASES:  sets a new variable and reads it back with its value; replaces the value of an existing variable, matching the name without regard to case; keeps the spelling and the place of an existing variable and adds a new one at the end of the block; a NULL value deletes the variable; deleting a variable that does not exist fails with ERROR_ENVVAR_NOT_FOUND; an empty value sets the variable to the empty string and it still exists; a value of RT_ENV_VALUE_MAX characters is accepted and one more fails with ERROR_INVALID_PARAMETER; an empty name with a value fails with ERROR_INVALID_PARAMETER; an empty name with NULL fails with ERROR_ENVVAR_NOT_FOUND; a name beginning with '=' is stored like any other; success does not change last error
 * RT-TRAPS:  NULL name; '=' after the first character of the name; a non-ASCII byte in the name; a non-ASCII byte in the value
 */
#include "rt/shim.h"
#include "rt/env.h"

SHIM(kernel32, BOOL, SetEnvironmentVariableA, LPCSTR lpName, LPCSTR lpValue)
{
    /* The documentation does not say what Windows does with a NULL name, a name with '=' after its
     * first character, or bytes that are not ASCII (code pages are the locale unit's business), so
     * the shim refuses them here.  A name may begin with '=' (the per-drive directory variables). */
    const CHAR *p;
    DWORD err = 0;

    if (lpName == NULL) RT_UNIMPLEMENTED(ctx, "SetEnvironmentVariableA with a NULL name");
    for (p = lpName; *p; p++) {
        if ((unsigned char)*p >= 0x80)
            RT_UNIMPLEMENTED(ctx, "SetEnvironmentVariableA with a non-ASCII byte in the name: %#x", (unsigned)(unsigned char)*p);
        if (*p == '=' && p != lpName)
            RT_UNIMPLEMENTED(ctx, "SetEnvironmentVariableA with '=' after the first character of the name: index %u",
                             (unsigned)(p - lpName));
    }
    if (lpValue)
        for (p = lpValue; *p; p++)
            if ((unsigned char)*p >= 0x80)
                RT_UNIMPLEMENTED(ctx, "SetEnvironmentVariableA with a non-ASCII byte in the value: %#x",
                                 (unsigned)(unsigned char)*p);

    /* The core returns the Win32 error it gives in err; the shim sets it only on failure. */
    if (rt_env_set_a(lpName, lpValue, &err)) return TRUE;
    return FAIL(ctx, err, FALSE);
}
