/* The host half of tests/core/hostsock.c: the only place in the tree that OPENS a host socket,
 * and it is test code, not the runtime (it is in rt-test, not in librt.a).  It exists so that
 * the census of rt/hostsock.h is shown to see sockets when there are some: a census that
 * always says "none" would prove nothing.
 *
 * What it opens never reaches a network: a datagram socket bound to 127.0.0.1 or ::1 with a
 * port the kernel picks, a datagram socket that is never bound, connected or written to, and
 * a unix-domain pair.  Nothing is ever sent on any of them.  This file includes no header of
 * the runtime (rt/win/net.h and the host's socket headers cannot be in one file).
 */
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int rt_test_hostsock_open(int kind);
int rt_test_hostsock_pair(int fds[2]);
void rt_test_hostsock_close(int fd);

/* kind 0: UDP over IPv4 bound to 127.0.0.1, any port; 1: UDP over IPv6 bound to ::1, any port;
 * 2: UDP over IPv4, not bound.  The descriptor, or -1. */
int rt_test_hostsock_open(int kind) {
    int fd = socket(kind == 1 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int r = 0;
    if (kind == 0) {
        struct sockaddr_in in;
        memset(&in, 0, sizeof in);
        in.sin_len = sizeof in;
        in.sin_family = AF_INET;
        in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        r = bind(fd, (struct sockaddr *)&in, sizeof in);
    } else if (kind == 1) {
        struct sockaddr_in6 in6;
        memset(&in6, 0, sizeof in6);
        in6.sin6_len = sizeof in6;
        in6.sin6_family = AF_INET6;
        in6.sin6_addr = in6addr_loopback;
        r = bind(fd, (struct sockaddr *)&in6, sizeof in6);
    }
    if (r != 0) { close(fd); return -1; }
    return fd;
}

int rt_test_hostsock_pair(int fds[2]) { return socketpair(AF_UNIX, SOCK_DGRAM, 0, fds); }

void rt_test_hostsock_close(int fd) { close(fd); }
