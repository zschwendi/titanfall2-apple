/* Sockets: what Windows Sockets looks like to the guest (wsock32, ws2_32).  This header is the
 * contract of the unit rt-contract-net; docs/handoff/rt-contract-net.md has the reasons, the
 * sources and the task table.  Types and constants: rt/win/net.h.
 *
 * THE MACHINE.  The guest runs on a machine with NO NETWORK BEYOND ITSELF, and the runtime
 * opens no host socket, ever.  Winsock starts.  A socket is an object of this process in the
 * kernel handle table.  The machine's only addresses are the loopback ones: 127.0.0.0/8, ::1,
 * and ::ffff:127.x.y.z as seen through an IPv6 socket ("loopback" below).  A datagram sent to a
 * loopback address and a port that a datagram socket of this process has bound is queued on
 * that socket; nothing else is ever delivered, and nothing ever arrives from outside.  Every
 * destination that is not loopback has no route (WSAENETUNREACH), every host name but
 * "localhost" and the machine's own does not exist (WSAHOST_NOT_FOUND), and no adapter is
 * listed.  Stream sockets can be made, named and configured, and no connection can be
 * established: nothing listens (listen and accept are stops, see the handoff).
 *
 * BUILT.  Everything here is built: core/net.c (the first two sections) and the five pieces
 * core/net_addr.c, net_opt.c, net_dgram.c, net_select.c, net_host.c (unit rt-net-core), each
 * with its test tests/core/net_<piece>.c.  A sentence that begins "THE PIECE:" is a point the
 * specification had left open and the piece settled; unless it names a source it is THIS
 * RUNTIME'S RULE, not a statement about Windows.
 *
 * THE RECORDING.  docs/trace/net-calls.md is what the real game called in one session, start
 * to mission to quit (81,613 calls, 26 of the 34 functions).  No argument it shows falls in a
 * case that STOPS here.  Its cases are in the tests (the engine's sockets, options, lengths).
 *
 * The comments were corrected
 * twice from reviews (docs/reviews/contracts-check.md, rows NV1 to NV6 and C11;
 * contracts-check-2.md, NV7 to NV29 confirmed, D22 to D35 wrong and corrected here, T1 to T18
 * NOT settled).  Where core/net.c does not yet do what a comment of the first two sections
 * says, the comment ends with "CODE TODAY:" and the fix task that brings it in line
 * (swarm/briefs/winfix-*.md).
 *
 * LAST ERROR.  WSAGetLastError is the calling thread's last error, the same value
 * GetLastError returns (Wine ws2_32/tests/sock.c: test_WSASocket sets it with SetLastError and
 * reads WSAGetLastError, test_set_getsockopt reads GetLastError after a socket call; review
 * NV29; that a value written with SetLastError is what WSAGetLastError then reads is from
 * memory, review T1).  THE CORE DOES ALL OF IT; a shim returns the result and sets nothing.
 *   A function that FAILS sets it, through rt_thread_set_last_error, to the code its comment
 *   names, and returns the failure value.
 *   A function that SUCCEEDS sets it to 0, as the last thing it does, if it is one of
 *       rt_net_socket, rt_net_bind, rt_net_getsockname, rt_net_shutdown, rt_net_setsockopt,
 *       rt_net_getsockopt (every option; SO_ERROR was review C11), rt_net_sendto,
 *       rt_net_recvfrom, rt_net_getaddrinfo, rt_net_cleanup.
 *   Windows 7 and later do that (Wine ws2_32/tests/sock.c: test_bind, test_getsockname,
 *   test_shutdown, test_WSASocket, test_set_getsockopt, test_ip_pktinfo, test_WSASendMsg;
 *   protocol.c: test_getaddrinfo, test_startup; WSAGetLastError's remarks: "some functions may
 *   reset the last extended error code to 0 if they succeed"; review D22).  The test of each
 *   has the case: last error 0xDEAD, a call that succeeds, last error 0.
 *   These never touch it when they do not fail: rt_net_pton, also when they return 0 or
 *   INADDR_NONE for a text (protocol.c, test_inet_pton), and rt_net_startup, which returns its
 *   code.  rt_net_inet_addr: NULL sets WSAEFAULT (its comment below); every other text leaves
 *   it alone, also when it gives 0 or INADDR_NONE (protocol.c, test_inet_addr; review Z8).
 *   The others (rt_net_close, rt_net_connect, rt_net_getpeername, rt_net_ioctl,
 *   rt_net_select, rt_net_gethostname, rt_net_ntop) leave it alone on success: what Windows
 *   does there was not in the tests read, so this is the runtime's rule, not a statement
 *   about Windows.
 * rt_net_getaddrinfo returns its code and sets last error to it as well.
 *
 * NOT STARTED.  Except where a comment says otherwise, a function called while the start
 * count is 0 fails with WSANOTINITIALISED before it looks at any argument (documented on
 * each function's page).
 *
 * SOCKET VALUES.  A SOCKET is the object's HANDLE in the kernel table of rt/handle.h, zero
 * extended: 32 bits, a multiple of 4, never 0 and never negative as an int.  The game keeps
 * sockets in 32-bit ints, takes 0 for "no socket" and tests the sign (engine+0x21ac13,
 * +0x1f27ee).  A value with any of bits 32 to 63 set, a closed socket and a handle of another
 * kind are "not a socket": WSAENOTSOCK.  closesocket is the only way to close one: CloseHandle
 * on a socket fails with ERROR_INVALID_HANDLE and is logged (the rule find-file handles have);
 * no wait function accepts one.
 *
 * STOPS.  "STOPS" means the unimplemented report (rt_fatal "<function>: not implemented:
 * <what>" with the offending value): the run ends.  A stop marks a case whose Windows
 * behaviour is not verified or that this machine was not built for.  Never turn one into a
 * return value without the source that says which.  A NULL pointer where no rule is given
 * STOPS.
 *
 * THREADS.  Every function may be called from any thread.  One lock, private to
 * core/net*.c, guards the start count, every socket's fields and every queue; nothing that
 * can block or call the guest runs under it.  A socket closed by one thread while another
 * uses it: the user holds a reference, finishes its call on the object, and the next call
 * with that value is "not a socket".
 */
