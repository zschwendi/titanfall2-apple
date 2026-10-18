/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock/nf-winsock-gethostname
 * RT-CASES:  returns 0 and the computer name with its terminator; a namelen one short of the name and its terminator fails with WSAEFAULT; a NULL buffer fails with WSAEFAULT; a call before the start fails with WSANOTINITIALISED
 * RT-TRAPS:  core: COMPUTERNAME not set; core: COMPUTERNAME 256 bytes or longer
 */
#include "rt/shim.h"
#include "rt/net.h"

SHIM_ORD(wsock32, 57, int, gethostname, char *name, int namelen)
{
    /* The argument checks (NULL buffer, namelen too small, start count) and the failure codes
     * are the core's: it sets last error itself and leaves the buffer unchanged on failure. */
    return rt_net_gethostname(name, namelen);
}
