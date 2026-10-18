#include <string.h>

#include "rt/test.h"
#include "rt/env.h"
#include "rt/net.h"
#include "rt/win/net.h"

/* The oracle is the environment variable COMPUTERNAME as rt_env_get_a reads it.  This test
 * sets it itself first, so that its value is known; the two STOP cases run in forked
 * children (T_TRAPS), so their changes to the variable do not reach the other checks. */
TEST(wsock32, gethostname)
{
    char buf[0x200], want[0x200], big[257];
    DWORD err = 0, n;
    int len;

    T_CASE("a call before the start fails with WSANOTINITIALISED");
    memset(buf, 0x55, sizeof buf);
    T_EQ(CALL(wsock32, gethostname, buf, sizeof buf), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);
    T_EQ(CALL(wsock32, gethostname, NULL, 0), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSANOTINITIALISED);

    WSADATA d;
    T_EQ(rt_net_startup(0x0202, &d), 0);
    T_EQ(rt_env_set_a("COMPUTERNAME", "TESTHOST", &err), TRUE);
    T_EQ(err, 0);
    n = rt_env_get_a("COMPUTERNAME", want, sizeof want, &err);
    T_EQ(err, 0);
    len = (int)n;
    T_TRUE(len > 0 && len < 256);

    T_CASE("returns 0 and the computer name with its terminator");
    memset(buf, 0x55, sizeof buf);
    CALL(kernel32, SetLastError, 0xDEAD);
    T_EQ(CALL(wsock32, gethostname, buf, sizeof buf), 0);
    T_EQ(T_LAST_ERROR(), 0xDEAD);
    T_EQ(strcmp(buf, want), 0);
    T_EQ(buf[len], 0);
    T_TRUE((unsigned char)buf[len + 1] == 0x55);      /* nothing written past the terminator */
    T_TRUE((unsigned char)buf[sizeof buf - 1] == 0x55);
    T_EQ(CALL(wsock32, gethostname, buf, len + 1), 0);   /* exactly the name and its terminator */
    T_EQ(strcmp(buf, want), 0);

    T_CASE("a namelen one short of the name and its terminator fails with WSAEFAULT");
    memset(buf, 0x55, sizeof buf);
    T_EQ(CALL(wsock32, gethostname, buf, len), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);
    T_TRUE((unsigned char)buf[0] == 0x55 && (unsigned char)buf[len] == 0x55);   /* buffer unchanged */

    T_CASE("a NULL buffer fails with WSAEFAULT");
    T_EQ(CALL(wsock32, gethostname, NULL, sizeof buf), SOCKET_ERROR);
    T_EQ(T_LAST_ERROR(), WSAEFAULT);

    T_TRAPS((rt_env_set_a("COMPUTERNAME", NULL, &err), CALL(wsock32, gethostname, buf, sizeof buf)),
            "gethostname");
    memset(big, 'A', sizeof big - 1);
    big[sizeof big - 1] = 0;
    T_TRAPS((rt_env_set_a("COMPUTERNAME", big, &err), CALL(wsock32, gethostname, buf, sizeof buf)),
            "gethostname");
}
