/* core/net_host.c: gethostname, getaddrinfo, freeaddrinfo.  Specification: rt/net.h.
 * Where the expected values come from: the comments of the three functions in rt/net.h, each
 * of which names its source (Microsoft's pages of gethostname and getaddrinfo; Wine
 * ws2_32/tests/protocol.c test_getaddrinfo and test_gethostname; the reviews' rows NV25, NV26,
 * D22, D35, Z1; "from memory" where the comment says so: T13, T14); the lookups the real game
 * made (its own name and "localhost" with no service from engine+0x41fa72, hosts of the
 * internet with service "443" from libcurl's resolver threads): the recording,
 * docs/trace/net-calls.md.  Which names exist is THE MACHINE in rt/net.h.  The plan is
 * rt-contract-net.md section 5.  No host socket is opened and no name is resolved outside. */
#include <pthread.h>
#include <stdatomic.h>

#include "net_test.h"
#include "rt/env.h"

static const ADDRINFOA ENGINE_HINTS = { .ai_family = AF_INET6, .ai_flags = AI_V4MAPPED | AI_ALL };  /* engine+0x41fa55 */

/* Entry `e` is an address of family `af` with text `addr` and `port`, every other member as
 * rt/net.h says. */
static void entry_is(const ADDRINFOA *e, int af, const char *addr, USHORT port, const ADDRINFOA *hints) {
    T_REQUIRE(e != NULL);
    T_EQ(e->ai_flags, 0);
    T_EQ(e->ai_family, af);
    T_EQ(e->ai_socktype, hints ? hints->ai_socktype : 0);
    T_EQ(e->ai_protocol, hints ? hints->ai_protocol : 0);
    T_EQ(e->ai_addrlen, af == AF_INET ? 16 : 28);
    T_TRUE(e->ai_canonname == NULL);
    T_REQUIRE(e->ai_addr != NULL);
    SOCKADDR_IN6 want;
    memset(&want, 0, sizeof want);
    if (af == AF_INET) {
        SOCKADDR_IN *w = (SOCKADDR_IN *)&want;
        w->sin_family = AF_INET;
        T_EQ(rt_net_pton(AF_INET, addr, &w->sin_addr), 1);
    } else {
        want.sin6_family = AF_INET6;
        T_EQ(rt_net_pton(AF_INET6, addr, &want.sin6_addr), 1);
    }
    ((BYTE *)&want)[2] = (BYTE)(port >> 8);                 /* the port, in network order */
    ((BYTE *)&want)[3] = (BYTE)port;
    T_EQ(memcmp(e->ai_addr, &want, e->ai_addrlen), 0);
}

/* A lookup that gives exactly one entry. */
static void one(const char *node, const char *service, const ADDRINFOA *hints, int af, const char *addr, USHORT port) {
    ADDRINFOA *r = NULL;
    rt_thread_set_last_error(0xdeadbeef);
    T_EQ(rt_net_getaddrinfo(node, service, hints, &r), 0);
    T_EQ(T_LAST_ERROR(), 0);                                /* review D22 */
    entry_is(r, af, addr, port, hints);
    if (r) T_TRUE(r->ai_next == NULL);
    rt_net_freeaddrinfo(r);
}

/* The local machine with the engine's hints (AF_INET6, AI_V4MAPPED | AI_ALL): two AF_INET6
 * entries of 28 bytes, ::1 and ::ffff:127.0.0.1, in either order (review census-rows H2: the
 * order is not documented, so it is not asserted). */
