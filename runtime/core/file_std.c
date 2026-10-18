/* Standard handles, GetFileType, console control handlers (docs/RUNTIME.md section 7.3).
 * Specification: rt/file.h.  Shared internals: core/file_priv.h (all built, in core/file.c).
 *
 * BUILT: the piece `rt-file-std` (docs/handoff/rt-contract-file.md has its task, its test
 * plan and its brief).  Each function does what its comment in rt/file.h says; the test is
 * tests/core/file_std.c.
 *
 * STATE: the standard handles themselves are made by rt_file_init (core/file.c) and live in
 * the three atomic slots behind rt_file_std_slot / rt_file_std_slot_swap; this file keeps only
 * the list of console control handlers below, under `ctrl_lock` (a leaf: nothing is called
 * under it).
 */
#include <os/lock.h>

#include "file_priv.h"

#define RT_CTRL_MAX 64

static os_unfair_lock ctrl_lock = OS_UNFAIR_LOCK_INIT;
static GUESTFN        ctrl_handlers[RT_CTRL_MAX];     /* ctrl_lock; in the order added */
static unsigned       ctrl_count;                    /* ctrl_lock */
static bool           ctrl_ignore_c;                 /* ctrl_lock */

/* The standard handle `which` is -10, -11 or -12: its slot is 0, 1 or 2.  Anything else
 * leaves the unsigned difference above 2. */
static unsigned std_slot_index(DWORD which) {
    return (unsigned)(STD_INPUT_HANDLE - which);
}

static HANDLE std_fail(DWORD e) { rt_thread_set_last_error(e); return INVALID_HANDLE_VALUE; }

HANDLE rt_std_get(DWORD which) {
    const unsigned i = std_slot_index(which);
    if (i > 2) return std_fail(ERROR_INVALID_HANDLE);
    return rt_file_std_slot(i);
}

BOOL rt_std_set(DWORD which, HANDLE h) {
    const unsigned i = std_slot_index(which);
    if (i > 2) return rt_file_fail(ERROR_INVALID_HANDLE);
    (void)rt_file_std_slot_swap(i, h);              /* the old value stays open: closes nothing */
    return TRUE;
}

DWORD rt_file_type(HANDLE h) {
    RtFile *f = rt_file_ref(h);                     /* sets ERROR_INVALID_HANDLE if not a file */
    if (!f) return FILE_TYPE_UNKNOWN;
    const DWORD t = f->type == RT_FT_CHAR ? FILE_TYPE_CHAR : FILE_TYPE_DISK;
    rt_file_unref(f);
    return t;
}

BOOL rt_console_ctrl_handler(GUESTFN routine, BOOL add) {
    if (routine == 0) {                             /* the "ignore Ctrl+C" flag */
        os_unfair_lock_lock(&ctrl_lock);
        ctrl_ignore_c = add != 0;
        os_unfair_lock_unlock(&ctrl_lock);
        return TRUE;
    }
    if (add) {
        os_unfair_lock_lock(&ctrl_lock);
        const bool full = ctrl_count == RT_CTRL_MAX;
        if (!full) ctrl_handlers[ctrl_count++] = routine;
        os_unfair_lock_unlock(&ctrl_lock);
        if (full) RT_FILE_STOP("rt_console_ctrl_handler", "more than %d handlers", RT_CTRL_MAX);
        return TRUE;
    }
    /* Remove the entry added last with this routine: search from the end, then close the gap. */
    os_unfair_lock_lock(&ctrl_lock);
    unsigned i = ctrl_count;
    while (i > 0 && ctrl_handlers[i - 1] != routine) i--;
    const bool found = i > 0;
    if (found) {
        for (unsigned j = i - 1; j + 1 < ctrl_count; j++) ctrl_handlers[j] = ctrl_handlers[j + 1];
        ctrl_count--;
        ctrl_handlers[ctrl_count] = 0;
    }
    os_unfair_lock_unlock(&ctrl_lock);
    if (!found)
        RT_FILE_STOP("rt_console_ctrl_handler", "removing routine 0x%llx, which is not in the list",
                     (unsigned long long)routine);
    return TRUE;
}