#ifndef RT_NET_H
#define RT_NET_H

#include "rt/wintypes.h"
#include "rt/win/net.h"

/* ---- Winsock start and the socket object (core/net.c, built) ---------------------------- */

/* WSAStartup.  Returns 0, or the error code itself (never through last error).  In this
 * order (Wine ws2_32/tests/protocol.c, test_startup, which asserts the structure after every
 * call, failed ones included; WSAStartup's page; reviews NV7, D23, Z5):
 *   1. If `out` is not NULL it is filled, WHATEVER the version: wHighVersion 0x0202; wVersion
 *      the requested version if it is 1.0, 1.1, 2.0, 2.1 or 2.2, 1.1 for 1.2 to 1.255, and
 *      0x0202 for a major version of 0 and for anything above 2.2; szDescription
 *      "WinSock 2.0", szSystemStatus "Running" (each zero-padded to the end of its array);
 *      iMaxSockets 32767 and iMaxUdpDg 65467 when the requested major version is 1, both 0
 *      otherwise.  lpVendorInfo is NOT written; no other byte of *out is written.
 *   2. A major version (low byte of `requested`) of 0: WSAVERNOTSUPPORTED, also with `out`
 *      NULL.  The version is looked at BEFORE the pointer.
 *   3. `out` NULL: WSAEFAULT.
 *   4. Otherwise the start count goes up by one (a second start counts) and the result is 0.
 * Neither failure counts as a start.  The cases its test must have (D23, Z5):
 * rt_net_startup(0x0000, NULL) is 10092; rt_net_startup(0x0202, NULL) is 10014 and
 * rt_net_cleanup() then fails with 10093; with d.lpVendorInfo preset to 0xdeadbeef,
 * rt_net_startup(0x0202, &d) is 0 and the member is unchanged; rt_net_startup(0x0101, &d) is
 * 0 with iMaxSockets 32767 and iMaxUdpDg 65467; rt_net_startup(0x0201, &d) is 0 with wVersion
 * 0x0101; rt_net_startup(0x0100, &d) is 10092 with wVersion 0x0202 and the count unchanged.
 * The game asks for 2.2 at all five call sites, and its libcurl refuses any other wVersion
 * (engine+0x250f2d).  Works while the count is 0, any number of times. */
int rt_net_startup(WORD requested, WSADATA *out);

/* WSACleanup.  Count 0: SOCKET_ERROR, WSANOTINITIALISED (protocol.c, test_startup; sock.c,
 * test_WithWSAStartup; review NV8).  Otherwise the count goes down by one, the result is 0
 * and last error is set to 0 (LAST ERROR; D22).  When it reaches 0 the sockets still open stay in the handle table
 * and every call on them fails with WSANOTINITIALISED until the next start; their number is
 * logged (net, info).  Windows documents that it closes them; that difference is deliberate
 * (the game's last cleanup is the launcher's at exit, launcher+0xd6cd). */
int rt_net_cleanup(void);

/* socket.  Returns the new socket and sets last error to 0 (LAST ERROR), or INVALID_SOCKET
 * with, CHECKED IN THIS ORDER (Wine ws2_32/tests/sock.c, test_WSASocket: its table goes
 * through WSASocketA without protocol information, which is what socket is; reviews NV10,
 * D24; win-behaviour-check-5 Z4):
 *   WSAESOCKTNOSUPPORT  type is not 0, SOCK_STREAM or SOCK_DGRAM.  The type is looked at
 *                       BEFORE the family: family 0xdead with type 0xdead gives this code;
 *   WSAEAFNOSUPPORT     af is not AF_UNSPEC, AF_INET or AF_INET6 (family 0xdead with a bad
 *                       protocol gives this code);
 *   WSAEINVAL           af is AF_UNSPEC and protocol is 0, whatever the type;
 *   WSAEPROTONOSUPPORT  protocol does not go with the type: SOCK_DGRAM takes 0 or
 *                       IPPROTO_UDP, SOCK_STREAM takes 0 or IPPROTO_TCP, type 0 takes 0,
 *                       IPPROTO_TCP or IPPROTO_UDP;
 *   WSAENOBUFS          the handle table is full.
 * Type 0 takes the type from the protocol: IPPROTO_TCP a stream socket, IPPROTO_UDP a
 * datagram socket, 0 a stream socket.  AF_UNSPEC with IPPROTO_TCP or IPPROTO_UDP makes an
 * AF_INET socket (Windows 11 makes an AF_INET6 one there; the game never passes AF_UNSPEC).
 * The new socket is blocking, unbound, unconnected, with the default options of
 * rt_net_getsockopt.  A protocol of 0 is stored as the type's protocol.  The cases its test
 * must have (D24): rt_net_socket(0xdead, 0xdead, IPPROTO_TCP) fails with WSAESOCKTNOSUPPORT;
 * rt_net_socket(AF_INET, 0, IPPROTO_UDP) is a socket whose SO_TYPE is SOCK_DGRAM;
 * rt_net_socket(AF_UNSPEC, SOCK_DGRAM, 0) fails with WSAEINVAL. */
