#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/win/process.h"

/* The oracle is the core's comment: cb is the structure's size, the named handles, desktop,
 * title, flags and show state have the values it gives, and the other fields are not named
 * there, so the test checks only that they are written.  The structure sits in a box with a
 * guard after it.  Before the call every byte of the box is SI_FILL and the guard is SI_GUARD,
 * so a byte the shim does not write still shows its fill. */
#define SI_FILL   0xA5u
#define SI_GUARD  0x5Au

typedef struct {
    STARTUPINFOW si;
    unsigned char guard[32];
} SiBox;

TEST(kernel32, GetStartupInfoW)
{
    SiBox box;
    const unsigned char *raw = (const unsigned char *)&box.si;
    unsigned unwritten = 0, bad_guard = 0;

    memset(&box, SI_FILL, sizeof box);
    memset(box.guard, SI_GUARD, sizeof box.guard);
    CALL(kernel32, GetStartupInfoW, &box.si);

    T_CASE("cb is the structure's size");
    T_EQ(box.si.cb, sizeof(STARTUPINFOW));

    T_CASE("no inherited handles: hStdInput, hStdOutput and hStdError are NULL");
    T_EQ((uintptr_t)box.si.hStdInput, 0);
    T_EQ((uintptr_t)box.si.hStdOutput, 0);
    T_EQ((uintptr_t)box.si.hStdError, 0);

    T_CASE("lpDesktop and lpTitle are NULL");
    T_EQ((uintptr_t)box.si.lpDesktop, 0);
    T_EQ((uintptr_t)box.si.lpTitle, 0);

    T_CASE("dwFlags and wShowWindow are 0");
    T_EQ(box.si.dwFlags, 0);
    T_EQ(box.si.wShowWindow, 0);

    T_CASE("writes all 104 bytes and nothing after them");
    for (size_t i = 0; i < sizeof(STARTUPINFOW); i++)
        if (raw[i] == SI_FILL) unwritten++;
    T_EQ(unwritten, 0);
    for (size_t i = 0; i < sizeof box.guard; i++)
        if (box.guard[i] != SI_GUARD) bad_guard++;
    T_EQ(bad_guard, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetStartupInfoW, &box.si);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRAPS(CALL(kernel32, GetStartupInfoW, NULL), "NULL");
}
