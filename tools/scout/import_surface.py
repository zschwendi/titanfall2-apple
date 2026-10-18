#!/usr/bin/env python3
"""Generate docs/findings/24-import-surface.md: the external imports the runtime must provide.

Lists and counts come from inventory/inventory.json.  The grouping into subsystems, the
marks and the implementation order for tier0 + vstdlib are judgments recorded in the tables
below; the script refuses to run if any import is unclassified or any classified name is
not in the inventory, so the document cannot drift from the data.

With --game, two columns of list (a) are measured from the two DLLs themselves: how many
places reference each import slot, and whether the import is reachable from the DLL entry
point by direct calls (which includes the C runtime start-up and the static constructors).

    .venv/bin/python tools/scout/import_surface.py inventory/inventory.json --game game \
        > docs/findings/24-import-surface.md

Single process, read only.
"""

import argparse
import bisect
import json
import os
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))

EXCLUDED = "d3dcompiler_46.dll"
SET_A = ("tier0.dll", "vstdlib.dll")
STUBBED = {"GFSDK_SSAO.win64.dll", "GFSDK_TXAA.win64.dll"}      # PLAN.md: stubbed, not lifted

# ---- subsystems: code -> (RUNTIME.md section, heading) -----------------------------------

SUBSYS = [
    ("L", "1", "Loader and modules"),
    ("P", "1", "Process lifetime and child processes"),
    ("A", "3", "COM"),
    ("H", "4", "Handles and waits"),
    ("M", "5", "Memory"),
    ("T", "6", "Threads, thread-local storage and locks"),
    ("F", "7", "Files and paths"),
    ("C", "7", "Console and standard handles"),
    ("E", "8", "Time"),
    ("V", "8", "Environment"),
    ("R", "8", "Registry"),
    ("S", "8", "Locale and strings"),
    ("Y", "8", "System information"),
    ("X", "9", "Errors, exceptions and debugging"),
    ("Q", "3", "Pure helpers (no state)"),
    ("W", "11", "Windows and messages (deferred: SDL3)"),
    ("WD", "11", "Display, metrics and session (deferred)"),
    ("I", "11", "Input: keyboard, mouse, cursor, devices, IME (deferred)"),
    ("WC", "11", "Clipboard (deferred)"),
    ("G", "11", "GDI, icons and drawing (deferred)"),
    ("U", "11", "Audio (deferred)"),
    ("N", "11", "Network (deferred)"),
    ("K", "11", "Cryptography (deferred)"),
    ("D", "11", "Direct3D entry (deferred: null device at Gate 3, DXMT at Gate 4)"),
    ("SH", "11", "Shell (deferred)"),
    ("O", "11", "C and C++ runtime DLLs (needed only if OriginSDK.dll is lifted, issue #32)"),
    ("Z", "11", "Other middleware interfaces"),
]
SUBSYS_ORDER = [c for c, _, _ in SUBSYS]
SUBSYS_NAME = {c: (sec, name) for c, sec, name in SUBSYS}

