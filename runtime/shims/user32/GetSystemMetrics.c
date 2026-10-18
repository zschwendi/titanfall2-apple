/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getsystemmetrics
 * RT-CASES:  every metric the core answers, with its value; does not change last error
 * RT-TRAPS:  a metric the core does not answer
 */
#include "rt/shim.h"
#include "rt/window.h"

SHIM(user32, INT, GetSystemMetrics, INT nIndex)
{
    /* The core answers the metrics this runtime has a value for and gives up on every other
     * index (rt/window.h, rt_desktop_metric): a metric is never guessed, so the run stops with
     * the index. The documented failure return of 0 is therefore never produced. */
    INT v;
    if (!rt_desktop_metric(nIndex, &v)) RT_UNIMPLEMENTED(ctx, "a metric the core does not answer: %d", nIndex);
    return v;
}
