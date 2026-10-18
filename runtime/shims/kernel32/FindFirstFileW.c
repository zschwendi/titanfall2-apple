/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-findfirstfilew
 * RT-CASES:  a handle and the first entry, which is ., with its attributes, times and size; the names follow in the core's order, .. first and then the rest sorted by name with a-z folded to A-Z; *.txt gives only the matching file; *.* matches a name without a period; a pattern that matches nothing fails with ERROR_FILE_NOT_FOUND; a directory that does not exist fails with ERROR_PATH_NOT_FOUND; success leaves last error alone; the drive root also lists . and ..
 * RT-TRAPS:  an empty name; a NULL name; a name of MAX_PATH or more WCHARs; core: lpFindFileData NULL; core: a ? in the pattern; core: a pattern ending in .; core: a pattern ending in .* other than *.*; core: a search for . or ..
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, HANDLE, FindFirstFileW, LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData)
{
    /* The documentation says the name should not be NULL or empty, and gives no result for
     * either: not guessed.  It limits the name to MAX_PATH characters by default and says
     * nothing about a longer one (the limit depends on the process's manifest, which is not
     * modelled): not guessed either, so a name of MAX_PATH or more WCHARs is refused.
     * The core does the rest (rt_find_first_w, rt/file.h), including every stop it makes. */
    DWORD n = 0;
    if (!lpFileName) RT_UNIMPLEMENTED(ctx, "FindFirstFileW with a NULL name");
    if (!lpFileName[0]) RT_UNIMPLEMENTED(ctx, "FindFirstFileW with an empty name");
    while (n < MAX_PATH && lpFileName[n]) n++;
    if (n == MAX_PATH) RT_UNIMPLEMENTED(ctx, "FindFirstFileW with a name of MAX_PATH or more WCHARs");
    return rt_find_first_w(lpFileName, lpFindFileData);
}