SOCKET rt_net_socket(int af, int type, int protocol);

/* closesocket.  0, or SOCKET_ERROR with WSAENOTSOCK (closesocket's page; sock.c,
 * test_set_getsockopt; review NV11).  The value is dead at once; the object
 * (its port, its queue) goes with its last reference.  Threads waiting in rt_net_select are
 * woken and find the value "not a socket". */
int rt_net_close(SOCKET s);

/* ---- text and byte order: no state, no start count (core/net.c, built) ------------------- */

/* inet_pton (inet_pton's page; Wine ws2_32/tests/protocol.c, test_inet_pton; reviews NV12,
 * D25; win-behaviour-check-5 Z6).  1: converted, 4 (AF_INET) or 16 (AF_INET6) bytes stored at
 * `out` in network order.  0: `text` is not an address of that family; WHAT IS AT `out` THEN
 * IS NOT SPECIFIED (Windows writes what it had parsed: "3.4.5.6 " gives 0 with the four bytes
 * written; this runtime stores nothing; a test must not look at `out` after a 0).  -1 with
 * WSAEFAULT: `text` NULL (looked at before the family), or `out` NULL (looked at after it).
 * -1 with WSAEAFNOSUPPORT: another family.  Last error is not touched by a result of 1 or 0.
 *   AF_INET   exactly four decimal numbers 0 to 255 separated by '.', nothing else.  A number
 *             written with a leading zero ("1.1.1.010", "1.1.1.00"): 0.
 *   AF_INET6  RFC 4291 section 2.2: eight groups of 1 to 4 hex digits separated by ':', at
 *             most one "::" for a run of zero groups, the last two groups optionally as a
 *             dotted IPv4 address ("::FFFF:127.0.0.1" is what the engine passes,
 *             engine+0x41f9de) whose numbers have one to three digits, leading zeros allowed
 *             there ("1::001.2.3.4" is 1::1.2.3.4; four digits: 0).  A '%' (a scope,
 *             "::%16"): 0.  Brackets are not part of the form: 0.
 */
int rt_net_pton(int af, const char *text, void *out);

/* inet_ntop.  Returns `buf` holding the text, or NULL with WSAEAFNOSUPPORT (family; review
 * NV13), or NULL with last error STATUS_INVALID_PARAMETER (0xC000000D, the NTSTATUS value,
 * not ERROR_INVALID_PARAMETER) when `buf` is NULL or `size` cannot hold the text and its
 * terminator (Wine ws2_32/tests/protocol.c, test_addr_to_print: a NULL buffer, size 0, sizes
 * too small; Windows 7 gave WSAEINVAL, and the page documents 87, which no tested Windows
 * returns; review D26).  The same code for `addr` NULL, and the limit taken as exactly the
 * text plus its terminator (both from memory: review T11).
 *   AF_INET   "a.b.c.d", decimal.
 *   AF_INET6  lower-case hex groups without leading zeros; a single zero group is written "0";
 *             the longest run of two or more zero groups (the first of equal runs) is written
 *             "::".  The last 32 bits are dotted in four forms only, and hex otherwise
 *             (win-behaviour-check-5 Z7, from Wine ws2_32 test_addr_to_print and the ntdll test
 *             test_RtlIpv6AddressToString; that inet_ntop writes what RtlIpv6AddressToString
 *             writes is an inference, Decisions 2):
 *               first 96 bits zero, g6 not 0:            "::a.b.c.d"  ("::13.1.68.3", "::0.1.0.0")
 *               g5 = ffff after 80 zero bits, g6 not 0:  "::ffff:a.b.c.d"  ("::ffff:13.1.68.3")
 *               g4 = ffff, g5 = 0 after 64 zero bits, g6 not 0:  "::ffff:0:a.b.c.d"
 *               g5 = 5efe, g4 = 0 or 0200 (ISATAP), any prefix:  "prefix:5efe:a.b.c.d"
 *                   ("1111:2222:3333:4444:0:5efe:129.144.52.38", "::200:5efe:129.144.52.38")
 *             Hex: "::ffff" (0.0.255.255), "::1", "::", "::ffff:0:4403", "::100:5efe:8190:3426". */
const char *rt_net_ntop(int af, const void *addr, char *buf, SIZE_T size);

/* inet_addr (inet_addr's page; Wine ws2_32/tests/protocol.c, test_inet_addr; reviews NV14,
 * D27; win-behaviour-check-5 Z8).  The address in network order (as the bytes lie in
 * memory), or INADDR_NONE: for NULL, for an empty string, and for text that is not one to
 * four numbers separated by '.', each decimal, octal (leading 0) or hex (leading 0x), the
 * last one filling the bytes the others leave (a.b.c.d, a.b.c, a.b, a: the documented
 * forms), each within its range.  "255.255.255.255" also gives INADDR_NONE.
 * NULL, and only NULL, sets last error: WSAEFAULT (10014).  Every text leaves it alone.
 * White space: the text " " (one space) gives 0; two spaces, a tab, or white space before
 * the address give INADDR_NONE; after a valid address one white-space character ends the
 * text and what follows is ignored ("3.4.5.6  7" is 3.4.5.6, "1 2 3 4" is the one-part form
 * "1").  The cases its test must have (D27): last error 0xDEAD, rt_net_inet_addr(NULL) is
 * 0xFFFFFFFF with last error 10014; rt_net_inet_addr(" ") is 0.
 * No start count (from memory: review T16, the tests start Winsock first). */
ULONG rt_net_inet_addr(const char *text);

