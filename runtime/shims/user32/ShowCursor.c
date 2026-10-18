/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-showcursor
 * RT-CASES:  counts FALSE down and TRUE up from 0, returning the new counter; does not change last error on success
 * RT-TRAPS:  a bShow that is neither TRUE nor FALSE
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, INT, ShowCursor, BOOL bShow)
{
    /* The documentation settles TRUE and FALSE only; any other value is not guessed at. */
    if (bShow != TRUE && bShow != FALSE) RT_UNIMPLEMENTED(ctx, "a bShow that is neither TRUE nor FALSE: %d", bShow);
    return rt_cursor_show(bShow);
}
