/* Sockets: select, the one call of the socket layer that sleeps.
 * Specification: rt/net.h.  Shared internals: core/net_priv.h (in core/net.c).
 * BUILT (unit rt-net-core).  The test is tests/core/net_select.c.
 *
 * The call holds a reference on every listed socket from its first look to its return, so a
 * socket closed by another thread during the wait is still an object here, with `closed`
 * set.  It sleeps in rt_net_wait, which rt_net_sendto and rt_net_close wake (rt_net_wake,
 * a broadcast under the same lock: no wake-up is lost between the look and the sleep), and
 * looks at every socket again after each wake-up.  Nothing else is needed of the wait core.
 */
#include <string.h>

#include "net_priv.h"
#include "rt/time.h"

typedef struct { RtSocket *so[RT_WS_FD_SETSIZE]; bool ready[RT_WS_FD_SETSIZE]; unsigned n; } Refs;

static bool listed(const Refs *r, const RtSocket *so) {
    for (unsigned i = 0; i < r->n; i++) if (r->so[i] == so) return true;
    return false;
}

static void drop(Refs *r) {
    for (unsigned i = 0; i < r->n; i++) if (r->so[i]) rt_net_unref(r->so[i]);
    r->n = 0;
}

/* A reference on every entry of `set` (NULL: none).  false if one is not a socket. */
static bool take(const FD_SET *set, Refs *r) {
    r->n = 0;
    if (!set) return true;
    for (unsigned i = 0; i < set->fd_count; i++) {
        r->so[i] = rt_net_ref(set->fd_array[i]);
        r->n = i + 1;
        if (!r->so[i]) return false;
    }
    return true;
}

/* Rewrites `set` to its ready entries in their order, a socket listed twice kept once. */
static void rewrite(FD_SET *set, const Refs *r) {
    if (!set) return;
    unsigned out = 0;
    for (unsigned i = 0; i < r->n; i++) {
        if (!r->ready[i]) continue;
        bool dup = false;
        for (unsigned j = 0; j < i; j++) if (r->so[j] == r->so[i]) dup = true;
        if (!dup) set->fd_array[out++] = set->fd_array[i];
    }
    set->fd_count = out;
}

static unsigned count(const Refs *r) {
    unsigned n = 0;
    for (unsigned i = 0; i < r->n; i++) {
        if (!r->ready[i]) continue;
        bool dup = false;
        for (unsigned j = 0; j < i; j++) if (r->so[j] == r->so[i]) dup = true;
        n += !dup;
    }
    return n;
}

int rt_net_select(int nfds, FD_SET *readfds, FD_SET *writefds, FD_SET *exceptfds, const TIMEVAL *timeout) {
    (void)nfds;
    rt_net_lock();
    bool started = rt_net_started();
    rt_net_unlock();
    if (!started) return rt_net_fail(WSANOTINITIALISED);
    FD_SET *sets[3] = { readfds, writefds, exceptfds };
    unsigned total = 0;
    for (int i = 0; i < 3; i++) {
        if (!sets[i]) continue;
        if (sets[i]->fd_count > RT_WS_FD_SETSIZE)
            RT_NET_STOP("rt_net_select", "a set of %u sockets (more than %d)", sets[i]->fd_count, RT_WS_FD_SETSIZE);
        total += sets[i]->fd_count;
    }
    if (!total) return rt_net_fail(WSAEINVAL);       /* three NULL or empty sets (select's page) */
    if (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0)) return rt_net_fail(WSAEINVAL);

    Refs r, w, x;
    bool ok = take(readfds, &r);
    ok = take(writefds, &w) && ok;
    ok = take(exceptfds, &x) && ok;
    if (!ok) { drop(&r); drop(&w); drop(&x); return rt_net_fail(WSAENOTSOCK); }

    bool limited = timeout != NULL;
    uint64_t wait_ms = limited ? (uint64_t)timeout->tv_sec * 1000 + ((uint64_t)timeout->tv_usec + 999) / 1000 : 0;
    uint64_t deadline = rt_time_tick_ms() + wait_ms;
    int result = 0;
    bool stop_write = false, not_sock = false;
    rt_net_lock();
    for (;;) {
        unsigned ready = 0;
        for (unsigned i = 0; i < r.n; i++)          /* a socket closed during the wait is reported readable (review D34) */
            ready += r.ready[i] = r.so[i]->closed || (r.so[i]->type == SOCK_DGRAM && r.so[i]->q_head);
        for (unsigned i = 0; i < w.n; i++) {
            if (w.so[i]->closed) { w.ready[i] = false; if (!listed(&r, w.so[i])) stop_write = true; }
            else ready += w.ready[i] = w.so[i]->type == SOCK_DGRAM;
        }
        for (unsigned i = 0; i < x.n; i++) {
            x.ready[i] = false;
            if (x.so[i]->closed && !listed(&r, x.so[i]) && !listed(&w, x.so[i])) not_sock = true;
        }
        if (stop_write || not_sock) break;
        if (ready) {
            result = (int)(count(&r) + count(&w));
            rewrite(readfds, &r); rewrite(writefds, &w); rewrite(exceptfds, &x);
            break;
        }
        uint64_t now = rt_time_tick_ms();
        if (limited && now >= deadline) {           /* the time is over: three empty sets, 0 */
            for (int i = 0; i < 3; i++) if (sets[i]) sets[i]->fd_count = 0;
            break;
        }
        rt_net_wait(limited ? (DWORD)(deadline - now > 0x7fffffff ? 0x7fffffff : deadline - now) : INFINITE);
    }
    rt_net_unlock();
    drop(&r); drop(&w); drop(&x);
    if (stop_write) RT_NET_STOP("rt_net_select", "a socket in writefds and not in readfds was closed during the wait");
    if (not_sock) return rt_net_fail(WSAENOTSOCK);
    return result;                                  /* last error is left alone: rt/net.h, LAST ERROR */
}