static void mapped_pair(const char *node) {
    static const BYTE LOOP[16] = { [15] = 1 }, MAPPED[16] = { [10] = 0xff, [11] = 0xff, [12] = 127, [15] = 1 };
    ADDRINFOA *r = NULL;
    unsigned loop = 0, mapped = 0, count = 0;
    rt_thread_set_last_error(0xdeadbeef);
    T_EQ(rt_net_getaddrinfo(node, NULL, &ENGINE_HINTS, &r), 0);
    T_EQ(T_LAST_ERROR(), 0);
    for (const ADDRINFOA *e = r; e; e = e->ai_next, count++) {
        T_EQ(e->ai_flags, 0);
        T_EQ(e->ai_family, AF_INET6);
        T_EQ(e->ai_addrlen, 28);
        T_TRUE(e->ai_canonname == NULL);
        T_REQUIRE(e->ai_addr != NULL);
        const SOCKADDR_IN6 *a = (const SOCKADDR_IN6 *)e->ai_addr;
        T_EQ(a->sin6_family, AF_INET6);
        T_EQ(a->sin6_port, 0);
        loop += memcmp(a->sin6_addr.s6_addr, LOOP, 16) == 0;
        mapped += memcmp(a->sin6_addr.s6_addr, MAPPED, 16) == 0;
    }
    T_EQ(count, 2);
    T_EQ(loop, 1);
    T_EQ(mapped, 1);
    rt_net_freeaddrinfo(r);
}

/* A lookup with no hints of the local machine: ::1, then 127.0.0.1 (the order: from memory, T13). */
static void both(const char *node, const char *service, USHORT port) {
    ADDRINFOA *r = NULL;
    rt_thread_set_last_error(0xdeadbeef);
    T_EQ(rt_net_getaddrinfo(node, service, NULL, &r), 0);
    T_EQ(T_LAST_ERROR(), 0);
    entry_is(r, AF_INET6, "::1", port, NULL);
    T_REQUIRE(r != NULL);
    entry_is(r->ai_next, AF_INET, "127.0.0.1", port, NULL);
    if (r->ai_next) T_TRUE(r->ai_next->ai_next == NULL);
    rt_net_freeaddrinfo(r);                                 /* 11: a two-entry list */
}

static void fails(const char *node, const char *service, const ADDRINFOA *hints, DWORD code) {
    ADDRINFOA *r = (ADDRINFOA *)(uintptr_t)0x1234;
    LE_SET();
    T_EQ(rt_net_getaddrinfo(node, service, hints, &r), (int)code);
    T_EQ(T_LAST_ERROR(), code);                             /* returned, and set as well */
    T_TRUE(r == NULL);
}

