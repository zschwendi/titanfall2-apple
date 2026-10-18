/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw
 * RT-CASES:  a handle and the first entry, which is ., with its attributes, times and size; FindExInfoBasic gives the same first entry; the names follow in the core's order, .. first and then the rest sorted without case; *.txt gives only the matching file; *.* matches a name without a period; FindExSearchLimitToDirectories is accepted and files are still returned; FIND_FIRST_EX_LARGE_FETCH is accepted and ignored; a pattern that matches nothing fails with ERROR_FILE_NOT_FOUND; a directory that does not exist fails with ERROR_PATH_NOT_FOUND; success leaves last error alone; the drive root also lists . and ..
 * RT-TRAPS:  a NULL name; an empty name; a name of MAX_PATH or more characters; fInfoLevelId other than FindExInfoStandard or FindExInfoBasic; fSearchOp other than FindExSearchNameMatch or FindExSearchLimitToDirectories; lpSearchFilter not NULL; dwAdditionalFlags other than FIND_FIRST_EX_LARGE_FETCH; core: lpFindFileData NULL; core: a ? in the pattern; core: a pattern ending in .;core: a pattern ending in .* other than *.*; core: a search for . or ..
 */
#include "rt/shim.h"
#include "rt/file.h"

SHIM(kernel32, HANDLE, FindFirstFileExW, LPCWSTR lpFileName, FINDEX_INFO_LEVELS fInfoLevelId, LPVOID lpFindFileData, FINDEX_SEARCH_OPS fSearchOp, LPVOID lpSearchFilter, DWORD dwAdditionalFlags)
{
    /* The documentation gives no result for a NULL or an empty name, and says the name should
     * not end in a trailing backslash: not guessed.  It limits the name to MAX_PATH characters
     * by default and says nothing about a longer one (the limit depends on the process's
     * manifest, which is not modelled): not guessed either.  A name ending in a backslash
     * (the documentation says it always fails), or with a `<`, `>` or `"` in it, is refused by
     * the path translation before the core's own stop, so no core case is listed for it.  The
     * extra arguments are decided from
     * the pages of the function and of its enumerations, in the order given there.  The core
     * does the rest (rt_find_first_w, rt/file.h), including every stop it makes. */
    DWORD n = 0;
    if (!lpFileName) RT_UNIMPLEMENTED(ctx, "FindFirstFileExW with a NULL name");
    if (!lpFileName[0]) RT_UNIMPLEMENTED(ctx, "FindFirstFileExW with an empty name");
    while (n < MAX_PATH && lpFileName[n]) n++;
    if (n == MAX_PATH) RT_UNIMPLEMENTED(ctx, "FindFirstFileExW with a name of MAX_PATH or more characters");
    if (fInfoLevelId != FindExInfoStandard && fInfoLevelId != FindExInfoBasic)
        RT_UNIMPLEMENTED(ctx, "FindFirstFileExW with fInfoLevelId other than FindExInfoStandard or FindExInfoBasic: %d", (int)fInfoLevelId);
    if (fSearchOp != FindExSearchNameMatch && fSearchOp != FindExSearchLimitToDirectories)
        RT_UNIMPLEMENTED(ctx, "FindFirstFileExW with fSearchOp other than FindExSearchNameMatch or FindExSearchLimitToDirectories: %d", (int)fSearchOp);
    if (lpSearchFilter) RT_UNIMPLEMENTED(ctx, "FindFirstFileExW with lpSearchFilter not NULL: %p", (void *)lpSearchFilter);
    if (dwAdditionalFlags & ~FIND_FIRST_EX_LARGE_FETCH)
        RT_UNIMPLEMENTED(ctx, "FindFirstFileExW with dwAdditionalFlags other than FIND_FIRST_EX_LARGE_FETCH: 0x%x", (unsigned)dwAdditionalFlags);
    return rt_find_first_w(lpFileName, (WIN32_FIND_DATAW *)lpFindFileData);
}
