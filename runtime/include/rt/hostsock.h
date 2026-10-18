/* THE HOST'S SOCKETS, COUNTED: how "no socket of the guest reaches the network" is measured in
 * a running process, and not only believed (unit gate3-run-7, decision E;
 * docs/handoff/gate3-run-7.md, section 1).  Built in core/hostsock.c; the test is
 * tests/core/hostsock.c; the link-level half of the rule is runtime/tools/host_net_lint.sh.
 *
 * THE RULE.  The runtime opens no host socket.  A guest socket is an object of the runtime
 * (core/net.c): its bind is a row in the runtime's own table of ports, its datagrams go from
 * one such object's queue to another's, a destination that is not a loopback address is
 * WSAENETUNREACH before anything else happens, and a name that is not the machine's own or
 * "localhost" is WSAHOST_NOT_FOUND from a table, not from a resolver (rt/net.h, THE MACHINE).
 * So no datagram can leave this process and none can arrive from outside it, by construction:
 * there is no descriptor to send on.  Three things hold that in place:
 *   1. the lint: no object of the runtime, and no program linked with it that the lint is
 *      given, refers to a socket, resolver or interface function of the host; the two
 *      exceptions are getsockname and getpeername in core/hostsock.o, which only read.  On the
 *      owner's decision of 12 October the DXMT loader (runtime/dxback/dxback.o, the one object of
 *      a link that loads DXMT's libraries with dlopen and dlsym) may ask for dlopen and dlsym, and
 *      for nothing else of this list, and no other object of the link may.  In exchange the lint
 *      (--program with --dxmt-loader, --inputs and --dxmt-libs) checks that the loader asks for
 *      both, that no other input does, and that each DXMT library asks for no socket, resolver,
 *      interface, dlopen, dlsym or syscall function; it fails otherwise;
 *   2. the census below: every descriptor of the process that is a socket, by family, and for
 *      the internet families whether its own address (and its peer's, if it has one) is
 *      loopback.  The runtime makes none, so every socket it finds is somebody else's (a
 *      library of the host in the same process);
 *   3. the line a run ends with (rt_hostsock_report): the guest's own counts beside the census.
 *
 * This header has no host type in it and may be included beside rt/win/net.h.
 */
#ifndef RT_HOSTSOCK_H
#define RT_HOSTSOCK_H

#include <stddef.h>

typedef struct {
    unsigned sockets;           /* descriptors of this process that are sockets, of any family */
    unsigned local;             /* AF_UNIX: cannot leave this machine */
    unsigned inet;              /* AF_INET and AF_INET6 */
    unsigned inet_loopback;     /* of `inet`: its own address is a loopback address and its peer, if it has one, too */
    unsigned other;             /* any other family (the kernel's own: AF_SYSTEM, AF_ROUTE and so on) */
    int      first_fd;          /* the first socket of an internet family that is not loopback, or -1 */
    char     first_text[120];   /* that socket's own address and its peer's, as text */
} RtHostSockCensus;

/* Look at every descriptor of the process (fstat, getsockname, getpeername: nothing is opened,
 * closed, written or changed).  A socket of an internet family counts as loopback only if it
 * is bound to 127.0.0.0/8, ::1 or ::ffff:127.0.0.0/104 and is not connected to anything else:
 * an unbound socket and one bound to the unspecified address are NOT loopback.  Takes no lock
 * and allocates nothing: callable inside a fatal report. */
void rt_hostsock_census(RtHostSockCensus *out);

/* What the guest did with its sockets since the process started (core/net.c).  Atomic
 * counters, readable at any time without a lock. */
enum {
    RT_NC_SOCKETS,              /* guest sockets made (rt_net_socket succeeded) */
    RT_NC_BINDS,                /* binds that succeeded: each a row of the runtime's own table of ports */
    RT_NC_LOOP_SENDS,           /* datagrams sent to a loopback address: queued on a guest socket of this process, or dropped */
    RT_NC_REFUSED_SENDS,        /* sends to an address that is not loopback: WSAENETUNREACH, nothing sent */
    RT_NC_REFUSED_CONNECTS,     /* connects to an address that is not loopback: WSAENETUNREACH */
    RT_NC_LOOKUPS_UNKNOWN,      /* getaddrinfo of a name that is not this machine: WSAHOST_NOT_FOUND, no resolver asked */
    RT_NC_N
};
void rt_net_counts(unsigned out[RT_NC_N]);

/* The line "net: host sockets: ..." into buf[at..n), without a newline; the new length.  A
 * pure function of its arguments. */
size_t rt_hostsock_line(char *buf, size_t n, size_t at, const unsigned guest[RT_NC_N], const RtHostSockCensus *c);

/* Take the census and the guest's counts now and write that line with rt_log_raw.  No lock,
 * no allocation: it is meant for rt_com_counts_also (rt/com.h), so that every end of a run
 * (a stop, the guest's exit, a return) has the line once. */
void rt_hostsock_report(void);

#endif
