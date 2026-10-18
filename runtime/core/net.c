/* Sockets: Winsock's start count, the socket object and its lifetime, the text helpers, and
 * what the other pieces share (docs/handoff/rt-contract-net.md).
 * Specification: rt/net.h.  Shared internals: core/net_priv.h (all built here).
 *
 * BUILT: rt_net_startup, rt_net_cleanup, rt_net_socket, rt_net_close, rt_net_pton,
 * rt_net_ntop, rt_net_inet_addr.  The test is tests/core/net.c.
 *
 * STATE: one mutex (`net_mu`) for the start count, the list of sockets and every socket's
 * fields, and one condition variable (`net_cv`) that rt_net_wake broadcasts.  No host socket
 * exists: nothing in this file, or in any core/net_*.c, makes a host socket call.  That is
 * held by a lint and measured in every run: rt/hostsock.h, THE RULE.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "net_priv.h"

static pthread_mutex_t net_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  net_cv = PTHREAD_COND_INITIALIZER;
static unsigned  start_count;       /* net_mu */
static RtSocket *all_sockets;       /* net_mu: sockets that are not closed */
static unsigned  open_count;        /* net_mu */

int rt_net_fail(DWORD code) { rt_thread_set_last_error(code); return SOCKET_ERROR; }

/* What the guest did, counted for rt_hostsock_report (rt/hostsock.h): read without the lock,
 * possibly inside a fatal report. */
static _Atomic unsigned net_counts[RT_NC_N];
void rt_net_note(int which) { atomic_fetch_add(&net_counts[which], 1); }
void rt_net_counts(unsigned out[RT_NC_N]) { for (int i = 0; i < RT_NC_N; i++) out[i] = atomic_load(&net_counts[i]); }

void rt_net_lock(void)   { pthread_mutex_lock(&net_mu); }
void rt_net_unlock(void) { pthread_mutex_unlock(&net_mu); }
bool rt_net_started(void) { return start_count > 0; }
RtSocket *rt_net_all(void) { return all_sockets; }
void rt_net_wake(void) { pthread_cond_broadcast(&net_cv); }

bool rt_net_wait(DWORD ms) {
    if (ms == INFINITE) { pthread_cond_wait(&net_cv, &net_mu); return true; }
    struct timespec rel = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    return pthread_cond_timedwait_relative_np(&net_cv, &net_mu, &rel) == 0;
}

unsigned rt_net_test_open_count(void)  { rt_net_lock(); unsigned n = open_count;  rt_net_unlock(); return n; }
unsigned rt_net_test_start_count(void) { rt_net_lock(); unsigned n = start_count; rt_net_unlock(); return n; }

/* ---- the socket object --------------------------------------------------------------------- */

/* net_mu held.  Take the socket out of the list and off its port; returns its queue for the
 * caller to free after unlocking. */
static RtDgram *sock_retire(RtSocket *so) {
    RtDgram *q = so->q_head;
    so->q_head = so->q_tail = NULL;
    so->q_bytes = 0;
    if (!so->closed) {
        so->closed = true;
        so->bound = false;
        if (so->prev) so->prev->next = so->next; else all_sockets = so->next;
        if (so->next) so->next->prev = so->prev;
        so->next = so->prev = NULL;
        open_count--;
    }
    return q;
}

static void queue_free(RtDgram *q) {
    while (q) { RtDgram *n = q->next; free(q); q = n; }
}

static void sock_destroy(RtObject *obj) {
    RtSocket *so = (RtSocket *)obj;
    rt_net_lock();
    RtDgram *q = sock_retire(so);
    rt_net_unlock();
    queue_free(q);
}

static const RtObjectOps sock_ops = { .name = "socket", .destroy = sock_destroy, .no_close_handle = true };

RtSocket *rt_net_ref(SOCKET s) {
    if (s >> 32) return NULL;
    RtSocket *so = (RtSocket *)rt_handle_ref((HANDLE)(uintptr_t)s, RT_K_SOCKET);
    if (!so) return NULL;
    rt_net_lock();
    bool closed = so->closed;
    rt_net_unlock();
    if (closed) { rt_object_unref(&so->obj); return NULL; }
    return so;
}

