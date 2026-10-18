/* What the guest is told about the machine (sysinfoapi.h, winnt.h, timezoneapi.h):
 * SYSTEM_INFO, filled by rt_machine_system_info (rt/machine.h); OSVERSIONINFOA/EXA,
 * MEMORYSTATUSEX and SYSTEM_LOGICAL_PROCESSOR_INFORMATION, which shims fill from rt_machine();
 * TIME_ZONE_INFORMATION (rt_time_zone_bias_minutes, rt/time.h).
 * docs/RUNTIME.md sections 5.1, 8.1 and 8.5.  Planner-owned.
 */
#ifndef RT_WIN_SYSINFO_H
#define RT_WIN_SYSINFO_H

#include "rt/wintypes.h"
#include "rt/win/base.h"            /* SYSTEMTIME */

#define PROCESSOR_ARCHITECTURE_AMD64  9
#define PROCESSOR_AMD_X8664           8664u

typedef struct {
    union {
        DWORD dwOemId;
        struct { WORD wProcessorArchitecture; WORD wReserved; };
    };
    DWORD     dwPageSize;
    LPVOID    lpMinimumApplicationAddress;
    LPVOID    lpMaximumApplicationAddress;
    DWORD_PTR dwActiveProcessorMask;
    DWORD     dwNumberOfProcessors;
    DWORD     dwProcessorType;
    DWORD     dwAllocationGranularity;
    WORD      wProcessorLevel;
    WORD      wProcessorRevision;
} SYSTEM_INFO;
typedef SYSTEM_INFO *LPSYSTEM_INFO;

_Static_assert(sizeof(SYSTEM_INFO) == 48, "SYSTEM_INFO");
_Static_assert(offsetof(SYSTEM_INFO, dwPageSize) == 4 &&
               offsetof(SYSTEM_INFO, lpMinimumApplicationAddress) == 8 &&
               offsetof(SYSTEM_INFO, lpMaximumApplicationAddress) == 16 &&
               offsetof(SYSTEM_INFO, dwActiveProcessorMask) == 24 &&
               offsetof(SYSTEM_INFO, dwNumberOfProcessors) == 32 &&
               offsetof(SYSTEM_INFO, dwProcessorType) == 36 &&
               offsetof(SYSTEM_INFO, dwAllocationGranularity) == 40 &&
               offsetof(SYSTEM_INFO, wProcessorLevel) == 44 &&
               offsetof(SYSTEM_INFO, wProcessorRevision) == 46, "SYSTEM_INFO offsets");

/* ---- GetVersionExA (winnt.h) -------------------------------------------------------------- */
/* The caller sets dwOSVersionInfoSize to the size of the structure it passes: 148 for
 * OSVERSIONINFOA, 156 for OSVERSIONINFOEXA, which begins with the same six members. */

#define VER_PLATFORM_WIN32_NT   2u
#define VER_NT_WORKSTATION      1        /* wProductType */
#define VER_SUITE_SINGLEUSERTS  0x0100   /* wSuiteMask: one interactive remote session (every desktop Windows) */

typedef struct {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR  szCSDVersion[128];
} OSVERSIONINFOA;
typedef OSVERSIONINFOA *POSVERSIONINFOA, *LPOSVERSIONINFOA;

typedef struct {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR  szCSDVersion[128];
    WORD  wServicePackMajor;
    WORD  wServicePackMinor;
    WORD  wSuiteMask;
    BYTE  wProductType;
    BYTE  wReserved;
} OSVERSIONINFOEXA;
typedef OSVERSIONINFOEXA *POSVERSIONINFOEXA, *LPOSVERSIONINFOEXA;

/* ---- VerifyVersionInfoA, VerSetConditionMask (winbase.h, winnt.h) --------------------------- */
/* dwTypeMask / TypeMask: the members of OSVERSIONINFOEXA to test. */
#define VER_MINORVERSION        0x00000001u
#define VER_MAJORVERSION        0x00000002u
#define VER_BUILDNUMBER         0x00000004u
#define VER_PLATFORMID          0x00000008u
#define VER_SERVICEPACKMINOR    0x00000010u
#define VER_SERVICEPACKMAJOR    0x00000020u
#define VER_SUITENAME           0x00000040u
#define VER_PRODUCT_TYPE        0x00000080u
/* Condition: the comparison operator of one member (VER_AND and VER_OR: VER_SUITENAME only). */
#define VER_EQUAL               1
#define VER_GREATER             2
#define VER_GREATER_EQUAL       3
#define VER_LESS                4
#define VER_LESS_EQUAL          5
#define VER_AND                 6
#define VER_OR                  7

