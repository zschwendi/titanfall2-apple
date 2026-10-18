#include <string.h>

#include "rt/test.h"
#include "rt/net.h"
#include "rt/time.h"

/* Datagram sockets only: a datagram socket is always writable, and readable once a datagram
 * is queued on it.  The bound socket is at 127.0.0.1 port 4000 (0xA00F in network order as a
 * USHORT).  No host socket and no packet: rt_net_sendto queues on the core's own port. */
#define PORT_4000  0xA00F

static void set_add(FD_SET *f, SOCKET s)
{
    f->fd_array[f->fd_count++] = s;
}

TEST(wsock32, select)
{
    WSADATA d;
    FD_SET r, w, e, keep_r, keep_w, keep_e;
    TIMEVAL tv0, tvLong, tvNeg, tvCopy;
    SOCKET a, b, c;
    SOCKADDR_IN sa;
    struct { FD_SET s; SOCKET extra[200]; } big;
    uint64_t t0;

    T_EQ(rt_net_startup(0x0202, &d), 0);
    a = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    b = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    c = rt_net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = PORT_4000;
    sa.sin_addr.s_b[0] = 127;
    sa.sin_addr.s_b[3] = 1;
    T_EQ(rt_net_bind(a, (const SOCKADDR *)&sa, sizeof sa), 0);

    tv0.tv_sec = 0;  tv0.tv_usec = 0;
    tvLong.tv_sec = 0;  tvLong.tv_usec = 20000;
    tvNeg.tv_sec = -1;  tvNeg.tv_usec = 0;

    T_CASE("three NULL sets and three empty sets get WSAEINVAL");
    T_EQ(CALL(wsock32, select, 0, NULL, NULL, NULL, &tv0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);
    memset(&r, 0, sizeof r); memset(&w, 0, sizeof w); memset(&e, 0, sizeof e);
    T_EQ(CALL(wsock32, select, 0, &r, &w, &e, &tv0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);

    T_CASE("a timeout with a negative member gets WSAEINVAL");
    memset(&r, 0, sizeof r);
    set_add(&r, a);
    T_EQ(CALL(wsock32, select, 0, &r, NULL, NULL, &tvNeg), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);
    tvNeg.tv_sec = 0;  tvNeg.tv_usec = -1;
    T_EQ(CALL(wsock32, select, 0, &r, NULL, NULL, &tvNeg), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEINVAL);

    T_CASE("a zero timeout does not wait and empties an unready set");
    memset(&r, 0, sizeof r);
    set_add(&r, a);
    T_EQ(CALL(wsock32, select, 0, &r, NULL, NULL, &tv0), 0);
    T_EQ(r.fd_count, 0);

    T_CASE("a timeout of 20 ms waits and then gives 0 with the set emptied");
    memset(&r, 0, sizeof r);
    set_add(&r, a);
    tvCopy = tvLong;
    t0 = rt_time_tick_ms();
    T_EQ(CALL(wsock32, select, 0, &r, NULL, NULL, &tvLong), 0);
    T_TRUE(rt_time_tick_ms() - t0 >= 20);
    T_EQ(r.fd_count, 0);
    T_TRUE(tvLong.tv_sec == tvCopy.tv_sec && tvLong.tv_usec == tvCopy.tv_usec);

    T_CASE("a datagram socket in writefds is ready and stays in the set");
    CALL(kernel32, SetLastError, 0x1234);
    memset(&w, 0, sizeof w);
    set_add(&w, a);
    T_EQ(CALL(wsock32, select, 0, NULL, &w, NULL, &tv0), 1);
    T_EQ(w.fd_count, 1);
    T_TRUE(w.fd_array[0] == a);

    T_CASE("success leaves last error alone");
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a queued datagram makes readfds ready");
    T_EQ(rt_net_sendto(b, "x", 1, 0, (const SOCKADDR *)&sa, sizeof sa), 1);
    memset(&r, 0, sizeof r);
    set_add(&r, a);
    set_add(&r, b);
    T_EQ(CALL(wsock32, select, 0, &r, NULL, NULL, &tv0), 1);
    T_EQ(r.fd_count, 1);
    T_TRUE(r.fd_array[0] == a);

    T_CASE("a socket listed twice in one set counts once");
    memset(&r, 0, sizeof r);
    set_add(&r, a);
    set_add(&r, a);
    T_EQ(CALL(wsock32, select, 0, &r, NULL, NULL, &tv0), 1);

    T_CASE("nfds is ignored");
    memset(&r, 0, sizeof r);
    set_add(&r, a);
    T_EQ(CALL(wsock32, select, 0, &r, NULL, NULL, &tv0), 1);
    memset(&r, 0, sizeof r);
    set_add(&r, a);
    T_EQ(CALL(wsock32, select, 1000, &r, NULL, NULL, &tv0), 1);

    T_CASE("the TIMEVAL is unchanged");
    memset(&r, 0, sizeof r);
    set_add(&r, a);
    tvLong.tv_sec = 0;  tvLong.tv_usec = 5000;
    T_EQ(CALL(wsock32, select, 0, &r, NULL, NULL, &tvLong), 1);
    T_TRUE(tvLong.tv_sec == 0 && tvLong.tv_usec == 5000);

    T_CASE("a socket closed before the call gets WSAENOTSOCK and the sets are unchanged");
    T_EQ(rt_net_close(c), 0);
    memset(&r, 0, sizeof r); memset(&w, 0, sizeof w); memset(&e, 0, sizeof e);
    set_add(&r, a);
    set_add(&r, c);
    set_add(&w, c);
    set_add(&e, c);
    keep_r = r; keep_w = w; keep_e = e;
    T_EQ(CALL(wsock32, select, 0, &r, &w, &e, &tv0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAENOTSOCK);
    T_TRUE(memcmp(&r, &keep_r, sizeof r) == 0);
    T_TRUE(memcmp(&w, &keep_w, sizeof w) == 0);
    T_TRUE(memcmp(&e, &keep_e, sizeof e) == 0);

    /* fd_count 65 STOPS in the core (Windows accepts 200): proved here, not guarded. */
    memset(&big, 0, sizeof big);
    big.s.fd_count = 65;
    T_TRAPS(CALL(wsock32, select, 0, &big.s, NULL, NULL, &tv0), "rt_net_select: not implemented");

    rt_net_close(a);
    rt_net_close(b);
}