/* ---- names of a socket [piece rt-net-addr, core/net_addr.c] ------------------------------- */

/* bind.  0 with last error set to 0 (LAST ERROR), or SOCKET_ERROR.  The codes: Wine
 * ws2_32/tests/sock.c, test_bind; bind's page; review NV15.  The order among the first three
 * is from memory (review T6).  In this order:
 *   WSAENOTSOCK;
 *   WSAEFAULT          name NULL, or namelen below 16 (AF_INET socket) or 28 (AF_INET6);
 *   WSAEAFNOSUPPORT    name->sa_family is not the socket's family (asserted for family
 *                      0xdead; for an AF_INET name on an AF_INET6 socket, or the reverse,
 *                      Windows may give WSAEFAULT: from memory, review T5);
 *   WSAEINVAL          the socket is already bound (documented);
 *   WSAEADDRNOTAVAIL   the address is neither unspecified (0.0.0.0, ::) nor loopback: this
 *                      machine has no other;
 *   WSAEADDRINUSE      another socket of the same type has the port.  One port space per
 *                      type, whatever the family and address: stricter than Windows for an
 *                      IPv4 and an IPv6-only socket on one number, which the game never asks
 *                      for.  The engine walks to the next port on this code (engine+0x21af26).
 * Port 0 takes the lowest free port from 49152 up (Windows' dynamic range; which one Windows
 * picks is not specified); none free: WSAENOBUFS.  THE PIECE: the address given with port 0 is
 * kept (getsockname gives it back with the port picked).  If this socket or the port's holder has
 * SO_REUSEADDR set and the port is taken: STOPS (Windows' sharing rules are not built, and
 * for datagram sockets not settled: review T18).  The case its test must have (D22): last
 * error 0xDEAD; an AF_INET6 datagram socket bound to :: port 37015 gives 0 and last error 0. */
int rt_net_bind(SOCKET s, const SOCKADDR *name, int namelen);

/* getsockname.  0, last error set to 0 (LAST ERROR), and the bound address in the socket's
 * own family form, *namelen set to 16 or 28; the address is the one given to bind
 * (unspecified stays unspecified), the port the real one.  SOCKET_ERROR with WSAENOTSOCK;
 * WSAEFAULT (name or namelen NULL, or *namelen too small, which is left unchanged);
 * WSAEINVAL (not bound; the name is not written).  Wine ws2_32/tests/sock.c,
 * test_getsockname and test_bind; review NV16.  THE PIECE: in that order, so an unbound socket
 * with a short length is WSAEFAULT. */
int rt_net_getsockname(SOCKET s, SOCKADDR *name, int *namelen);

/* connect (connect's page; Wine ws2_32/tests/sock.c, test_errors and test_connect_udp;
 * reviews NV17, D28).  WSAENOTSOCK and WSAEFAULT as bind.  Then, BEFORE the family is looked
 * at: on a SOCK_DGRAM socket a name whose sa_family is AF_UNSPEC (callers pass 16 zero
 * bytes) returns 0 and removes the peer; rt_net_getpeername then fails with WSAENOTCONN.
 * (An AF_UNSPEC name with namelen below 16, or on a stream socket, STOPS: not read.)  Then
 * WSAEAFNOSUPPORT as bind.  Then:
 *   SOCK_STREAM  WSAEADDRNOTAVAIL for the unspecified address (0.0.0.0, ::: connect's error
 *                table); WSAECONNREFUSED if the address is loopback; WSAENETUNREACH if not
 *                (this machine: no route): at once, in blocking and in non-blocking mode
 *                alike.  (Windows returns WSAEWOULDBLOCK to a non-blocking connect and
 *                reports the refusal later: the socket appears in select's exceptfds and
 *                SO_ERROR is WSAECONNREFUSED, sock.c test_connecting_socket.  Not done here,
 *                the contract's decision 5: the game's libcurl has the immediate path,
 *                engine+0x261dbc.)  An unbound socket is bound first, as port 0 of
 *                rt_net_bind.
 *   SOCK_DGRAM   a loopback address with a port that is not 0: the peer is stored (send and
 *                recv use it, and only datagrams from it are accepted from then on), an
 *                unbound socket is bound as above, result 0; it may be repeated with another
 *                peer.  Not loopback: WSAENETUNREACH.  The unspecified address of the
 *                socket's own family, or port 0: STOPS (not read).
 * The cases its test must have (D28): a datagram socket connected to 127.0.0.1:4000, then
 * connect with a zeroed 16-byte name: 0, and rt_net_getpeername fails with WSAENOTCONN; a
 * stream socket to 0.0.0.0:80: WSAEADDRNOTAVAIL.
 * THE PIECE: name NULL or namelen below 2 is WSAEFAULT; the AF_UNSPEC name is recognised next,
 * and 16 bytes are enough for it on an AF_INET6 socket too; only then the size of the
 * socket's own family.  A stream socket and port 0 STOPS.  WSAEADDRNOTAVAIL binds nothing.
 * An IPv4-mapped peer on a datagram socket whose IPV6_V6ONLY is 1 STOPS.  One line (net,
 * info) per destination with no route ("connect <address>: no route") and one per refused
 * loopback connect.
 * THE RECORDING: the real game's four connects are libcurl's, on non-blocking stream sockets
 * to hosts of the internet, each -1 with WSAEWOULDBLOCK and completed through select.  Here
 * the lookup before them fails, so they are not expected to be made. */
int rt_net_connect(SOCKET s, const SOCKADDR *name, int namelen);