void rt_net_unref(RtSocket *so) { rt_object_unref(&so->obj); }

RtSocket *rt_net_enter(SOCKET s) {
    rt_net_lock();
    bool started = rt_net_started();
    rt_net_unlock();
    if (!started) { rt_thread_set_last_error(WSANOTINITIALISED); return NULL; }
    RtSocket *so = rt_net_ref(s);
    if (!so) rt_thread_set_last_error(WSAENOTSOCK);
    return so;
}

void rt_net_name_read(int af, const SOCKADDR *name, BYTE addr[16], USHORT *port) {
    memset(addr, 0, 16);
    if (af == AF_INET) {
        const SOCKADDR_IN *in = (const SOCKADDR_IN *)name;
        memcpy(addr, in->sin_addr.s_b, 4);
        *port = (USHORT)((in->sin_port >> 8) | (in->sin_port << 8));
    } else {
        const SOCKADDR_IN6 *in6 = (const SOCKADDR_IN6 *)name;
        memcpy(addr, in6->sin6_addr.s6_addr, 16);
        *port = (USHORT)((in6->sin6_port >> 8) | (in6->sin6_port << 8));
    }
}

void rt_net_name_write(int af, SOCKADDR *name, const BYTE *addr, USHORT port) {
    USHORT nport = (USHORT)((port >> 8) | (port << 8));
    if (af == AF_INET) {
        SOCKADDR_IN in = { .sin_family = AF_INET, .sin_port = nport };
        memcpy(in.sin_addr.s_b, addr, 4);
        memcpy(name, &in, sizeof in);
    } else {
        SOCKADDR_IN6 in6 = { .sin6_family = AF_INET6, .sin6_port = nport };
        memcpy(in6.sin6_addr.s6_addr, addr, 16);
        memcpy(name, &in6, sizeof in6);
    }
}

RtSocket *rt_net_port_holder(int type, USHORT port) {
    for (RtSocket *so = all_sockets; so; so = so->next)
        if (so->bound && so->type == type && so->local_port == port) return so;
    return NULL;
}

bool rt_net_autobind(RtSocket *so) {
    for (unsigned port = 49152; port <= 65535; port++) {
        if (rt_net_port_holder(so->type, (USHORT)port)) continue;
        memset(so->local, 0, sizeof so->local);
        so->local_port = (USHORT)port;
        so->bound = true;
        return true;
    }
    return false;
}

RtNetAddrKind rt_net_addr_kind(int af, const BYTE *a) {
    static const BYTE zero[16];
    if (af == AF_INET) {
        if (!memcmp(a, zero, 4)) return RT_NA_ANY;
        return a[0] == 127 ? RT_NA_LOOP4 : RT_NA_OTHER;
    }
    if (!memcmp(a, zero, 16)) return RT_NA_ANY;
    if (!memcmp(a, zero, 15) && a[15] == 1) return RT_NA_LOOP6;
    if (!memcmp(a, zero, 10) && a[10] == 0xff && a[11] == 0xff && a[12] == 127) return RT_NA_LOOP4;
    return RT_NA_OTHER;
}

/* ---- WSAStartup, WSACleanup ---------------------------------------------------------------- */

int rt_net_startup(WORD requested, WSADATA *out) {
    /* Order and values: rt/net.h, WSAStartup (Wine ws2_32/tests/protocol.c, test_startup; review
     * Z5).  The structure is filled first, whatever the version; the version is looked at before
     * the pointer (review D23 of docs/reviews/contracts-check-2.md). */
    unsigned major = requested & 0xff, minor = requested >> 8;
    WORD use = 0x0202;
    if (major == 1) use = minor <= 1 ? requested : 0x0101;
    else if (major == 2 && minor <= 2) use = requested;
    if (out) {
        out->wVersion = use;
        out->wHighVersion = 0x0202;
        out->iMaxSockets = major == 1 ? 32767 : 0;
        out->iMaxUdpDg = major == 1 ? 65467 : 0;
        /* The texts are zero-padded to the end of their arrays; lpVendorInfo is not written. */
        memset(out->szDescription, 0, sizeof out->szDescription);
        strcpy(out->szDescription, "WinSock 2.0");
        memset(out->szSystemStatus, 0, sizeof out->szSystemStatus);
        strcpy(out->szSystemStatus, "Running");
    }
    if (major == 0) return WSAVERNOTSUPPORTED;
    if (!out) return WSAEFAULT;
    rt_net_lock();
    start_count++;
    rt_net_unlock();
    return 0;
}

