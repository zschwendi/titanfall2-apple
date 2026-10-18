#include "rt/test.h"

TEST(wsock32, WSAGetLastError)
{
    T_CASE("returns WSAEWOULDBLOCK the thread stored");
    rt_set_last_error(T_CTX(), WSAEWOULDBLOCK);
    T_EQ(CALL(wsock32, WSAGetLastError), WSAEWOULDBLOCK);

    T_CASE("returns 0xC0000005 as the int -1073741819");
    rt_set_last_error(T_CTX(), 0xC0000005u);
    T_EQ(CALL(wsock32, WSAGetLastError), -1073741819);

    T_CASE("two calls in a row give the same value");
    rt_set_last_error(T_CTX(), WSAEWOULDBLOCK);
    T_EQ(CALL(wsock32, WSAGetLastError), WSAEWOULDBLOCK);
    T_EQ(CALL(wsock32, WSAGetLastError), WSAEWOULDBLOCK);
    T_EQ(T_LAST_ERROR(), WSAEWOULDBLOCK);

    T_CASE("reads the thread block field at +0x68");
    ST32(T_CTX()->gs_base + 0x68, 0x12345678);          /* what inlined guest code would write */
    T_EQ(CALL(wsock32, WSAGetLastError), 0x12345678);
    T_EQ(LD32(T_CTX()->gs_base + 0x68), 0x12345678);
}
