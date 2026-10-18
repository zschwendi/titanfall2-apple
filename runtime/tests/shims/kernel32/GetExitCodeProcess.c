#include "rt/test.h"
#include "rt/sync.h"

TEST(kernel32, GetExitCodeProcess)
{
    DWORD code[2];
    HANDLE ev;

    T_CASE("(HANDLE)-1 gives TRUE and STILL_ACTIVE, and leaves last error alone");
    code[0] = 0x11111111;
    code[1] = 0x22222222;                           /* the word after the DWORD must stay */
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetExitCodeProcess, (HANDLE)-1, &code[0]), TRUE);
    T_EQ(code[0], STILL_ACTIVE);
    T_EQ(code[1], 0x22222222);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("NULL and an event handle fail with ERROR_INVALID_HANDLE and do not write the DWORD");
    code[0] = 0x11111111;
    T_EQ(CALL(kernel32, GetExitCodeProcess, NULL, &code[0]), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(code[0], 0x11111111);
    ev = rt_event_create(T_CTX(), FALSE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(kernel32, GetExitCodeProcess, ev, &code[0]), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(code[0], 0x11111111);
    T_EQ(code[1], 0x22222222);

    /* A NULL lpExitCode with a valid handle is a stop of the core: the test proves it. */
    T_TRAPS(CALL(kernel32, GetExitCodeProcess, (HANDLE)-1, NULL), "GetExitCodeProcess with lpExitCode NULL");
}
