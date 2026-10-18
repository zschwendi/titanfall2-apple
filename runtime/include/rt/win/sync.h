/* The locks the game embeds in its own structures (synchapi.h, winnt.h), with the layouts
 * the runtime keeps in them.  docs/RUNTIME.md section 6.4; the functions are in rt/lock.h.
 *
 * The game allocates these inside its objects and statics, so sizes and observable fields
 * match Windows exactly.  All state lives inside the structure; nothing is allocated.
 * Planner-owned.
 */
#ifndef RT_WIN_SYNC_H
#define RT_WIN_SYNC_H

#include "rt/wintypes.h"

/* 40 bytes.  Must be initialised before use; DeleteCriticalSection zeroes it. */
typedef struct {
    PVOID     DebugInfo;        /* +0x00  -1 once initialised */
    LONG      LockCount;        /* +0x08  -1 when free, 0 when held: not the Windows 7 encoding (rt/lock.h) */
    LONG      RecursionCount;   /* +0x0C  maintained truthfully: debug code reads it */
    HANDLE    OwningThread;     /* +0x10  thread id of the owner, 0 when free */
    HANDLE    LockSemaphore;    /* +0x18  low 32 bits are the wait word: 0 free, 1 held, 2 contended */
    ULONG_PTR SpinCount;        /* +0x20 */
} CRITICAL_SECTION;
typedef CRITICAL_SECTION *PCRITICAL_SECTION, *LPCRITICAL_SECTION;

/* 8 bytes.  All-zero is a valid unlocked lock (SRWLOCK_INIT).  Low 32 bits: bit 0 exclusive,
 * bit 1 waiters, bits 2 and up the shared count. */
typedef struct { PVOID Ptr; } SRWLOCK;
typedef SRWLOCK *PSRWLOCK;

/* 8 bytes.  All-zero is valid.  Low 32 bits: the wake sequence number. */
typedef struct { PVOID Ptr; } CONDITION_VARIABLE;
typedef CONDITION_VARIABLE *PCONDITION_VARIABLE;
/* Flag of InitializeCriticalSectionEx: do not allocate the debug information block. */
#define CRITICAL_SECTION_NO_DEBUG_INFO 0x01000000u
/* Flags of SleepConditionVariableSRW: the SRW lock is held in shared mode. */
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1u

/* 16 bytes, 16-byte aligned.  Low qword: depth in bits 0-15 (where inlined QueryDepthSList
 * reads it), sequence above; high qword: address of the first entry. */
typedef struct __attribute__((aligned(16))) {
    ULONGLONG Alignment;
    ULONGLONG Region;
} SLIST_HEADER;
typedef SLIST_HEADER *PSLIST_HEADER;

typedef struct __attribute__((aligned(16))) RT_SLIST_ENTRY {
    struct RT_SLIST_ENTRY *Next;
} SLIST_ENTRY;
typedef SLIST_ENTRY *PSLIST_ENTRY;

_Static_assert(sizeof(CRITICAL_SECTION) == 40, "CRITICAL_SECTION");
_Static_assert(offsetof(CRITICAL_SECTION, LockCount) == 0x08 && offsetof(CRITICAL_SECTION, RecursionCount) == 0x0C &&
               offsetof(CRITICAL_SECTION, OwningThread) == 0x10 && offsetof(CRITICAL_SECTION, LockSemaphore) == 0x18 &&
               offsetof(CRITICAL_SECTION, SpinCount) == 0x20, "CRITICAL_SECTION offsets");
_Static_assert(sizeof(SRWLOCK) == 8 && sizeof(CONDITION_VARIABLE) == 8, "SRWLOCK, CONDITION_VARIABLE");
_Static_assert(sizeof(SLIST_HEADER) == 16 && _Alignof(SLIST_HEADER) == 16, "SLIST_HEADER");
_Static_assert(sizeof(SLIST_ENTRY) == 16 && _Alignof(SLIST_ENTRY) == 16, "SLIST_ENTRY");

#endif /* RT_WIN_SYNC_H */
