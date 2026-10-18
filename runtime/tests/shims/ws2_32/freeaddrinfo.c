#include "rt/test.h"
#include "rt/heap.h"
#include "rt/net.h"
#include "rt/win/net.h"

TEST(ws2_32, freeaddrinfo)
{
    WSADATA d;
    ADDRINFOA *r;
    ADDRINFOA *first, *second;
    HANDLE ph;

    T_CASE("NULL does nothing and leaves last error alone, also before WSAStartup");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(ws2_32, freeaddrinfo, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_EQ(rt_net_startup(0x0202, &d), 0);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(ws2_32, freeaddrinfo, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a list of two entries is freed and its blocks are no longer live");
    T_EQ(rt_net_getaddrinfo("localhost", NULL, NULL, &r), 0);          /* ::1, then 127.0.0.1 */
    T_REQUIRE(r != NULL && r->ai_next != NULL && r->ai_next->ai_next == NULL);
    ph = rt_heap_process();
    first = r;                                                          /* each entry is its block's address */
    second = r->ai_next;
    T_EQ(rt_heap_validate(ph, 0, first), TRUE);
    T_EQ(rt_heap_validate(ph, 0, second), TRUE);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(ws2_32, freeaddrinfo, r);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_heap_validate(ph, 0, first), FALSE);
    T_EQ(rt_heap_validate(ph, 0, second), FALSE);
}
