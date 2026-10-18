/* The host's sockets, counted.  Specification and the rule it serves: rt/hostsock.h.
 *
 * THIS IS THE ONE FILE OF THE RUNTIME THAT INCLUDES THE HOST'S SOCKET HEADERS, and it calls
 * exactly two of their functions, getsockname and getpeername, on descriptors it did not open:
 * it reads, and makes, binds, connects and sends nothing.  It therefore includes none of the
 * runtime's Windows headers (rt/win/net.h refuses to be included after a host socket header:
 * the two sets of constants disagree).  runtime/tools/host_net_lint.sh holds the rest of the
 * runtime, and this file beyond those two names, to "no host socket function at all".
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "report.h"             /* rt_report_appendf: formatting without a lock */
#include "rt/hostsock.h"
#include "rt/log.h"

/* Is `sa` an address of an internet family that cannot leave the machine?  The unspecified
 * address is not one: a socket bound to it listens on every interface. */
static bool is_loopback(const struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        return (ntohl(in->sin_addr.s_addr) >> 24) == 127;
    }
    if (sa->sa_family == AF_INET6) {
        const struct in6_addr *a = &((const struct sockaddr_in6 *)sa)->sin6_addr;
        if (IN6_IS_ADDR_LOOPBACK(a)) return true;
        return IN6_IS_ADDR_V4MAPPED(a) && a->s6_addr[12] == 127;
    }
    return false;
}

static size_t name_text(char *buf, size_t n, size_t at, const struct sockaddr *sa) {
    char text[INET6_ADDRSTRLEN] = "?";
    unsigned port = 0;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
        inet_ntop(AF_INET, &in->sin_addr, text, sizeof text);
        port = ntohs(in->sin_port);
    } else {
        const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
        inet_ntop(AF_INET6, &in6->sin6_addr, text, sizeof text);
        port = ntohs(in6->sin6_port);
    }
    return rt_report_appendf(buf, n, at, "%s port %u", text, port);
}

void rt_hostsock_census(RtHostSockCensus *out) {
    memset(out, 0, sizeof *out);
    out->first_fd = -1;
    int top = getdtablesize();
    if (top < 0 || top > 65536) top = 65536;
    for (int fd = 0; fd < top; fd++) {
        struct stat st;
        if (fstat(fd, &st) != 0 || !S_ISSOCK(st.st_mode)) continue;
        out->sockets++;
        struct sockaddr_storage own, peer;
        socklen_t len = sizeof own;
        memset(&own, 0, sizeof own);
        if (getsockname(fd, (struct sockaddr *)&own, &len) != 0) { out->other++; continue; }
        if (own.ss_family == AF_UNIX) { out->local++; continue; }
        if (own.ss_family != AF_INET && own.ss_family != AF_INET6) { out->other++; continue; }
        out->inet++;
        socklen_t plen = sizeof peer;
        memset(&peer, 0, sizeof peer);
        bool has_peer = getpeername(fd, (struct sockaddr *)&peer, &plen) == 0;
        if (is_loopback((struct sockaddr *)&own) && (!has_peer || is_loopback((struct sockaddr *)&peer))) { out->inet_loopback++; continue; }
        if (out->first_fd >= 0) continue;
        out->first_fd = fd;
        size_t at = name_text(out->first_text, sizeof out->first_text, 0, (struct sockaddr *)&own);
        if (has_peer) {
            at = rt_report_appendf(out->first_text, sizeof out->first_text, at, ", connected to ");
            name_text(out->first_text, sizeof out->first_text, at, (struct sockaddr *)&peer);
        }
    }
}

size_t rt_hostsock_line(char *buf, size_t n, size_t at, const unsigned guest[RT_NC_N], const RtHostSockCensus *c) {
    const unsigned not_loop = c->inet - c->inet_loopback;
    at = rt_report_appendf(buf, n, at,
        "net: host sockets: made for the guest: 0 (the guest made %u socket(s) and %u bind(s), each an object of the runtime with no "
        "host socket behind it; %u datagram(s) to loopback stayed inside the process; refused before any host call: %u send(s) and "
        "%u connect(s) to an address that is not loopback, %u lookup(s) of another machine's name); socket descriptors of the whole "
        "process at this end: %u of an internet family (%u loopback, %u NOT loopback), %u unix-domain, %u of another family",
        guest[RT_NC_SOCKETS], guest[RT_NC_BINDS], guest[RT_NC_LOOP_SENDS], guest[RT_NC_REFUSED_SENDS], guest[RT_NC_REFUSED_CONNECTS],
        guest[RT_NC_LOOKUPS_UNKNOWN], c->inet, c->inet_loopback, not_loop, c->local, c->other);
    if (not_loop)
        at = rt_report_appendf(buf, n, at, "; THE RULE IS BROKEN OR A HOST LIBRARY HAS A SOCKET: descriptor %d is %s", c->first_fd, c->first_text);
    else
        at = rt_report_appendf(buf, n, at, "; all loopback: yes%s", c->inet ? "" : " (the process has no socket of an internet family at all)");
    return at;
}

void rt_hostsock_report(void) {
    static char line[900];              /* static: this may run on a small signal stack */
    RtHostSockCensus c;
    unsigned guest[RT_NC_N];
    rt_hostsock_census(&c);
    rt_net_counts(guest);
    size_t at = rt_hostsock_line(line, sizeof line - 1, 0, guest, &c);
    if (at > sizeof line - 2) at = sizeof line - 2;
    line[at++] = '\n';
    rt_log_raw(line, at);
}