int rt_net_cleanup(void) {
    rt_net_lock();
    if (!start_count) { rt_net_unlock(); return rt_net_fail(WSANOTINITIALISED); }
    unsigned left = --start_count, open = open_count;
    rt_net_unlock();
    if (!left && open)
        rt_log(RT_CH_NET, RT_LL_INFO, "WSACleanup: the last one, with %u socket(s) still open (kept; they fail "
               "with WSANOTINITIALISED until the next WSAStartup)", open);
    rt_thread_set_last_error(0);                    /* success resets it: rt/net.h, LAST ERROR (review D22) */
    return 0;
}

/* ---- socket, closesocket ------------------------------------------------------------------- */

SOCKET rt_net_socket(int af, int type, int protocol) {
    rt_net_lock();
    bool started = rt_net_started();
    rt_net_unlock();
    /* The order of rt/net.h: the type, then the family, then the protocol (win-behaviour-check-5 Z4). */
    DWORD e = 0;
    if (!started) e = WSANOTINITIALISED;
    else if (type != 0 && type != SOCK_STREAM && type != SOCK_DGRAM) e = WSAESOCKTNOSUPPORT;
    else if (af != AF_UNSPEC && af != AF_INET && af != AF_INET6) e = WSAEAFNOSUPPORT;
    else if (af == AF_UNSPEC && protocol == 0) e = WSAEINVAL;
    else if (type == 0) {                           /* the type comes from the protocol: 0 and IPPROTO_TCP stream, IPPROTO_UDP datagram */
        if (protocol == IPPROTO_UDP) type = SOCK_DGRAM;
        else if (protocol == 0 || protocol == IPPROTO_TCP) type = SOCK_STREAM;
        else e = WSAEPROTONOSUPPORT;
    }
    else if (protocol != 0 && protocol != (type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP)) e = WSAEPROTONOSUPPORT;
    if (e) { rt_thread_set_last_error(e); return INVALID_SOCKET; }

    RtSocket *so = (RtSocket *)rt_object_new(RT_K_SOCKET, sizeof *so, &sock_ops, NULL, NULL);
    so->af = af == AF_UNSPEC ? AF_INET : af;        /* AF_UNSPEC with IPPROTO_TCP or IPPROTO_UDP is an AF_INET socket */
    so->type = type;
    so->protocol = type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
    so->opt_v6only = 1;
    so->opt_sndbuf = so->opt_rcvbuf = 65536;
    rt_net_lock();
    so->next = all_sockets;
    if (all_sockets) all_sockets->prev = so;
    all_sockets = so;
    open_count++;
    rt_net_unlock();
    HANDLE h = rt_handle_new(&so->obj);             /* a full table drops the reference: sock_destroy */
    if (!h) { rt_thread_set_last_error(WSAENOBUFS); return INVALID_SOCKET; }
    rt_net_note(RT_NC_SOCKETS);
    rt_thread_set_last_error(0);                    /* success resets it: rt/net.h, LAST ERROR (review D22) */
    return (SOCKET)(uintptr_t)h;
}

int rt_net_close(SOCKET s) {
    rt_net_lock();
    bool started = rt_net_started();
    rt_net_unlock();
    if (!started) return rt_net_fail(WSANOTINITIALISED);
    RtSocket *so = rt_net_ref(s);
    if (!so) return rt_net_fail(WSAENOTSOCK);
    rt_net_lock();
    if (so->closed) {                                /* another thread closed it meanwhile */
        rt_net_unlock();
        rt_net_unref(so);
        return rt_net_fail(WSAENOTSOCK);
    }
    RtDgram *q = sock_retire(so);
    rt_net_wake();
    rt_net_unlock();
    queue_free(q);
    rt_handle_close_kind((HANDLE)(uintptr_t)s, RT_K_SOCKET);
    rt_net_unref(so);
    return 0;
}

