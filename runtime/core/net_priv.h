/* Private to runtime/core/net*.c: what the pieces of the socket layer share.  The public
 * specification is rt/net.h; nothing outside core/net*.c includes this file (tests declare
 * the test hooks at the end by hand).
 *
 * Everything declared here is BUILT, in core/net.c.
 *
 * NO HOST SOCKET HEADER may be included by a file that includes this one (rt/win/net.h says
 * why), and no host socket call is made anywhere in core/net*.c.
 */
#ifndef RT_NET_PRIV_H
#define RT_NET_PRIV_H

#include "core.h"
#include "rt/handle.h"
#include "rt/hostsock.h"        /* the counters RT_NC_*: no host type in it */
#include "rt/log.h"
#include "rt/net.h"
#include "rt/thread.h"

/* A STOP of rt/net.h: the run ends with "FATAL ... <function>: not implemented: <what>".
 * `fn` is the public function's name as a string literal; a test expects the text
 * "<function>: not implemented".  Put the offending value in the message. */
#define RT_NET_STOP(fn, fmt, ...)  rt_fatal(fn ": not implemented: " fmt, ##__VA_ARGS__)

/* A failure: sets the calling thread's last error and returns SOCKET_ERROR. */
int rt_net_fail(DWORD code);

/* One more of what the guest did (RT_NC_* of rt/hostsock.h), for the line a run ends with.
 * Atomic: callable with or without the lock. */
void rt_net_note(int which);

/* One queued datagram.  Allocated with malloc by the sender, freed with free by whoever
 * takes it off (or by the socket's end). */
typedef struct RtDgram {
    struct RtDgram *next;
    int             len;
    USHORT          from_port;      /* host byte order */
    bool            from_v4;        /* sent to an IPv4 loopback address (else to ::1) */
    BYTE            data[];
} RtDgram;

/* The socket object.  Every field after `obj` is guarded by the net lock. */
typedef struct RtSocket {
    RtObject obj;
    struct RtSocket *next, *prev;   /* every socket that is not closed: rt_net_all */
    bool   closed;                  /* rt_net_close ran: no port, no queue, in no list */
    int    af, type, protocol;      /* AF_INET or AF_INET6; SOCK_*; IPPROTO_TCP or _UDP */
    bool   nonblocking;             /* FIONBIO */
    bool   bound;
    BYTE   local[16];               /* the address given to bind: 4 bytes (AF_INET) or 16 */
    USHORT local_port;              /* host byte order; meaningful while bound */
    bool   connected;               /* datagram sockets: rt_net_connect stored a peer */
    BYTE   peer[16];
    USHORT peer_port;               /* host byte order */
    bool   shut_rd, shut_wr;        /* rt_net_shutdown */
    int    opt_reuseaddr, opt_keepalive, opt_broadcast, opt_nodelay, opt_v6only;
    bool   opt_v6only_set;          /* IPV6_V6ONLY was set (an AF_INET socket has no default to read) */
    int    opt_sndbuf, opt_rcvbuf;
    LINGER opt_linger;
    RtDgram *q_head, *q_tail;       /* datagrams waiting to be received, oldest first */
    unsigned q_bytes;               /* the sum of their len */
} RtSocket;

/* The net lock.  A leaf: under it call nothing but the functions of this header marked
 * "lock held", malloc/free, and memcpy.  Never rt_fatal, a log call, rt_object_unref or
 * rt_handle_* under it: unlock first. */
void rt_net_lock(void);
void rt_net_unlock(void);

/* Lock held.  Is the start count above 0? */
bool rt_net_started(void);

/* The socket behind `s` with one more reference, or NULL (not a socket: the caller fails
 * with WSAENOTSOCK).  Call WITHOUT the lock; drop the reference with rt_net_unref, also
 * without the lock.  A socket that rt_net_close has closed is never returned. */
RtSocket *rt_net_ref(SOCKET s);
void      rt_net_unref(RtSocket *so);

/* The start of every call on a socket.  NULL after setting last error: WSANOTINITIALISED if
 * the start count is 0, else WSAENOTSOCK.  Otherwise the socket with one more reference
 * (rt_net_unref).  Call WITHOUT the lock.  The socket may be closed by another thread at any
 * time after: look at so->closed under the lock and fail with WSAENOTSOCK. */
RtSocket *rt_net_enter(SOCKET s);

/* The size of a name of family `af`: 16 (SOCKADDR_IN) or 28 (SOCKADDR_IN6). */
static inline int rt_net_namelen(int af) { return af == AF_INET ? (int)sizeof(SOCKADDR_IN) : (int)sizeof(SOCKADDR_IN6); }

/* The address (4 or 16 bytes, the rest of addr[16] zero) and the port (host order) of a name
 * of family `af`, and the reverse (every other member of the name 0).  No lock needed. */
void rt_net_name_read(int af, const SOCKADDR *name, BYTE addr[16], USHORT *port);
void rt_net_name_write(int af, SOCKADDR *name, const BYTE *addr, USHORT port);

/* "a.b.c.d" or the IPv6 text of rt_net_ntop, for a log line or a stop.  `buf` holds 65 bytes. */
const char *rt_net_addr_text(int af, const BYTE *addr, char *buf);

/* Lock held.  The head of the list of sockets that are not closed (follow ->next). */
RtSocket *rt_net_all(void);

/* Lock held.  The socket of `type` (SOCK_DGRAM or SOCK_STREAM) bound to `port` (host order),
 * or NULL. */
RtSocket *rt_net_port_holder(int type, USHORT port);

/* Lock held.  Port 0 of rt_net_bind for a socket that is not bound: the unspecified address
 * and the lowest free port from 49152 up.  false if none is free (the socket is unchanged). */
bool rt_net_autobind(RtSocket *so);

/* What an address is to this machine.  `addr` is 4 bytes for AF_INET and 16 for AF_INET6.
 *   RT_NA_ANY    0.0.0.0 or ::
 *   RT_NA_LOOP4  127.x.y.z, or ::ffff:127.x.y.z
 *   RT_NA_LOOP6  ::1
 *   RT_NA_OTHER  everything else (::ffff:0.0.0.0 included): no route, not ours */
typedef enum { RT_NA_ANY, RT_NA_LOOP4, RT_NA_LOOP6, RT_NA_OTHER } RtNetAddrKind;
RtNetAddrKind rt_net_addr_kind(int af, const BYTE *addr);

/* Lock held.  Wake every thread in rt_net_wait: call it after queueing a datagram and after
 * closing a socket. */
void rt_net_wake(void);

/* Lock held on entry and on return; released while asleep.  Sleeps until rt_net_wake or
 * until `ms` milliseconds have passed (INFINITE: no limit).  false if the time ran out.
 * It can return true with nothing changed: the caller checks its condition again and
 * computes the time left itself (rt_time_tick_ms, rt/time.h). */
bool rt_net_wait(DWORD ms);

/* Test hooks (declared by hand in tests): sockets that are not closed; the start count. */
unsigned rt_net_test_open_count(void);
unsigned rt_net_test_start_count(void);

#endif
