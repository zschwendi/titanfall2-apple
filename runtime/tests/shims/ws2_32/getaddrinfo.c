#include <string.h>

#include "rt/test.h"
#include "rt/net.h"
#include "rt/win/net.h"

/* Entry e is one AF_INET6 address `want` on `port` (network order: port 80 is 0x5000), with every
 * member rt/net.h says: ai_flags 0, ai_socktype and ai_protocol from the hints (0 in every case
 * here), ai_addrlen 28, ai_canonname NULL, and the other members of the address 0.  The caller
 * checks ai_next. */
static void v6_entry(const ADDRINFOA *e, const unsigned char want[16], USHORT port)
{
    const SOCKADDR_IN6 *a;

    T_REQUIRE(e != NULL);
    T_EQ(e->ai_flags, 0);
    T_EQ(e->ai_family, AF_INET6);
    T_EQ(e->ai_socktype, 0);
    T_EQ(e->ai_protocol, 0);
    T_EQ(e->ai_addrlen, 28);
    T_TRUE(e->ai_canonname == NULL);
    T_REQUIRE(e->ai_addr != NULL);
    a = (const SOCKADDR_IN6 *)e->ai_addr;
    T_EQ(a->sin6_family, AF_INET6);
    T_EQ(a->sin6_port, port);
    T_EQ(a->sin6_flowinfo, 0);
    T_EQ(a->sin6_scope_id, 0);
    T_TRUE(memcmp(a->sin6_addr.s6_addr, want, 16) == 0);
}

/* As v6_entry, for an AF_INET address `want`: ai_addrlen 16, sin_zero all 0. */
static void v4_entry(const ADDRINFOA *e, const unsigned char want[4], USHORT port)
{
    const SOCKADDR_IN *a;
    int i;

    T_REQUIRE(e != NULL);
    T_EQ(e->ai_flags, 0);
    T_EQ(e->ai_family, AF_INET);
    T_EQ(e->ai_socktype, 0);
    T_EQ(e->ai_protocol, 0);
    T_EQ(e->ai_addrlen, 16);
    T_TRUE(e->ai_canonname == NULL);
    T_REQUIRE(e->ai_addr != NULL);
    a = (const SOCKADDR_IN *)e->ai_addr;
    T_EQ(a->sin_family, AF_INET);
    T_EQ(a->sin_port, port);
    T_TRUE(memcmp(a->sin_addr.s_b, want, 4) == 0);
    for (i = 0; i < 8; i++) T_EQ(a->sin_zero[i], 0);
}

/* The engine's hints (engine+0x41fa55): AF_INET6 with AI_V4MAPPED | AI_ALL. */
static const ADDRINFOA ENGINE_HINTS = { .ai_family = AF_INET6, .ai_flags = AI_V4MAPPED | AI_ALL };
static const unsigned char LOOP6[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };   /* ::1 */
static const unsigned char LOOP4[4] = { 127, 0, 0, 1 };

