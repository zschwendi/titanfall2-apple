/* Process creation: the structures of winbase.h that describe how a process was started.
 * Guest-visible.  GetStartupInfoW is over rt_env_startup_info (rt/env.h, which includes
 * this file).
 */
#ifndef RT_WIN_PROCESS_H
#define RT_WIN_PROCESS_H

#include <stddef.h>

#include "rt/wintypes.h"

/* STARTUPINFOW: 104 bytes on x64. */
typedef struct {
    DWORD  cb;
    LPWSTR lpReserved;
    LPWSTR lpDesktop;
    LPWSTR lpTitle;
    DWORD  dwX;
    DWORD  dwY;
    DWORD  dwXSize;
    DWORD  dwYSize;
    DWORD  dwXCountChars;
    DWORD  dwYCountChars;
    DWORD  dwFillAttribute;
    DWORD  dwFlags;
    WORD   wShowWindow;
    WORD   cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput;
    HANDLE hStdOutput;
    HANDLE hStdError;
} STARTUPINFOW;
typedef STARTUPINFOW *LPSTARTUPINFOW;

_Static_assert(sizeof(STARTUPINFOW) == 104, "STARTUPINFOW");
_Static_assert(offsetof(STARTUPINFOW, dwX) == 32 && offsetof(STARTUPINFOW, dwFlags) == 60 &&
               offsetof(STARTUPINFOW, wShowWindow) == 64 && offsetof(STARTUPINFOW, lpReserved2) == 72 &&
               offsetof(STARTUPINFOW, hStdInput) == 80 && offsetof(STARTUPINFOW, hStdError) == 96,
               "STARTUPINFOW offsets");

/* ---- threads and processes: constants of winbase.h and winnt.h ------------------------------ */

/* dwCreationFlags of CreateThread.  rt_thread_create (rt/thread.h) handles these two and
 * stops on any other bit. */
#define CREATE_SUSPENDED                   0x00000004u
#define STACK_SIZE_PARAM_IS_A_RESERVATION  0x00010000u

/* GetExitCodeThread and GetExitCodeProcess while the thread or process has not ended. */
#define STILL_ACTIVE                       259u

/* TlsAlloc and FlsAlloc when no index is left. */
#define TLS_OUT_OF_INDEXES                 0xFFFFFFFFu
#define FLS_OUT_OF_INDEXES                 0xFFFFFFFFu

/* nPriority of SetThreadPriority, and what GetThreadPriority returns on failure. */
#define THREAD_PRIORITY_IDLE               (-15)
#define THREAD_PRIORITY_LOWEST             (-2)
#define THREAD_PRIORITY_BELOW_NORMAL       (-1)
#define THREAD_PRIORITY_NORMAL             0
#define THREAD_PRIORITY_ABOVE_NORMAL       1
#define THREAD_PRIORITY_HIGHEST            2
#define THREAD_PRIORITY_TIME_CRITICAL      15
#define THREAD_PRIORITY_ERROR_RETURN       0x7FFFFFFF

/* dwPriorityClass of SetPriorityClass. */
#define NORMAL_PRIORITY_CLASS              0x00000020u
#define IDLE_PRIORITY_CLASS                0x00000040u
#define HIGH_PRIORITY_CLASS                0x00000080u
#define REALTIME_PRIORITY_CLASS            0x00000100u
#define BELOW_NORMAL_PRIORITY_CLASS        0x00004000u
#define ABOVE_NORMAL_PRIORITY_CLASS        0x00008000u
#define PROCESS_MODE_BACKGROUND_BEGIN      0x00100000u
#define PROCESS_MODE_BACKGROUND_END        0x00200000u

/* ---- STARTUPINFOA (GetStartupInfoA, CreateProcessA): STARTUPINFOW with CHAR strings -------- */
typedef struct {
    DWORD  cb;
    LPSTR  lpReserved;
    LPSTR  lpDesktop;
    LPSTR  lpTitle;
    DWORD  dwX, dwY, dwXSize, dwYSize;
    DWORD  dwXCountChars, dwYCountChars;
    DWORD  dwFillAttribute;
    DWORD  dwFlags;
    WORD   wShowWindow;
    WORD   cbReserved2;
    LPBYTE lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOA;
typedef STARTUPINFOA *LPSTARTUPINFOA;

_Static_assert(sizeof(STARTUPINFOA) == 104, "STARTUPINFOA");
_Static_assert(offsetof(STARTUPINFOA, dwX) == 32 && offsetof(STARTUPINFOA, dwFlags) == 60 &&
               offsetof(STARTUPINFOA, wShowWindow) == 64 && offsetof(STARTUPINFOA, lpReserved2) == 72 &&
               offsetof(STARTUPINFOA, hStdInput) == 80 && offsetof(STARTUPINFOA, hStdError) == 96,
               "STARTUPINFOA offsets");

/* PROCESS_INFORMATION (CreateProcess): 24 bytes.  No child process is ever made (section 7.3),
 * so nothing fills one. */
typedef struct {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD  dwProcessId;
    DWORD  dwThreadId;
} PROCESS_INFORMATION;
typedef PROCESS_INFORMATION *PPROCESS_INFORMATION, *LPPROCESS_INFORMATION;

_Static_assert(sizeof(PROCESS_INFORMATION) == 24, "PROCESS_INFORMATION");

/* SetThreadExecutionState (winbase.h; the values are on the function's documentation page). */
typedef DWORD EXECUTION_STATE;
#define ES_SYSTEM_REQUIRED                 0x00000001u
#define ES_DISPLAY_REQUIRED                0x00000002u
#define ES_USER_PRESENT                    0x00000004u
#define ES_AWAYMODE_REQUIRED               0x00000040u
#define ES_CONTINUOUS                      0x80000000u

/* uMode of SetErrorMode (winbase.h; checked against Wine's include/winbase.h). */
#define SEM_FAILCRITICALERRORS             0x0001u
#define SEM_NOGPFAULTERRORBOX              0x0002u
#define SEM_NOALIGNMENTFAULTEXCEPT         0x0004u
#define SEM_NOOPENFILEERRORBOX             0x8000u

/* CONTEXT, the x64 register record of GetThreadContext and SetThreadContext, with PCONTEXT
 * and LPCONTEXT, is in rt/win/exception.h.  Both functions are stops (docs/RUNTIME.md
 * section 6.3), so nothing here reads a field of it. */
#include "rt/win/exception.h"

#endif /* RT_WIN_PROCESS_H */