CLASS = {
    "kernel32.dll": {
        "L": """FreeLibrary GetModuleFileNameA GetModuleFileNameW GetModuleHandleA GetModuleHandleExA
                GetModuleHandleExW GetModuleHandleW GetProcAddress LoadLibraryA LoadLibraryExA
                LoadLibraryExW LoadLibraryW RtlPcToFileHeader""",
        "P": """ExitProcess TerminateProcess GetCurrentProcess GetCurrentProcessId GetCommandLineA
                GetStartupInfoA GetStartupInfoW CreateProcessA CreateProcessW GetExitCodeProcess
                OpenProcess""",
        "H": """CloseHandle DuplicateHandle WaitForSingleObject WaitForSingleObjectEx
                WaitForMultipleObjects CreateEventA SetEvent ResetEvent OpenEventA CreateMutexA
                ReleaseMutex CreateSemaphoreA CreateSemaphoreW CreateSemaphoreExW ReleaseSemaphore
                SetHandleCount""",
        "M": """VirtualAlloc VirtualFree VirtualQuery HeapAlloc HeapCreate HeapDestroy HeapFree
                HeapQueryInformation HeapReAlloc HeapSetInformation HeapSize HeapValidate HeapWalk
                GetProcessHeap GlobalAlloc GlobalFree GlobalLock GlobalSize GlobalUnlock LocalAlloc
                LocalFree EncodePointer DecodePointer""",
        "T": """CreateThread ExitThread GetCurrentThread GetCurrentThreadId GetExitCodeThread
                GetThreadContext SetThreadContext GetThreadId GetThreadPriority SetThreadPriority
                SetThreadPriorityBoost SetThreadAffinityMask SetThreadIdealProcessor OpenThread
                ResumeThread SuspendThread TerminateThread QueueUserAPC Sleep SleepEx SwitchToThread
                TlsAlloc TlsFree TlsGetValue TlsSetValue FlsAlloc FlsFree FlsGetValue FlsSetValue
                InitializeCriticalSection InitializeCriticalSectionAndSpinCount
                InitializeCriticalSectionEx EnterCriticalSection TryEnterCriticalSection
                LeaveCriticalSection DeleteCriticalSection InitializeSRWLock AcquireSRWLockExclusive
                AcquireSRWLockShared ReleaseSRWLockExclusive ReleaseSRWLockShared
                InitializeConditionVariable SleepConditionVariableSRW WakeConditionVariable
                WakeAllConditionVariable InitializeSListHead InterlockedFlushSList
                InterlockedPopEntrySList InterlockedPushEntrySList QueryDepthSList SetPriorityClass
                GetProcessAffinityMask SetProcessAffinityMask""",
        "F": """AreFileApisANSI CreateDirectoryA CreateDirectoryW CreateFileA CreateFileW
                CreateFileMappingA OpenFileMappingA MapViewOfFile UnmapViewOfFile DeleteFileA
                DeleteFileW DeviceIoControl FindClose FindFirstFileA FindFirstFileExA
                FindFirstFileExW FindFirstFileW FindNextFileA FindNextFileW FlushFileBuffers
                GetCurrentDirectoryA GetCurrentDirectoryW GetDiskFreeSpaceA GetDiskFreeSpaceExA
                GetDriveTypeW GetFileAttributesA GetFileAttributesExA GetFileAttributesExW
                GetFileInformationByHandle GetFileSize GetFileSizeEx GetFileType GetFullPathNameA
                GetFullPathNameW GetLogicalDriveStringsA GetOverlappedResult GetTempPathA
                GetSystemDirectoryA GetSystemDirectoryW MoveFileA MoveFileExW ReadFile ReadFileEx
                RemoveDirectoryA SetCurrentDirectoryA SetCurrentDirectoryW SetEndOfFile
                SetFileAttributesA SetFileAttributesW SetFilePointer SetFilePointerEx SetFileTime
                WriteFile CreatePipe PeekNamedPipe SetCommTimeouts""",
        "C": """AllocConsole FlushConsoleInputBuffer GetConsoleCP GetConsoleMode
                GetConsoleScreenBufferInfo GetNumberOfConsoleInputEvents GetStdHandle
                PeekConsoleInputA ReadConsoleInputA ReadConsoleW SetConsoleCtrlHandler SetConsoleMode
                SetConsoleTextAttribute SetStdHandle WriteConsoleW""",
        "E": """GetLocalTime GetSystemTime GetSystemTimeAsFileTime GetTickCount GetTickCount64
                GetTimeZoneInformation QueryPerformanceCounter QueryPerformanceFrequency
                FileTimeToDosDateTime DosDateTimeToFileTime FileTimeToLocalFileTime
                FileTimeToSystemTime SystemTimeToFileTime SystemTimeToTzSpecificLocalTime
                GetProcessTimes""",
        "V": """ExpandEnvironmentStringsA FreeEnvironmentStringsA FreeEnvironmentStringsW
                GetEnvironmentStrings GetEnvironmentStringsW GetEnvironmentVariableA
                SetEnvironmentVariableA SetEnvironmentVariableW""",
        "S": """CompareStringW EnumSystemLocalesA EnumSystemLocalesW FormatMessageA GetACP GetCPInfo
                GetDateFormatW GetLocaleInfoA GetLocaleInfoW GetOEMCP GetStringTypeA GetStringTypeW
                GetTimeFormatW GetUserDefaultLCID GetUserDefaultUILanguage IsValidCodePage
                IsValidLocale LCMapStringA LCMapStringW MultiByteToWideChar WideCharToMultiByte
                lstrcmpA lstrcmpiW""",
        "Y": """GetLogicalProcessorInformation GetSystemInfo GetVersion GetVersionExA
                GlobalMemoryStatus GlobalMemoryStatusEx IsProcessorFeaturePresent VerSetConditionMask
                VerifyVersionInfoA VerifyVersionInfoW SetThreadExecutionState""",
        "X": """DebugBreak FatalAppExitA GetLastError SetLastError SetErrorMode IsDebuggerPresent
                OutputDebugStringA OutputDebugStringW RaiseException RtlCaptureContext
                RtlLookupFunctionEntry RtlUnwindEx RtlVirtualUnwind SetUnhandledExceptionFilter
                UnhandledExceptionFilter""",
    },
    "user32.dll": {
        "W": """AdjustWindowRectEx AllowSetForegroundWindow BeginPaint CallNextHookEx CallWindowProcA
                CallWindowProcW ClientToScreen CreateWindowExA CreateWindowExW DefWindowProcA
                DefWindowProcW DestroyWindow DispatchMessageA DispatchMessageW EndPaint EnumWindows
                FindWindowA FlashWindow GetActiveWindow GetClassLongPtrA GetClientRect
                GetDesktopWindow GetDlgItem GetFocus GetForegroundWindow GetMessageA GetMessageTime
                GetShellWindow GetTopWindow GetWindow GetWindowLongA GetWindowLongPtrA
                GetWindowLongPtrW GetWindowRect GetWindowTextA GetWindowThreadProcessId
                InvalidateRect IsIconic IsWindowVisible MessageBoxA MessageBoxW
                MsgWaitForMultipleObjects PeekMessageA PeekMessageW PostMessageA RegisterClassA
                RegisterClassExA RegisterClassExW RegisterWindowMessageA ScreenToClient SendMessageA
                SetActiveWindow SetFocus SetForegroundWindow SetTimer SetWindowLongA
                SetWindowLongPtrA SetWindowLongPtrW SetWindowPos SetWindowTextA SetWindowTextW
                SetWindowsHookExA ShowWindow TranslateAcceleratorA TranslateMessage
                UnhookWindowsHookEx UnregisterClassA UnregisterClassW UpdateWindow ValidateRect
                WindowFromPoint""",
        "WD": """ChangeDisplaySettingsA EnumDisplayDevicesW EnumDisplayMonitors GetSystemMetrics
                 SystemParametersInfoA GetProcessWindowStation GetUserObjectInformationW""",
        "I": """ActivateKeyboardLayout ClipCursor DestroyCursor GetClipCursor GetCursorPos GetKeyState
                GetKeyboardLayout GetKeyboardLayoutList GetRawInputDeviceInfoA GetRawInputDeviceList
                LoadCursorA LoadCursorFromFileA LoadKeyboardLayoutA MapVirtualKeyExA ReleaseCapture
                SetCapture SetCursor SetCursorPos ShowCursor""",
        "WC": "CloseClipboard EmptyClipboard GetClipboardData OpenClipboard SetClipboardData",
        "G": """CopyIcon CreateIconIndirect DestroyIcon DrawIconEx DrawTextA GetDC GetIconInfo
                LoadIconA LoadImageA ReleaseDC""",
        "Q": "IntersectRect SetRect UnionRect wvsprintfA",
    },
    "gdi32.dll": {
        "G": """AddFontMemResourceEx BitBlt CreateBitmap CreateCompatibleBitmap CreateCompatibleDC
                CreateDCW CreateDIBSection CreateFontA CreatePen CreateRectRgn CreateSolidBrush
                DeleteDC DeleteObject ExtEscape ExtTextOutA ExtTextOutW GetBkColor GetDIBits
                GetDeviceCaps GetDeviceGammaRamp GetObjectA GetObjectW GetPixel GetTextColor MoveToEx
                Polygon Polyline Rectangle RemoveFontMemResourceEx RemoveFontResourceA SelectObject
                SetBkColor SetBkMode SetDIBits SetPixel SetRectRgn SetStretchBltMode SetTextAlign
                SetTextColor SetViewportOrgEx StretchBlt""",
    },
    "winmm.dll": {
        "E": "timeBeginPeriod timeEndPeriod timeGetTime",
        "U": """PlaySoundA mixerClose mixerGetControlDetailsA mixerGetDevCapsA mixerGetLineControlsA
                mixerGetLineInfoA mixerGetNumDevs mixerOpen mixerSetControlDetails waveOutClose
                waveOutOpen waveOutPause waveOutPrepareHeader waveOutReset waveOutRestart
                waveOutSetVolume waveOutUnprepareHeader waveOutWrite""",
    },
    "advapi32.dll": {
        "R": """RegCloseKey RegCreateKeyExA RegDeleteValueA RegOpenKeyExA RegOpenKeyExW
                RegQueryValueExA RegQueryValueExW RegSetValueExA""",
        "X": "DeregisterEventSource RegisterEventSourceW ReportEventW",
    },
    "shlwapi.dll": {"R": "SHDeleteKeyA"},
    "ole32.dll": {
        "A": """CoCreateInstance CoInitialize CoInitializeEx CoTaskMemFree CoUninitialize OleInitialize
                RegisterDragDrop ReleaseStgMedium RevokeDragDrop""",
    },
    "shell32.dll": {
        "F": "SHGetFolderPathA SHGetSpecialFolderPathA",
        "SH": "DragQueryFileA SHGetFileInfoA SHGetFileInfoW ShellExecuteA Shell_NotifyIconA",
    },
    "rpcrt4.dll": {"Y": "RpcStringFreeA UuidCreate UuidFromStringA UuidToStringA"},
    "avrt.dll": {"T": "AvRevertMmThreadCharacteristics AvSetMmThreadCharacteristicsW"},
    "psapi.dll": {"P": "EnumProcesses GetModuleBaseNameW"},
    "imm32.dll": {
        "I": """ImmGetCandidateListCountW ImmGetCandidateListW ImmGetCompositionStringW ImmGetContext
                ImmGetConversionStatus ImmGetProperty ImmIsUIMessageA ImmNotifyIME ImmReleaseContext
                ImmSetCandidateWindow ImmSetConversionStatus""",
    },
    "hid.dll": {
        "I": """HidD_FreePreparsedData HidD_GetAttributes HidD_GetHidGuid HidD_GetPreparsedData
                HidP_GetCaps""",
    },
    "setupapi.dll": {
        "I": """SetupDiDestroyDeviceInfoList SetupDiEnumDeviceInterfaces SetupDiGetClassDevsA
                SetupDiGetDeviceInterfaceDetailA""",
    },
    "bcrypt.dll": {
        "K": """BCryptDecrypt BCryptDestroyKey BCryptEncrypt BCryptExportKey BCryptGenRandom
                BCryptGenerateSymmetricKey BCryptGetProperty BCryptOpenAlgorithmProvider
                BCryptSetProperty""",
    },
    "iphlpapi.dll": {"N": "GetAdaptersInfo"},
    "d3d11.dll": {"D": "D3D11CreateDevice"},
    "d3d9.dll": {"Z": "D3DPERF_BeginEvent D3DPERF_EndEvent"},
}
WHOLE_LIB = {"wsock32.dll": "N", "ws2_32.dll": "N", "opengl32.dll": "Z", "avifil32.dll": "Z",
             "gbclient.dll": "Z", "mss64.dll": "Z", "msvcr110.dll": "O", "msvcp110.dll": "O"}

