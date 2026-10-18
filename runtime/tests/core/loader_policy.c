/* The loader: the GetProcAddress policy table (runtime/core/loader_probe.c), rt_dispatch_fn,
 * and the proof that the loader functions that are not built yet fail loudly.  The task that
 * builds one of them deletes its line of core/loader_unbuilt below. */
#include "rt/test.h"
#include "../../core/loader.h"

TEST(core, loader_probe_policy)
{
    RtModule *k = rt_module_find("kernel32.dll");
    T_REQUIRE(k != NULL);
    /* The probed names marked absent are absent whatever shims exist: NULL,
     * ERROR_PROC_NOT_FOUND, one warning.  The list is what the C runtimes of the game's DLLs
     * ask for when they start (docs/handoff/gate3-load.md, entry 1: read from the runtime's
     * log of the loads), less the two marked present, plus CreateFile2. */
    static const char *const absent[] = { "FlsAlloc", "FlsFree", "FlsGetValue", "FlsSetValue",
        "InitializeCriticalSectionEx", "CreateSemaphoreExW", "SetThreadStackGuarantee", "CreateThreadpoolTimer",
        "SetThreadpoolTimer", "WaitForThreadpoolTimerCallbacks", "CloseThreadpoolTimer", "CreateThreadpoolWait",
        "SetThreadpoolWait", "CloseThreadpoolWait", "FlushProcessWriteBuffers", "FreeLibraryWhenCallbackReturns",
        "GetCurrentProcessorNumber", "CreateSymbolicLinkW", "SetDefaultDllDirectories", "EnumSystemLocalesEx",
        "CompareStringEx", "GetDateFormatEx", "GetLocaleInfoEx", "GetTimeFormatEx", "GetUserDefaultLocaleName",
        "IsValidLocaleName", "LCMapStringEx", "GetCurrentPackageId", "CreateFile2",
        /* the Visual Studio 2013 C runtime's own (amd_ags_x64.dll) */
        "CreateEventExW", "GetFileInformationByHandleExW", "SetFileInformationByHandleW" };
    T_EQ(sizeof absent / sizeof absent[0], 32);
    for (unsigned i = 0; i < 32; i++) {
        GUESTFN a = 1;
        rt_thread_set_last_error(0);
        T_EQ(rt_module_export(k, absent[i], 0), 0);
        T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);
        T_EQ(rt_ldr_probe(k, absent[i], &a), -1);
        T_EQ(a, 1);
    }
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "kernel32.dll!FlsAlloc is not provided"));
    /* Two probed names are present by the table, not by the accident that a shim file exists:
     * every C runtime asks for the first, the 2013 one also for the second, and the same DLLs
     * import both directly. */
    GUESTFN p = 0;
    T_EQ(rt_ldr_probe(k, "GetLogicalProcessorInformation", &p), 1);
    T_EQ(p, rt_test_import_slot(RT_ID_kernel32_GetLogicalProcessorInformation));
    T_EQ(rt_module_export(k, "GetLogicalProcessorInformation", 0), p);
    T_EQ(rt_ldr_probe(k, "GetTickCount64", &p), 1);
    T_EQ(p, rt_test_import_slot(RT_ID_kernel32_GetTickCount64));
    T_EQ(rt_module_export(k, "GetTickCount64", 0), p);
    /* Any other name is not the table's business, on kernel32 or elsewhere. */
    GUESTFN a = 0;
    T_EQ(rt_ldr_probe(k, "GetLastError", &a), 0);
    T_EQ(rt_ldr_probe(rt_module_find("user32.dll"), "FlsAlloc", &a), 0);
    T_TRUE(rt_module_export(k, "GetLastError", 0) == rt_test_import_slot(RT_ID_kernel32_GetLastError));
}

TEST(core, loader_dispatch_fn)
{
    /* rt_dispatch_fn is rt_lookup as a function: the registered function, or the miss hook
     * with the target recorded. */
    uint64_t stub = rt_test_import_slot(RT_ID_kernel32_GetLastError);
    T_TRUE(rt_dispatch_fn(T_CTX(), stub) == rt_disp_find(stub));
    T_TRUE(rt_dispatch_fn(T_CTX(), stub + 1) == rt_dispatch_miss);
    T_EQ(RT_THREAD(T_CTX())->miss_target, stub + 1);
    T_TRUE(rt_dispatch_fn(T_CTX(), 0) != NULL && rt_dispatch_fn(T_CTX(), 0) != rt_disp_find(stub));
    T_TRAPS(rt_dispatch_fn(T_CTX(), 0x1234)(T_CTX()), "0x1234");
}
