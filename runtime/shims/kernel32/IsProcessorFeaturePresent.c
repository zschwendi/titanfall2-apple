/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent
 * RT-CASES:  returns nonzero for a feature the processor has; returns FALSE for a feature the processor lacks; returns FALSE for a feature number the machine does not describe and logs it; does not change last error
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/machine.h"

SHIM(kernel32, BOOL, IsProcessorFeaturePresent, DWORD ProcessorFeature)
{
    /* The answer is the machine's (RUNTIME.md 8.5): the PF_* numbers are read there from the
     * same bits as CPUID, so the two cannot disagree.  Microsoft documents nonzero for a
     * supported feature and zero otherwise, with no failure return, so last error is left alone. */
    return rt_machine_processor_feature(ProcessorFeature);
}