/* ---- text ---------------------------------------------------------------------------------- */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Strict dotted decimal, the whole string.  1 or 0.  A number with a leading zero ("01") is 0,
 * except in the dotted tail of an IPv6 text (`tail`), where one to three digits may have them
 * (Wine ws2_32/tests/protocol.c, test_inet_pton: "1.1.1.010" is 0, "1::001.2.3.4" is 1). */
static int pton4(const char *s, BYTE out[4], bool tail) {
    for (int i = 0; i < 4; i++) {
        unsigned v = 0, digits = 0;
        const char *start = s;
        while (*s >= '0' && *s <= '9' && digits < 4) { v = v * 10 + (unsigned)(*s - '0'); s++; digits++; }
        if (digits == 0 || digits > 3 || v > 255) return 0;
        if (!tail && digits > 1 && *start == '0') return 0;
        out[i] = (BYTE)v;
        if (*s != (i < 3 ? '.' : '\0')) return 0;
        if (i < 3) s++;
    }
    return 1;
}

static int pton6(const char *s, BYTE out[16]) {
    if (strchr(s, '%')) return 0;                    /* a scope is not an address: "::%16" is 0 */
    WORD g[8];
    int n = 0, gap = -1;
    if (s[0] == ':') {
        if (s[1] != ':') return 0;
        gap = 0;
        s += 2;
    }
    while (*s) {
        if (n == 8) return 0;
        const char *start = s;
        unsigned v = 0, digits = 0;
        while (hexval(*s) >= 0 && digits < 5) { v = v * 16 + (unsigned)hexval(*s); s++; digits++; }
        if (*s == '.') {                             /* the last 32 bits as a dotted IPv4 address */
            BYTE v4[4];
            if (n > 6 || pton4(start, v4, true) != 1) return 0;
            g[n++] = (WORD)(v4[0] << 8 | v4[1]);
            g[n++] = (WORD)(v4[2] << 8 | v4[3]);
            break;
        }
        if (digits == 0 || digits > 4) return 0;
        g[n++] = (WORD)v;
        if (!*s) break;
        if (*s != ':') return 0;
        s++;
        if (*s == ':') {
            if (gap >= 0) return 0;
            gap = n;
            s++;
        } else if (!*s) return 0;                    /* a single ':' at the end */
    }
    if (gap < 0 ? n != 8 : n > 7) return 0;
    WORD full[8] = { 0 };
    if (gap < 0) gap = n;
    for (int i = 0; i < gap; i++) full[i] = g[i];
    for (int i = gap; i < n; i++) full[8 - (n - i)] = g[i];
    for (int i = 0; i < 8; i++) { out[2 * i] = (BYTE)(full[i] >> 8); out[2 * i + 1] = (BYTE)full[i]; }
    return 1;
}

int rt_net_pton(int af, const char *text, void *out) {
    if (!text) return rt_net_fail(WSAEFAULT);          /* before the family (Wine protocol.c) */
    if (af != AF_INET && af != AF_INET6) return rt_net_fail(WSAEAFNOSUPPORT);
    if (!out) return rt_net_fail(WSAEFAULT);
    BYTE tmp[16];
    int ok = af == AF_INET ? pton4(text, tmp, false) : pton6(text, tmp);
    if (ok == 1) memcpy(out, tmp, af == AF_INET ? 4 : 16);
    return ok;
}

static const char *ntop_fail(DWORD e) { rt_thread_set_last_error(e); return NULL; }