/* getpeername.  0 and the peer of a connected datagram socket, as getsockname gives a name.
 * SOCKET_ERROR with, in this order (Wine ws2_32/tests/sock.c, test_getpeername, "Test the
 * parameter validation order"; reviews NV18, D29): WSAENOTSOCK; WSAENOTCONN (no peer: every
 * stream socket, on this machine; a sendto does not make one), reported BEFORE the pointers
 * are looked at; WSAEFAULT (name or namelen NULL, or *namelen too small, which is left
 * unchanged).  The case its test must have: on a new datagram socket
 * rt_net_getpeername(s, NULL, NULL) fails with WSAENOTCONN. */
int rt_net_getpeername(SOCKET s, SOCKADDR *name, int *namelen);

/* ---- options and modes [piece rt-net-opt, core/net_opt.c] --------------------------------- */

/* setsockopt.  0 with last error set to 0 (LAST ERROR), or SOCKET_ERROR with WSAENOTSOCK,
 * WSAEFAULT (optval NULL, or optlen too small for the option, below).  The options, all
 * stored and given back by rt_net_getsockopt, none with any other effect unless said.  THE
 * SIZES (Wine ws2_32/tests/sock.c, test_set_getsockopt with its test_optsize table; reviews
 * NV19, D30):
 *   one byte      SO_KEEPALIVE, TCP_NODELAY, IPV6_V6ONLY.  setsockopt accepts any optlen
 *                 from 1 and reads ONLY THE FIRST BYTE (an int of 0x100 stores 0); stored as
 *                 0 or 1.  optlen 0: WSAEFAULT.
 *   four bytes, short accepted   SO_BROADCAST, SO_REUSEADDR: stored as 0 or 1.  setsockopt
 *                 accepts optlen 1 to 3 as well (the bytes given are the value, the rest 0:
 *                 from memory).  optlen 0 succeeds on Windows too; what it stores was not
 *                 read: STOPS.
 *   four bytes    SO_SNDBUF, SO_RCVBUF (int: also the limit of the datagram queue, see
 *                 rt_net_sendto), SO_LINGER (LINGER): optlen below 4 is WSAEFAULT.
 * A negative optlen STOPS (Windows: WSAENOBUFS or WSAEFAULT by option, the same test).
 *   SOL_SOCKET    SO_REUSEADDR, SO_KEEPALIVE, SO_BROADCAST, SO_SNDBUF, SO_RCVBUF as above;
 *                 SO_LINGER on a stream socket; on a datagram socket it STOPS (reading it
 *                 there is WSAENOPROTOOPT, D32; setting it was not tested);
 *   IPPROTO_TCP   TCP_NODELAY, stream sockets only; on a datagram socket WSAENOPROTOOPT
 *                 (from memory: review T7);
 *   IPPROTO_IPV6  IPV6_V6ONLY, only before bind (after: STOPS).  On an AF_INET6 socket 0
 *                 makes the socket dual: it accepts and sends IPv4 as ::ffff: addresses
 *                 (engine+0x21adec sets 0 on every UDP socket).  On an AF_INET socket it
 *                 succeeds too, is stored and read back, and has no effect (sock.c,
 *                 test_ipv6only; review D31).
 * Any other level or option STOPS, with both numbers in the message: a run then names what
 * the game wanted, and the option gets a row here.  A negative buffer size STOPS.  The cases
 * its test must have (D30, D31): setsockopt(SOL_SOCKET, SO_REUSEADDR, &one, 1) is 0; on an
 * AF_INET datagram socket setsockopt(IPPROTO_IPV6, IPV6_V6ONLY, &zero, 4) is 0.
 * THE PIECE: the option is identified first (unknown: STOPS), then WSAENOPROTOOPT where the
 * option does not go with the socket, then a negative optlen (STOPS), then optval NULL, then
 * the size.  rt_net_getsockopt in the same order; setting SO_ERROR STOPS.
 * THE RECORDING: SO_SNDBUF, SO_RCVBUF, IPV6_V6ONLY and TCP_NODELAY, each with optlen 4, and
 * nothing else, through wsock32's own setsockopt (which renumbers only level IPPROTO_IP: from
 * memory of Wine's wsock32); getsockopt only for SO_ERROR. */
int rt_net_setsockopt(SOCKET s, int level, int optname, const void *optval, int optlen);

/* getsockopt.  0 with last error set to 0 (LAST ERROR; every option), the value stored at
 * optval and its size at *optlen: ONE byte and *optlen 1 for SO_KEEPALIVE, TCP_NODELAY and
 * IPV6_V6ONLY (with v preset to 0xdeadbeef and *optlen 4, TCP_NODELAY 0 leaves v 0xdeadbe00
 * and *optlen 1); four bytes and *optlen 4 for the others (sock.c, test_set_getsockopt;
 * review D30).  SOCKET_ERROR with WSAENOTSOCK; WSAEFAULT for optval NULL, optlen NULL,
 * *optlen 0 (review NV19) and a *optlen below 4 for a four-byte option; a negative *optlen
 * STOPS.  The options of setsockopt, with these values on a new socket: the BOOLs 0, except
 * IPV6_V6ONLY 1 on an AF_INET6 socket (documented default; on an AF_INET socket reading it
 * before it was set STOPS: its default there was not read); SO_SNDBUF and SO_RCVBUF 65536
 * (from memory: review T2); SO_LINGER {0, 0} on a stream socket, and on a datagram socket
 * SOCKET_ERROR with WSAENOPROTOOPT (sock.c, test_extendedSocketOptions; review D32).  And,
 * read-only (setting one: WSAENOPROTOOPT for SO_TYPE, NV19):
 *   SOL_SOCKET SO_TYPE   the socket's type;
 *   SOL_SOCKET SO_ERROR  0: no call on this machine leaves an error pending on a socket.
 *                        This one success ALSO sets the thread's last error to 0 (LAST ERROR
 *                        above; review C11).  The case its test must have:
 *                        WSASetLastError(10035); getsockopt(s, SOL_SOCKET, SO_ERROR, &v, &len)
 *                        returns 0 with v 0, and WSAGetLastError() is then 0. */