_Static_assert(sizeof(OSVERSIONINFOA) == 148, "OSVERSIONINFOA");
_Static_assert(offsetof(OSVERSIONINFOA, szCSDVersion) == 20, "OSVERSIONINFOA offsets");
_Static_assert(sizeof(OSVERSIONINFOEXA) == 156, "OSVERSIONINFOEXA");
_Static_assert(offsetof(OSVERSIONINFOEXA, szCSDVersion) == 20 &&
               offsetof(OSVERSIONINFOEXA, wServicePackMajor) == 148 &&
               offsetof(OSVERSIONINFOEXA, wServicePackMinor) == 150 &&
               offsetof(OSVERSIONINFOEXA, wSuiteMask) == 152 &&
               offsetof(OSVERSIONINFOEXA, wProductType) == 154 &&
               offsetof(OSVERSIONINFOEXA, wReserved) == 155, "OSVERSIONINFOEXA offsets");

/* ---- GlobalMemoryStatusEx (sysinfoapi.h) -------------------------------------------------- */

typedef struct {
    DWORD     dwLength;                 /* set by the caller: 64 */
    DWORD     dwMemoryLoad;             /* 0 to 100: per cent of physical memory in use */
    DWORDLONG ullTotalPhys;
    DWORDLONG ullAvailPhys;
    DWORDLONG ullTotalPageFile;         /* the commit limit */
    DWORDLONG ullAvailPageFile;         /* what this process can still commit */
    DWORDLONG ullTotalVirtual;          /* the user-mode address space */
    DWORDLONG ullAvailVirtual;
    DWORDLONG ullAvailExtendedVirtual;  /* reserved: always 0 */
} MEMORYSTATUSEX;
typedef MEMORYSTATUSEX *LPMEMORYSTATUSEX;

_Static_assert(sizeof(MEMORYSTATUSEX) == 64, "MEMORYSTATUSEX");
_Static_assert(offsetof(MEMORYSTATUSEX, ullTotalPhys) == 8 &&
               offsetof(MEMORYSTATUSEX, ullTotalVirtual) == 40 &&
               offsetof(MEMORYSTATUSEX, ullAvailExtendedVirtual) == 56, "MEMORYSTATUSEX offsets");

/* ---- GetLogicalProcessorInformation (winnt.h) ---------------------------------------------- */

typedef INT LOGICAL_PROCESSOR_RELATIONSHIP;     /* an enum on Windows: 4 bytes */
#define RelationProcessorCore     0
#define RelationNumaNode          1
#define RelationCache             2
#define RelationProcessorPackage  3
#define RelationGroup             4

typedef INT PROCESSOR_CACHE_TYPE;               /* an enum on Windows: 4 bytes */
#define CacheUnified      0
#define CacheInstruction  1
#define CacheData         2
#define CacheTrace        3

typedef struct {
    BYTE  Level;
    BYTE  Associativity;
    WORD  LineSize;
    DWORD Size;
    PROCESSOR_CACHE_TYPE Type;
} CACHE_DESCRIPTOR;

typedef struct {
    ULONG_PTR ProcessorMask;
    LOGICAL_PROCESSOR_RELATIONSHIP Relationship;
    union {
        struct { BYTE Flags; } ProcessorCore;   /* 1: the processors of the mask share a core (SMT) */
        struct { DWORD NodeNumber; } NumaNode;
        CACHE_DESCRIPTOR Cache;
        ULONGLONG Reserved[2];
    };
} SYSTEM_LOGICAL_PROCESSOR_INFORMATION;
typedef SYSTEM_LOGICAL_PROCESSOR_INFORMATION *PSYSTEM_LOGICAL_PROCESSOR_INFORMATION;

