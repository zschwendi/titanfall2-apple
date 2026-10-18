/* Basic guest-visible constants and structs (winbase.h, windef.h, winnt.h).
 *
 * Rules for every header under rt/win/ (docs/RUNTIME.md section 3.3): declared with the
 * types of rt/wintypes.h, no bit-fields, no long / wchar_t / size_t, and a _Static_assert
 * on the size and on every offset the runtime's code relies on.  Planner-owned.
 */
#ifndef RT_WIN_BASE_H
#define RT_WIN_BASE_H

#include "rt/wintypes.h"

#define INFINITE              0xFFFFFFFFu
#define MAX_PATH              260
#define INVALID_HANDLE_VALUE  ((HANDLE)(intptr_t)-1)

/* Options of DuplicateHandle. */
#define DUPLICATE_CLOSE_SOURCE  0x00000001u
#define DUPLICATE_SAME_ACCESS   0x00000002u

/* Standard and generic access rights that go into an access mask (winnt.h). */
#define DELETE                           0x00010000u
#define READ_CONTROL                     0x00020000u
#define WRITE_DAC                        0x00040000u
#define WRITE_OWNER                      0x00080000u
#define SYNCHRONIZE                      0x00100000u
#define ACCESS_SYSTEM_SECURITY           0x01000000u
#define MAXIMUM_ALLOWED                  0x02000000u
#define GENERIC_ALL                      0x10000000u
#define GENERIC_EXECUTE                  0x20000000u
#define GENERIC_WRITE                    0x40000000u
#define GENERIC_READ                     0x80000000u

/* Pseudo-handles (section 4.1): what GetCurrentProcess and GetCurrentThread return. */
#define RT_PSEUDO_PROCESS     ((HANDLE)(intptr_t)-1)
#define RT_PSEUDO_THREAD      ((HANDLE)(intptr_t)-2)

typedef struct { LONG x, y; } POINT;                               /* 8 bytes: by value in a register */
typedef struct { SHORT X, Y; } COORD;                              /* 4 bytes: by value in a register */
typedef struct { LONG left, top, right, bottom; } RECT;            /* 16 bytes: by pointer */
typedef struct {                                                    /* 16 bytes: by pointer */
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;  /* 100 ns units since 1601-01-01 UTC */

typedef union {
    struct { DWORD LowPart; LONG HighPart; };
    struct { DWORD LowPart; LONG HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef union {
    struct { DWORD LowPart; DWORD HighPart; };
    struct { DWORD LowPart; DWORD HighPart; } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER;

typedef struct {
    DWORD  nLength;
    LPVOID lpSecurityDescriptor;
    BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct { DWORD Offset; DWORD OffsetHigh; };
        PVOID Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED;

typedef POINT               *PPOINT, *LPPOINT;
typedef RECT                *PRECT, *LPRECT;
typedef const RECT          *LPCRECT;
typedef SYSTEMTIME          *PSYSTEMTIME, *LPSYSTEMTIME;
typedef FILETIME            *PFILETIME, *LPFILETIME;
typedef LARGE_INTEGER       *PLARGE_INTEGER;
typedef ULARGE_INTEGER      *PULARGE_INTEGER;
typedef SECURITY_ATTRIBUTES *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef OVERLAPPED          *LPOVERLAPPED;

_Static_assert(sizeof(POINT) == 8, "POINT");
_Static_assert(sizeof(COORD) == 4, "COORD");
_Static_assert(sizeof(RECT) == 16, "RECT");
_Static_assert(sizeof(SYSTEMTIME) == 16, "SYSTEMTIME");
_Static_assert(sizeof(FILETIME) == 8, "FILETIME");
_Static_assert(sizeof(LARGE_INTEGER) == 8 && sizeof(ULARGE_INTEGER) == 8, "LARGE_INTEGER");
_Static_assert(sizeof(SECURITY_ATTRIBUTES) == 24 && offsetof(SECURITY_ATTRIBUTES, lpSecurityDescriptor) == 8 &&
               offsetof(SECURITY_ATTRIBUTES, bInheritHandle) == 16, "SECURITY_ATTRIBUTES");
_Static_assert(sizeof(OVERLAPPED) == 32 && offsetof(OVERLAPPED, Offset) == 16 &&
               offsetof(OVERLAPPED, OffsetHigh) == 20 && offsetof(OVERLAPPED, hEvent) == 24, "OVERLAPPED");

#endif /* RT_WIN_BASE_H */