int rt_net_getsockopt(SOCKET s, int level, int optname, void *optval, int *optlen);

/* ioctlsocket (sock.c, test_fionbio and test_fionread_siocatmark; "Winsock IOCTLs"; review
 * NV20).  0, or SOCKET_ERROR with WSAENOTSOCK, WSAEFAULT (argp NULL).
 *   FIONBIO   *argp not 0: non-blocking mode; 0: blocking mode.
 *   FIONREAD  *argp = the bytes of all datagrams queued (documented: the total for a message
 *             socket); 0 for a stream socket.
 * Any other command STOPS with its number (THE PIECE: before argp is looked at).
 * THE RECORDING: FIONBIO with 1, once per socket, and nothing else. */
int rt_net_ioctl(SOCKET s, LONG cmd, ULONG *argp);

/* ---- data [piece rt-net-dgram, core/net_dgram.c] ------------------------------------------ */

/* sendto, and send (to NULL, tolen 0).  The number of bytes taken (len) with last error set
 * to 0 (LAST ERROR), or SOCKET_ERROR.  Checked: sendto's page; Wine ws2_32/tests/sock.c,
 * test_connecting_socket, test_connect_udp, test_WSASendMsg, test_shutdown, test_UDP;
 * reviews NV21, D33.
 * In this order: WSAENOTSOCK; flags other than 0 STOP; len < 0 or buf NULL with len > 0:
 * WSAEFAULT; WSAESHUTDOWN after rt_net_shutdown for sending.
 *   SOCK_STREAM  WSAENOTCONN (documented for an unconnected socket; `to` is ignored).
 *   SOCK_DGRAM   destination: `to` if given (tolen below 16 or 28: WSAEFAULT; a family that
 *     is not the socket's: STOPS), else the peer of rt_net_connect, else WSAENOTCONN.  `to`
 *     given on a connected socket STOPS.  len above 65507: WSAEMSGSIZE (from memory: review
 *     T9).  An unbound socket is bound first (port 0 of rt_net_bind: documented).  Then
 *       - the unspecified address: WSAEADDRNOTAVAIL (documented).  Port 0 is NOT refused: on
 *         a loopback address it is a port nobody holds, so the result is len and the
 *         datagram is dropped (sock.c, test_UDP, "Test sending to port 0", 127.0.0.1 and
 *         ::1; review D33).  The case its test must have: an unbound AF_INET datagram
 *         socket, 16 bytes to 127.0.0.1 port 0: 16;
 *       - not loopback: WSAENETUNREACH (this machine's answer; what Windows gives with no
 *         adapter is from memory, review T3), and one line per destination (net, info:
 *         "sendto <address>: no route") so that a run shows whom the game tried to reach.
 *         The engine counts such a datagram as sent (engine+0x21c5ca);
 *       - loopback: the result is len.  The datagram is queued on the datagram socket of
 *         this process that has the port, if there is one, it takes the destination's kind
 *         (an AF_INET socket takes IPv4; an AF_INET6 socket takes ::1, and IPv4 if its
 *         IPV6_V6ONLY is 0), its peer (if connected) is the sender, it is not shut down for
 *         receiving, and its queued bytes plus len do not exceed its SO_RCVBUF.  Otherwise
 *         the datagram is dropped, silently.  (Windows makes a later receive of the sender
 *         fail with WSAECONNRESET when nothing has the port: not done.)  A queued datagram
 *         carries the sender's port and, as its address, 127.0.0.1 or ::1 in the receiver's
 *         family (::ffff:127.0.0.1 for IPv4 into an AF_INET6 socket).  Waiters of
 *         rt_net_select are woken.
 * THE PIECE: an IPv4-mapped destination on a socket whose IPV6_V6ONLY is 1 STOPS; a receiver
 * bound to one loopback address (not to the unspecified one) takes only datagrams sent to
 * that kind of address; out of memory for the copy STOPS.
 * THE RECORDING: the engine sends from its "clientping" socket (368 datagrams of 14 bytes,
 * each answered) and from its "server" socket (33 bytes every 10 s in a mission), tolen 28,
 * flags 0; the destinations are not visible.  Nothing of the campaign's own client and server
 * goes through here: see rt_net_recvfrom. */
int rt_net_sendto(SOCKET s, const void *buf, int len, int flags, const SOCKADDR *to, int tolen);

