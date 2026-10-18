#!/usr/bin/env python3
"""Where the game calls thread and synchronisation APIs (issue #29).

For every module: find the import slots of Win32 thread/sync functions, find every code
site that calls or loads them, and attach the enclosing function. Writes one JSON file per
module into the cache directory and prints a per-module table.

    .venv/bin/python tools/scout/threads_scan.py CACHE_DIR [module ...]

Runs up to 4 worker processes. Run it from a file, never from stdin.
"""

import json
import multiprocessing as mp
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import Module, all_module_names  # noqa: E402

CATEGORIES = {
    "thread_create": ["CreateThread", "CreateRemoteThread", "_beginthread", "_beginthreadex",
                      "RtlCreateUserThread", "QueueUserWorkItem", "CreateThreadpool",
                      "CreateThreadpoolWork", "SubmitThreadpoolWork", "CreateTimerQueueTimer",
                      "RegisterWaitForSingleObject", "timeSetEvent", "SetTimer", "QueueUserAPC",
                      "CreateIoCompletionPort", "GetQueuedCompletionStatus", "CreateTimerQueue"],
    "thread_exit_ctl": ["ExitThread", "TerminateThread", "SuspendThread", "ResumeThread",
                        "GetThreadContext", "SetThreadContext", "OpenThread", "GetExitCodeThread",
                        "SwitchToThread", "GetThreadPriority"],
    "affinity_priority": ["SetThreadAffinityMask", "SetProcessAffinityMask", "GetProcessAffinityMask",
                          "SetThreadPriority", "SetThreadPriorityBoost", "SetThreadIdealProcessor",
                          "SetPriorityClass", "SetThreadExecutionState", "AvSetMmThreadCharacteristicsA",
                          "AvSetMmThreadCharacteristicsW", "SetThreadGroupAffinity"],
    "cpu_count": ["GetLogicalProcessorInformation", "GetSystemInfo", "GetNativeSystemInfo",
                  "GetLogicalProcessorInformationEx", "GetActiveProcessorCount"],
    "critical_section": ["InitializeCriticalSection", "InitializeCriticalSectionAndSpinCount",
                         "InitializeCriticalSectionEx", "EnterCriticalSection", "LeaveCriticalSection",
                         "TryEnterCriticalSection", "DeleteCriticalSection", "SetCriticalSectionSpinCount"],
    "srw_lock": ["InitializeSRWLock", "AcquireSRWLockExclusive", "AcquireSRWLockShared",
                 "ReleaseSRWLockExclusive", "ReleaseSRWLockShared", "TryAcquireSRWLockExclusive",
                 "TryAcquireSRWLockShared"],
    "condition_var": ["InitializeConditionVariable", "SleepConditionVariableSRW",
                      "SleepConditionVariableCS", "WakeConditionVariable", "WakeAllConditionVariable"],
    "event": ["CreateEventA", "CreateEventW", "CreateEventExA", "CreateEventExW", "SetEvent",
              "ResetEvent", "PulseEvent", "OpenEventA", "OpenEventW"],
    "semaphore": ["CreateSemaphoreA", "CreateSemaphoreW", "CreateSemaphoreExW", "ReleaseSemaphore",
                  "OpenSemaphoreA", "OpenSemaphoreW"],
    "mutex": ["CreateMutexA", "CreateMutexW", "CreateMutexExW", "ReleaseMutex", "OpenMutexA", "OpenMutexW"],
    "wait": ["WaitForSingleObject", "WaitForSingleObjectEx", "WaitForMultipleObjects",
             "WaitForMultipleObjectsEx", "MsgWaitForMultipleObjects", "MsgWaitForMultipleObjectsEx",
             "SignalObjectAndWait"],
    "sleep": ["Sleep", "SleepEx", "SwitchToThread"],
    "tls": ["TlsAlloc", "TlsFree", "TlsGetValue", "TlsSetValue", "FlsAlloc", "FlsFree", "FlsGetValue",
            "FlsSetValue"],
    "slist": ["InitializeSListHead", "InterlockedPushEntrySList", "InterlockedPopEntrySList",
              "InterlockedFlushSList", "QueryDepthSList"],
    "fiber": ["CreateFiber", "CreateFiberEx", "ConvertThreadToFiber", "ConvertThreadToFiberEx",
              "SwitchToFiber", "DeleteFiber", "ConvertFiberToThread", "GetCurrentFiber"],
    "interlocked_api": ["InterlockedIncrement", "InterlockedDecrement", "InterlockedExchange",
                        "InterlockedCompareExchange", "InterlockedExchangeAdd"],
    "thread_ids": ["GetCurrentThreadId", "GetCurrentThread", "GetCurrentProcessorNumber"],
    "name_exc": ["RaiseException"],
}
API_TO_CAT = {}
for cat, names in CATEGORIES.items():
    for n in names:
        API_TO_CAT[n] = cat


def scan_module(args):
    name, cache = args
    m = Module(name)
    slots = m.import_slots()
    wanted = {}   # slot -> (dll, api, category)
    for slot, (dll, api) in slots.items():
        if api in API_TO_CAT:
            wanted[slot] = (dll, api, API_TO_CAT[api])
    refs = m.iat_refs(wanted.keys())
    sites = []
    thunks = {}   # thunk rva -> slot, for `jmp [slot]` stubs that sit outside any function
    for site, kind, slot in refs:
        dll, api, cat = wanted[slot]
        f = m.func_of(site)
        sites.append({"site": site, "kind": kind, "api": api, "dll": dll, "cat": cat, "func": f})
        if kind == "jmp" and f is None:
            thunks[site] = slot
    for site, kind, tgt in m.rel_refs(thunks.keys()):
        dll, api, cat = wanted[thunks[tgt]]
        f = m.func_of(site)
        sites.append({"site": site, "kind": kind + "-thunk", "api": api, "dll": dll, "cat": cat,
                      "func": f, "thunk": tgt})
    # who imports from tier0/vstdlib: record imports by name too (needed for the export scan)
    out = {"module": name, "base": m.base, "funcs": m.n_funcs(),
           "imports_found": {("%s!%s" % (d, a)): s for s, (d, a, c) in wanted.items()},
           "sites": sites}
    with open(os.path.join(cache, name + ".scan.json"), "w") as f:
        json.dump(out, f)
    return name, len(sites)


def main():
    cache = sys.argv[1]
    os.makedirs(cache, exist_ok=True)
    names = sys.argv[2:] or all_module_names()
    with mp.Pool(4) as pool:
        for name, n in pool.imap_unordered(scan_module, [(n, cache) for n in names]):
            print("scanned %-28s %d sites" % (name, n), flush=True)


if __name__ == "__main__":
    main()
