#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/env.h"
#include "rt/win/process.h"

/* The oracle is the core's comment, with the A structure's layout: cb is the structure's size
 * (104), the named handles have the values it gives, the three start-up strings are NULL, and
 * flags and show state are 0.  Every one of the 104 bytes must equal what the core wrote into a
 * zeroed STARTUPINFOW, so padding and the members the core does not name are checked too.  The
 * structure sits in a box with a guard after it: before the call every byte of the box is
 * SI_FILL and the guard is SI_GUARD, so a byte the shim does not write still shows its fill. */
#define SI_FILL   0xA5u
#define SI_GUARD  0x5Au

typedef struct {
    STARTUPINFOA si;
    unsigned char guard[32];
} SiBox;

TEST(kernel32, GetStartupInfoA)
{
    SiBox box;
    STARTUPINFOW want;
    unsigned bad_guard = 0;

    memset(&box, SI_FILL, sizeof box);
    memset(box.guard, SI_GUARD, sizeof box.guard);
    memset(&want, 0, sizeof want);
    rt_env_startup_info(&want);
    CALL(kernel32, GetStartupInfoA, &box.si);

    T_CASE("cb is the structure's size");
    T_EQ(box.si.cb, sizeof(STARTUPINFOA));
    T_EQ(box.si.cb, 104);

    T_CASE("no inherited handles: hStdInput, hStdOutput and hStdError are NULL");
    T_EQ((uintptr_t)box.si.hStdInput, 0);
    T_EQ((uintptr_t)box.si.hStdOutput, 0);
    T_EQ((uintptr_t)box.si.hStdError, 0);

    T_CASE("lpReserved, lpDesktop and lpTitle are NULL");
    T_EQ((uintptr_t)box.si.lpReserved, 0);
    T_EQ((uintptr_t)box.si.lpDesktop, 0);
    T_EQ((uintptr_t)box.si.lpTitle, 0);

    T_CASE("dwFlags and wShowWindow are 0");
    T_EQ(box.si.dwFlags, 0);
    T_EQ(box.si.wShowWindow, 0);

    T_CASE("writes all 104 bytes and nothing after them");
    T_EQ(memcmp(&box.si, &want, sizeof want), 0);
    for (size_t i = 0; i < sizeof box.guard; i++)
        if (box.guard[i] != SI_GUARD) bad_guard++;
    T_EQ(bad_guard, 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetStartupInfoA, &box.si);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRAPS(CALL(kernel32, GetStartupInfoA, NULL), "GetStartupInfoA with NULL lpStartupInfo");
}
