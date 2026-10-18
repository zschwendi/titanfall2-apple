/* Sockets, data: sendto (and send), recvfrom (and recv), shutdown.
 * Specification: rt/net.h.  Shared internals: core/net_priv.h (in core/net.c).
 * BUILT (unit rt-net-core).  The test is tests/core/net_dgram.c.
 *
 * A datagram never leaves this process: sendto finds the datagram socket that holds the
 * destination port and appends to its queue, under the one net lock, so a sender and a
 * receiver never see half a datagram and the order of one sender is kept.  No call here
 * sleeps: an empty queue is WSAEWOULDBLOCK, or a stop in blocking mode.
 */
#include <stdlib.h>
#include <string.h>

#include "net_priv.h"

static const BYTE LOOP4_AS_V4[4]  = { 127, 0, 0, 1 };
static const BYTE LOOP4_AS_V6[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 127, 0, 0, 1 };
static const BYTE LOOP6[16]       = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };

/* The sender of a datagram as a socket of family `af` sees it: 127.0.0.1, ::ffff:127.0.0.1 or ::1. */
static const BYTE *sender_addr(int af, bool from_v4) {
    if (!from_v4) return LOOP6;
    return af == AF_INET ? LOOP4_AS_V4 : LOOP4_AS_V6;
}

/* Lock held.  The socket that takes a datagram of `len` bytes sent from `from_port` to a
 * loopback address of kind `kind` and `port`, or NULL: the datagram is dropped. */
static RtSocket *receiver(RtNetAddrKind kind, USHORT port, USHORT from_port, int len) {
    RtSocket *r = port ? rt_net_port_holder(SOCK_DGRAM, port) : NULL;
    if (!r || r->shut_rd) return NULL;
    bool v4 = kind == RT_NA_LOOP4;
    if (v4 ? (r->af == AF_INET6 && r->opt_v6only) : r->af != AF_INET6) return NULL;    /* the destination's kind */
    RtNetAddrKind bound = rt_net_addr_kind(r->af, r->local);
    if (bound != RT_NA_ANY && bound != kind) return NULL;       /* bound to the other loopback address */
    if (r->connected && (r->peer_port != from_port ||
                         memcmp(r->peer, sender_addr(r->af, v4), r->af == AF_INET ? 4 : 16) != 0)) return NULL;
    if (r->q_bytes + (unsigned)len > (unsigned)r->opt_rcvbuf) return NULL;
    return r;
}

int rt_net_sendto(SOCKET s, const void *buf, int len, int flags, const SOCKADDR *to, int tolen) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    enum { OK, STOP_FLAGS, STOP_FAMILY, STOP_CONNECTED, STOP_V6ONLY, STOP_MEMORY } what = OK;
    DWORD e = 0;
    BYTE addr[16] = { 0 };
    USHORT port = 0;
    int af = so->af;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (flags != 0) what = STOP_FLAGS;
    else if (len < 0 || (!buf && len > 0)) e = WSAEFAULT;
    else if (so->shut_wr) e = WSAESHUTDOWN;
    else if (so->type == SOCK_STREAM) e = WSAENOTCONN;
    else {
        if (to) {
            if (so->connected) what = STOP_CONNECTED;
            else if (tolen < rt_net_namelen(af)) e = WSAEFAULT;
            else if (to->sa_family != af) what = STOP_FAMILY;
            else rt_net_name_read(af, to, addr, &port);
        } else if (so->connected) {
            memcpy(addr, so->peer, 16);
            port = so->peer_port;
        } else e = WSAENOTCONN;
        if (!e && what == OK) {
            RtNetAddrKind kind = rt_net_addr_kind(af, addr);
            if (len > 65507) e = WSAEMSGSIZE;
            else if (!so->bound && !rt_net_autobind(so)) e = WSAENOBUFS;
            else if (kind == RT_NA_ANY) e = WSAEADDRNOTAVAIL;
            else if (kind == RT_NA_OTHER) e = WSAENETUNREACH;
            else if (kind == RT_NA_LOOP4 && af == AF_INET6 && so->opt_v6only) what = STOP_V6ONLY;
            else {
                RtSocket *r = receiver(kind, port, so->local_port, len);
                if (r) {
                    RtDgram *d = malloc(sizeof *d + (size_t)len);
                    if (!d) what = STOP_MEMORY;
                    else {
                        d->next = NULL;
                        d->len = len;
                        d->from_port = so->local_port;
                        d->from_v4 = kind == RT_NA_LOOP4;
                        if (len) memcpy(d->data, buf, (size_t)len);
                        if (r->q_tail) r->q_tail->next = d; else r->q_head = d;
                        r->q_tail = d;
                        r->q_bytes += (unsigned)len;
                        rt_net_wake();
                    }
                }
            }
        }
    }
    rt_net_unlock();
    rt_net_unref(so);
    char text[65];
    switch (what) {
    case STOP_FLAGS:     RT_NET_STOP("rt_net_sendto", "flags 0x%x", (unsigned)flags);
    case STOP_FAMILY:    RT_NET_STOP("rt_net_sendto", "a destination of family %d on a socket of family %d", to->sa_family, af);
    case STOP_CONNECTED: RT_NET_STOP("rt_net_sendto", "a destination given on a connected datagram socket");
    case STOP_V6ONLY:    RT_NET_STOP("rt_net_sendto", "an IPv4-mapped destination on a socket with IPV6_V6ONLY 1");
    case STOP_MEMORY:    RT_NET_STOP("rt_net_sendto", "out of memory for a datagram of %d bytes", len);
    default: break;
    }
    if (e == WSAENETUNREACH) {
        rt_net_note(RT_NC_REFUSED_SENDS);           /* not loopback: nothing was sent, and there is nothing to send it with */
        rt_log_once(RT_CH_NET, RT_LL_INFO, rt_net_addr_text(af, addr, text), "sendto %s: no route", text);
    }
    if (e) return rt_net_fail(e);
    rt_net_note(RT_NC_LOOP_SENDS);                  /* loopback: on a guest socket's queue in this process, or dropped */
    rt_thread_set_last_error(0);                    /* rt/net.h, LAST ERROR (review D22) */
    return len;
}