# ---- marks ---------------------------------------------------------------------------------

MARK_LEGEND = [
    ("T", "tool or debug path, judged from the name and the importers"),
    ("U", "looks unused in single-player, judged from the name and the importers"),
    ("O", "imported only by `OriginSDK.dll`: needed only if that DLL is lifted (stop point 2, issue #32)"),
    ("S", "imported only by the NVIDIA `GFSDK_*` DLLs, which PLAN.md stubs instead of lifting"),
    ("N", "delay-loaded from a DLL the install does not contain, so the shipped game cannot reach it"),
]
MANUAL_MARK = {
    "T": {
        "kernel32.dll": """AllocConsole FlushConsoleInputBuffer GetConsoleScreenBufferInfo
                           GetNumberOfConsoleInputEvents PeekConsoleInputA ReadConsoleInputA
                           SetConsoleMode SetConsoleTextAttribute DebugBreak HeapValidate HeapWalk
                           HeapQueryInformation GetThreadContext SetThreadContext SuspendThread
                           TerminateThread SetCommTimeouts GetLogicalDriveStringsA
                           GetDiskFreeSpaceExA""",
        "advapi32.dll": "DeregisterEventSource RegisterEventSourceW ReportEventW",
        "shell32.dll": "DragQueryFileA Shell_NotifyIconA SHGetFileInfoA SHGetFileInfoW",
        "ole32.dll": "OleInitialize RegisterDragDrop RevokeDragDrop ReleaseStgMedium",
        "avifil32.dll": "*",
    },
    "U": {
        "winmm.dll": """mixerClose mixerGetControlDetailsA mixerGetDevCapsA mixerGetLineControlsA
                        mixerGetLineInfoA mixerGetNumDevs mixerOpen mixerSetControlDetails""",
        "gbclient.dll": "*",
    },
}
AUDITION = {"auditionwin64.dll"}        # Miles' live link to its authoring tool

# ---- Winsock ordinals (published assignments; 10-12 differ between the two DLLs) -----------

_COMMON_ORD = {1: "accept", 2: "bind", 3: "closesocket", 4: "connect", 5: "getpeername",
               6: "getsockname", 7: "getsockopt", 8: "htonl", 9: "htons", 13: "listen",
               14: "ntohl", 15: "ntohs", 16: "recv", 17: "recvfrom", 18: "select", 19: "send",
               20: "sendto", 21: "setsockopt", 22: "shutdown", 23: "socket", 51: "gethostbyaddr",
               52: "gethostbyname", 53: "getprotobyname", 54: "getprotobynumber",
               55: "getservbyname", 56: "getservbyport", 57: "gethostname",
               111: "WSAGetLastError", 112: "WSASetLastError", 115: "WSAStartup",
               116: "WSACleanup", 151: "__WSAFDIsSet"}
ORDINALS = {
    "wsock32.dll": {**_COMMON_ORD, 10: "inet_addr", 11: "inet_ntoa", 12: "ioctlsocket"},
    "ws2_32.dll": {**_COMMON_ORD, 10: "ioctlsocket", 11: "inet_addr", 12: "inet_ntoa"},
}

# ---- implementation order for tier0 + vstdlib ------------------------------------------------