TEST(core, net_host_names)
{
    char own[256] = { 0 }, buf[0x200];
    DWORD err = 0;

    T_PLAN(11, "freeaddrinfo(NULL) before any start, and a lookup before any start") {
        rt_net_freeaddrinfo(NULL);                          /* the engine frees the pointer of a failed lookup */
        fails("localhost", NULL, NULL, WSANOTINITIALISED);
        FAILS(rt_net_gethostname(buf, sizeof buf), WSANOTINITIALISED);
    }
    net_start();
    DWORD n = rt_env_get_a("COMPUTERNAME", own, sizeof own, &err);
    T_REQUIRE(n > 0 && n < sizeof own);

    T_PLAN(1, "gethostname is COMPUTERNAME") {
        memset(buf, 0xAA, sizeof buf);
        LE_SET();
        T_EQ(rt_net_gethostname(buf, 0x200), 0);            /* engine+0x219e7f passes 512 */
        LE_UNTOUCHED();                                     /* this runtime's rule: rt/net.h, LAST ERROR */
        T_EQ(strcmp(buf, own), 0);
        T_EQ((BYTE)buf[n + 1], 0xAA);
        memset(buf, 0xAA, sizeof buf);
        FAILS(rt_net_gethostname(buf, (int)n), WSAEFAULT);  /* no room for the terminator */
        T_EQ((BYTE)buf[0], 0xAA);
        T_EQ(rt_net_gethostname(buf, (int)n + 1), 0);
        FAILS(rt_net_gethostname(NULL, 0x200), WSAEFAULT);
        FAILS(rt_net_gethostname(buf, -1), WSAEFAULT);
    }

    T_PLAN(2, "a host of the internet does not exist: the code, and one line per name") {
        fails("example.invalid", NULL, &ENGINE_HINTS, WSAHOST_NOT_FOUND);
        T_TRUE(T_LOGGED(RT_CH_NET, "getaddrinfo example.invalid: no such host"));
        unsigned lines = T_LOG_COUNT(RT_CH_NET);
        fails("example.invalid", NULL, &ENGINE_HINTS, WSAHOST_NOT_FOUND);
        T_EQ(T_LOG_COUNT(RT_CH_NET), lines);
        ADDRINFOA curl = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };     /* libcurl's resolver thread */
        fails("api.example.invalid", "443", &curl, WSAHOST_NOT_FOUND);
        T_EQ(T_LOG_COUNT(RT_CH_NET), lines + 1);
        fails("192.168.1.253:1024", NULL, NULL, WSAHOST_NOT_FOUND);     /* a port after an IPv4 address is a name */
        fails("[beef::cafe", NULL, NULL, WSAHOST_NOT_FOUND);            /* so is an unmatched bracket */
    }

    T_PLAN(3, "the local machine: localhost, the own name, the empty name") {
        char upper[256];
        for (unsigned i = 0; i <= n; i++) upper[i] = own[i] >= 'a' && own[i] <= 'z' ? (char)(own[i] - 32) : own[i];
        for (const char **p = (const char *[]){ "localhost", "LOCALHOST", own, upper, "", NULL }; *p; p++) {
            mapped_pair(*p);                                    /* the engine's two lookups (review census-rows H2) */
            ADDRINFOA only_mapped = { .ai_family = AF_INET6, .ai_flags = AI_V4MAPPED }, plain6 = { .ai_family = AF_INET6 };
            one(*p, NULL, &only_mapped, AF_INET6, "::1", 0);    /* without AI_ALL the IPv6 answer is enough */
            one(*p, NULL, &plain6, AF_INET6, "::1", 0);
            both(*p, NULL, 0);
            ADDRINFOA h4 = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM, .ai_protocol = IPPROTO_UDP };
            one(*p, NULL, &h4, AF_INET, "127.0.0.1", 0);
        }
        both("", "0", 0);                                   /* review D35 */
        one("[::1]", NULL, NULL, AF_INET6, "::1", 0);
        one("[::1]", "80", NULL, AF_INET6, "::1", 80);      /* win-behaviour-check-5 Z1 */
        one("[beef::cafe]:1024", "80", NULL, AF_INET6, "beef::cafe", 80);
        ADDRINFOA h4 = { .ai_family = AF_INET };
        fails("[::1]", NULL, &h4, WSAHOST_NOT_FOUND);
        T_TRAPS(fails("[::1]x", NULL, NULL, 0), "rt_net_getaddrinfo: not implemented");
    }

    T_PLAN(4, "an address in text, and the service") {
        ADDRINFOA h = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM, .ai_protocol = IPPROTO_TCP };
        one("127.0.0.1", "80", &h, AF_INET, "127.0.0.1", 80);
        one("127.0.0.1", "", NULL, AF_INET, "127.0.0.1", 0);
        one("192.0.2.7", "65535", NULL, AF_INET, "192.0.2.7", 65535);   /* an address needs no route to be an address */
        one("2001:db8::7", "443", NULL, AF_INET6, "2001:db8::7", 443);

        T_CASE("5: an IPv4 address asked for as AF_INET6");
        ADDRINFOA mapped = { .ai_family = AF_INET6, .ai_flags = AI_V4MAPPED }, plain = { .ai_family = AF_INET6 };
        one("10.1.2.3", NULL, &mapped, AF_INET6, "::ffff:10.1.2.3", 0);
        fails("10.1.2.3", NULL, &plain, WSAHOST_NOT_FOUND);  /* from memory: review T14 */

        T_CASE("6: an IPv6 address asked for as AF_INET");
        ADDRINFOA h4 = { .ai_family = AF_INET };
        fails("::1", NULL, &h4, WSAHOST_NOT_FOUND);
    }

    T_PLAN(7, "no node") {
        ADDRINFOA passive = { .ai_family = AF_INET6, .ai_flags = AI_PASSIVE }, passive4 = { .ai_family = AF_INET, .ai_flags = AI_PASSIVE };
        one(NULL, "0", &passive, AF_INET6, "::", 0);
        one(NULL, "37015", &passive4, AF_INET, "0.0.0.0", 37015);
        both(NULL, "0", 0);
        both(NULL, "", 0);                                  /* review D35 */
        fails(NULL, NULL, NULL, WSAHOST_NOT_FOUND);
    }

    T_PLAN(8, "hints that name no family or type") {
        ADDRINFOA h = { .ai_family = 99 };
        fails("localhost", NULL, &h, WSAEAFNOSUPPORT);
        h = (ADDRINFOA){ .ai_socktype = 3 };
        fails("localhost", NULL, &h, WSAESOCKTNOSUPPORT);
    }

    T_PLAN(9, "the stops") {
        ADDRINFOA *r = NULL;
        ADDRINFOA h = { .ai_flags = 0x10000 };
        T_TRAPS(rt_net_getaddrinfo("localhost", "http", NULL, &r), "rt_net_getaddrinfo: not implemented: service \"http\"");
        T_TRAPS(rt_net_getaddrinfo("localhost", "65536", NULL, &r), "rt_net_getaddrinfo: not implemented");
        T_TRAPS(rt_net_getaddrinfo("localhost", NULL, &h, &r), "rt_net_getaddrinfo: not implemented");
        h.ai_flags = AI_CANONNAME;
        T_TRAPS(rt_net_getaddrinfo("localhost", NULL, &h, &r), "rt_net_getaddrinfo: not implemented");
        h.ai_flags = AI_ADDRCONFIG;
        T_TRAPS(rt_net_getaddrinfo("localhost", NULL, &h, &r), "rt_net_getaddrinfo: not implemented");
        T_TRAPS(rt_net_getaddrinfo("127.0.0.1", NULL, &h, &r), "rt_net_getaddrinfo: not implemented");
        T_TRAPS(rt_net_getaddrinfo("localhost", NULL, NULL, NULL), "rt_net_getaddrinfo: not implemented");
    }

    T_PLAN(10, "AI_NUMERICHOST with a name: the code and no line") {
        ADDRINFOA h = { .ai_flags = AI_NUMERICHOST };
        unsigned lines = T_LOG_COUNT(RT_CH_NET);
        fails("numeric.example.invalid", NULL, &h, WSAHOST_NOT_FOUND);
        T_EQ(T_LOG_COUNT(RT_CH_NET), lines);
        one("127.0.0.1", "80", &h, AF_INET, "127.0.0.1", 80);
    }
}

static atomic_uint host_bad;

static void *looker(void *arg) {
    (void)arg;
    for (int i = 0; i < 200; i++) {
        ADDRINFOA *r = NULL;
        if (rt_net_getaddrinfo("localhost", "80", NULL, &r) != 0 || !r || !r->ai_next || r->ai_family != AF_INET6
            || r->ai_next->ai_family != AF_INET) atomic_fetch_add(&host_bad, 1);
        rt_net_freeaddrinfo(r);
        r = NULL;
        if (rt_net_getaddrinfo("threads.example.invalid", NULL, NULL, &r) != WSAHOST_NOT_FOUND || r) atomic_fetch_add(&host_bad, 1);
        rt_net_freeaddrinfo(r);
    }
    return NULL;
}

TEST(core, net_host_threads)
{
    T_PLAN(12, "8 threads look up a local and an unknown name 200 times each") {
        net_start();
        pthread_t t[8];
        for (int i = 0; i < 8; i++) T_EQ(pthread_create(&t[i], NULL, looker, NULL), 0);
        for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
        T_EQ(atomic_load(&host_bad), 0);
        T_EQ(T_LOG_COUNT(RT_CH_NET), 1);                    /* one line for the one unknown name */
    }
}
