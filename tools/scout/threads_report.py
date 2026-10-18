#!/usr/bin/env python3
"""Tables for the thread findings from the scan cache (issue #29).

    .venv/bin/python tools/scout/threads_report.py CACHE_DIR
Prints markdown: call-site counts per synchronisation family and module, plus the per-API
totals for thread control and CPU-count APIs.
"""
import json
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from threads_lib import SOURCE_MODULES, THIRD_PARTY  # noqa: E402

FAMILIES = [
    ("CritSec", "critical_section"), ("SRW", "srw_lock"), ("CondVar", "condition_var"),
    ("Event", "event"), ("Semaphore", "semaphore"), ("Mutex", "mutex"),
    ("Wait*", "wait"), ("Sleep", "sleep"), ("TLS", "tls"), ("SList", "slist"), ("Fiber", "fiber"),
]


def main():
    cache = sys.argv[1]
    data = {}
    for n in SOURCE_MODULES + THIRD_PARTY:
        d = json.load(open(os.path.join(cache, n + ".scan.json")))
        # real call sites only: direct `call [slot]`, `call thunk`; import-thunk stubs (`jmp [slot]` outside
        # a function) are not counted, and `load` forms were never seen.
        sites = [s for s in d["sites"] if s["kind"] in ("call", "call-thunk", "load", "jmp-thunk")
                 or (s["kind"] == "jmp" and s["func"] is not None)]
        data[n] = sites
    hdr = "| Module | " + " | ".join(f for f, _ in FAMILIES) + " |"
    print(hdr)
    print("|" + "---|" * (len(FAMILIES) + 1))
    tot = Counter()
    for group in (SOURCE_MODULES, THIRD_PARTY):
        for n in group:
            c = Counter(s["cat"] for s in data[n])
            row = [str(c.get(cat, 0)) for _, cat in FAMILIES]
            for _, cat in FAMILIES:
                if group is SOURCE_MODULES:
                    tot[cat] += c.get(cat, 0)
            print("| %s | %s |" % (n, " | ".join(row)))
        if group is SOURCE_MODULES:
            print("| **Source subtotal** | %s |" % " | ".join(str(tot[cat]) for _, cat in FAMILIES))
    print()
    api = defaultdict(Counter)
    for n in SOURCE_MODULES:
        for s in data[n]:
            api[s["api"]][n] += 1
    for a in ("WaitForSingleObject", "WaitForSingleObjectEx", "WaitForMultipleObjects", "WaitForMultipleObjectsEx",
              "SleepEx", "Sleep", "SetThreadPriority", "SetThreadAffinityMask", "SetProcessAffinityMask",
              "GetProcessAffinityMask", "GetLogicalProcessorInformation", "GetSystemInfo", "SuspendThread",
              "ResumeThread", "TerminateThread", "ExitThread", "OpenThread", "SetThreadPriorityBoost",
              "GetThreadContext", "SetThreadContext", "CreateThread", "TryEnterCriticalSection",
              "SleepConditionVariableSRW", "WakeAllConditionVariable", "WakeConditionVariable",
              "TlsAlloc", "TlsGetValue", "TlsSetValue", "InterlockedPushEntrySList", "InterlockedPopEntrySList",
              "InterlockedFlushSList", "SwitchToThread", "SetEvent", "CreateEventA", "CreateSemaphoreW",
              "CreateSemaphoreA", "ReleaseSemaphore", "CreateMutexA", "InitializeCriticalSectionAndSpinCount"):
        if api.get(a):
            print("%-40s %4d  %s" % (a, sum(api[a].values()), ", ".join("%s %d" % (k.replace(".dll", ""), v) for k, v in api[a].most_common())))


if __name__ == "__main__":
    main()
