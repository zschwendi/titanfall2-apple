#include "rt/test.h"

TEST(kernel32, IsDebuggerPresent)
{
    T_CASE("returns FALSE in a normal run");
    T_EQ(CALL(kernel32, IsDebuggerPresent), FALSE);

    T_CASE("agrees with the BeingDebugged byte of the process block");
    uint64_t peb = LD64(T_CTX()->gs_base + 0x60);       /* where inlined guest code finds the process block */
    T_REQUIRE(peb != 0);
    T_EQ(LD8(peb + 2), 0);                              /* the runtime starts with no debugger */
    ST8(peb + 2, 1);
    T_EQ(CALL(kernel32, IsDebuggerPresent), TRUE);
    ST8(peb + 2, 0x80);                                 /* any nonzero byte counts, and the answer is exactly 1 */
    T_EQ(CALL(kernel32, IsDebuggerPresent), TRUE);
    ST8(peb + 2, 0);
    T_EQ(CALL(kernel32, IsDebuggerPresent), FALSE);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, IsDebuggerPresent);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
