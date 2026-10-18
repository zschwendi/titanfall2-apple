/* RT-STATUS: trap
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-gethostbyname
 * RT-CASES:  none
 * RT-TRAPS:  every call
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 52, HOSTENT *, gethostbyname, const char *name)
{
    RT_UNIMPLEMENTED(ctx, "gethostbyname(%s)", name ? name : "(null)");
}
