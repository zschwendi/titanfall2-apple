/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/winnt/nf-winnt-versetconditionmask
 * RT-CASES:  stores the operator of one version member and keeps the operators already in the mask; a later call for the same member replaces nothing and adds its bits; does not change last error
 * RT-TRAPS:  VerSetConditionMask with TypeMask; VerSetConditionMask with Condition
 */
#include "rt/shim.h"
#include "rt/win/sysinfo.h"

/* The mask is opaque to the guest: the documentation says only that the function "stores the
 * comparison information in the bits of this variable" and that the result is passed to
 * VerifyVersionInfo.  Both ends are this runtime's, so the layout only has to agree with
 * VerifyVersionInfoA.c: three bits for each member, at three times the bit number of the
 * member's TypeMask flag (the layout of the VER_SET_CONDITION macro's helper in winnt.h).
 *
 * Decided here, with no row (docs/handoff/gate2-run.md): tier0.dll's processor-topology
 * probe looks both functions up with GetProcAddress at load and calls this one four times,
 * for the major and minor version and the two service pack members, with VER_GREATER_EQUAL.
 * Refused, having no caller yet: a TypeMask that is not exactly one of those four members
 * (build number, platform, suite, product type, none, several) and a Condition that is not
 * one of the five comparison operators (the suite operators VER_AND and VER_OR, zero). */
SHIM(kernel32, ULONGLONG, VerSetConditionMask, ULONGLONG ConditionMask, DWORD TypeMask, BYTE Condition)
{
    unsigned shift;

    switch (TypeMask) {
    case VER_MINORVERSION:     shift = 0;  break;
    case VER_MAJORVERSION:     shift = 3;  break;
    case VER_SERVICEPACKMINOR: shift = 12; break;
    case VER_SERVICEPACKMAJOR: shift = 15; break;
    default:
        RT_UNIMPLEMENTED(ctx, "VerSetConditionMask with TypeMask 0x%x", (unsigned)TypeMask);
    }
    if (Condition < VER_EQUAL || Condition > VER_LESS_EQUAL)
        RT_UNIMPLEMENTED(ctx, "VerSetConditionMask with Condition %u", (unsigned)Condition);
    return ConditionMask | ((ULONGLONG)Condition << shift);
}