int rt_net_recvfrom(SOCKET s, void *buf, int len, int flags, SOCKADDR *from, int *fromlen) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    enum { OK, STOP_FLAGS, STOP_BLOCK } what = OK;
    DWORD e = 0;
    int got = 0;
    RtDgram *d = NULL;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (flags != 0) what = STOP_FLAGS;
    else if (len <= 0 || !buf) e = WSAEFAULT;
    else if (from && (!fromlen || *fromlen < rt_net_namelen(so->af))) e = WSAEFAULT;
    else if (so->shut_rd) e = WSAESHUTDOWN;
    else if (so->type == SOCK_STREAM) e = WSAENOTCONN;
    else if (!so->bound) e = WSAEINVAL;
    else if (!so->q_head) {
        if (so->nonblocking) e = WSAEWOULDBLOCK;    /* the engine's loop ends on exactly this code (engine+0x21b99f) */
        else what = STOP_BLOCK;
    } else {
        d = so->q_head;
        so->q_head = d->next;
        if (!so->q_head) so->q_tail = NULL;
        so->q_bytes -= (unsigned)d->len;
        got = d->len < len ? d->len : len;
        if (got) memcpy(buf, d->data, (size_t)got);
        if (from) {
            rt_net_name_write(so->af, from, sender_addr(so->af, d->from_v4), d->from_port);
            *fromlen = rt_net_namelen(so->af);
        }
        if (d->len > len) e = WSAEMSGSIZE;          /* the rest of the datagram is lost */
    }
    rt_net_unlock();
    rt_net_unref(so);
    free(d);
    if (what == STOP_FLAGS) RT_NET_STOP("rt_net_recvfrom", "flags 0x%x", (unsigned)flags);
    if (what == STOP_BLOCK) RT_NET_STOP("rt_net_recvfrom", "blocking receive with nothing queued");
    if (e) return rt_net_fail(e);
    rt_thread_set_last_error(0);                    /* rt/net.h, LAST ERROR (review D22) */
    return got;
}

int rt_net_shutdown(SOCKET s, int how) {
    RtSocket *so = rt_net_enter(s);
    if (!so) return SOCKET_ERROR;
    DWORD e = 0;
    RtDgram *q = NULL;
    rt_net_lock();
    if (so->closed) e = WSAENOTSOCK;
    else if (how != SD_RECEIVE && how != SD_SEND && how != SD_BOTH) e = WSAEINVAL;
    else if (so->type == SOCK_STREAM) e = WSAENOTCONN;
    else {
        if (how != SD_RECEIVE) so->shut_wr = true;
        if (how != SD_SEND) {
            so->shut_rd = true;
            q = so->q_head;
            so->q_head = so->q_tail = NULL;
            so->q_bytes = 0;
        }
    }
    rt_net_unlock();
    rt_net_unref(so);
    while (q) { RtDgram *n = q->next; free(q); q = n; }
    if (e) return rt_net_fail(e);
    rt_thread_set_last_error(0);                    /* rt/net.h, LAST ERROR (review D22) */
    return 0;
}
