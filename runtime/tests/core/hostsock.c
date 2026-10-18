/* core/hostsock.c and the rule it measures: NO SOCKET OF THE GUEST REACHES THE NETWORK
 * (rt/hostsock.h, THE RULE; unit gate3-run-7, decision E).
 * Where the expected values come from:
 *   - the rule itself: the planner's decision "a start without a network"
 *     (docs/handoff/rt-contract-net.md, section 2, option C: "No packet can leave, by
 *     construction") and the brief of gate3-run-7, section 0;
 *   - the guest's side (what bind, getsockname, sendto, connect and getaddrinfo answer): the
 *     comments of those functions in rt/net.h, as tests/core/net_addr.c and net_dgram.c have
 *     them; the engine's calls (family 23, port 37015; family 2) are the recording's,
 *     docs/trace/net-calls.md section 1;
 *   - the host's side: the kernel, asked through the census: a descriptor that is a socket is
 *     counted, so "no host socket was made" is the count before and after being equal.  The
 *     second test shows the census does count sockets when some exist.
 *   - 192.0.2.1 and 2001:db8::1 are addresses reserved for documentation (RFC 5737, RFC 3849):
 *     public in form, routed nowhere.  Nothing is sent to them here or anywhere. */
#include "net_test.h"
#include "rt/hostsock.h"

int rt_test_hostsock_open(int kind);        /* tests/core/hostsock_host.c */
int rt_test_hostsock_pair(int fds[2]);
void rt_test_hostsock_close(int fd);

static void same_sockets(const RtHostSockCensus *a, const RtHostSockCensus *b) {
    T_EQ(a->sockets, b->sockets);
    T_EQ(a->inet, b->inet);
    T_EQ(a->inet_loopback, b->inet_loopback);
    T_EQ(a->local, b->local);
    T_EQ(a->other, b->other);
}

TEST(core, hostsock_rule)
{
    RtHostSockCensus before, after;
    unsigned n0[RT_NC_N], n1[RT_NC_N];
    rt_hostsock_census(&before);
    rt_net_counts(n0);
    net_start();

    T_CASE("the engine's bind of the wildcard address (:: port 37015, and 0.0.0.0): the guest is told the address it asked for, and the host has no socket more");
    SOCKET s6 = rt_net_socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET s4 = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_REQUIRE(s6 != INVALID_SOCKET && s4 != INVALID_SOCKET);
    SOCKADDR_IN6 any6 = v6("::", 37015), got6;
    SOCKADDR_IN any4 = v4(0, 0, 0, 0, 37016), got4;
    T_EQ(rt_net_bind(s6, SA(any6), sizeof any6), 0);
    T_EQ(rt_net_bind(s4, SA(any4), sizeof any4), 0);
    int len = sizeof got6;
    memset(&got6, 0xAA, sizeof got6);
    T_EQ(rt_net_getsockname(s6, SA(got6), &len), 0);
    T_EQ(memcmp(&got6.sin6_addr, &any6.sin6_addr, 16), 0);
    T_EQ(port_of(&got6), 37015);
    len = sizeof got4;
    memset(&got4, 0xAA, sizeof got4);
    T_EQ(rt_net_getsockname(s4, SA(got4), &len), 0);
    T_EQ(memcmp(&got4.sin_addr, &any4.sin_addr, 4), 0);
    T_EQ(port_of(&got4), 37016);
    rt_hostsock_census(&after);
    same_sockets(&before, &after);

    T_CASE("a send to a public address is WSAENETUNREACH and makes no host socket (so no host send: there is nothing to send it with)");
    SOCKADDR_IN pub4 = v4(192, 0, 2, 1, 37015);
    SOCKADDR_IN6 pub6 = v6("2001:db8::1", 37015);
    FAILS(rt_net_sendto(s4, "ping", 4, 0, SA(pub4), sizeof pub4), WSAENETUNREACH);
    FAILS(rt_net_sendto(s6, "ping", 4, 0, SA(pub6), sizeof pub6), WSAENETUNREACH);
    rt_hostsock_census(&after);
    same_sockets(&before, &after);

    T_CASE("a connect to a public address, datagram and stream, is WSAENETUNREACH and makes no host socket");
    SOCKET t = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    T_REQUIRE(t != INVALID_SOCKET);
    FAILS(rt_net_connect(s4, SA(pub4), sizeof pub4), WSAENETUNREACH);
    SOCKADDR_IN web = v4(192, 0, 2, 1, 443);
    FAILS(rt_net_connect(t, SA(web), sizeof web), WSAENETUNREACH);
    rt_hostsock_census(&after);
    same_sockets(&before, &after);

    T_CASE("the name of another machine is WSAHOST_NOT_FOUND at once; localhost is this machine; no host socket either way");
    ADDRINFOA *res = NULL;
    T_EQ(rt_net_getaddrinfo("example.invalid", "443", NULL, &res), WSAHOST_NOT_FOUND);
    T_EQ(rt_net_getaddrinfo("localhost", NULL, NULL, &res), 0);
    T_REQUIRE(res != NULL);
    rt_net_freeaddrinfo(res);
    rt_hostsock_census(&after);
    same_sockets(&before, &after);

    T_CASE("a datagram to loopback stays inside the process: the guest's own socket receives it, and the host still has no socket more");
    SOCKADDR_IN6 self = v6("::1", 37015);
    char buf[8];
    T_EQ(rt_net_sendto(s6, "self", 4, 0, SA(self), sizeof self), 4);
    T_EQ(rt_net_recvfrom(s6, buf, sizeof buf, 0, NULL, NULL), 4);
    T_EQ(memcmp(buf, "self", 4), 0);
    rt_hostsock_census(&after);
    same_sockets(&before, &after);

    T_CASE("the guest's counts, as the line of a run has them");
    rt_net_counts(n1);
    T_EQ(n1[RT_NC_SOCKETS] - n0[RT_NC_SOCKETS], 3);
    T_EQ(n1[RT_NC_BINDS] - n0[RT_NC_BINDS], 2);
    T_EQ(n1[RT_NC_REFUSED_SENDS] - n0[RT_NC_REFUSED_SENDS], 2);
    T_EQ(n1[RT_NC_REFUSED_CONNECTS] - n0[RT_NC_REFUSED_CONNECTS], 2);
    T_EQ(n1[RT_NC_LOOKUPS_UNKNOWN] - n0[RT_NC_LOOKUPS_UNKNOWN], 1);
    T_EQ(n1[RT_NC_LOOP_SENDS] - n0[RT_NC_LOOP_SENDS], 1);

    T_EQ(rt_net_close(s6), 0);
    T_EQ(rt_net_close(s4), 0);
    T_EQ(rt_net_close(t), 0);
    T_EQ(rt_net_cleanup(), 0);
}

