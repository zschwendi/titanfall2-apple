/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-getcpinfo
 * RT-CASES:  1252 and CP_ACP fill the CPINFO fields; 437 and CP_OEMCP fill the CPINFO fields; CP_THREAD_ACP fills the CPINFO fields as 1252; 20127 and 28591 fill the CPINFO fields; CP_UTF8 returns TRUE and MaxCharSize 4; NULL fails with ERROR_INVALID_PARAMETER; a code page the machine lacks fails with ERROR_INVALID_PARAMETER and writes nothing; success leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, BOOL, GetCPInfo, UINT CodePage, LPCPINFO lpCPInfo)
{
    /* The core's answer: NULL, and any code page the machine lacks, fail with
     * ERROR_INVALID_PARAMETER (set by the core) and write nothing.  CP_UTF8 is answered too
     * (TRUE, MaxCharSize 4); the core's UTF-8 values are Windows's: Wine locale.c test_GetCPInfo (locale.h). */
    return rt_cp_info(CodePage, lpCPInfo);
}
