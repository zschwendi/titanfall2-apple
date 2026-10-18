/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlogicalprocessorinformation
 * RT-CASES:  size query with NULL Buffer and length 0 returns the needed size and ERROR_INSUFFICIENT_BUFFER; a buffer 1 byte too small fails, reports the needed size and writes nothing; an exact buffer succeeds with the needed size; a buffer 32 bytes larger succeeds and writes nothing past the needed size; one core record per processor, a NUMA node record and a package record, every byte as specified; the core records and the package mask agree with GetSystemInfo; does not change last error on success
 * RT-TRAPS:  NULL ReturnedLength; NULL Buffer and a length that suffices
 */
#include "rt/shim.h"
#include "rt/machine.h"
#include "rt/win/sysinfo.h"

SHIM(kernel32, BOOL, GetLogicalProcessorInformation, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer, PDWORD ReturnedLength)
{
    /* The documentation does not say what a NULL ReturnedLength does. */
    if (ReturnedLength == NULL) RT_UNIMPLEMENTED(ctx, "GetLogicalProcessorInformation with NULL ReturnedLength");

    /* The machine has n processors, one package and no SMT (machine.h, RUNTIME.md 8.5): n
     * records RelationProcessorCore, one RelationNumaNode, one RelationProcessorPackage. */
    const RtMachine *m = rt_machine();
    const uint32_t n = m->cpu_count;
    const DWORD needed = (n + 2u) * (DWORD)sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);

    if (*ReturnedLength < needed) {
        /* Documented: ReturnLength is set to the required length, and the buffer is not written. */
        *ReturnedLength = needed;
        return FAIL(ctx, ERROR_INSUFFICIENT_BUFFER, FALSE);
    }
    if (Buffer == NULL) RT_UNIMPLEMENTED(ctx, "GetLogicalProcessorInformation with NULL Buffer and a length that suffices");

    /* The mask of all n processors: one bit per processor, 64 bits at most. */
    const ULONG_PTR all = (n >= 64u) ? ~(ULONG_PTR)0 : (((ULONG_PTR)1 << n) - 1u);

    /* Every byte that is not named is 0: each record is zeroed byte by byte first, so that the
     * padding and the unused members of the union are 0 as well. */
    for (uint32_t i = 0; i < n + 2u; i++) {
        unsigned char *p = (unsigned char *)&Buffer[i];
        for (unsigned k = 0; k < sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); k++) p[k] = 0;
    }

    for (uint32_t i = 0; i < n; i++) {
        Buffer[i].ProcessorMask = (ULONG_PTR)1 << i;
        Buffer[i].Relationship = RelationProcessorCore;
        Buffer[i].ProcessorCore.Flags = 0;          /* the processors of the mask do not share a core */
    }
    Buffer[n].ProcessorMask = all;
    Buffer[n].Relationship = RelationNumaNode;
    Buffer[n].NumaNode.NodeNumber = 0;              /* a non-NUMA system: all processors in node 0 */
    Buffer[n + 1u].ProcessorMask = all;
    Buffer[n + 1u].Relationship = RelationProcessorPackage;

    *ReturnedLength = needed;
    return TRUE;
}
