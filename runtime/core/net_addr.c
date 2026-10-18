/* Sockets, names of a socket: bind, getsockname, connect, getpeername.
 * Specification: rt/net.h.  Shared internals: core/net_priv.h (in core/net.c).
 * BUILT (unit rt-net-core).  The test is tests/core/net_addr.c.
 *
 * Shape of every function: rt_net_enter, lock, decide, unlock, then log or stop, unref.
 * Nothing is logged and nothing stops under the lock.
 */
#include <string.h>

#include "net_priv.h"

int rt_net_bind(SOCKET s, const SOCKADDR *name, int namelen) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    DWORD e = 0;
    bool reuse = false;
    BYTE addr[16];
    USHORT port = 0;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (!name || namelen < rt_net_namelen(so->af)) e = WSAEFAULT;
    else if (name->sa_family != so->af) e = WSAEAFNOSUPPORT;
    else if (so->bound) e = WSAEINVAL;
    else {
        rt_net_name_read(so->af, name, addr, &port);
        if (rt_net_addr_kind(so->af, addr) == RT_NA_OTHER) e = WSAEADDRNOTAVAIL;
        else if (port == 0) {
            if (!rt_net_autobind(so)) e = WSAENOBUFS;
            else memcpy(so->local, addr, 16);       /* the address given stays; the port is the one picked */
        } else {
            RtSocket *holder = rt_net_port_holder(so->type, port);
            if (!holder) {
                memcpy(so->local, addr, 16);
                so->local_port = port;
                so->bound = true;
            } else if (so->opt_reuseaddr || holder->opt_reuseaddr) reuse = true;
            else e = WSAEADDRINUSE;
        }
    }
    rt_net_unlock();
    rt_net_unref(so);
    if (reuse) RT_NET_STOP("rt_net_bind", "port %u is taken and SO_REUSEADDR is set on one of the two sockets", port);
    if (e) return rt_net_fail(e);
    rt_net_note(RT_NC_BINDS);                       /* a row of the runtime's own table of ports; no host socket (rt/hostsock.h) */
    rt_thread_set_last_error(0);                    /* rt/net.h, LAST ERROR (review D22) */
    return 0;
}

/* The shared end of getsockname and getpeername: the pointers, then the name.  Lock held. */
static DWORD name_out(int af, const BYTE *addr, USHORT port, SOCKADDR *name, int *namelen) {
    if (!name || !namelen || *namelen < rt_net_namelen(af)) return WSAEFAULT;
    rt_net_name_write(af, name, addr, port);
    *namelen = rt_net_namelen(af);
    return 0;
}

int rt_net_getsockname(SOCKET s, SOCKADDR *name, int *namelen) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    DWORD e;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (!name || !namelen || *namelen < rt_net_namelen(so->af)) e = WSAEFAULT;
    else if (!so->bound) e = WSAEINVAL;
    else e = name_out(so->af, so->local, so->local_port, name, namelen);
    rt_net_unlock();
    rt_net_unref(so);
    if (e) return rt_net_fail(e);
    rt_thread_set_last_error(0);                    /* rt/net.h, LAST ERROR (review D22) */
    return 0;
}

int rt_net_connect(SOCKET s, const SOCKADDR *name, int namelen) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    enum { OK, FAIL, STOP_UNSPEC, STOP_ANY, STOP_PORT0, STOP_V6ONLY } what = OK;
    DWORD e = 0;
    BYTE addr[16] = { 0 };
    USHORT port = 0;
    int af = so->af, type = so->type;               /* fixed at creation */
    RtNetAddrKind kind = RT_NA_OTHER;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (!name || namelen < (int)sizeof(USHORT)) e = WSAEFAULT;
    else if (name->sa_family == AF_UNSPEC) {
        /* Before the size of the socket's own family and before the family: a datagram socket
         * loses its peer (review D28).  16 bytes are enough on an AF_INET6 socket too. */
        if (type != SOCK_DGRAM || namelen < (int)sizeof(SOCKADDR)) what = STOP_UNSPEC;
        else so->connected = false;
    }
    else if (namelen < rt_net_namelen(af)) e = WSAEFAULT;
    else if (name->sa_family != af) e = WSAEAFNOSUPPORT;
    else {
        rt_net_name_read(af, name, addr, &port);
        kind = rt_net_addr_kind(af, addr);
        if (type == SOCK_STREAM) {
            if (kind == RT_NA_ANY) e = WSAEADDRNOTAVAIL;
            else if (port == 0) what = STOP_PORT0;
            else if (!so->bound && !rt_net_autobind(so)) e = WSAENOBUFS;
            else e = kind == RT_NA_OTHER ? WSAENETUNREACH : WSAECONNREFUSED;
        } else {
            if (kind == RT_NA_OTHER) e = WSAENETUNREACH;
            else if (kind == RT_NA_ANY) what = STOP_ANY;
            else if (port == 0) what = STOP_PORT0;
            else if (kind == RT_NA_LOOP4 && af == AF_INET6 && so->opt_v6only) what = STOP_V6ONLY;
            else if (!so->bound && !rt_net_autobind(so)) e = WSAENOBUFS;
            else {
                memcpy(so->peer, addr, 16);
                so->peer_port = port;
                so->connected = true;
            }
        }
    }
    rt_net_unlock();
    rt_net_unref(so);
    char text[65];
    switch (what) {
    case STOP_UNSPEC: RT_NET_STOP("rt_net_connect", "an AF_UNSPEC name on a %s socket, length %d",
                                  type == SOCK_DGRAM ? "datagram" : "stream", namelen);
    case STOP_ANY:    RT_NET_STOP("rt_net_connect", "a datagram socket connected to the unspecified address");
    case STOP_PORT0:  RT_NET_STOP("rt_net_connect", "port 0 of %s", rt_net_addr_text(af, addr, text));
    case STOP_V6ONLY: RT_NET_STOP("rt_net_connect", "an IPv4-mapped peer on a socket with IPV6_V6ONLY 1");
    default: break;
    }
    if (e == WSAENETUNREACH) {
        rt_net_note(RT_NC_REFUSED_CONNECTS);
        rt_log_once(RT_CH_NET, RT_LL_INFO, rt_net_addr_text(af, addr, text), "connect %s: no route", text);
    }
    else if (e == WSAECONNREFUSED)
        rt_log(RT_CH_NET, RT_LL_INFO, "connect %s port %u: refused, nothing listens on this machine",
               rt_net_addr_text(af, addr, text), port);
    if (e) return rt_net_fail(e);
    return 0;                                       /* last error is left alone: rt/net.h, LAST ERROR */
}

int rt_net_getpeername(SOCKET s, SOCKADDR *name, int *namelen) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    DWORD e;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (!so->connected) e = WSAENOTCONN;       /* before the pointers are looked at (review D29) */
    else e = name_out(so->af, so->peer, so->peer_port, name, namelen);
    rt_net_unlock();
    rt_net_unref(so);
    if (e) return rt_net_fail(e);
    return 0;
}