WAVES = [
    (1, "C runtime start-up",
     "Every game DLL's entry point runs the statically linked Visual Studio 2012 C runtime "
     "before its own code. Nothing else can be tested until these work.",
     """GetSystemTimeAsFileTime GetCurrentThreadId GetCurrentProcessId QueryPerformanceCounter
        GetTickCount64 GetProcessHeap HeapAlloc HeapFree HeapReAlloc HeapSize EncodePointer
        DecodePointer InitializeCriticalSectionAndSpinCount EnterCriticalSection
        LeaveCriticalSection DeleteCriticalSection TlsAlloc TlsGetValue TlsSetValue TlsFree
        GetLastError SetLastError GetModuleHandleW GetModuleHandleExW GetProcAddress
        GetStartupInfoW GetStdHandle GetFileType GetCommandLineA GetEnvironmentStringsW
        FreeEnvironmentStringsW WideCharToMultiByte MultiByteToWideChar GetModuleFileNameA
        GetModuleFileNameW GetACP GetOEMCP GetCPInfo IsValidCodePage GetStringTypeW LCMapStringW
        IsProcessorFeaturePresent IsDebuggerPresent SetUnhandledExceptionFilter
        UnhandledExceptionFilter Sleep LoadLibraryExW LoadLibraryW ExitProcess TerminateProcess
        GetCurrentProcess"""),
    (2, "Core services used by tier0 and vstdlib themselves",
     "Clocks, machine description, virtual memory, module lookup, the locks embedded in game "
     "structures, debug output.",
     """QueryPerformanceFrequency GetTickCount timeBeginPeriod GetSystemInfo
        GetLogicalProcessorInformation GlobalMemoryStatusEx GetVersionExA VirtualAlloc
        VirtualFree VirtualQuery GetModuleHandleA GetModuleHandleExA LoadLibraryA LoadLibraryExA
        FreeLibrary RtlPcToFileHeader InitializeSRWLock AcquireSRWLockExclusive
        ReleaseSRWLockExclusive InitializeConditionVariable SleepConditionVariableSRW
        WakeConditionVariable TryEnterCriticalSection InitializeSListHead
        InterlockedPushEntrySList InterlockedPopEntrySList InterlockedFlushSList QueryDepthSList
        OutputDebugStringA OutputDebugStringW SetEnvironmentVariableA GetUserDefaultLCID
        GetUserDefaultUILanguage"""),
    (3, "Threads and waitable objects",
     "Needs the handle table and the wait core. tier0's thread classes and the job system "
     "sit on these.",
     """CreateThread GetCurrentThread GetExitCodeThread ResumeThread SetThreadPriority
        GetThreadPriority SetThreadAffinityMask GetProcessAffinityMask SetProcessAffinityMask
        SetPriorityClass CloseHandle DuplicateHandle WaitForSingleObject WaitForMultipleObjects
        CreateEventA SetEvent ResetEvent OpenEventA CreateMutexA ReleaseMutex CreateSemaphoreA
        CreateSemaphoreW ReleaseSemaphore"""),
    (4, "Files and paths",
     "Needs path translation. Mostly reached through the C runtime's own file layer.",
     """CreateFileA CreateFileW ReadFile WriteFile SetFilePointerEx SetEndOfFile FlushFileBuffers
        GetFileAttributesA GetFileAttributesExA SetFileAttributesA FindFirstFileA FindNextFileA
        FindClose CreateDirectoryA RemoveDirectoryA DeleteFileA DeleteFileW MoveFileA
        GetFullPathNameA GetCurrentDirectoryA GetCurrentDirectoryW SetCurrentDirectoryA
        SetCurrentDirectoryW GetDriveTypeW AreFileApisANSI SetStdHandle CreateFileMappingA
        OpenFileMappingA"""),
    (5, "Console, date and locale back ends of the C runtime",
     "Called on demand (a non-default locale, a console handle, time formatting). Most are "
     "answered from the fixed machine description.",
     """GetConsoleCP GetConsoleMode WriteConsoleW ReadConsoleW SetConsoleCtrlHandler AllocConsole
        GetConsoleScreenBufferInfo SetConsoleTextAttribute GetTimeZoneInformation GetDateFormatW
        GetTimeFormatW CompareStringW EnumSystemLocalesW IsValidLocale GetLocaleInfoW
        FormatMessageA"""),
    (6, "Bind now, decide when a caller appears",
     "These bind to the loud trap stub at Gate 2. Each needs a decision (implement, report the "
     "feature absent, or keep trapping) the first time the driver or the engine reaches it.",
     """RaiseException RtlCaptureContext RtlLookupFunctionEntry RtlVirtualUnwind RtlUnwindEx
        FatalAppExitA GetThreadContext SetThreadContext SuspendThread TerminateThread OpenThread
        HeapValidate HeapWalk HeapQueryInformation DeviceIoControl CreateProcessA CreatePipe
        PeekNamedPipe GetExitCodeProcess
        AdjustWindowRectEx CallNextHookEx ClientToScreen CreateWindowExA DefWindowProcA
        EnumWindows FindWindowA GetClientRect GetDlgItem GetShellWindow GetSystemMetrics
        GetWindowRect GetWindowThreadProcessId IsIconic IsWindowVisible MessageBoxA
        RegisterClassExA ScreenToClient SetWindowPos SetWindowTextA SetWindowsHookExA
        UnhookWindowsHookEx"""),
]
NOTES_A = {
    "GetProcAddress": "the C runtime looks up about 30 newer functions by name at start-up; see the last section",
    "HeapAlloc": "process heap only; game allocations go through tier0's own allocator",
    "RaiseException": "thread naming (0x406D1388) and debug-print codes return; a C++ throw aborts with a log",
    "RtlCaptureContext": "most sites are in vstdlib: find out why before choosing (open question)",
    "RtlLookupFunctionEntry": "pure lookup in the mapped unwind table; implement with the unwinder",
    "RtlVirtualUnwind": "pure computation; shared with the runtime's own crash backtrace",
    "RtlUnwindEx": "transfer of control: plan A is abort with a log",
    "RtlPcToFileHeader": "used by dynamic_cast, not only by exceptions",
    "DeviceIoControl": "purpose unknown",
    "TerminateProcess": "tier0's Plat_ExitProcess export tail-jumps here: a normal exit, never a trap",
    "GetConsoleMode": "returns failure: standard handles are character devices that are not consoles",
    "WriteConsoleW": "unreachable while GetConsoleMode fails; report no console",
    "GetSystemMetrics": "pure query; could be answered from the machine description before Gate 4",
    "MessageBoxA": "fatal-error dialogs; log the text, then decide",
    "VirtualAlloc": "4 KB commit granularity on 16 KB host pages; see RUNTIME.md section 5",
    "TlsGetValue": "values live in the guest thread block at the Windows offsets",
    "InitializeCriticalSectionAndSpinCount": "40-byte guest layout; see RUNTIME.md section 6",
    "CreateFileMappingA": "named-object use; neither DLL imports MapViewOfFile",
    "LoadLibraryExW": "the C runtime loads user32, advapi32 and mscoree by name",
    "IsDebuggerPresent": "always FALSE",
    "GetVersionExA": "Windows 7 SP1 (6.1.7601)",
}


ABSENT_CANDIDATES = """VirtualProtect VirtualLock CreateFiber ConvertThreadToFiber SwitchToFiber
    CreateWaitableTimerA CreateWaitableTimerW CreateIoCompletionPort GetQueuedCompletionStatus
    CreateRemoteThread RtlRestoreContext AddVectoredExceptionHandler WaitForMultipleObjectsEx
    GetQueuedCompletionStatusEx CreateTimerQueueTimer GetComputerNameA GetUserNameA""".split()


def words(s):
    return s.split()


# ---- reading the inventory -------------------------------------------------------------------

def load(inv_path):
    inv = json.load(open(inv_path))
    files = [f for f in inv["files"] if f["machine"] == "x86-64"
             and (f["rel"].startswith("bin/x64_retail/") or f["rel"] == "server.dll")]
    game = [f for f in files if f["file"] != EXCLUDED]
    own = {f["file"].lower() for f in files}
    shipped = {os.path.basename(f["rel"]).lower() for f in inv["files"]}
    return inv, game, own, shipped


def external(mods, own):
    """lib -> name -> {'mods': set of importing modules, 'delay': set of those that delay-load}"""
    out = defaultdict(lambda: defaultdict(lambda: {"mods": set(), "delay": set()}))
    for f in mods:
        for kind in ("imports", "delay_imports"):
            for lib, fns in f[kind].items():
                lib = lib.lower()
                if lib in own:
                    continue
                for fn in fns:
                    out[lib][fn]["mods"].add(f["file"])
                    if kind == "delay_imports":
                        out[lib][fn]["delay"].add(f["file"])
    return out


