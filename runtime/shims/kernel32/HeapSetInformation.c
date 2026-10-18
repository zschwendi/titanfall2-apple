/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/heapapi/nf-heapapi-heapsetinformation
 * RT-CASES:  class 1 returns TRUE for a NULL heap and for the process heap; class 0 with the value 2 returns TRUE on a live heap; the accepted calls log one line, once; does not change last error
 * RT-TRAPS:  HeapOptimizeResources; unknown class; HeapCompatibilityInformation with a length other than 4; HeapCompatibilityInformation with a NULL HeapInformation; HeapCompatibilityInformation with a value other than 2; HeapCompatibilityInformation on a handle that is not a live heap
 */
#include "rt/shim.h"
#include "rt/heap.h"
#include "rt/log.h"
#include "rt/win/memory.h"

SHIM(kernel32, BOOL, HeapSetInformation, HANDLE HeapHandle, HEAP_INFORMATION_CLASS HeapInformationClass, PVOID HeapInformation, SIZE_T HeapInformationLength)
{
    /* Two classes are accepted, as the notes of this task state them.  HeapEnableTerminationOnCorruption
     * (1): HeapHandle may be NULL and the other two arguments are not looked at.
     * HeapCompatibilityInformation (0): the length must be 4 and the ULONG it points to 2 (the
     * low-fragmentation heap), on a live heap.  Every other case is a stop, with its values. */
    switch (HeapInformationClass) {
    case HeapEnableTerminationOnCorruption:
        break;
    case HeapCompatibilityInformation:
        if (HeapInformationLength != 4)
            RT_UNIMPLEMENTED(ctx, "HeapCompatibilityInformation with a length other than 4: %llu",
                             (unsigned long long)HeapInformationLength);
        if (HeapInformation == NULL)
            RT_UNIMPLEMENTED(ctx, "HeapCompatibilityInformation with a NULL HeapInformation");
        if (*(ULONG *)HeapInformation != 2)
            RT_UNIMPLEMENTED(ctx, "HeapCompatibilityInformation with a value other than 2: %u",
                             (unsigned int)*(ULONG *)HeapInformation);
        if (!rt_heap_validate(HeapHandle, 0, NULL))
            RT_UNIMPLEMENTED(ctx, "HeapCompatibilityInformation on a handle that is not a live heap: %p",
                             (void *)HeapHandle);
        break;
    case HeapOptimizeResources:
        RT_UNIMPLEMENTED(ctx, "HeapOptimizeResources with HeapHandle %p", (void *)HeapHandle);
    default:
        RT_UNIMPLEMENTED(ctx, "unknown class %d", (int)HeapInformationClass);
    }

    /* The accepted calls change nothing in the runtime: the line is printed once, not per call. */
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "kernel32.dll!HeapSetInformation",
                "HeapSetInformation: the class is accepted and nothing is changed");
    return TRUE;
}
