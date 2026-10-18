/* Windows Sockets and the IP helper as the guest sees them (winsock2.h, ws2def.h, ws2ipdef.h,
 * ws2tcpip.h, iphlpapi.h): the types and constants of the 34 functions the boot-path DLLs
 * import from wsock32, ws2_32 and iphlpapi.  The contract is rt/net.h and
 * docs/handoff/rt-contract-net.md.  Planner-owned.
 *
 * NAMES.  The lower-case names of the Windows headers (sockaddr, in_addr, timeval, fd_set,
 * hostent, addrinfo) also exist in the host's headers with other layouts, so the runtime uses
 * only the upper-case typedefs Windows gives the same structures: SOCKADDR, SOCKADDR_IN,
 * SOCKADDR_IN6, IN_ADDR, IN6_ADDR, TIMEVAL, FD_SET, HOSTENT, ADDRINFOA, LINGER.  No file that
 * includes this header may include a host socket header (sys/socket.h, netinet/in.h,
 * arpa/inet.h, netdb.h): the runtime opens no host socket, and the two sets of constants
 * disagree (AF_INET6 is 23 here and 30 on the host).  The host's macro FD_SET(n, p) is never
 * used; fill fd_count and fd_array yourself.
 *
 * SOURCES.  The values are those of the Windows SDK headers; each was compared with Wine's
 * include/winsock2.h, ws2def.h, ws2ipdef.h and winerror.h (docs/reviews/contracts-check.md),
 * and some also have a call site of the game that passes them (docs/handoff/rt-contract-net.md,
 * "What the game does").  The structure sizes are asserted below; WSADATA's 64-bit member
 * order (the two counters and the pointer before the two strings) is the _WIN64 branch of
 * Wine's winsock2.h.
 */
#ifndef RT_WIN_NET_H
#define RT_WIN_NET_H

#include "rt/wintypes.h"

#ifdef AF_INET6
#error "rt/win/net.h: a host socket header was included first; see NAMES at the top of this file"
#endif

typedef UINT_PTR SOCKET;
#define INVALID_SOCKET  ((SOCKET)~(SOCKET)0)
#define SOCKET_ERROR    (-1)

/* ---- address families, socket types, protocols ----------------------------------------- */
#define AF_UNSPEC       0
#define AF_INET         2
#define AF_INET6        23            /* engine+0x21abd3 passes 0x17 */
#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define IPPROTO_IP      0
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define IPPROTO_IPV6    41

/* ---- addresses ------------------------------------------------------------------------- */
typedef struct { union { BYTE s_b[4]; ULONG s_addr; }; } IN_ADDR;      /* network byte order */
typedef struct { union { BYTE s6_addr[16]; WORD s6_words[8]; }; } IN6_ADDR;
typedef struct { USHORT sa_family; char sa_data[14]; } SOCKADDR;
typedef struct { SHORT sin_family; USHORT sin_port; IN_ADDR sin_addr; char sin_zero[8]; } SOCKADDR_IN;
typedef struct {
    SHORT    sin6_family;
    USHORT   sin6_port;               /* network byte order */
    ULONG    sin6_flowinfo;
    IN6_ADDR sin6_addr;
    ULONG    sin6_scope_id;
} SOCKADDR_IN6;
typedef SOCKADDR *PSOCKADDR, *LPSOCKADDR;
_Static_assert(sizeof(SOCKADDR) == 16 && sizeof(SOCKADDR_IN) == 16, "sockaddr");
_Static_assert(sizeof(SOCKADDR_IN6) == 28, "sockaddr_in6");            /* engine+0x21af01 passes 0x1c */

#define INADDR_ANY        0x00000000u
#define INADDR_LOOPBACK   0x7f000001u  /* host byte order */
#define INADDR_NONE       0xffffffffu  /* what inet_addr returns for text it cannot read */
#define INET_ADDRSTRLEN   22           /* ws2ipdef.h's values (Wine's include/ws2ipdef.h), not POSIX's 16 */
#define INET6_ADDRSTRLEN  65           /* and 46; only buffer sizes use them here */

/* ---- WSAStartup ------------------------------------------------------------------------ */
#define WSADESCRIPTION_LEN  256
#define WSASYS_STATUS_LEN   128
typedef struct {
    WORD   wVersion;                  /* low byte major, high byte minor: 0x0202 is 2.2 */
    WORD   wHighVersion;
    USHORT iMaxSockets;               /* 64-bit order: counters and pointer first */
    USHORT iMaxUdpDg;
    char  *lpVendorInfo;
    char   szDescription[WSADESCRIPTION_LEN + 1];
    char   szSystemStatus[WSASYS_STATUS_LEN + 1];
} WSADATA, *LPWSADATA;
_Static_assert(sizeof(WSADATA) == 408, "WSADATA");

