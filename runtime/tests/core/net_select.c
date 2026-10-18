/* core/net_select.c: select on the runtime's own sockets.  Specification: rt/net.h.
 * Where the expected values come from: the comment of rt_net_select in rt/net.h, which names
 * its sources (Microsoft's page of select, read 2026-10-10: nfds ignored, WSAEINVAL for three
 * NULL or empty sets and for a negative time, WSAENOTSOCK, the sets rewritten, *timeout not
 * altered, {0,0} polls, NULL waits; Wine ws2_32/tests/sock.c, test_select, for a socket closed
 * during the wait: review D34); what is ready when is THE MACHINE in rt/net.h.  The two
 * shapes the real game used (write and except sets with a time, read and except sets with a
 * time, on stream sockets): docs/trace/net-calls.md section 5.
 * No host socket is opened and no packet is sent. */
#include <pthread.h>
#include <unistd.h>

#include "net_test.h"
#include "rt/time.h"

static FD_SET set_of(SOCKET a, SOCKET b, SOCKET c) {
    FD_SET s = { 0 };
    if (a) s.fd_array[s.fd_count++] = a;
    if (b) s.fd_array[s.fd_count++] = b;
    if (c) s.fd_array[s.fd_count++] = c;
    return s;
}

static void put(SOCKET from, USHORT port) {
    SOCKADDR_IN to = v4(127, 0, 0, 1, port);
    T_EQ(rt_net_sendto(from, "x", 1, 0, SA(to), sizeof to), 1);
}

TEST(core, net_select_args)
{
    FD_SET r, w, x;
    TIMEVAL now = { 0, 0 };

    T_CASE("before any start");
    r = set_of(4, 0, 0);
    FAILS(rt_net_select(0, &r, NULL, NULL, &now), WSANOTINITIALISED);
    net_start();
    SOCKET a = net_udp(AF_INET, -1, 45001, true);

    T_CASE("three NULL sets, three empty sets, a negative time");
    FAILS(rt_net_select(0, NULL, NULL, NULL, &now), WSAEINVAL);
    r = w = x = set_of(0, 0, 0);
    FAILS(rt_net_select(0, &r, &w, &x, &now), WSAEINVAL);
    FAILS(rt_net_select(0, &r, NULL, NULL, NULL), WSAEINVAL);
    r = set_of(a, 0, 0);
    FAILS(rt_net_select(0, &r, NULL, NULL, &(TIMEVAL){ -1, 0 }), WSAEINVAL);
    FAILS(rt_net_select(0, &r, NULL, NULL, &(TIMEVAL){ 0, -1 }), WSAEINVAL);
    T_EQ(r.fd_count, 1);

    T_CASE("an entry that is not a socket: WSAENOTSOCK and the sets unchanged");
    SOCKET gone = rt_net_socket(AF_INET, SOCK_DGRAM, 0);
    T_EQ(rt_net_close(gone), 0);
    r = set_of(a, gone, 0);
    w = set_of(a, 0, 0);
    FAILS(rt_net_select(0, &r, &w, NULL, &now), WSAENOTSOCK);
    T_EQ(r.fd_count, 2);
    T_TRUE(r.fd_array[0] == a && r.fd_array[1] == gone);
    T_EQ(w.fd_count, 1);
    x = set_of(0x100000004ull, 0, 0);
    FAILS(rt_net_select(0, NULL, NULL, &x, &now), WSAENOTSOCK);

    T_CASE("64 entries are taken, 65 are a stop");
    for (unsigned i = 0; i < 64; i++) w.fd_array[i] = a;
    w.fd_count = 64;
    T_EQ(rt_net_select(0, NULL, &w, NULL, &now), 1);        /* one socket listed 64 times counts once */
    T_EQ(w.fd_count, 1);
    w.fd_count = 65;
    T_TRAPS(rt_net_select(0, NULL, &w, NULL, &now), "rt_net_select: not implemented");
}

