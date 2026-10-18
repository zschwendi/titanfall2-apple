/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnls/nf-winnls-isvalidcodepage
 * RT-CASES:  1252, 437, 20127, 28591 and 65001 are valid; 0 and the other special values are not valid; code pages the machine lacks are not valid; success and failure leave last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/locale.h"

SHIM(kernel32, BOOL, IsValidCodePage, UINT CodePage)
{
    /* The core's answer.  "Valid" means this machine can convert the code page: TRUE for the
     * five it converts, FALSE for the rest (the special values included), with one log line on
     * the import channel the first time a code page is refused.  The core never sets last error
     * for this call, and neither does the shim. */
    return rt_cp_is_valid(CodePage);
}