_Static_assert(sizeof(CACHE_DESCRIPTOR) == 12, "CACHE_DESCRIPTOR");
_Static_assert(sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) == 32, "SYSTEM_LOGICAL_PROCESSOR_INFORMATION");
_Static_assert(offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION, Relationship) == 8 &&
               offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION, ProcessorCore) == 16 &&
               offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION, NumaNode) == 16 &&
               offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION, Cache) == 16 &&
               offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION, Reserved) == 16,
               "SYSTEM_LOGICAL_PROCESSOR_INFORMATION offsets");

/* ---- GetTimeZoneInformation (timezoneapi.h) ------------------------------------------------ */

#define TIME_ZONE_ID_UNKNOWN   0u          /* the zone has no daylight-saving rule */
#define TIME_ZONE_ID_STANDARD  1u
#define TIME_ZONE_ID_DAYLIGHT  2u
#define TIME_ZONE_ID_INVALID   0xFFFFFFFFu

typedef struct {
    LONG       Bias;                /* minutes: UTC = local time + Bias */
    WCHAR      StandardName[32];
    SYSTEMTIME StandardDate;        /* wMonth 0: no transition */
    LONG       StandardBias;
    WCHAR      DaylightName[32];
    SYSTEMTIME DaylightDate;
    LONG       DaylightBias;
} TIME_ZONE_INFORMATION;
typedef TIME_ZONE_INFORMATION *PTIME_ZONE_INFORMATION, *LPTIME_ZONE_INFORMATION;

_Static_assert(sizeof(TIME_ZONE_INFORMATION) == 172, "TIME_ZONE_INFORMATION");
_Static_assert(offsetof(TIME_ZONE_INFORMATION, StandardName) == 4 &&
               offsetof(TIME_ZONE_INFORMATION, StandardDate) == 68 &&
               offsetof(TIME_ZONE_INFORMATION, StandardBias) == 84 &&
               offsetof(TIME_ZONE_INFORMATION, DaylightName) == 88 &&
               offsetof(TIME_ZONE_INFORMATION, DaylightDate) == 152 &&
               offsetof(TIME_ZONE_INFORMATION, DaylightBias) == 168, "TIME_ZONE_INFORMATION offsets");

/* ---- OSVERSIONINFOEXW (winnt.h), for VerifyVersionInfoW ------------------------------------ */
/* The members of OSVERSIONINFOEXA with szCSDVersion in UTF-16: 284 bytes. */
typedef struct {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    WCHAR szCSDVersion[128];
    WORD  wServicePackMajor;
    WORD  wServicePackMinor;
    WORD  wSuiteMask;
    BYTE  wProductType;
    BYTE  wReserved;
} OSVERSIONINFOEXW;
typedef OSVERSIONINFOEXW *POSVERSIONINFOEXW, *LPOSVERSIONINFOEXW;

_Static_assert(sizeof(OSVERSIONINFOEXW) == 284, "OSVERSIONINFOEXW");
_Static_assert(offsetof(OSVERSIONINFOEXW, szCSDVersion) == 20 &&
               offsetof(OSVERSIONINFOEXW, wServicePackMajor) == 276 &&
               offsetof(OSVERSIONINFOEXW, wSuiteMask) == 280 &&
               offsetof(OSVERSIONINFOEXW, wProductType) == 282, "OSVERSIONINFOEXW offsets");

/* ---- MEMORYSTATUS (winbase.h), for GlobalMemoryStatus ---------------------------------------- */
/* The older form of MEMORYSTATUSEX: the six sizes are SIZE_T, 8 bytes on x64, so 56 bytes. */
typedef struct {
    DWORD  dwLength;
    DWORD  dwMemoryLoad;
    SIZE_T dwTotalPhys;
    SIZE_T dwAvailPhys;
    SIZE_T dwTotalPageFile;
    SIZE_T dwAvailPageFile;
    SIZE_T dwTotalVirtual;
    SIZE_T dwAvailVirtual;
} MEMORYSTATUS;
typedef MEMORYSTATUS *LPMEMORYSTATUS;

_Static_assert(sizeof(MEMORYSTATUS) == 56, "MEMORYSTATUS");
_Static_assert(offsetof(MEMORYSTATUS, dwTotalPhys) == 8 && offsetof(MEMORYSTATUS, dwAvailVirtual) == 48,
               "MEMORYSTATUS offsets");

#endif /* RT_WIN_SYSINFO_H */