TEST(core, net_select_ready)
{
    net_start();
    SOCKET a = net_udp(AF_INET, -1, 45011, true);
    SOCKET b = net_udp(AF_INET, -1, 45012, true);
    SOCKET c = net_udp(AF_INET6, 0, 45013, true);
    SOCKET t = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    FD_SET r, w, x;
    TIMEVAL now = { 0, 0 };

    T_CASE("a poll with nothing ready: 0, the sets emptied, the time not written, last error left alone");
    r = set_of(a, b, c);
    x = set_of(a, 0, 0);
    LE_SET();
    T_EQ(rt_net_select(0, &r, NULL, &x, &now), 0);
    LE_UNTOUCHED();
    T_EQ(r.fd_count, 0);
    T_EQ(x.fd_count, 0);
    T_TRUE(now.tv_sec == 0 && now.tv_usec == 0);

    T_CASE("libcurl's two shapes on a stream socket: never ready, the time passes");
    w = x = set_of(t, 0, 0);
    T_EQ(rt_net_select(0, NULL, &w, &x, &now), 0);          /* waiting for a connect */
    T_EQ(w.fd_count + x.fd_count, 0);
    r = x = set_of(t, 0, 0);
    T_EQ(rt_net_select(0, &r, NULL, &x, &now), 0);          /* waiting for data */
    T_EQ(r.fd_count + x.fd_count, 0);

    T_CASE("a datagram socket is writable at once");
    w = set_of(a, t, c);
    T_EQ(rt_net_select(12345, NULL, &w, NULL, NULL), 2);    /* no time limit, and no wait; nfds is ignored */
    T_EQ(w.fd_count, 2);
    T_TRUE(w.fd_array[0] == a && w.fd_array[1] == c);

    T_CASE("readable when a datagram is queued: the ready ones stay, in their order");
    put(b, 45011);
    put(a, 45013);
    r = set_of(a, b, c);
    x = set_of(a, b, c);
    TIMEVAL minute = { 60, 0 };
    T_EQ(rt_net_select(0, &r, NULL, &x, &minute), 2);
    T_EQ(r.fd_count, 2);
    T_TRUE(r.fd_array[0] == a && r.fd_array[1] == c);
    T_EQ(x.fd_count, 0);                                    /* nothing is ever exceptional */
    T_TRUE(minute.tv_sec == 60 && minute.tv_usec == 0);

    T_CASE("one socket in two sets counts in each; listed twice in one set it counts once");
    r = set_of(a, a, b);
    w = set_of(a, 0, 0);
    T_EQ(rt_net_select(0, &r, &w, NULL, &now), 2);
    T_EQ(r.fd_count, 1);
    T_TRUE(r.fd_array[0] == a);
    T_EQ(w.fd_count, 1);

    T_CASE("an empty datagram makes its socket readable");
    char buf[8];
    T_EQ(rt_net_recvfrom(a, buf, sizeof buf, 0, NULL, NULL), 1);
    SOCKADDR_IN to = v4(127, 0, 0, 1, 45011);
    T_EQ(rt_net_sendto(b, buf, 0, 0, SA(to), sizeof to), 0);
    r = set_of(a, 0, 0);
    T_EQ(rt_net_select(0, &r, NULL, NULL, &now), 1);
}

static SOCKET victim;
static USHORT victim_port;
static SOCKET helper_sock;

static void *send_later(void *arg) {
    (void)arg;
    usleep(200000);                         /* long enough for the main thread to be inside select on a loaded machine */
    SOCKADDR_IN to = { .sin_family = AF_INET, .sin_port = (USHORT)((victim_port >> 8) | (victim_port << 8)) };
    to.sin_addr.s_b[0] = 127; to.sin_addr.s_b[3] = 1;
    rt_net_sendto(helper_sock, "x", 1, 0, SA(to), sizeof to);
    return NULL;
}

