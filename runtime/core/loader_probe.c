/* GetProcAddress policy for the names the C runtime probes (docs/RUNTIME.md section 1.5,
 * docs/findings/24-import-surface.md, last section).  Internal interface: loader.h.
 *
 * The Visual Studio 2012 C runtime inside 16 game DLLs asks kernel32.dll for 29 functions by
 * name when its DLL starts, and uses an older import for each NULL; the Visual Studio 2013
 * one inside amd_ags_x64.dll asks for the same 29 and four more.  WHICH names is read from
 * the runtime's log of the 14 DLLs that load today (docs/handoff/gate3-load.md, entry 1:
 * every one of them asks for the same 29 in the same order), not from the strings in the
 * files: finding 24 took `CreateFile2` for the 29th where the run shows
 * `GetLogicalProcessorInformation`.  The answer decides which code path every one of those
 * C runtimes takes, so it is THIS TABLE and not the accident of which shim files exist: a
 * name marked absent answers NULL (the ordinary logged miss of rt_module_export) even if a
 * shim for it exists because some DLL imports it directly; a name marked present answers
 * its shim, or a named trap stub until the shim is written.  Members of one family (the
 * second column) are present or absent together: the fallback is per function, and FlsAlloc
 * with TlsGetValue would mix two index spaces (section 1.5).  rt_ldr_probe_prescan is fatal
 * if a family is mixed.
 *
 * Today every row is absent except two: the answers of a kernel32 older than Vista, plus
 * the two functions that the same DLLs also import directly (so no kernel32 that lacks them
 * could have loaded the game, and the shims exist anyway).  Each fallback is a function
 * tier0 and vstdlib import anyway (finding 24, waves 1 to 5), so no shim exists only for a
 * probe.  Windows 7, which the machine description reports, has all of them except the
 * ones marked "8" and "never": changing a family to present is a planner decision.
 */
#include <string.h>

#include "loader.h"

typedef struct { const char *name; const char *family; bool present; } RtProbe;

static const RtProbe probes[] = {
    /* name                              family        present    the C runtime's fallback */
    { "FlsAlloc",                        "fls",        false },   /* TlsAlloc */
    { "FlsFree",                         "fls",        false },   /* TlsFree */
    { "FlsGetValue",                     "fls",        false },   /* TlsGetValue */
    { "FlsSetValue",                     "fls",        false },   /* TlsSetValue */
    { "InitializeCriticalSectionEx",     "csex",       false },   /* InitializeCriticalSectionAndSpinCount */
    { "CreateSemaphoreExW",              "semex",      false },   /* CreateSemaphoreW */
    { "SetThreadStackGuarantee",         "stack",      false },   /* the call is skipped */
    { "CreateThreadpoolTimer",           "threadpool", false },   /* the C runtime's own timer path */
    { "SetThreadpoolTimer",              "threadpool", false },
    { "WaitForThreadpoolTimerCallbacks", "threadpool", false },
    { "CloseThreadpoolTimer",            "threadpool", false },
    { "CreateThreadpoolWait",            "threadpool", false },
    { "SetThreadpoolWait",               "threadpool", false },
    { "CloseThreadpoolWait",             "threadpool", false },
    { "FreeLibraryWhenCallbackReturns",  "threadpool", false },
    { "FlushProcessWriteBuffers",        "flush",      false },   /* the call is skipped */
    { "GetCurrentProcessorNumber",       "procnum",    false },   /* processor 0 is assumed */
    { "GetLogicalProcessorInformation",  "procinfo",   true  },   /* XP SP3 and later (Microsoft's page); 16 of the 17 DLLs import it too */
    { "CreateSymbolicLinkW",             "symlink",    false },   /* the call fails */
    { "SetDefaultDllDirectories",        "dlldirs",    false },   /* 8 (7 only with KB2533623): LoadLibraryExW without search flags */
    { "EnumSystemLocalesEx",             "localeex",   false },   /* EnumSystemLocalesW */
    { "CompareStringEx",                 "localeex",   false },   /* CompareStringW */
    { "GetDateFormatEx",                 "localeex",   false },   /* GetDateFormatW */
    { "GetLocaleInfoEx",                 "localeex",   false },   /* GetLocaleInfoW */
    { "GetTimeFormatEx",                 "localeex",   false },   /* GetTimeFormatW */
    { "GetUserDefaultLocaleName",        "localeex",   false },   /* GetUserDefaultLCID */
    { "IsValidLocaleName",               "localeex",   false },   /* IsValidLocale */
    { "LCMapStringEx",                   "localeex",   false },   /* LCMapStringW */
    { "GetCurrentPackageId",             "package",    false },   /* 8: "not a packaged application" */
    /* Not among the 29 asked for at start-up in any DLL loaded so far; kept from finding 24. */
    { "CreateFile2",                     "file2",      false },   /* 8: CreateFileW */
    /* The four more of the Visual Studio 2013 C runtime (amd_ags_x64.dll only). */
    { "CreateEventExW",                  "eventex",    false },   /* Vista: CreateEventW */
    { "GetTickCount64",                  "tick64",     true  },   /* Vista; 16 of the 17 DLLs import it too */
    { "GetFileInformationByHandleExW",   "fileinfoex", false },   /* never: kernel32 exports the name without the W */
    { "SetFileInformationByHandleW",     "fileinfoex", false },   /* never: the same */
};
#define NPROBES (sizeof probes / sizeof probes[0])

static const RtProbe *find(const RtModule *m, const char *name) {
    if (strcmp(m->lib, "kernel32")) return NULL;
    for (size_t i = 0; i < NPROBES; i++) if (!strcmp(probes[i].name, name)) return &probes[i];
    return NULL;
}

void rt_ldr_probe_prescan(void) {
    for (size_t i = 0; i < NPROBES; i++) {
        for (size_t j = 0; j < i; j++)
            if (!strcmp(probes[i].family, probes[j].family) && probes[i].present != probes[j].present)
                rt_fatal("GetProcAddress policy (runtime/core/loader_probe.c): %s and %s are one family (%s) and "
                         "must be present or absent together", probes[j].name, probes[i].name, probes[i].family);
        /* A present name is a dispatch key: its trap stub, if it needs one, is made now. */
        if (probes[i].present) (void)rt_native_import("kernel32.dll", probes[i].name, 0);
    }
}

int rt_ldr_probe(const RtModule *m, const char *name, GUESTFN *addr) {
    const RtProbe *p = find(m, name);
    if (!p) return 0;
    if (!p->present) return -1;
    *addr = rt_native_import(m->name, name, 0);
    return 1;
}
