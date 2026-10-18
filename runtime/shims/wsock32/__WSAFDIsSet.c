/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winsock2/nf-winsock2-__wsafdisset
 * RT-CASES:  an empty set gives 0; a socket in the set gives 1; a socket not in the set gives 0; an entry beyond fd_count is not found; a value that differs only in bit 32 is not found; does not change last error
 * RT-TRAPS:  set NULL; fd_count above 64
 */
#include "rt/shim.h"
#include "rt/win/net.h"

SHIM_ORD(wsock32, 151, int, __WSAFDIsSet, SOCKET fd, FD_SET *set)
{
    /* What Windows does with a NULL set, or with a count above FD_SETSIZE, is not verified. */
    if (!set) RT_UNIMPLEMENTED(ctx, "__WSAFDIsSet: set NULL");
    if (set->fd_count > RT_WS_FD_SETSIZE)
        RT_UNIMPLEMENTED(ctx, "__WSAFDIsSet: fd_count above 64 (fd_count %u)", set->fd_count);

    /* FD_ISSET: 1 if fd equals one of the first fd_count entries, all 64 bits compared; else 0. */
    for (UINT i = 0; i < set->fd_count; i++)
        if (set->fd_array[i] == fd) return 1;
    return 0;
}