TEST(ws2_32, getaddrinfo)
{
    WSADATA d;
    ADDRINFOA *r;
    ADDRINFOA sentinel, h;

    T_CASE("before WSAStartup gives WSANOTINITIALISED");
    CALL(kernel32, SetLastError, 0xDEAD);
    r = &sentinel;
    T_EQ(CALL(ws2_32, getaddrinfo, "localhost", NULL, NULL, &r), WSANOTINITIALISED);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);
    T_TRUE(r == NULL);

    T_EQ(rt_net_startup(0x0202, &d), 0);

    T_CASE("example.invalid gives WSAHOST_NOT_FOUND, NULL result and a net log line");
    CALL(kernel32, SetLastError, 0xDEAD);
    r = &sentinel;
    T_EQ(CALL(ws2_32, getaddrinfo, "example.invalid", NULL, &ENGINE_HINTS, &r), WSAHOST_NOT_FOUND);
    T_EQ(T_LAST_ERROR(), WSAHOST_NOT_FOUND);
    T_TRUE(r == NULL);
    T_TRUE(T_LOGGED(RT_CH_NET, "getaddrinfo example.invalid"));

    T_CASE("localhost with the engine's hints gives two AF_INET6 entries, ::1 and ::ffff:127.0.0.1 in either order, and last error 0");
    CALL(kernel32, SetLastError, 0xDEAD);
    r = NULL;
    T_EQ(CALL(ws2_32, getaddrinfo, "localhost", NULL, &ENGINE_HINTS, &r), 0);
    T_EQ(T_LAST_ERROR(), 0);
    {
        /* Review census-rows H2: loopback's IPv4 address comes too, in IPv4-mapped form (the
         * page of getaddrinfo, AI_ALL with AI_V4MAPPED); the order is not documented. */
        static const unsigned char MAPPED6[16] = { [10] = 0xff, [11] = 0xff, [12] = 127, [15] = 1 };
        T_REQUIRE(r != NULL && r->ai_next != NULL);
        T_TRUE(r->ai_next->ai_next == NULL);
        int first_is_loop = memcmp(((const SOCKADDR_IN6 *)r->ai_addr)->sin6_addr.s6_addr, LOOP6, 16) == 0;
        v6_entry(first_is_loop ? r : r->ai_next, LOOP6, 0);
        v6_entry(first_is_loop ? r->ai_next : r, MAPPED6, 0);
    }
    rt_net_freeaddrinfo(r);

    T_CASE("127.0.0.1 with service 80 and NULL hints gives one AF_INET entry on port 80 and last error 0");
    CALL(kernel32, SetLastError, 0xDEAD);
    r = NULL;
    T_EQ(CALL(ws2_32, getaddrinfo, "127.0.0.1", "80", NULL, &r), 0);
    T_EQ(T_LAST_ERROR(), 0);
    v4_entry(r, LOOP4, 0x5000);                 /* port 80 in network order */
    T_TRUE(r != NULL && r->ai_next == NULL);
    rt_net_freeaddrinfo(r);

    T_CASE("empty node with service 0 succeeds");
    CALL(kernel32, SetLastError, 0xDEAD);
    r = NULL;
    T_EQ(CALL(ws2_32, getaddrinfo, "", "0", NULL, &r), 0);
    T_EQ(T_LAST_ERROR(), 0);
    T_TRUE(r != NULL);
    rt_net_freeaddrinfo(r);

    T_CASE("[::1] with NULL hints gives one AF_INET6 entry ::1");
    CALL(kernel32, SetLastError, 0xDEAD);
    r = NULL;
    T_EQ(CALL(ws2_32, getaddrinfo, "[::1]", NULL, NULL, &r), 0);
    T_EQ(T_LAST_ERROR(), 0);
    v6_entry(r, LOOP6, 0);
    T_TRUE(r != NULL && r->ai_next == NULL);
    rt_net_freeaddrinfo(r);

    T_CASE("both names NULL give WSAHOST_NOT_FOUND");
    CALL(kernel32, SetLastError, 0xDEAD);
    r = &sentinel;
    T_EQ(CALL(ws2_32, getaddrinfo, NULL, NULL, NULL, &r), WSAHOST_NOT_FOUND);
    T_EQ(T_LAST_ERROR(), WSAHOST_NOT_FOUND);
    T_TRUE(r == NULL);

    T_CASE("ai_family 99 gives WSAEAFNOSUPPORT");
    CALL(kernel32, SetLastError, 0xDEAD);
    r = &sentinel;
    memset(&h, 0, sizeof h);
    h.ai_family = 99;
    T_EQ(CALL(ws2_32, getaddrinfo, "localhost", NULL, &h, &r), WSAEAFNOSUPPORT);
    T_EQ(T_LAST_ERROR(), WSAEAFNOSUPPORT);
    T_TRUE(r == NULL);

    /* The stops of the core (rt/net.h): each ends the run, and the shim adds no guard for it. */
    memset(&h, 0, sizeof h);
    T_TRAPS(CALL(ws2_32, getaddrinfo, "localhost", "http", NULL, &r), "rt_net_getaddrinfo: not implemented");
    T_TRAPS(CALL(ws2_32, getaddrinfo, "localhost", NULL, NULL, NULL), "rt_net_getaddrinfo: not implemented");
    h.ai_flags = 0x10000;
    T_TRAPS(CALL(ws2_32, getaddrinfo, "localhost", NULL, &h, &r), "rt_net_getaddrinfo: not implemented");
    h.ai_flags = AI_CANONNAME;
    T_TRAPS(CALL(ws2_32, getaddrinfo, "localhost", NULL, &h, &r), "rt_net_getaddrinfo: not implemented");
    h.ai_flags = AI_ADDRCONFIG;
    T_TRAPS(CALL(ws2_32, getaddrinfo, "localhost", NULL, &h, &r), "rt_net_getaddrinfo: not implemented");
    T_TRAPS(CALL(ws2_32, getaddrinfo, "127.0.0.1", NULL, &h, &r), "rt_net_getaddrinfo: not implemented");
    h.ai_flags = 0;
    T_TRAPS(CALL(ws2_32, getaddrinfo, "[::1]x", NULL, &h, &r), "rt_net_getaddrinfo: not implemented");
}