/* recvfrom, and recv (from and fromlen NULL).  The bytes stored with last error set to 0
 * (LAST ERROR), or SOCKET_ERROR.  Checked: the pages of recvfrom and recv; sock.c,
 * test_connect_udp, test_UDP, test_connecting_socket, test_shutdown; review NV22.
 * WSAENOTSOCK; flags other than 0 STOP (MSG_PEEK included); len <= 0 or buf NULL: WSAEFAULT;
 * `from` given with fromlen NULL or *fromlen below 16 or 28: WSAEFAULT; WSAESHUTDOWN after
 * rt_net_shutdown for receiving.
 *   SOCK_STREAM  WSAENOTCONN.
 *   SOCK_DGRAM   not bound: WSAEINVAL (documented).  A datagram queued: the oldest is taken
 *     off; its first len bytes are stored, its sender at *from with *fromlen set; if it was
 *     longer the rest is lost and the result is SOCKET_ERROR with WSAEMSGSIZE (documented),
 *     else its length (0 for an empty datagram).  Nothing queued: in non-blocking mode
 *     SOCKET_ERROR with WSAEWOULDBLOCK, with *from and *fromlen untouched (the two untouched:
 *     from memory, review T10).  THE ENGINE'S RECEIVE
 *     LOOP DEPENDS ON EXACTLY THIS CODE: it repeats the call at once on any other one but
 *     WSAEMSGSIZE (engine+0x21b99f).  In blocking mode it STOPS ("blocking receive with
 *     nothing queued"): nothing can arrive but what this process sends, and every receive
 *     the game makes at start is non-blocking.
 * THE PIECE: `from` and *fromlen are written also when the result is WSAEMSGSIZE.
 * THE RECORDING: 39,619 calls, flags 0, lengths 1264 and 1300, `from` always given; every
 * socket was non-blocking.  NO CALL ON THE "server" (37015) OR THE "client" (37005) SOCKET
 * EVER RETURNED A DATAGRAM, in the menu or in a campaign mission: the campaign's client and
 * server talk through the engine's in-process path, not through these sockets. */
int rt_net_recvfrom(SOCKET s, void *buf, int len, int flags, SOCKADDR *from, int *fromlen);

/* shutdown (shutdown's page; sock.c, test_shutdown; review NV23).  WSAENOTSOCK; `how` not
 * SD_RECEIVE, SD_SEND or SD_BOTH: WSAEINVAL; SOCK_STREAM: WSAENOTCONN (an unconnected
 * socket); SOCK_DGRAM: the direction is closed for the calls above, what is queued is
 * dropped for SD_RECEIVE and SD_BOTH (the dropping: from memory), result 0 with last error
 * set to 0 (LAST ERROR). */
int rt_net_shutdown(SOCKET s, int how);

/* ---- select [piece rt-net-select, core/net_select.c] -------------------------------------- */

/* select (the page was read on 2026-10-10; Wine ws2_32/tests/sock.c, test_select; reviews
 * NV24, D34).  nfds is ignored.  The three sets are checked
 * first: all NULL or all empty: SOCKET_ERROR, WSAEINVAL; an fd_count above 64 STOPS (Windows
 * accepts 200); an entry that is not a socket: WSAENOTSOCK, the sets unchanged; a socket
 * listed twice in one set counts once.  timeout with a negative member: WSAEINVAL.
 * A socket is ready
 *   in readfds    when a datagram is queued on it (never, for a stream socket);
 *   in writefds   when it is a datagram socket (a send never blocks here); never for a
 *                 stream socket, which is never connected;
 *   in exceptfds  never.
 * If any is ready: each set is rewritten to hold only its ready sockets, in their original
 * order, and the result is the total over the three sets.  Otherwise the call waits until
 * one becomes ready, a listed socket is closed, or the time is over: timeout {0,0} does not
 * wait; NULL waits without limit.  A SOCKET CLOSED DURING THE WAIT that is in readfds is
 * reported READY: the result counts it and it stays in readfds (the other sockets by the
 * rules above); only one that is in exceptfds alone ends the call with SOCKET_ERROR and
 * WSAENOTSOCK; one that is in writefds and not in readfds STOPS (not tested).  (test_select,
 * the two "close the socket being selected" cases, which use a connected TCP pair; that a
 * datagram socket is treated alike is the likely reading.  The case its test must have:
 * thread A selects with readfds {s} and no time limit, thread B closes s: A gets 1 and
 * readfds is still {s}.)  When the time is over the three sets are
 * emptied (fd_count 0) and the result is 0.  *timeout is never written.  The wait is not
 * alertable (Windows documents an alertable wait for a NULL timeout: no APC is run here).
 * THE PIECE: a listed socket found closed at the call's first look is treated as closed
 * during the wait; a closed socket that is in readfds and in writefds counts in readfds only;
 * a time is rounded up to whole milliseconds.  It needs nothing of the wait core: it sleeps on
 * the socket layer's own condition, which rt_net_sendto and rt_net_close signal.
 * THE RECORDING: 1,902 calls, all libcurl's, on stream sockets: write and except sets while a
 * connect is pending, read and except sets while waiting for data, always with a time. */
int rt_net_select(int nfds, FD_SET *readfds, FD_SET *writefds, FD_SET *exceptfds, const TIMEVAL *timeout);

/* ---- host names [piece rt-net-host, core/net_host.c] -------------------------------------- */

/* gethostname.  0 and the machine's name with its terminator in `name`: the value of the
 * environment variable COMPUTERNAME as the runtime has it (rt/env.h), unchanged (this
 * contract's choice: Windows gives the DNS host name, the NetBIOS name only as a fallback,
 * gethostname's page; whether the two differ in case or length is review T12).
 * SOCKET_ERROR with WSAEFAULT when name is NULL or namelen cannot hold it (documented; the
 * buffer is then unchanged, protocol.c).  THE PIECE: the start count is checked (NOT STARTED);
 * COMPUTERNAME not set or 256 bytes and longer STOPS. */
int rt_net_gethostname(char *name, int namelen);