def classify(ext):
    """(lib, name) -> subsystem code.  Exits if the tables and the inventory disagree."""
    sub, problems = {}, []
    for lib, names in ext.items():
        if lib in WHOLE_LIB:
            for n in names:
                sub[(lib, n)] = WHOLE_LIB[lib]
            continue
        table = CLASS.get(lib)
        if table is None:
            problems.append("library %s has no classification" % lib)
            continue
        seen = Counter()
        for code, text in table.items():
            for n in words(text):
                seen[n] += 1
                if n in names:
                    sub[(lib, n)] = code
        for n in names:
            if (lib, n) not in sub:
                problems.append("%s!%s unclassified" % (lib, n))
        for n, c in seen.items():
            if c > 1:
                problems.append("%s!%s classified %d times" % (lib, n, c))
    for lib, table in CLASS.items():
        for code, text in table.items():
            for n in words(text):
                if lib not in ext or n not in ext[lib]:
                    problems.append("%s!%s is classified but the game does not import it" % (lib, n))
    if problems:
        sys.exit("classification does not match inventory.json:\n  " + "\n  ".join(problems))
    return sub


def marks(ext, shipped):
    out = {}
    manual = {}
    for mark, libs in MANUAL_MARK.items():
        for lib, text in libs.items():
            for n in (ext[lib] if text == "*" else words(text)):
                if n not in ext[lib]:
                    sys.exit("mark table names %s!%s, which is not imported" % (lib, n))
                manual[(lib, n)] = mark
    for lib, names in ext.items():
        for n, rec in names.items():
            mods = rec["mods"]
            m = []
            if rec["delay"] == mods and lib not in shipped:
                m.append("N")
            if mods <= STUBBED:
                m.append("S")
            if mods == {"OriginSDK.dll"}:
                m.append("O")
            if mods <= AUDITION and "T" not in m:
                m.append("T")
            if (lib, n) in manual and manual[(lib, n)] not in m:
                m.append(manual[(lib, n)])
            out[(lib, n)] = "".join(m)
    return out


def display(lib, name):
    if name.startswith("#") and lib in ORDINALS:
        o = int(name[1:])
        return "%s (`%s`)" % (name, ORDINALS[lib].get(o, "?"))
    return "`%s`" % name


def sort_key(name):
    return (0, int(name[1:]), "") if name.startswith("#") and name[1:].isdigit() else (1, 0, name.lower())


# ---- optional measurement on the two DLLs ----------------------------------------------------

