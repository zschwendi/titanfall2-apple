#include "rt/test.h"
#include "rt/machine.h"
#include "rt/win/error.h"
#include "rt/win/sysinfo.h"

#define ALL4 (VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR)

/* The mask for `types` with one operator, built as a program builds it. */
static DWORDLONG mask(DWORD types, BYTE op)
{
    DWORDLONG m = 0;
    for (DWORD bit = 1; bit <= 0x80; bit <<= 1)
        if (types & bit)
            m = CALL(kernel32, VerSetConditionMask, m, bit, op);
    return m;
}

/* VerifyVersionInfoA for version major.minor, service pack spmaj.spmin, members `types`. */
static BOOL verify(DWORD major, DWORD minor, WORD spmaj, WORD spmin, DWORD types, BYTE op)
{
    OSVERSIONINFOEXA v;
    memset(&v, 0x5a, sizeof v);                    /* members that are not tested hold garbage */
    v.dwOSVersionInfoSize = sizeof v;
    v.dwMajorVersion = major;
    v.dwMinorVersion = minor;
    v.wServicePackMajor = spmaj;
    v.wServicePackMinor = spmin;
    return CALL(kernel32, VerifyVersionInfoA, &v, types, mask(types, op));
}

TEST(kernel32, VerifyVersionInfoA)
{
    const RtMachine *m = rt_machine();
    OSVERSIONINFOEXA v;

    /* The system the runtime describes: Windows 7 Service Pack 1. */
    T_EQ(m->os_major, 6);
    T_EQ(m->os_minor, 1);
    T_EQ(m->sp_major, 1);
    T_EQ(m->sp_minor, 0);

    T_CASE("a requirement the system meets returns TRUE and leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(verify(6, 0, 0, 0, ALL4, VER_GREATER_EQUAL), TRUE);        /* what tier0.dll asks at load */
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(verify(6, 1, 1, 0, ALL4, VER_EQUAL), TRUE);
    T_EQ(verify(6, 1, 1, 0, ALL4, VER_GREATER_EQUAL), TRUE);
    T_EQ(verify(6, 1, 1, 0, ALL4, VER_LESS_EQUAL), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a requirement it does not meet returns FALSE with ERROR_OLD_WIN_VERSION");
    CALL(kernel32, SetLastError, 0);
    T_EQ(verify(6, 2, 0, 0, ALL4, VER_GREATER_EQUAL), FALSE);       /* Windows 8 or later */
    T_EQ(T_LAST_ERROR(), ERROR_OLD_WIN_VERSION);
    CALL(kernel32, SetLastError, 0);
    T_EQ(verify(6, 1, 1, 0, ALL4, VER_GREATER), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_OLD_WIN_VERSION);
    CALL(kernel32, SetLastError, 0);
    T_EQ(verify(6, 1, 1, 0, ALL4, VER_LESS), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_OLD_WIN_VERSION);
    CALL(kernel32, SetLastError, 0);
    T_EQ(verify(6, 1, 0, 0, ALL4, VER_EQUAL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_OLD_WIN_VERSION);

    T_CASE("major version, minor version and service pack are tested in that order, while they are equal");
    /* the documentation's three examples, moved from 5.1 SP1 to this system */
    T_EQ(verify(5, 1, 2, 0, ALL4, VER_GREATER_EQUAL), TRUE);        /* the major version is greater: stops */
    T_EQ(verify(6, 0, 2, 0, ALL4, VER_GREATER_EQUAL), TRUE);        /* the minor version is greater: stops */
    T_EQ(verify(6, 2, 0, 0, ALL4, VER_GREATER_EQUAL), FALSE);       /* the minor version is less */
    T_EQ(verify(6, 1, 2, 0, ALL4, VER_GREATER_EQUAL), FALSE);       /* equal down to the service pack, which is less */
    T_EQ(verify(6, 1, 1, 1, ALL4, VER_GREATER_EQUAL), FALSE);       /* decided by the last member */
    T_EQ(verify(6, 1, 0, 9, ALL4, VER_GREATER), TRUE);
    T_EQ(verify(7, 0, 0, 0, ALL4, VER_LESS), TRUE);
    T_EQ(verify(5, 9, 9, 9, ALL4, VER_LESS), FALSE);
    T_EQ(verify(6, 1, 1, 0, ALL4, VER_LESS_EQUAL), TRUE);
    T_EQ(verify(5, 1, 1, 0, ALL4, VER_EQUAL), FALSE);

    T_CASE("members whose dwTypeMask bit is clear are ignored");
    T_EQ(verify(6, 99, 99, 99, VER_MAJORVERSION, VER_EQUAL), TRUE);
    T_EQ(verify(6, 1, 99, 99, VER_MAJORVERSION | VER_MINORVERSION, VER_EQUAL), TRUE);
    T_EQ(verify(99, 99, 1, 99, VER_SERVICEPACKMAJOR, VER_EQUAL), TRUE);
    T_EQ(verify(99, 2, 99, 99, VER_MINORVERSION, VER_LESS), TRUE);
    T_EQ(verify(99, 99, 99, 1, VER_SERVICEPACKMINOR, VER_GREATER_EQUAL), FALSE);
    /* and so are the operators in the mask for members that are not tested */
    memset(&v, 0, sizeof v);
    v.dwOSVersionInfoSize = sizeof v;
    v.dwMajorVersion = 6;
    T_EQ(CALL(kernel32, VerifyVersionInfoA, &v, VER_MAJORVERSION,
              CALL(kernel32, VerSetConditionMask, mask(VER_MAJORVERSION, VER_EQUAL), VER_MINORVERSION, VER_LESS)), TRUE);

    memset(&v, 0, sizeof v);
    v.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    T_TRAPS(CALL(kernel32, VerifyVersionInfoA, &v, VER_MAJORVERSION, mask(VER_MAJORVERSION, VER_EQUAL)),
            "VerifyVersionInfoA with dwOSVersionInfoSize 148");
    v.dwOSVersionInfoSize = sizeof v;
    T_TRAPS(CALL(kernel32, VerifyVersionInfoA, &v, 0, 0), "VerifyVersionInfoA with dwTypeMask 0x0");
    T_TRAPS(CALL(kernel32, VerifyVersionInfoA, &v, VER_MAJORVERSION | VER_BUILDNUMBER, mask(VER_MAJORVERSION, VER_EQUAL)),
            "VerifyVersionInfoA with dwTypeMask 0x6");
    T_TRAPS(CALL(kernel32, VerifyVersionInfoA, &v, VER_MAJORVERSION | VER_MINORVERSION, mask(VER_MAJORVERSION, VER_EQUAL)),
            "VerifyVersionInfoA with no operator for member 0x1");
    T_TRAPS(CALL(kernel32, VerifyVersionInfoA, &v, VER_MAJORVERSION | VER_MINORVERSION,
                 CALL(kernel32, VerSetConditionMask, mask(VER_MAJORVERSION, VER_GREATER), VER_MINORVERSION, VER_LESS_EQUAL)),
            "VerifyVersionInfoA with two operators in one mask, 2 and 5");
}
