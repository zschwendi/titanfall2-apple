/* Sockets, options and modes: setsockopt, getsockopt, ioctlsocket.
 * Specification: rt/net.h.  Shared internals: core/net_priv.h (in core/net.c).
 * BUILT (unit rt-net-core).  The test is tests/core/net_opt.c.
 */
#include <string.h>

#include "net_priv.h"

/* What an option is: how many bytes it has and where it lives. */
typedef enum { O_UNKNOWN, O_REUSEADDR, O_KEEPALIVE, O_BROADCAST, O_SNDBUF, O_RCVBUF, O_LINGER,
               O_TYPE, O_ERROR, O_NODELAY, O_V6ONLY } Opt;

static Opt opt_of(int level, int optname) {
    if (level == SOL_SOCKET)
        switch (optname) {
        case SO_REUSEADDR: return O_REUSEADDR;
        case SO_KEEPALIVE: return O_KEEPALIVE;
        case SO_BROADCAST: return O_BROADCAST;
        case SO_SNDBUF:    return O_SNDBUF;
        case SO_RCVBUF:    return O_RCVBUF;
        case SO_LINGER:    return O_LINGER;
        case SO_TYPE:      return O_TYPE;
        case SO_ERROR:     return O_ERROR;
        default:           return O_UNKNOWN;
        }
    if (level == IPPROTO_TCP && optname == TCP_NODELAY) return O_NODELAY;
    if (level == IPPROTO_IPV6 && optname == IPV6_V6ONLY) return O_V6ONLY;
    return O_UNKNOWN;
}

static bool one_byte(Opt o) { return o == O_KEEPALIVE || o == O_NODELAY || o == O_V6ONLY; }

int rt_net_setsockopt(SOCKET s, int level, int optname, const void *optval, int optlen) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    Opt o = opt_of(level, optname);
    const char *stop = NULL;
    DWORD e = 0;
    int v = 0;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (o == O_UNKNOWN) stop = "this level and option";
    else if (o == O_TYPE) e = WSAENOPROTOOPT;                   /* read-only (review NV19) */
    else if (o == O_ERROR) stop = "setting SO_ERROR";
    else if (o == O_NODELAY && so->type != SOCK_STREAM) e = WSAENOPROTOOPT;
    else if (o == O_LINGER && so->type != SOCK_STREAM) stop = "setting SO_LINGER on a datagram socket";
    else if (optlen < 0) stop = "a negative optlen";
    else if (!optval) e = WSAEFAULT;
    else if (one_byte(o)) {
        if (optlen < 1) e = WSAEFAULT;
        else if (o == O_V6ONLY && so->bound) stop = "IPV6_V6ONLY after bind";
        else {
            v = *(const BYTE *)optval != 0;                     /* only the first byte is read (review D30) */
            if (o == O_KEEPALIVE) so->opt_keepalive = v;
            else if (o == O_NODELAY) so->opt_nodelay = v;
            else { so->opt_v6only = v; so->opt_v6only_set = true; }
        }
    } else if (o == O_REUSEADDR || o == O_BROADCAST) {
        if (optlen == 0) stop = "optlen 0 for a BOOL option";
        else {
            memcpy(&v, optval, optlen < 4 ? (size_t)optlen : 4);    /* the bytes given, the rest 0 */
            if (o == O_REUSEADDR) so->opt_reuseaddr = v != 0; else so->opt_broadcast = v != 0;
        }
    } else if (optlen < 4) e = WSAEFAULT;
    else if (o == O_LINGER) memcpy(&so->opt_linger, optval, sizeof so->opt_linger);
    else {
        memcpy(&v, optval, 4);
        if (v < 0) stop = "a negative buffer size";
        else if (o == O_SNDBUF) so->opt_sndbuf = v; else so->opt_rcvbuf = v;
    }
    rt_net_unlock();
    rt_net_unref(so);
    if (stop) RT_NET_STOP("rt_net_setsockopt", "%s (level %d = 0x%x, option %d = 0x%x, optlen %d, value %d)",
                          stop, level, (unsigned)level, optname, (unsigned)optname, optlen, v);
    if (e) return rt_net_fail(e);
    rt_thread_set_last_error(0);                    /* rt/net.h, LAST ERROR (review D22) */
    return 0;
}

int rt_net_getsockopt(SOCKET s, int level, int optname, void *optval, int *optlen) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    Opt o = opt_of(level, optname);
    const char *stop = NULL;
    DWORD e = 0;
    int v = 0;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (o == O_UNKNOWN) stop = "this level and option";
    else if (o == O_NODELAY && so->type != SOCK_STREAM) e = WSAENOPROTOOPT;
    else if (o == O_LINGER && so->type != SOCK_STREAM) e = WSAENOPROTOOPT;      /* review D32 */
    else if (!optval || !optlen || *optlen == 0) e = WSAEFAULT;
    else if (*optlen < 0) stop = "a negative *optlen";
    else if (o == O_V6ONLY && so->af != AF_INET6 && !so->opt_v6only_set) stop = "IPV6_V6ONLY read on an AF_INET socket before it was set";
    else if (one_byte(o)) {
        *(BYTE *)optval = (BYTE)(o == O_KEEPALIVE ? so->opt_keepalive : o == O_NODELAY ? so->opt_nodelay : so->opt_v6only);
        *optlen = 1;
    } else if (*optlen < 4) e = WSAEFAULT;
    else {
        switch (o) {
        case O_REUSEADDR: v = so->opt_reuseaddr; break;
        case O_BROADCAST: v = so->opt_broadcast; break;
        case O_SNDBUF:    v = so->opt_sndbuf; break;
        case O_RCVBUF:    v = so->opt_rcvbuf; break;
        case O_TYPE:      v = so->type; break;
        case O_LINGER:    memcpy(&v, &so->opt_linger, 4); break;
        default:          v = 0; break;             /* SO_ERROR: nothing is ever pending on this machine */
        }
        memcpy(optval, &v, 4);
        *optlen = 4;
    }
    rt_net_unlock();
    rt_net_unref(so);
    if (stop) RT_NET_STOP("rt_net_getsockopt", "%s (level %d = 0x%x, option %d = 0x%x)",
                          stop, level, (unsigned)level, optname, (unsigned)optname);
    if (e) return rt_net_fail(e);
    rt_thread_set_last_error(0);                    /* every option, SO_ERROR included (reviews C11, D22) */
    return 0;
}

int rt_net_ioctl(SOCKET s, LONG cmd, ULONG *argp) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    bool stop = false;
    DWORD e = 0;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (cmd != FIONBIO && cmd != FIONREAD) stop = true;
    else if (!argp) e = WSAEFAULT;
    else if (cmd == FIONBIO) so->nonblocking = *argp != 0;
    else *argp = so->type == SOCK_DGRAM ? so->q_bytes : 0;
    rt_net_unlock();
    rt_net_unref(so);
    if (stop) RT_NET_STOP("rt_net_ioctl", "command 0x%08x", (unsigned)cmd);
    if (e) return rt_net_fail(e);
    return 0;                                       /* last error is left alone: rt/net.h, LAST ERROR */
}