/* ---- select ---------------------------------------------------------------------------- */
#define RT_WS_FD_SETSIZE  64          /* Windows' FD_SETSIZE; the host's is another number */
typedef struct { UINT fd_count; SOCKET fd_array[RT_WS_FD_SETSIZE]; } FD_SET, *PFD_SET, *LPFD_SET;
typedef struct { LONG tv_sec; LONG tv_usec; } TIMEVAL, *PTIMEVAL, *LPTIMEVAL;
_Static_assert(sizeof(FD_SET) == 520 && sizeof(TIMEVAL) == 8, "fd_set, timeval");

/* ---- options (setsockopt, getsockopt) and ioctlsocket ------------------------------------ */
#define SOL_SOCKET      0xffff
#define SO_REUSEADDR    0x0004
#define SO_KEEPALIVE    0x0008
#define SO_BROADCAST    0x0020
#define SO_LINGER       0x0080
#define SO_SNDBUF       0x1001
#define SO_RCVBUF       0x1002
#define SO_ERROR        0x1007
#define SO_TYPE         0x1008
#define TCP_NODELAY     0x0001        /* level IPPROTO_TCP */
#define IPV6_V6ONLY     27            /* level IPPROTO_IPV6; engine+0x21addb passes 0x1b */
typedef struct { USHORT l_onoff; USHORT l_linger; } LINGER;

#define FIONBIO         ((LONG)0x8004667e)    /* engine+0x21ac47 */
#define FIONREAD        ((LONG)0x4004667f)

#define SD_RECEIVE      0
#define SD_SEND         1
#define SD_BOTH         2
#define MSG_OOB         0x1
#define MSG_PEEK        0x2

/* ---- names ----------------------------------------------------------------------------- */
typedef struct {
    char  *h_name;
    char **h_aliases;
    SHORT  h_addrtype;
    SHORT  h_length;
    char **h_addr_list;
} HOSTENT, *PHOSTENT, *LPHOSTENT;
_Static_assert(sizeof(HOSTENT) == 32, "hostent");

typedef struct ADDRINFOA {
    int               ai_flags;
    int               ai_family;
    int               ai_socktype;
    int               ai_protocol;
    SIZE_T            ai_addrlen;
    char             *ai_canonname;
    SOCKADDR         *ai_addr;
    struct ADDRINFOA *ai_next;
} ADDRINFOA, *PADDRINFOA;
_Static_assert(sizeof(ADDRINFOA) == 48, "addrinfo");

#define AI_PASSIVE      0x0001
#define AI_CANONNAME    0x0002
#define AI_NUMERICHOST  0x0004
#define AI_ALL          0x0100
#define AI_ADDRCONFIG   0x0400
#define AI_V4MAPPED     0x0800        /* engine+0x41fa55 passes AI_V4MAPPED | AI_ALL */

/* ---- error codes (winerror.h).  Kept here, under #ifndef, while other units edit
 * rt/win/error.h: move them there at merge. ------------------------------------------------ */
#ifndef WSAEINTR
#define WSAEINTR            10004
#define WSAEFAULT           10014
#define WSAEINVAL           10022
#define WSAEWOULDBLOCK      10035      /* engine+0x21b99f, +0x21c5bc compare with it */
#define WSAENOTSOCK         10038
#define WSAEDESTADDRREQ     10039
#define WSAEMSGSIZE         10040      /* engine+0x21b9a6 */
#define WSAEPROTOTYPE       10041
#define WSAENOPROTOOPT      10042
#define WSAEPROTONOSUPPORT  10043
#define WSAESOCKTNOSUPPORT  10044
#define WSAEOPNOTSUPP       10045
#define WSAEAFNOSUPPORT     10047
#define WSAEADDRINUSE       10048      /* engine+0x21af26 */
#define WSAEADDRNOTAVAIL    10049
#define WSAENETDOWN         10050
#define WSAENETUNREACH      10051
#define WSAECONNRESET       10054      /* engine+0x21c5c3 */
#define WSAENOBUFS          10055
#define WSAEISCONN          10056
#define WSAENOTCONN         10057
#define WSAESHUTDOWN        10058
#define WSAECONNREFUSED     10061
#define WSASYSNOTREADY      10091
#define WSAVERNOTSUPPORTED  10092
#define WSANOTINITIALISED   10093
#define WSAHOST_NOT_FOUND   11001
#define WSANO_DATA          11004
#endif
#ifndef ERROR_BUFFER_OVERFLOW
#define ERROR_BUFFER_OVERFLOW  111     /* engine+0x2175f5 */
#endif
#ifndef ERROR_NO_DATA
#define ERROR_NO_DATA          232
#endif

#endif
