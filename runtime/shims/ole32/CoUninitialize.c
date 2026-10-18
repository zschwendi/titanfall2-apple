/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/combaseapi/nf-combaseapi-couninitialize
 * RT-CASES:  after S_OK and S_FALSE, one call leaves the thread initialised in its model; a second call uninitialises the thread, which may then take either model; a call on a thread without an initialisation is logged at warn level and does nothing; a preset last error is left alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/thread.h"

SHIM(ole32, void, CoUninitialize)
{
    /* The core drops one initialisation of the calling thread, and logs (and does nothing
     * more) on a thread whose count is 0 (rt/thread.h).  The function returns nothing and
     * the page gives no failure, so last error is not touched. */
    rt_com_uninitialize(ctx);
}
