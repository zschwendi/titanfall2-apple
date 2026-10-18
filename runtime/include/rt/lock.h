/* The locks the game embeds in its own structures (docs/RUNTIME.md section 6.4).
 *
 * Built in runtime/core/lock.c (unit rt-core-lock).  The comments are the specification;
 * the structure layouts are in rt/win/sync.h.
 *
 * These are the hot paths (3,371 critical-section sites, 525 SRW sites, 434 SList sites in
 * the Respawn modules), so the uncontended case must be one atomic operation on memory the
 * guest already owns.  All of them block on one primitive, os_sync_wait_on_address
 * (macOS 14.4 and later), applied to a 32-bit word inside the guest structure.  None
 * allocates and none uses the handle table or the dispatcher lock.
 *
 * The shims of one family are thin wrappers over these functions; a shim never calls
 * another shim.
 */
#ifndef RT_LOCK_H
#define RT_LOCK_H

#include "runtime.h"
#include "rt/wintypes.h"
#include "rt/win/sync.h"

/* ---- CRITICAL_SECTION: recursive, owned by a thread ---------------------------------------- */

/* InitializeCriticalSection[AndSpinCount|Ex]: DebugInfo = -1, LockCount = -1, the rest 0,
 * SpinCount = spin & 0x00FFFFFF. */
void rt_cs_init(CRITICAL_SECTION *cs, DWORD spin);
/* DeleteCriticalSection: zero the structure.  Deleting a held section is logged. */
void rt_cs_delete(CRITICAL_SECTION *cs);
/* EnterCriticalSection.  If the caller already owns it, RecursionCount goes up.  Otherwise
 * acquire the wait word (0 -> 1, or wait while it is contended), then set OwningThread to
 * the caller's thread id and RecursionCount to 1.  LockCount is -1 while the section is
 * free and 0 while it is held, whatever the number of waiters.  That is NOT how Windows 7
 * keeps the field: it is the encoding of Windows before Server 2003 SP1.  Windows 7 keeps
 * "free" in bit 0 and the complement of the waiter count above bit 1, so a held section
 * with no waiters reads -2 there (as core/lock.c reports it; Microsoft's "Displaying a Critical Section").  If
 * guest code turns out to read LockCount, the two constants in lock.c are the place.
 * Entering a section that was never initialised (DebugInfo == 0) is fatal. */
void rt_cs_enter(Ctx *ctx, CRITICAL_SECTION *cs);
/* TryEnterCriticalSection: TRUE if entered (also recursively), FALSE without waiting. */
BOOL rt_cs_try_enter(Ctx *ctx, CRITICAL_SECTION *cs);
/* LeaveCriticalSection.  Lower RecursionCount; at 0 clear OwningThread, release the wait
 * word and wake one waiter if it was contended.  Leaving a section the caller does not
 * own is fatal (Windows corrupts the section). */
void rt_cs_leave(Ctx *ctx, CRITICAL_SECTION *cs);
/* SetCriticalSectionSpinCount: store the new count, return the previous one. */
DWORD rt_cs_set_spin(CRITICAL_SECTION *cs, DWORD spin);

/* ---- SRWLOCK: not recursive, not owned ----------------------------------------------------- */
/* An all-zero SRWLOCK is unlocked, so InitializeSRWLock just zeroes it. */

void    rt_srw_init(SRWLOCK *l);
void    rt_srw_acquire_exclusive(SRWLOCK *l);
void    rt_srw_release_exclusive(SRWLOCK *l);
void    rt_srw_acquire_shared(SRWLOCK *l);
void    rt_srw_release_shared(SRWLOCK *l);
BOOLEAN rt_srw_try_acquire_exclusive(SRWLOCK *l);
BOOLEAN rt_srw_try_acquire_shared(SRWLOCK *l);

/* ---- CONDITION_VARIABLE ---------------------------------------------------------------------- */
/* All-zero is valid.  Sleep reads the wake sequence, releases the lock, waits for the
 * sequence to change (or the timeout), and re-acquires the lock before returning in every
 * case.  Returns TRUE if woken, FALSE with last error ERROR_TIMEOUT on timeout.  Spurious
 * wake-ups are allowed, as on Windows.
 *   flags  CONDITION_VARIABLE_LOCKMODE_SHARED (1): the SRW lock is held shared. */

void rt_cv_init(CONDITION_VARIABLE *cv);
BOOL rt_cv_sleep_srw(Ctx *ctx, CONDITION_VARIABLE *cv, SRWLOCK *l, DWORD timeout_ms, ULONG flags);
BOOL rt_cv_sleep_cs(Ctx *ctx, CONDITION_VARIABLE *cv, CRITICAL_SECTION *cs, DWORD timeout_ms);
void rt_cv_wake(CONDITION_VARIABLE *cv);
void rt_cv_wake_all(CONDITION_VARIABLE *cv);

/* ---- SLIST_HEADER: lock-free singly linked list ----------------------------------------------- */
/* Push, pop and flush are one 128-bit compare-and-swap on the 16-byte header
 * (x_atomic_cmpxchg128 in harness/runtime.h).  The sequence in the low qword defeats ABA;
 * the depth is in its bits 0-15, where inlined QueryDepthSList reads it. */

void         rt_slist_init(SLIST_HEADER *h);
/* Both return the previous first entry (NULL if the list was empty). */
PSLIST_ENTRY rt_slist_push(SLIST_HEADER *h, SLIST_ENTRY *entry);
PSLIST_ENTRY rt_slist_pop(SLIST_HEADER *h);
/* Remove everything; returns the old first entry with its chain intact. */
PSLIST_ENTRY rt_slist_flush(SLIST_HEADER *h);
USHORT       rt_slist_depth(const SLIST_HEADER *h);

#endif /* RT_LOCK_H */
