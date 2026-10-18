/* Sockets, host names: gethostname, getaddrinfo, freeaddrinfo.
 * Specification: rt/net.h.  Shared internals: core/net_priv.h (in core/net.c).
 * BUILT (unit rt-net-core).  The test is tests/core/net_host.c.
 *
 * No resolver exists: a name is an address in text, the local machine ("", "localhost", the
 * value of COMPUTERNAME), or unknown.  An unknown name is logged once (channel net), which
 * is how a run lists the hosts the game asks for.
 */
#include <string.h>

#include "net_priv.h"
#include "rt/env.h"
#include "rt/heap.h"

#define NAME_MAX_BYTES 256

/* The machine's name: COMPUTERNAME as the runtime has it.  Its length, without terminator. */
static unsigned own_name(char *buf) {
    DWORD err = 0;
    DWORD n = rt_env_get_a("COMPUTERNAME", buf, NAME_MAX_BYTES, &err);
    if (n == 0 || n >= NAME_MAX_BYTES) RT_NET_STOP("rt_net_gethostname", "COMPUTERNAME is not set or longer than %d bytes", NAME_MAX_BYTES - 1);
    return n;
}

static bool started(void) {
    rt_net_lock();
    bool yes = rt_net_started();
    rt_net_unlock();
    return yes;
}

int rt_net_gethostname(char *name, int namelen) {
    if (!started()) return rt_net_fail(WSANOTINITIALISED);
    char own[NAME_MAX_BYTES];
    unsigned n = own_name(own);
    if (!name || namelen < 0 || (unsigned)namelen < n + 1) return rt_net_fail(WSAEFAULT);   /* the buffer is unchanged */
    memcpy(name, own, n + 1);
    return 0;                                       /* last error is left alone: rt/net.h, LAST ERROR */
}

static bool same_nocase(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
        if (!x) return true;
    }
}

/* One entry: the ADDRINFOA and its address in one block of the process heap. */
static ADDRINFOA *entry(int af, const BYTE *addr, USHORT port, const ADDRINFOA *hints) {
    SIZE_T alen = (SIZE_T)rt_net_namelen(af);
    ADDRINFOA *ai = rt_heap_alloc(rt_heap_process(), 0, sizeof *ai + alen);
    if (!ai) RT_NET_STOP("rt_net_getaddrinfo", "out of memory");
    memset(ai, 0, sizeof *ai);
    ai->ai_family = af;
    ai->ai_socktype = hints ? hints->ai_socktype : 0;
    ai->ai_protocol = hints ? hints->ai_protocol : 0;
    ai->ai_addrlen = alen;
    ai->ai_addr = (SOCKADDR *)(ai + 1);
    rt_net_name_write(af, ai->ai_addr, addr, port);
    return ai;
}

static int host_fail(DWORD code) { rt_thread_set_last_error(code); return (int)code; }

