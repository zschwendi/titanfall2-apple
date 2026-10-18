#include "rt/test.h"

/* The FILETIME as the 64-bit value it stands for (low DWORD first, as the guest lays it out). */
static ULONGLONG ft_value(const FILETIME *f)
{
    return ((ULONGLONG)f->dwHighDateTime << 32) | f->dwLowDateTime;
}

/* Each FILETIME is preset to PRESET_VALUE and the two DWORDs after the four are preset to
 * GUARD: a write past the end of its FILETIME, or a write where the shim must not write,
 * shows up in one of them. */
#define PRESET_VALUE  (((ULONGLONG)0x22222222 << 32) | 0x11111111)
#define GUARD         0x5A5A5A5A

typedef struct {
    FILETIME creation, exit, kernel, user;
    DWORD guard[2];
} TIMES;

static void preset(TIMES *t)
{
    t->creation.dwLowDateTime = 0x11111111;  t->creation.dwHighDateTime = 0x22222222;
    t->exit.dwLowDateTime = 0x11111111;      t->exit.dwHighDateTime = 0x22222222;
    t->kernel.dwLowDateTime = 0x11111111;    t->kernel.dwHighDateTime = 0x22222222;
    t->user.dwLowDateTime = 0x11111111;      t->user.dwHighDateTime = 0x22222222;
    t->guard[0] = GUARD;
    t->guard[1] = GUARD;
}

TEST(kernel32, GetProcessTimes)
{
    TIMES t;
    ULONGLONG creation1, kernel1, user1;
    HANDLE ev;

    T_CASE("(HANDLE)-1 gives TRUE, a creation time, kernel and user times, an exit time of 0, and leaves last error alone");
    preset(&t);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetProcessTimes, (HANDLE)-1, &t.creation, &t.exit, &t.kernel, &t.user), TRUE);
    T_TRUE(ft_value(&t.creation) != 0);
    /* The host reports microseconds and the FILETIME is in 100 ns units: whole tens. */
    T_TRUE(ft_value(&t.kernel) % 10 == 0);
    T_TRUE(ft_value(&t.user) % 10 == 0);
    T_EQ(t.exit.dwLowDateTime, 0);
    T_EQ(t.exit.dwHighDateTime, 0);
    T_EQ(t.guard[0], GUARD);
    T_EQ(t.guard[1], GUARD);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a second call gives the same creation time, and kernel and user times not less than before");
    creation1 = ft_value(&t.creation);
    kernel1 = ft_value(&t.kernel);
    user1 = ft_value(&t.user);
    preset(&t);
    T_EQ(CALL(kernel32, GetProcessTimes, (HANDLE)-1, &t.creation, &t.exit, &t.kernel, &t.user), TRUE);
    T_EQ(ft_value(&t.creation), creation1);
    T_TRUE(ft_value(&t.kernel) >= kernel1);
    T_TRUE(ft_value(&t.user) >= user1);
    T_EQ(t.exit.dwLowDateTime, 0);
    T_EQ(t.exit.dwHighDateTime, 0);
    T_EQ(t.guard[0], GUARD);
    T_EQ(t.guard[1], GUARD);

    T_CASE("an event handle gives FALSE with ERROR_INVALID_HANDLE and writes nothing");
    ev = CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    preset(&t);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetProcessTimes, ev, &t.creation, &t.exit, &t.kernel, &t.user), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(ft_value(&t.creation), PRESET_VALUE);
    T_EQ(ft_value(&t.exit), PRESET_VALUE);
    T_EQ(ft_value(&t.kernel), PRESET_VALUE);
    T_EQ(ft_value(&t.user), PRESET_VALUE);
    T_EQ(t.guard[0], GUARD);
    T_EQ(t.guard[1], GUARD);

    /* A NULL pointer is a stop of the shim (RT_UNIMPLEMENTED): one proof per pointer. */
    T_TRAPS(CALL(kernel32, GetProcessTimes, (HANDLE)-1, NULL, &t.exit, &t.kernel, &t.user), "GetProcessTimes with lpCreationTime NULL");
    T_TRAPS(CALL(kernel32, GetProcessTimes, (HANDLE)-1, &t.creation, NULL, &t.kernel, &t.user), "GetProcessTimes with lpExitTime NULL");
    T_TRAPS(CALL(kernel32, GetProcessTimes, (HANDLE)-1, &t.creation, &t.exit, NULL, &t.user), "GetProcessTimes with lpKernelTime NULL");
    T_TRAPS(CALL(kernel32, GetProcessTimes, (HANDLE)-1, &t.creation, &t.exit, &t.kernel, NULL), "GetProcessTimes with lpUserTime NULL");
}
