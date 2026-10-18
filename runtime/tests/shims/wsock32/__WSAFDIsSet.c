#include <string.h>

#include "rt/test.h"
#include "rt/win/net.h"

/* The Windows fd_set is filled by hand in each case: the host's FD_SET macro is never used.
 * memset clears the whole structure, so the entries beyond fd_count are zero unless a case
 * sets them. */
TEST(wsock32, __WSAFDIsSet)
{
    FD_SET s, copy;

    T_CASE("an empty set gives 0");
    memset(&s, 0, sizeof s);
    s.fd_count = 0;
    s.fd_array[0] = 7;                  /* in the array, but outside fd_count = 0 */
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)7, &s), 0);
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)0, &s), 0);

    T_CASE("a socket in the set gives 1");
    memset(&s, 0, sizeof s);
    s.fd_count = 3;
    s.fd_array[0] = 10;
    s.fd_array[1] = 20;
    s.fd_array[2] = 30;
    memcpy(&copy, &s, sizeof s);
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)10, &s), 1);
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)20, &s), 1);
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)30, &s), 1);
    T_TRUE(memcmp(&s, &copy, sizeof s) == 0);      /* the set is only read */

    T_CASE("a socket not in the set gives 0");
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)40, &s), 0);
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)0, &s), 0);

    T_CASE("an entry beyond fd_count is not found");
    memset(&s, 0, sizeof s);
    s.fd_count = 2;
    s.fd_array[0] = 10;
    s.fd_array[1] = 20;
    s.fd_array[2] = 30;                 /* beyond fd_count: not a member */
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)30, &s), 0);
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)20, &s), 1);     /* the last counted entry */

    T_CASE("a value that differs only in bit 32 is not found");
    memset(&s, 0, sizeof s);
    s.fd_count = 1;
    s.fd_array[0] = 0x5;
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)0x100000005ull, &s), 0);
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)0x5, &s), 1);
    s.fd_array[0] = 0x100000005ull;
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)0x5, &s), 0);
    T_EQ(CALL(wsock32, __WSAFDIsSet, (SOCKET)0x100000005ull, &s), 1);

    T_CASE("does not change last error");
    memset(&s, 0, sizeof s);
    s.fd_count = 1;
    s.fd_array[0] = 10;
    CALL(kernel32, SetLastError, 0x1234);
    CALL(wsock32, __WSAFDIsSet, (SOCKET)10, &s);      /* found: returns 1 */
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(wsock32, __WSAFDIsSet, (SOCKET)11, &s);      /* not found: returns 0 */
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The two stops, after the checks of the last case. */
    memset(&s, 0, sizeof s);
    T_TRAPS(CALL(wsock32, __WSAFDIsSet, (SOCKET)10, NULL), "set NULL");
    s.fd_count = 65;
    T_TRAPS(CALL(wsock32, __WSAFDIsSet, (SOCKET)10, &s), "fd_count above 64");
}