/* getaddrinfo (getaddrinfo's page; Wine ws2_32/tests/protocol.c, test_getaddrinfo and its
 * hint tests; reviews NV25, D35; win-behaviour-check-5 Z1).  Returns 0 and a list at
 * *result, with last error set to 0 (LAST ERROR), or a code, which is also set as last
 * error; *result is then set to NULL.  result NULL STOPS.
 *   hints     NULL means all zero.  ai_family other than AF_UNSPEC, AF_INET, AF_INET6:
 *             WSAEAFNOSUPPORT.  ai_socktype other than 0, SOCK_STREAM, SOCK_DGRAM:
 *             WSAESOCKTNOSUPPORT.  Flags outside AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
 *             AI_ALL | AI_V4MAPPED | AI_ADDRCONFIG, or AI_CANONNAME at all, STOP.
 *   service   NULL or "" (port 0) or a decimal number 0 to 65535; anything else ("http")
 *             STOPS (Windows: WSATYPE_NOT_FOUND for a name no service has; names it knows
 *             were not tested, review T15).
 *   node      NULL with service NULL: WSAHOST_NOT_FOUND (documented).  NULL otherwise: the
 *             unspecified address with AI_PASSIVE, the loopback address without.
 *             An address in text (rt_net_pton says so; its stops apply): that address.  An
 *             IPv6 address in brackets, "[::1]" or "[beef::cafe]", is that address too
 *             (D35; with ai_family AF_INET: WSAHOST_NOT_FOUND).  "[addr]:port" is that address
 *             too, and the entry's port is the one from service (Z1 asserts the address only;
 *             what else the node may hold after the bracket is not settled).  An unmatched
 *             bracket, "[beef::cafe" or "beef::cafe]", is a name, so with AI_NUMERICHOST it is
 *             WSAHOST_NOT_FOUND.  So is a port after an IPv4 address, "192.168.1.253:1024":
 *             rt_net_pton does not take it, so it is a name, and with AI_NUMERICHOST
 *             WSAHOST_NOT_FOUND.
 *             "" (the local machine, D35), "localhost" or the machine's own name, compared
 *             without case: loopback (for "" and the own name this is the contract's
 *             decision 9: Windows gives the machine's addresses, and what it gives with no
 *             adapter up is not settled, review T4).
 *             Any other name: WSAHOST_NOT_FOUND, and one line per name (net, info:
 *             "getaddrinfo <name>: no such host"), so that a run lists the hosts the game
 *             asks for.  With AI_NUMERICHOST a name that is not an address is
 *             WSAHOST_NOT_FOUND without the line.  AI_ADDRCONFIG with an address or a local
 *             name STOPS (what Windows gives with only loopback configured is not verified).
 *   the list  one entry per family asked for, AF_INET6 first for AF_UNSPEC (the order: from
 *             memory, review T13): an IPv4 address gives an AF_INET entry, and for ai_family
 *             AF_INET6 an entry ::ffff:a.b.c.d if AI_V4MAPPED is set and WSAHOST_NOT_FOUND
 *             if not (from memory: T14); an IPv6 address gives an
 *             AF_INET6 entry, and WSAHOST_NOT_FOUND for ai_family AF_INET; loopback and
 *             unspecified exist in both families.  For AF_INET6 with AI_V4MAPPED | AI_ALL
 *             (the engine's hints) loopback is TWO AF_INET6 entries, ::1 and then
 *             ::ffff:127.0.0.1 (review census-rows H2, from the page of getaddrinfo:
 *             "localhost" returns "all loopback addresses on the local computer", and with
 *             AI_ALL "a request is made for IPv6 addresses and IPv4 addresses with
 *             AI_V4MAPPED"; their order is not documented, and no test asserts this call);
 *             the unspecified address of a passive NULL node stays the one entry ::, which
 *             has no IPv4 twin to map.  Each entry: ai_flags 0,
 *             ai_family, ai_socktype and ai_protocol from the hints, ai_addrlen 16 or 28,
 *             ai_addr a SOCKADDR_IN or SOCKADDR_IN6 with the port in network order and
 *             every other member 0, ai_canonname NULL.
 *   The cases its test must have (D35; win-behaviour-check-5 Z1), the last error preset to
 *   0xdeadbeef first: rt_net_getaddrinfo("", NULL, NULL, &r) is 0, r not NULL, last error 0;
 *   rt_net_getaddrinfo(NULL, "", NULL, &r2) is 0 with port 0; rt_net_getaddrinfo("[::1]",
 *   "80", NULL, &r) is 0 with one AF_INET6 entry ::1, port 80.
 * THE PIECE: result NULL (STOPS) is looked at first, then the start count, then the flags
 * that STOP, the family and the type, then the service.  A NULL node with AI_ADDRCONFIG STOPS like a
 * local name.  After "[addr]" only ":digits" may follow; anything else STOPS.
 * THE RECORDING: 51 calls, every one returning 0 there.  From engine+0x41fa72 with no service:
 * the machine's own name at start, "localhost" when a mission's client joins its server (both
 * succeed here), and ten hosts of the internet (WSAHOST_NOT_FOUND here).  From libcurl's
 * resolver threads with service "443": two hosts of the internet (WSAHOST_NOT_FOUND here).
 * Memory: each entry is one block of the process heap (rt_heap_alloc(rt_heap_process(), 0,
 * n)) holding the ADDRINFOA and its address.  Out of memory STOPS.  Works on any thread:
 * the game's libcurl calls it on a thread of its own (engine+0x256350). */
int rt_net_getaddrinfo(const char *node, const char *service, const ADDRINFOA *hints, ADDRINFOA **result);

/* freeaddrinfo.  Frees every entry of a list of rt_net_getaddrinfo.  NULL does nothing: the
 * engine frees the pointer of a failed lookup (engine+0x41fa7e), and so does Wine's test
 * (protocol.c, test_getaddrinfo; review NV26).  No start count check, no
 * last error. */
void rt_net_freeaddrinfo(ADDRINFOA *list);

#endif