static void *close_later(void *arg) {
    (void)arg;
    usleep(200000);                         /* long enough for the main thread to be inside select on a loaded machine */
    rt_net_close(victim);
    return NULL;
}

/* Runs in the child of T_TRAPS: a stream socket in writefds alone, closed during the wait. */
static void closed_writer(void) {
    victim = rt_net_socket(AF_INET, SOCK_STREAM, 0);
    pthread_t th;
    pthread_create(&th, NULL, close_later, NULL);
    FD_SET w = set_of(victim, 0, 0);
    rt_net_select(0, NULL, &w, NULL, NULL);
}

TEST(core, net_select_wait)
{
    net_start();
    pthread_t th;
    FD_SET r, x;

    T_CASE("a time limit with nothing ready: at least the time, and the sets emptied");
    victim = net_udp(AF_INET, -1, 45021, true);
    victim_port = 45021;
    helper_sock = net_udp(AF_INET, -1, 45022, true);
    r = set_of(victim, 0, 0);
    TIMEVAL t = { 0, 60000 };
    uint64_t before = rt_time_tick_ms();
    T_EQ(rt_net_select(0, &r, NULL, NULL, &t), 0);
    uint64_t took = rt_time_tick_ms() - before;
    T_TRUE(took >= 59 && took < 5000);
    T_EQ(r.fd_count, 0);
    T_TRUE(t.tv_sec == 0 && t.tv_usec == 60000);

    T_CASE("no time limit: woken by a datagram another thread sends");
    r = set_of(victim, 0, 0);
    T_EQ(pthread_create(&th, NULL, send_later, NULL), 0);
    T_EQ(rt_net_select(0, &r, NULL, NULL, NULL), 1);
    pthread_join(th, NULL);
    T_EQ(r.fd_count, 1);

    T_CASE("a long time limit: woken the same way, long before it ends");
    char buf[4];
    T_EQ(rt_net_recvfrom(victim, buf, sizeof buf, 0, NULL, NULL), 1);
    r = set_of(helper_sock, victim, 0);
    t = (TIMEVAL){ 30, 0 };
    before = rt_time_tick_ms();
    T_EQ(pthread_create(&th, NULL, send_later, NULL), 0);
    T_EQ(rt_net_select(0, &r, NULL, NULL, &t), 1);
    pthread_join(th, NULL);
    T_TRUE(rt_time_tick_ms() - before < 5000);
    T_TRUE(r.fd_count == 1 && r.fd_array[0] == victim);
    T_EQ(rt_net_recvfrom(victim, buf, sizeof buf, 0, NULL, NULL), 1);

    T_CASE("a socket in readfds closed by another thread during the wait: counted, and still in readfds");
    r = set_of(victim, 0, 0);
    SOCKET was = victim;
    T_EQ(pthread_create(&th, NULL, close_later, NULL), 0);
    LE_SET();
    T_EQ(rt_net_select(0, &r, NULL, NULL, NULL), 1);        /* review D34 */
    LE_UNTOUCHED();
    pthread_join(th, NULL);
    T_TRUE(r.fd_count == 1 && r.fd_array[0] == was);
    FAILS(rt_net_select(0, &r, NULL, NULL, NULL), WSAENOTSOCK);     /* the next call: not a socket */

    T_CASE("a socket in exceptfds alone closed during the wait: WSAENOTSOCK");
    victim = net_udp(AF_INET, -1, 45023, true);
    x = set_of(victim, 0, 0);
    T_EQ(pthread_create(&th, NULL, close_later, NULL), 0);
    FAILS(rt_net_select(0, NULL, NULL, &x, NULL), WSAENOTSOCK);
    pthread_join(th, NULL);

    T_CASE("a socket in writefds alone closed during the wait: a stop");
    T_TRAPS(closed_writer(), "rt_net_select: not implemented");
    T_EQ(rt_net_close(helper_sock), 0);
    T_EQ(rt_net_test_open_count(), 0);
}
