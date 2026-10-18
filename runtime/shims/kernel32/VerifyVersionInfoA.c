/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-verifyversioninfoa
 * RT-CASES:  a requirement the system meets returns TRUE and leaves last error alone; a requirement it does not meet returns FALSE with ERROR_OLD_WIN_VERSION; major version, minor version and service pack are tested in that order, while they are equal; members whose dwTypeMask bit is clear are ignored
 * RT-TRAPS:  VerifyVersionInfoA with dwOSVersionInfoSize; VerifyVersionInfoA with dwTypeMask; with no operator for member; with two operators in one mask
 */
#include "rt/shim.h"
#include "rt/machine.h"
#include "rt/win/sysinfo.h"

/* The system is the one rt_machine describes: Windows 7 Service Pack 1, version 6.1.
 *
 * The documentation: "major, minor, and service pack versions are tested in a hierarchical
 * manner ... The function tests these values in this order: major version, minor version,
 * and service pack version.  The function continues testing values while they are equal, and
 * stops when one of the values does not meet the specified condition."  So the members
 * tested are compared as one number, most significant first, with the one operator.
 *
 * It also says the operator of the major version "supersedes" those of the members after
 * it, without saying what the request becomes for every pair of operators; a mask with two
 * different operators is refused here rather than guessed, and so is a member tested
 * with no operator in the mask.  The other members (build number, platform, suite mask,
 * product type), an empty dwTypeMask and a dwOSVersionInfoSize that is not
 * sizeof(OSVERSIONINFOEXA) have no caller yet and are refused too.  The mask's layout is
 * this runtime's own: see VerSetConditionMask.c.
 *
 * Decided here, with no row (docs/handoff/gate2-run.md): tier0.dll's processor-topology
 * probe asks "6.0 with no service pack, or later" at load and uses
 * GetLogicalProcessorInformation only if the answer is TRUE, as it is on Windows 7. */
SHIM(kernel32, BOOL, VerifyVersionInfoA, LPOSVERSIONINFOEXA lpVersionInformation, DWORD dwTypeMask, DWORDLONG dwlConditionMask)
{
    static const struct { DWORD flag; unsigned shift; } order[4] = {
        { VER_MAJORVERSION, 3 }, { VER_MINORVERSION, 0 }, { VER_SERVICEPACKMAJOR, 15 }, { VER_SERVICEPACKMINOR, 12 },
    };
    const RtMachine *m = rt_machine();
    const OSVERSIONINFOEXA *v = lpVersionInformation;
    unsigned op = 0;
    int cmp = 0;                                  /* the system against the request: -1, 0, 1 */

    if (v->dwOSVersionInfoSize != sizeof(OSVERSIONINFOEXA))
        RT_UNIMPLEMENTED(ctx, "VerifyVersionInfoA with dwOSVersionInfoSize %u", (unsigned)v->dwOSVersionInfoSize);
    if (dwTypeMask == 0 ||
        (dwTypeMask & ~(VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR)))
        RT_UNIMPLEMENTED(ctx, "VerifyVersionInfoA with dwTypeMask 0x%x", (unsigned)dwTypeMask);

    for (unsigned i = 0; i < 4; i++) {
        unsigned c;
        uint32_t have, want;

        if (!(dwTypeMask & order[i].flag))
            continue;
        c = (unsigned)(dwlConditionMask >> order[i].shift) & 7u;
        if (c < VER_EQUAL || c > VER_LESS_EQUAL)
            RT_UNIMPLEMENTED(ctx, "VerifyVersionInfoA with no operator for member 0x%x (mask bits %u)", (unsigned)order[i].flag, c);
        if (op && c != op)
            RT_UNIMPLEMENTED(ctx, "VerifyVersionInfoA with two operators in one mask, %u and %u", op, c);
        op = c;
        switch (order[i].flag) {
        case VER_MAJORVERSION:     have = m->os_major; want = v->dwMajorVersion;    break;
        case VER_MINORVERSION:     have = m->os_minor; want = v->dwMinorVersion;    break;
        case VER_SERVICEPACKMAJOR: have = m->sp_major; want = v->wServicePackMajor; break;
        default:                   have = m->sp_minor; want = v->wServicePackMinor; break;
        }
        if (cmp == 0 && have != want)
            cmp = have > want ? 1 : -1;           /* the first member that differs decides */
    }

    switch (op) {
    case VER_EQUAL:         if (cmp == 0) return TRUE; break;
    case VER_GREATER:       if (cmp > 0)  return TRUE; break;
    case VER_GREATER_EQUAL: if (cmp >= 0) return TRUE; break;
    case VER_LESS:          if (cmp < 0)  return TRUE; break;
    default:                if (cmp <= 0) return TRUE; break;     /* VER_LESS_EQUAL */
    }
    return FAIL(ctx, ERROR_OLD_WIN_VERSION, FALSE);
}
