/* A look-up by name in a stubbed game DLL other than EA's (rt_module_export, rt/module.h):
 * the shims we wrote are found, and every other name or ordinal STOPS THE RUN.  It is never
 * answered NULL: these DLLs were not lifted and not read, so "not exported" would be made up.
 * (originsdk.dll, the fourth stubbed module, has runtime/tests/standins/origin_getproc.c.)
 *
 * Where the expected values come from:
 *   - WHICH MODULES ARE STUBBED: runtime/modules.def (the rows `stubbed`).
 *   - WHICH NAMES HAVE A SHIM: the files of runtime/shims/gfsdk_ssao_win64/ and
 *     runtime/shims/d3dcompiler_46/; GFSDK_TXAA.win64.dll has no shim directory.
 *   - THE RULE AND THE REPORT'S FIRST LINE: the brief of unit fix-review-night, section 1 (the
 *     planner's rule for the trap-stub libraries is the same as for EA's: halt, do not answer
 *     NULL), in the form of docs/RUNTIME.md section 9.1.
 *   - THAT A SYSTEM MODULE STILL ANSWERS NULL: RUNTIME.md section 1.5 and core/modules.c; the C
 *     runtime's probes of kernel32.dll depend on it. */
#include <stdio.h>
#include <string.h>

#include "rt/test.h"
#include "rt/module.h"

TEST(core, stubbed_export)
{
    RtModule *ssao = rt_module_find("GFSDK_SSAO.win64.dll"), *txaa = rt_module_find("GFSDK_TXAA.win64.dll");
    RtModule *d3dc = rt_module_find("d3dcompiler_46.dll"), *k32 = rt_module_find("kernel32.dll");
    T_REQUIRE(ssao != NULL && txaa != NULL && d3dc != NULL && k32 != NULL);

    T_CASE("the three are stubbed modules; kernel32.dll is not");
    T_EQ(ssao->kind, RT_MOD_STUBBED);
    T_EQ(txaa->kind, RT_MOD_STUBBED);
    T_EQ(d3dc->kind, RT_MOD_STUBBED);
    T_EQ(k32->kind, RT_MOD_SYSTEM);

    T_CASE("a name with a shim gives the shim's stub, the one an import slot gets, and leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)ssao->base, "GFSDK_SSAO_CreateContext_D3D11"),
         rt_native_import("gfsdk_ssao.win64.dll", "GFSDK_SSAO_CreateContext_D3D11", 0));
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)d3dc->base, "D3DReflect"),
         rt_native_import("d3dcompiler_46.dll", "D3DReflect", 0));
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)d3dc->base, "D3DGetBlobPart"),
         rt_native_import("d3dcompiler_46.dll", "D3DGetBlobPart", 0));
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(txaa->nshims, 0);

    T_CASE("a name with no shim stops the run, in each of the three");
    T_TRAPS(CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)ssao->base, "NoShimForThisName"),
            "FATAL unimplemented import GetProcAddress(gfsdk_ssao.win64.dll, NoShimForThisName)\n");
    T_TRAPS(CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)txaa->base, "NoShimForThisName"),
            "FATAL unimplemented import GetProcAddress(gfsdk_txaa.win64.dll, NoShimForThisName)\n");
    T_TRAPS(CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)d3dc->base, "NoShimForThisName"),
            "FATAL unimplemented import GetProcAddress(d3dcompiler_46.dll, NoShimForThisName)\n");
    T_TRAPS(CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)d3dc->base, "NoShimForThisName"),
            "in shim kernel32.dll!GetProcAddress, called from ");

    T_CASE("a shim's name in another case, and an ordinal, stop as well");
    T_TRAPS(CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)d3dc->base, "d3dreflect"),
            "FATAL unimplemented import GetProcAddress(d3dcompiler_46.dll, d3dreflect)\n");
    T_TRAPS(CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)ssao->base, (LPCVOID)(uintptr_t)1),
            "FATAL unimplemented import GetProcAddress(gfsdk_ssao.win64.dll, #1)\n");
    T_TRAPS(rt_module_export(txaa, NULL, 9), "FATAL unimplemented import GetProcAddress(gfsdk_txaa.win64.dll, #9)\n");

    T_CASE("a system module is as it was: a name it does not have gives NULL with ERROR_PROC_NOT_FOUND and no stop");
    CALL(kernel32, SetLastError, 0);
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)k32->base, "NoShimForThisName"), 0);
    T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);
    CALL(kernel32, SetLastError, 0);
    T_EQ((uint64_t)(uintptr_t)CALL(kernel32, GetProcAddress, (HMODULE)(uintptr_t)k32->base, "FlsAlloc"), 0);
    T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);
}