const char *rt_net_ntop(int af, const void *addr, char *buf, SIZE_T size) {
    if (af != AF_INET && af != AF_INET6) return ntop_fail(WSAEAFNOSUPPORT);
    if (!addr || !buf) return ntop_fail((DWORD)STATUS_INVALID_PARAMETER);       /* 0xC000000D: review D26 */
    const BYTE *a = addr;
    char text[INET6_ADDRSTRLEN + 2];
    if (af == AF_INET) {
        snprintf(text, sizeof text, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    } else {
        WORD g[8];
        for (int i = 0; i < 8; i++) g[i] = (WORD)(a[2 * i] << 8 | a[2 * i + 1]);
        /* The last 32 bits are dotted in these four forms only (win-behaviour-check-5 Z7); every other
         * address is hex.  The four are exclusive: they differ in g4 or g5. */
        bool dotted = false;
        if (!(g[0] | g[1] | g[2] | g[3] | g[4] | g[5]) && g[6]) dotted = true;                    /* ::a.b.c.d */
        else if (!(g[0] | g[1] | g[2] | g[3] | g[4]) && g[5] == 0xffff && g[6]) dotted = true;   /* ::ffff:a.b.c.d */
        else if (!(g[0] | g[1] | g[2] | g[3]) && g[4] == 0xffff && !g[5] && g[6]) dotted = true; /* ::ffff:0:a.b.c.d */
        else if (g[5] == 0x5efe && (g[4] == 0 || g[4] == 0x0200)) dotted = true;                /* ISATAP, any prefix */
        int hex = dotted ? 6 : 8;                       /* the groups written as hex, before the dotted tail */
        int best = -1, best_len = 0;                    /* the longest run of two or more zero groups, the first of equal runs */
        for (int i = 0; i < hex; ) {
            if (g[i]) { i++; continue; }
            int j = i;
            while (j < hex && !g[j]) j++;
            if (j - i >= 2 && j - i > best_len) { best = i; best_len = j - i; }
            i = j;
        }
        char *p = text;
        for (int i = 0; i < hex; ) {
            if (i == best) { *p++ = ':'; *p++ = ':'; i += best_len; continue; }
            if (i && p[-1] != ':') *p++ = ':';
            p += snprintf(p, 6, "%x", g[i]);            /* a single zero group is "0" */
            i++;
        }
        if (dotted) {
            if (p[-1] != ':') *p++ = ':';               /* after a run "::" is already there */
            p += snprintf(p, 16, "%u.%u.%u.%u", a[12], a[13], a[14], a[15]);
        }
        *p = 0;
    }
    SIZE_T need = strlen(text) + 1;
    if (size < need) return ntop_fail((DWORD)STATUS_INVALID_PARAMETER);
    memcpy(buf, text, need);
    return buf;
}

/* The white space that ends the number in inet_addr: space, \t, \n, \v, \f, \r. */
static bool inet_white(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

ULONG rt_net_inet_addr(const char *text) {
    if (!text) { rt_thread_set_last_error(WSAEFAULT); return INADDR_NONE; }
    if (text[0] == ' ' && !text[1]) return 0;      /* the one-space text gives 0 (review Z8) */
    if (!*text) return INADDR_NONE;
    uint64_t part[4];
    int n = 0;
    const char *s = text;
    for (;;) {
        int base = 10;
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0' && s[1] >= '0' && s[1] <= '9') { base = 8; s++; }
        uint64_t v = 0;
        int digits = 0;
        for (;; s++, digits++) {
            int d = hexval(*s);
            if (d < 0 || d >= base) break;
            v = v * (unsigned)base + (unsigned)d;
            if (v > 0xffffffffu) return INADDR_NONE;
        }
        if (!digits) return INADDR_NONE;
        part[n++] = v;
        if (*s != '.') break;
        if (n == 4) return INADDR_NONE;
        s++;
    }
    for (int i = 0; i < n - 1; i++) if (part[i] > 255) return INADDR_NONE;
    if (part[n - 1] > (0xffffffffu >> (8 * (n - 1)))) return INADDR_NONE;
    if (*s && !inet_white(*s)) return INADDR_NONE;   /* white space ends the text: what follows is ignored */
    uint32_t host = (uint32_t)part[n - 1];
    for (int i = 0; i < n - 1; i++) host |= (uint32_t)part[i] << (24 - 8 * i);
    return (ULONG)__builtin_bswap32(host);
}

const char *rt_net_addr_text(int af, const BYTE *addr, char *buf) {
    return rt_net_ntop(af, addr, buf, 65) ? buf : "?";
}
