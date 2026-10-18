#include "rt/test.h"

TEST(wsock32, WSASetLastError)
{
    /* First, and before any WSAStartup in this test process. */
    T_CASE("works before WSAStartup");
    CALL(wsock32, WSASetLastError, WSAEWOULDBLOCK);
    T_EQ(T_LAST_ERROR(), WSAEWOULDBLOCK);

    T_CASE("stores the value in the thread block at +0x68");
    CALL(wsock32, WSASetLastError, WSAEWOULDBLOCK);
    T_EQ(LD32(T_CTX()->gs_base + 0x68), 10035u);
    T_EQ(T_LAST_ERROR(), WSAEWOULDBLOCK);

    T_CASE("zero is stored like any other value");
    CALL(wsock32, WSASetLastError, WSAEWOULDBLOCK);  /* so that the zero overwrites something */
    CALL(wsock32, WSASetLastError, 0);
    T_EQ(LD32(T_CTX()->gs_base + 0x68), 0u);
    T_EQ(T_LAST_ERROR(), 0u);

    T_CASE("a negative value is stored as its 32 bits");
    /* The harness fills the upper half of the argument register with garbage on every call; the
     * 4 bytes after the field must stay as they were. */
    ST32(T_CTX()->gs_base + 0x6C, 0xA5A5A5A5);
    CALL(wsock32, WSASetLastError, -1);
    T_EQ(LD32(T_CTX()->gs_base + 0x68), 0xFFFFFFFFu);
    T_EQ(LD32(T_CTX()->gs_base + 0x6C), 0xA5A5A5A5u);
    CALL(wsock32, WSASetLastError, -1073741819);
    T_EQ(T_LAST_ERROR(), 0xC0000005u);
    T_EQ(LD32(T_CTX()->gs_base + 0x6C), 0xA5A5A5A5u);
}
