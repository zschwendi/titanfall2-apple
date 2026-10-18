#include <string.h>

#include "rt/test.h"
#include "rt/log.h"

/* The engine's two calls (engine+0x2175f0): a 704-byte buffer preset to 0xAA with a size of 0,
 * then with a size of 704. Both return ERROR_NO_DATA, so the engine does not call again. */
enum { ADAPTER_BUF = 704 };

/* How many of the n bytes at p are not v. */
static unsigned bytes_not(const unsigned char *p, unsigned n, unsigned char v)
{
    unsigned bad = 0, i;
    for (i = 0; i < n; i++)
        if (p[i] != v) bad++;
    return bad;
}

TEST(iphlpapi, GetAdaptersInfo)
{
    unsigned char buf[ADAPTER_BUF];
    ULONG size, r;

    T_CASE("two calls with a 704-byte buffer and sizes 0 then 704 return ERROR_NO_DATA and change nothing");
    memset(buf, 0xAA, sizeof buf);
    size = 0;
    r = CALL(iphlpapi, GetAdaptersInfo, buf, &size);
    T_EQ(r, ERROR_NO_DATA);
    T_EQ(size, 0);
    T_EQ(bytes_not(buf, ADAPTER_BUF, 0xAA), 0);
    size = 704;
    r = CALL(iphlpapi, GetAdaptersInfo, buf, &size);
    T_EQ(r, ERROR_NO_DATA);
    T_EQ(size, 704);
    T_EQ(bytes_not(buf, ADAPTER_BUF, 0xAA), 0);

    T_CASE("a NULL buffer with a size returns ERROR_NO_DATA");
    size = 704;
    r = CALL(iphlpapi, GetAdaptersInfo, NULL, &size);
    T_EQ(r, ERROR_NO_DATA);
    T_EQ(size, 704);

    T_CASE("a NULL size pointer returns ERROR_INVALID_PARAMETER");
    memset(buf, 0xAA, sizeof buf);
    r = CALL(iphlpapi, GetAdaptersInfo, buf, NULL);
    T_EQ(r, ERROR_INVALID_PARAMETER);
    T_EQ(bytes_not(buf, ADAPTER_BUF, 0xAA), 0);

    T_CASE("last error is left alone");
    CALL(kernel32, SetLastError, 0x1234);
    size = 704;
    r = CALL(iphlpapi, GetAdaptersInfo, buf, &size);
    T_EQ(r, ERROR_NO_DATA);
    T_EQ(T_LAST_ERROR(), 0x1234);
    r = CALL(iphlpapi, GetAdaptersInfo, NULL, &size);
    T_EQ(r, ERROR_NO_DATA);
    T_EQ(T_LAST_ERROR(), 0x1234);
    r = CALL(iphlpapi, GetAdaptersInfo, buf, NULL);
    T_EQ(r, ERROR_INVALID_PARAMETER);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that the machine has no network adapter");
    size = 704;
    r = CALL(iphlpapi, GetAdaptersInfo, buf, &size);
    T_EQ(r, ERROR_NO_DATA);
    T_LOGGED(RT_CH_NET, "GetAdaptersInfo: the machine has no network adapter");
}