int rt_net_getaddrinfo(const char *node, const char *service, const ADDRINFOA *hints, ADDRINFOA **result) {
    if (!result) RT_NET_STOP("rt_net_getaddrinfo", "result is NULL");
    *result = NULL;
    if (!started()) return host_fail(WSANOTINITIALISED);
    int family = hints ? hints->ai_family : AF_UNSPEC, flags = hints ? hints->ai_flags : 0;
    if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_ALL | AI_V4MAPPED | AI_ADDRCONFIG))
        RT_NET_STOP("rt_net_getaddrinfo", "ai_flags 0x%x", (unsigned)flags);
    if (flags & AI_CANONNAME) RT_NET_STOP("rt_net_getaddrinfo", "AI_CANONNAME");
    if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) return host_fail(WSAEAFNOSUPPORT);
    if (hints && hints->ai_socktype != 0 && hints->ai_socktype != SOCK_STREAM && hints->ai_socktype != SOCK_DGRAM)
        return host_fail(WSAESOCKTNOSUPPORT);

    unsigned port = 0;
    if (service && *service) {
        const char *p = service;
        for (; *p >= '0' && *p <= '9' && port <= 65535; p++) port = port * 10 + (unsigned)(*p - '0');
        if (*p || port > 65535) RT_NET_STOP("rt_net_getaddrinfo", "service \"%s\" is not a port number", service);
    }

    /* What the node is: an IPv4 address, an IPv6 address, or the local machine (loopback, or
     * the unspecified address for a NULL node with AI_PASSIVE). */
    enum { V4, V6, LOCAL } what;
    BYTE a4[4] = { 127, 0, 0, 1 }, a6[16] = { [15] = 1 };
    if (!node) {
        if (!service) return host_fail(WSAHOST_NOT_FOUND);
        what = LOCAL;
        if (flags & AI_PASSIVE) { memset(a4, 0, 4); memset(a6, 0, 16); }
    } else {
        char own[NAME_MAX_BYTES], inner[64];
        const char *close = node[0] == '[' ? strchr(node, ']') : NULL;
        if (rt_net_pton(AF_INET, node, a4) == 1) what = V4;
        else if (rt_net_pton(AF_INET6, node, a6) == 1) what = V6;
        else if (close && (size_t)(close - node - 1) < sizeof inner
                 && (memcpy(inner, node + 1, (size_t)(close - node - 1)), inner[close - node - 1] = 0,
                     rt_net_pton(AF_INET6, inner, a6) == 1)) {
            /* "[addr]" and "[addr]:port" (review D35, win-behaviour-check-5 Z1): the address;
             * the port stays the one from `service`. */
            const char *q = close + 1;
            if (*q == ':') for (q++; *q >= '0' && *q <= '9'; q++) { }
            if (*q) RT_NET_STOP("rt_net_getaddrinfo", "text after the bracket of an address: \"%s\"", node);
            what = V6;
        }
        else if (!*node || same_nocase(node, "localhost") || (own_name(own), same_nocase(node, own))) what = LOCAL;
        else {
            if (!(flags & AI_NUMERICHOST)) {
                rt_net_note(RT_NC_LOOKUPS_UNKNOWN); /* a name of another machine: answered here, no resolver exists */
                rt_log_once(RT_CH_NET, RT_LL_INFO, node, "getaddrinfo %s: no such host", node);
            }
            return host_fail(WSAHOST_NOT_FOUND);
        }
    }
    if (flags & AI_ADDRCONFIG)
        RT_NET_STOP("rt_net_getaddrinfo", "AI_ADDRCONFIG with an address or a local name (\"%s\")", node ? node : "(null)");

    ADDRINFOA *first = NULL;
    if (what == V4) {
        if (family == AF_INET6) {
            if (!(flags & AI_V4MAPPED)) return host_fail(WSAHOST_NOT_FOUND);
            BYTE mapped[16] = { [10] = 0xff, [11] = 0xff };
            memcpy(mapped + 12, a4, 4);
            first = entry(AF_INET6, mapped, (USHORT)port, hints);
        } else first = entry(AF_INET, a4, (USHORT)port, hints);
    } else if (what == V6) {
        if (family == AF_INET) return host_fail(WSAHOST_NOT_FOUND);
        first = entry(AF_INET6, a6, (USHORT)port, hints);
    } else {                                        /* both families exist; AF_INET6 first for AF_UNSPEC */
        if (family != AF_INET) first = entry(AF_INET6, a6, (USHORT)port, hints);
        if (family != AF_INET6) {
            ADDRINFOA *v4 = entry(AF_INET, a4, (USHORT)port, hints);
            if (first) first->ai_next = v4; else first = v4;
        } else if ((flags & (AI_V4MAPPED | AI_ALL)) == (AI_V4MAPPED | AI_ALL) && a4[0] == 127) {
            /* The engine's hints.  Loopback has an IPv4 address too, and AI_ALL asks for it in
             * IPv4-mapped form beside ::1 (review census-rows H2, from the page of getaddrinfo);
             * the unspecified address of a passive NULL node has no IPv4 twin to map. */
            BYTE mapped[16] = { [10] = 0xff, [11] = 0xff };
            memcpy(mapped + 12, a4, 4);
            first->ai_next = entry(AF_INET6, mapped, (USHORT)port, hints);
        }
    }
    *result = first;
    rt_thread_set_last_error(0);                    /* rt/net.h, LAST ERROR (review D22) */
    return 0;
}

void rt_net_freeaddrinfo(ADDRINFOA *list) {
    while (list) {
        ADDRINFOA *next = list->ai_next;
        rt_heap_free(rt_heap_process(), 0, list);
        list = next;
    }
}