TEST(core, hostsock_census)
{
    RtHostSockCensus base, c;
    rt_hostsock_census(&base);
    unsigned zero[RT_NC_N] = { 0 };
    char line[900];

    T_CASE("the line of a process with no socket of an internet family that is not loopback");
    size_t at = rt_hostsock_line(line, sizeof line, 0, zero, &base);
    T_TRUE(at > 0 && at < sizeof line);
    T_EQ(base.inet - base.inet_loopback, 0);                /* this test program has none */
    T_TRUE(strstr(line, "net: host sockets: made for the guest: 0 ") == line);
    T_TRUE(strstr(line, "all loopback: yes") != NULL);
    T_TRUE(strstr(line, "THE RULE IS BROKEN") == NULL);

    T_CASE("a host socket bound to 127.0.0.1 and one bound to ::1 are counted, as loopback");
    int a = rt_test_hostsock_open(0), b = rt_test_hostsock_open(1);
    T_REQUIRE(a >= 0 && b >= 0);
    rt_hostsock_census(&c);
    T_EQ(c.sockets - base.sockets, 2);
    T_EQ(c.inet - base.inet, 2);
    T_EQ(c.inet_loopback - base.inet_loopback, 2);
    T_EQ(c.first_fd, -1);

    T_CASE("a host socket of an internet family that is not bound is counted as NOT loopback, and named");
    int u = rt_test_hostsock_open(2);
    T_REQUIRE(u >= 0);
    rt_hostsock_census(&c);
    T_EQ(c.inet - base.inet, 3);
    T_EQ(c.inet_loopback - base.inet_loopback, 2);
    T_EQ(c.first_fd, u);
    T_EQ(strcmp(c.first_text, "0.0.0.0 port 0"), 0);
    at = rt_hostsock_line(line, sizeof line, 0, zero, &c);
    T_TRUE(at > 0 && at < sizeof line);
    T_TRUE(strstr(line, "1 NOT loopback") != NULL);
    T_TRUE(strstr(line, "THE RULE IS BROKEN OR A HOST LIBRARY HAS A SOCKET: descriptor ") != NULL);
    T_TRUE(strstr(line, " is 0.0.0.0 port 0") != NULL);
    T_TRUE(strstr(line, "all loopback: yes") == NULL);

    T_CASE("a unix-domain pair is counted apart");
    int pair[2];
    T_REQUIRE(rt_test_hostsock_pair(pair) == 0);
    rt_hostsock_census(&c);
    T_EQ(c.local - base.local, 2);
    T_EQ(c.inet - base.inet, 3);

    T_CASE("closed again: the census is the first one");
    rt_test_hostsock_close(a);
    rt_test_hostsock_close(b);
    rt_test_hostsock_close(u);
    rt_test_hostsock_close(pair[0]);
    rt_test_hostsock_close(pair[1]);
    rt_hostsock_census(&c);
    same_sockets(&base, &c);
    T_EQ(c.first_fd, -1);
}