def measure(game_root):
    """name -> {'sites': n, 'startup': bool} over tier0 + vstdlib, keyed by (lib, name)."""
    from pe_inventory import PE
    import capstone
    from capstone import x86_const as X

    result = defaultdict(lambda: {"sites": 0, "startup": False})
    info = {}
    for mod in SET_A:
        path = os.path.join(game_root, "bin", "x64_retail", mod)
        pe = PE(path)
        slots = {}
        rva, _ = pe.dirs[1]
        off = pe.rva_to_off(rva)
        while off is not None:
            oft, _, _, name, ft = struct.unpack_from("<IIIII", pe.data, off)
            if not (oft or name or ft):
                break
            dll = pe.cstr(name).lower()
            for i, n in enumerate(pe._thunks(oft or ft)):
                slots[ft + 8 * i] = (dll, n)
            off += 20

        text = [s for s in pe.sections if pe.is_exec(s)]

        def in_text(r):
            return any(s["va"] <= r < s["va"] + s["vsize"] for s in text)

        # Import stubs: a bare "jmp [rip+slot]" outside every unwind range.  A direct call or
        # jump to one counts as a reference to the import; the stub itself does not.
        stubs = {}
        for s in text:
            d = pe.data[s["rptr"]:s["rptr"] + min(s["rsize"], s["vsize"])]
            i = d.find(b"\xff\x25")
            while i >= 0:
                (disp,) = struct.unpack_from("<i", d, i + 2)
                target = s["va"] + i + 6 + disp
                if target in slots:
                    stubs[s["va"] + i] = slots[target]
                i = d.find(b"\xff\x25", i + 1)

        # Reachability from the entry point by direct calls and jumps, plus the functions
        # listed in initializer tables (runs of code pointers bounded by two lea targets).
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        md.detail = True
        rva, size = pe.dirs[3]
        off = pe.rva_to_off(rva)
        ranges = sorted(struct.unpack_from("<II", pe.data, off + 12 * i) for i in range(size // 12))
        begins = [b for b, _ in ranges]

        def range_of(r):
            i = bisect.bisect_right(begins, r) - 1
            if i >= 0 and ranges[i][0] <= r < ranges[i][1]:
                return ranges[i]
            return None

        def decode(b, e):
            refs, out, leas = [], set(), set()
            o = pe.rva_to_off(b)
            for ins in md.disasm(pe.data[o:o + (e - b)], b):
                for op in ins.operands:
                    if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                        t = ins.address + ins.size + op.mem.disp
                        if t in slots:
                            refs.append(slots[t])
                        elif ins.id == X.X86_INS_LEA and not in_text(t) and pe.rva_to_off(t) is not None:
                            leas.add(t)
                if ins.id == X.X86_INS_CALL or X.X86_GRP_JUMP in ins.groups:
                    op = ins.operands[0]
                    if op.type == X.X86_OP_IMM and not (b <= op.imm < e):
                        out.add(op.imm)
                        if op.imm in stubs:
                            refs.append(stubs[op.imm])
            leas = sorted(leas)
            for i, a in enumerate(leas):
                for z in leas[i + 1:]:
                    if (z - a) % 8 or z - a > 0x8000:
                        continue
                    ao, ents, ok = pe.rva_to_off(a), [], True
                    for k in range((z - a) // 8):
                        (v,) = struct.unpack_from("<Q", pe.data, ao + 8 * k)
                        if v == 0:
                            continue
                        if not in_text(v - pe.image_base):
                            ok = False
                            break
                        ents.append(v - pe.image_base)
                    if ok:
                        out.update(ents)
            return refs, out

        def decode_leaf(start):
            """Recursive descent for code with no unwind entry (import thunks, tail jumps)."""
            refs, out, seen, work, n = set(), set(), set(), [start], 0
            while work and n < 400:
                pc = work.pop()
                while pc not in seen and in_text(pc) and range_of(pc) is None and n < 400:
                    seen.add(pc)
                    o = pe.rva_to_off(pc)
                    ins = next(md.disasm(pe.data[o:o + 16], pc, 1), None)
                    if ins is None:
                        break
                    n += 1
                    for op in ins.operands:
                        if op.type == X.X86_OP_MEM and op.mem.base == X.X86_REG_RIP:
                            t = ins.address + ins.size + op.mem.disp
                            if t in slots:
                                refs.add(slots[t])
                    if ins.id in (X.X86_INS_RET, X.X86_INS_INT3):
                        break
                    if ins.id == X.X86_INS_CALL or X.X86_GRP_JUMP in ins.groups:
                        op = ins.operands[0]
                        if op.type == X.X86_OP_IMM:
                            if range_of(op.imm) or ins.id == X.X86_INS_CALL:
                                out.add(op.imm)
                            else:
                                work.append(op.imm)
                        if ins.id == X.X86_INS_JMP:
                            break
                    pc = ins.address + ins.size
            return refs, out

        decoded = {r: decode(*r) for r in ranges}
        for refs, _ in decoded.values():
            for imp in refs:
                result[imp]["sites"] += 1

        seen, work, visited = set(), [pe.entry_rva], 0
        while work:
            t = work.pop()
            r = range_of(t)
            key = r if r else ("leaf", t)
            if key in seen or not in_text(t):
                continue
            seen.add(key)
            visited += 1
            refs, out = decoded[r] if r else decode_leaf(t)
            for imp in refs:
                result[imp]["startup"] = True
            work.extend(out)
        info[mod] = (visited, len(ranges))
    return result, info


# ---- names looked up at run time ---------------------------------------------------------------

CRT_PROBE_CANDIDATES = """FlsAlloc FlsFree FlsGetValue FlsSetValue InitializeCriticalSectionEx
    CreateEventExW CreateSemaphoreExW SetThreadStackGuarantee CreateThreadpoolTimer
    SetThreadpoolTimer WaitForThreadpoolTimerCallbacks CloseThreadpoolTimer CreateThreadpoolWait
    SetThreadpoolWait CloseThreadpoolWait FlushProcessWriteBuffers FreeLibraryWhenCallbackReturns
    GetCurrentProcessorNumber GetLogicalProcessorInformation CreateSymbolicLinkW
    SetDefaultDllDirectories EnumSystemLocalesEx CompareStringEx GetDateFormatEx GetLocaleInfoEx
    GetTimeFormatEx GetUserDefaultLocaleName IsValidLocaleName LCMapStringEx GetCurrentPackageId
    GetTickCount64 GetFileInformationByHandleExW SetFileInformationByHandleW CreateFile2""".split()
CRT_MARKER = "GetCurrentPackageId"      # a probe name that no module imports
CRT_ON_DEMAND = [
    ("user32.dll", "MessageBoxW GetActiveWindow GetLastActivePopup GetProcessWindowStation "
                   "GetUserObjectInformationW", "its error dialog"),
    ("advapi32.dll", "SystemFunction036", "`rand_s`"),
    ("mscoree.dll", "CorExitProcess", "process exit"),
]
OTHER_DYNAMIC = [
    ("tier0.dll", "ntdll.dll vtuneapi.dll", "IsWow64Process VerSetConditionMask VerifyVersionInfoA"),
    ("inputsystem.dll", "xinput1_3.dll", "XInputGetCapabilities XInputSetState RegisterRawInputDevices GetRawInputData"),
    ("mileswin64.dll", "xaudio2_7.dll dsound.dll", ""),
    ("bink2w64.dll", "ddraw.dll", ""),
    ("materialsystem_dx11.dll", "nvapi64.dll nvpowerapi.dll crypt32.dll ntdll.dll ole32.dll",
     "RegOpenKeyExW RegEnumValueW SHGetFolderPathW StringFromGUID2 CreateProcessAsUserW "
     "CryptQueryObject CertFindCertificateInStore"),
    ("engine.dll", "dsound.dll netapi32.dll cryptsp.dll rsaenh.dll combase.dll client.dll server.dll "
                   "materialsystem_dx11.dll materialsystem_nodx.dll",
     "CreateToolhelp32Snapshot GetAsyncKeyState GetQueueStatus CryptGenRandom"),
    ("launcher.dll", "engine.dll datacache.dll inputsystem.dll studiorender.dll vgui2.dll "
                     "vguimatsurface.dll vphysics.dll materialsystem_dx11.dll materialsystem_nodx.dll", ""),
    ("amd_ags_x64.dll", "atiadlxx.dll atiadlxy.dll amdxc64.dll atidxx64.dll aticfx64.dll", ""),
]


def dynamic_section(game_root, game, shipped, p):
    """Every name printed here is checked against the DLL's bytes; a missing one stops the run."""
    blobs = {}

    def blob(mod):
        if mod not in blobs:
            sub = "" if mod == "server.dll" else os.path.join("bin", "x64_retail")
            with open(os.path.join(game_root, sub, mod), "rb") as f:
                blobs[mod] = f.read().lower()
        return blobs[mod]

    def has(mod, name):
        n = name.lower().encode()
        b = blob(mod)
        return (n + b"\0") in b or n.decode().encode("utf-16-le") in b

    imported = {f["file"]: {n for k in ("imports", "delay_imports") for fns in f[k].values() for n in fns}
                for f in game}
    crt_mods = sorted(f["file"] for f in game if has(f["file"], CRT_MARKER))
    others = sorted(f["file"] for f in game if f["file"] not in crt_mods)
    probes = [n for n in CRT_PROBE_CANDIDATES
              if all(has(m, n) and n not in imported[m] for m in SET_A)]
    bad = []
    p("Read from the DLLs' strings, not from `inventory.json`; every name in this section was")
    p("checked to be present in the DLL it is listed under. These reach the runtime through")
    p("`GetProcAddress`, `LoadLibrary` and `GetModuleHandle`, so they need no import slot, but the")
    p("answers have to be deliberate (RUNTIME.md section 1: an unknown name returns NULL and is")
    p("logged, it does not trap). Wider listing: `tools/scout/loader_facts.py`.")
    p()
    hint = {f["file"]: f["toolset_hint"] for f in game}
    odd = ["`%s` (%s)" % (m[:-4], hint[m]) for m in crt_mods if hint[m] != "Visual Studio 2012"]
    p("**A static C runtime that looks functions up by name** is in %d of the 24 DLLs (all except" % len(crt_mods))
    p("%s). It is the Visual Studio 2012 one%s." % (
        ", ".join("`%s`" % m[:-4] for m in others),
        (" except in " + ", ".join(odd)) if odd else ""))
    p("At start-up it asks `kernel32.dll` for the %d functions below by name and falls back to an" % len(probes))
    p("older import when the answer is NULL. In tier0 and vstdlib none of them is also imported.")
    p()
    p(", ".join("`%s`" % n for n in probes) + ".")
    p()
    p("On demand it also loads:")
    p()
    for dll, fns, why in CRT_ON_DEMAND:
        for m in SET_A:
            for n in [dll] + fns.split():
                if not has(m, n):
                    bad.append("%s: %s" % (m, n))
        p("- `%s` for %s: %s" % (dll, why, ", ".join("`%s`" % n for n in fns.split())))
    p()
    p("**Other names held as strings and not imported by that DLL:**")
    p()
    p("| DLL | DLL names | Function names |")
    p("|---|---|---|")
    for mod, dlls, fns in OTHER_DYNAMIC:
        for n in dlls.split() + fns.split():
            if not has(mod, n):
                bad.append("%s: %s" % (mod, n))
            if n in imported[mod]:
                bad.append("%s imports %s" % (mod, n))
        p("| %s | %s | %s |" % (mod[:-4], ", ".join("`%s`" % n for n in dlls.split()),
                               ", ".join("`%s`" % n for n in fns.split())))
    if bad:
        sys.exit("dynamic-name table does not match the DLLs:\n  " + "\n  ".join(bad))
    p()
    if "materialsystem_nodx.dll" not in shipped:
        p("`materialsystem_nodx.dll` is named by the launcher and the engine but is not in the install.")


# ---- output ----------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inventory")
    ap.add_argument("--game", help="directory holding bin/x64_retail (adds two measured columns)")
    args = ap.parse_args()

    inv, game, own, shipped = load(args.inventory)
    ext_b = external(game, own)
    ext_a = external([f for f in game if f["file"] in SET_A], own)
    sub = classify(ext_b)
    mk = marks(ext_b, shipped)
    total_b = sum(len(v) for v in ext_b.values())
    total_a = sum(len(v) for v in ext_a.values())

    wave_of = {}
    for num, _, _, text in WAVES:
        for n in words(text):
            if n in wave_of:
                sys.exit("wave table lists %s twice" % n)
            wave_of[n] = num
    names_a = {n for v in ext_a.values() for n in v}
    if set(wave_of) != names_a:
        sys.exit("wave table does not match list (a):\n  missing %s\n  extra %s" % (
            sorted(names_a - set(wave_of)), sorted(set(wave_of) - names_a)))
    if len(names_a) != total_a:
        sys.exit("a name is imported from two libraries in list (a); the wave table cannot key on names")

    measured, info = (measure(args.game) if args.game else (None, None))

    p = print
    p("# Issue 24: the import surface the runtime has to provide")
    p()
    p("Generated by `tools/scout/import_surface.py` from `inventory/inventory.json` (build")
    p("v2.0.11.0). Do not edit by hand: change the tables in the script and regenerate. The")
    p("script stops if any import is unclassified or any classified name is missing from the")
    p("inventory, so the lists and counts below are exactly the inventory's.")
    p()
    p("Design this list is assigned against: `docs/RUNTIME.md`. Section numbers below refer to it.")
    p()
    p("## Summary")
    p()
    p("| List | Modules | Libraries | Distinct external imports |")
    p("|---|---|---|---|")
    p("| (a) `tier0.dll` + `vstdlib.dll` | 2 | %d | **%d** |" % (len(ext_a), total_a))
    p("| (b) the 24 game DLLs, without `d3dcompiler_46.dll` | %d | %d | **%d** |" % (
        len(game), len(ext_b), total_b))
    p()
    internal = set()
    for f in game:
        for kind in ("imports", "delay_imports"):
            for lib, fns in f[kind].items():
                if lib.lower() in own:
                    internal.update((lib.lower(), fn) for fn in fns)
    p("\"External\" means imported from a DLL outside the game's own set (the 24 plus")
    p("`d3dcompiler_46.dll`). Imports between game DLLs (%d more, for example `vstdlib.dll` from" % len(internal))
    p("`tier0.dll`) are bound by the loader to lifted code and need no shim; the ones Gate 2 meets")
    p("are listed at the end of list (a).")
    p()
    by_mark = Counter()
    for key, m in mk.items():
        for ch in m:
            by_mark[ch] += 1
    unmarked = sum(1 for m in mk.values() if not m)
    never = sum(1 for m in mk.values() if set(m) & set("NSO"))
    p("Of the %d in list (b), %d carry no mark. %d can leave the work list without running" % (
        total_b, unmarked, never))
    p("anything: they are delay-loaded from a DLL the install does not contain (N), imported only")
    p("by the stubbed GFSDK DLLs (S), or imported only by `OriginSDK.dll` (O). That leaves **%d**" % (total_b - never))
    p("for as long as `OriginSDK.dll` is not lifted.")
    p()
    p("| Mark | Meaning | Imports |")
    p("|---|---|---|")
    for ch, text in MARK_LEGEND:
        p("| %s | %s | %d |" % (ch, text, by_mark[ch]))
    p()
    p("Marks T and U are judgments from names and importers. Nothing here was observed at run")
    p("time; a trace of the original under Proton (RUNTIME.md, experiment E1) would replace them")
    p("with facts.")
    p()
    p("Two notes on numbers quoted elsewhere:")
    p()
    adv_all = set()
    for f in inv["files"]:
        if f["machine"] == "x86-64" and (f["rel"].startswith("bin/x64_retail/") or f["rel"] == "server.dll"):
            adv_all.update(f["imports"].get("advapi32.dll", []))
    p("- `advapi32.dll` is **%d** functions in the 24 game DLLs, not %d: the other %d are imported" % (
        len(ext_b["advapi32.dll"]), len(adv_all), len(adv_all) - len(ext_b["advapi32.dll"])))
    p("  only by `d3dcompiler_46.dll`.")
    every = {n for v in ext_b.values() for n in v}
    absent = [n for n in ABSENT_CANDIDATES if n not in every]
    p("- Not imported by any of the 24 DLLs: %s." % ", ".join("`%s`" % n for n in absent))
    p()
    p("| Library | (b) imports | Importing modules | (a) imports |")
    p("|---|---|---|---|")
    for lib in sorted(ext_b, key=lambda l: (-len(ext_b[l]), l)):
        mods = set()
        for rec in ext_b[lib].values():
            mods |= rec["mods"]
        p("| %s | %d | %d | %s |" % (lib, len(ext_b[lib]), len(mods),
                                     len(ext_a[lib]) if lib in ext_a else ""))
    p("| **total** | **%d** | | **%d** |" % (total_b, total_a))
    p()

    # ---- list (a) --------------------------------------------------------------------------
    p("## List (a): `tier0.dll` + `vstdlib.dll` (%d imports)" % total_a)
    p()
    p("This is the Gate 2 work list. One row is one unit of work for one worker.")
    p()
    p("### Suggested order")
    p()
    p("Wave 0 is not in the list: the loader, the dispatch table, the thread block, the handle")
    p("table, the virtual-memory map, the shim generator and the test harness (RUNTIME.md")
    p("sections 1-6 and 10) are built and reviewed by the planning session before any worker")
    p("starts.")
    p()
    p("| Wave | What | Imports | Why here |")
    p("|---|---|---|---|")
    for num, title, why, text in WAVES:
        p("| %d | %s | %d | %s |" % (num, title, len(words(text)), why))
    p()
    if measured:
        p("Suggested order inside each wave: imports the entry points can reach first (Start), then")
        p("by number of referencing sites.")
        p()
        for num, title, _, text in WAVES:
            lib_of = {n: lib for lib in ext_a for n in ext_a[lib]}
            order = sorted(words(text), key=lambda n: (not measured[(lib_of[n], n)]["startup"],
                                                       -measured[(lib_of[n], n)]["sites"], n.lower()))
            p("- **Wave %d.** %s." % (num, ", ".join("`%s`" % n for n in order)))
        p()
    p("Families land together, one worker per family, because the C runtime mixes their")
    p("members: the four `Tls*`, the four critical-section calls in wave 1, the SRW lock and")
    p("condition-variable calls, the five `SList` calls, and each A/W pair.")
    p()
    if measured:
        p("Measured columns (from the two DLLs, `--game`): **Sites** is the number of instructions")
        p("that reference the import's slot, counted in the functions that have unwind entries")
        p("(calls to a `jmp [slot]` import stub are counted; references from leaf wrappers without")
        p("unwind entries are not, which is why a few imports the C runtime does use show 0).")
        p("**Start** is \"yes\" when the import is reachable from a DLL entry point by direct calls")
        p("and jumps, following the initializer tables the start-up code walks. That covers the C")
        p("runtime start-up and the static constructors, but it also includes their error and")
        p("shutdown branches, and it misses anything reached only through a function pointer. The")
        p("walk visited %d of %d unwind ranges in tier0 and %d of %d in vstdlib." % (
            info["tier0.dll"][0], info["tier0.dll"][1], info["vstdlib.dll"][0], info["vstdlib.dll"][1]))
        p()
        early = sorted((n for lib in ext_a for n in ext_a[lib]
                        if measured[(lib, n)]["startup"] and wave_of[n] > 1),
                       key=lambda n: (wave_of[n], n.lower()))
        p("Start is the best static predictor of what the two entry points need. %d imports outside" % len(early))
        p("wave 1 have it; pull these forward inside their wave, because a static constructor may")
        p("reach them before `DllMain` returns: %s." % ", ".join("`%s` (%d)" % (n, wave_of[n]) for n in early))
        p()
    for lib in sorted(ext_a, key=lambda l: (-len(ext_a[l]), l)):
        p("### %s (%d)" % (lib, len(ext_a[lib])))
        p()
        groups = defaultdict(list)
        for n in ext_a[lib]:
            groups[sub[(lib, n)]].append(n)
        for code in SUBSYS_ORDER:
            if code not in groups:
                continue
            sec, title = SUBSYS_NAME[code]
            p("**%s** (section %s, %d)" % (title, sec, len(groups[code])))
            p()
            if measured:
                p("| Function | tier0 | vstdlib | Sites | Start | Wave | Mark | Note |")
                p("|---|---|---|---|---|---|---|---|")
            else:
                p("| Function | tier0 | vstdlib | Wave | Mark | Note |")
                p("|---|---|---|---|---|---|")
            for n in sorted(groups[code], key=lambda x: (wave_of[x], sort_key(x))):
                mods = ext_a[lib][n]["mods"]
                cols = [display(lib, n), "x" if "tier0.dll" in mods else "", "x" if "vstdlib.dll" in mods else ""]
                if measured:
                    m = measured[(lib, n)]
                    cols += [str(m["sites"]), "yes" if m["startup"] else ""]
                cols += [str(wave_of[n]), mk[(lib, n)], NOTES_A.get(n, "")]
                p("| " + " | ".join(cols) + " |")
            p()
    p("### Imports from other game DLLs that Gate 2 meets")
    p()
    p("Not external, so not counted above, but the loader has to put something in the slots.")
    p()
    p("| Importer | From | Count | At Gate 2 |")
    p("|---|---|---|---|")
    for f in game:
        if f["file"] not in SET_A:
            continue
        for lib, fns in f["imports"].items():
            if lib.lower() in own:
                what = ("bound to lifted tier0 exports (functions and data)" if lib.lower() == "tier0.dll"
                        else "%s: bound to named trap stubs until issue #32 is decided" % ", ".join(
                            "`%s`" % n for n in sorted(fns)))
                p("| %s | %s | %d | %s |" % (f["file"], lib.lower(), len(fns), what))
    p()

    # ---- list (b) --------------------------------------------------------------------------
    p("## List (b): all 24 game DLLs (%d imports)" % total_b)
    p()
    p("**Mods** is the number of the 24 DLLs that import the function; importers are named when")
    p("there are three or fewer. Rows in list (a) are repeated here so that this list is complete;")
    p("they are flagged in the **(a)** column.")
    p()
    for lib in sorted(ext_b, key=lambda l: (-len(ext_b[l]), l)):
        mods_all = set()
        for rec in ext_b[lib].values():
            mods_all |= rec["mods"]
        p("### %s (%d)" % (lib, len(ext_b[lib])))
        p()
        if len(mods_all) <= 3:
            p("Imported only by %s." % ", ".join("`%s`" % m for m in sorted(mods_all)))
            p()
        if lib in ORDINALS:
            p("Ordinal imports are shown with the name from the published Winsock ordinal assignments")
            p("(written from memory of those tables: check each against a real export table when the")
            p("shim is written). Ordinals 10, 11 and 12 mean different functions in `wsock32.dll` and")
            p("`ws2_32.dll`.")
            p()
        groups = defaultdict(list)
        for n in ext_b[lib]:
            groups[sub[(lib, n)]].append(n)
        for code in SUBSYS_ORDER:
            if code not in groups:
                continue
            sec, title = SUBSYS_NAME[code]
            p("**%s** (section %s, %d)" % (title, sec, len(groups[code])))
            p()
            p("| Function | Mods | Importers | (a) | Mark |")
            p("|---|---|---|---|---|")
            for n in sorted(groups[code], key=sort_key):
                rec = ext_b[lib][n]
                who = ", ".join(sorted(m[:-4] for m in rec["mods"])) if len(rec["mods"]) <= 3 else ""
                if rec["delay"]:
                    who += " (delay-load)"
                p("| %s | %d | %s | %s | %s |" % (
                    display(lib, n), len(rec["mods"]), who,
                    "a" if lib in ext_a and n in ext_a[lib] else "", mk[(lib, n)]))
            p()

    # ---- cross-check table -------------------------------------------------------------------
    p("## Count by subsystem")
    p()
    p("| Section | Subsystem | (a) | (b) |")
    p("|---|---|---|---|")
    ca, cb = Counter(), Counter()
    for (lib, n), code in sub.items():
        cb[code] += 1
        if lib in ext_a and n in ext_a[lib]:
            ca[code] += 1
    for code in SUBSYS_ORDER:
        if cb[code]:
            sec, title = SUBSYS_NAME[code]
            p("| %s | %s | %s | %d |" % (sec, title, ca[code] or "", cb[code]))
    p("| | **total** | **%d** | **%d** |" % (sum(ca.values()), sum(cb.values())))
    p()
    p("## Names resolved at run time, which no import table shows")
    p()
    if not args.game:
        p("Run with `--game` to produce this section (it is read from the DLLs' strings).")
        return
    dynamic_section(args.game, game, shipped, p)


if __name__ == "__main__":
    main()
