/* Hook records (specification: rt/window.h; private side: core/win_priv.h).
 *
 * BUILT: the piece `win-hook` (docs/handoff/rt-contract-window.md): rt_hook_set and
 * rt_hook_remove.  A hook is a record in the user table (rt_win_table) of kind RT_K_HOOK.  Its
 * procedure is kept and never called: no input exists before Gate 4.  Tests: tests/core/win_hook.c.
 *
 * State: the records only, in the user table, whose own lock covers them; the table is used
 * with the win lock held, which the table allows (core/win_priv.h).  Every check is made
 * before a record is made, so a call that fails or stops leaves no record behind.
 */
#include "win_priv.h"
#include "rt/thread.h"

/* A hook record: the user table's object of kind RT_K_HOOK. */
typedef struct RtHook {
    RtObject  obj;                      /* kind RT_K_HOOK */
    INT       type;
    HOOKPROC  proc;
    HINSTANCE mod;
    DWORD     thread_id;                /* 0: a global hook */
} RtHook;

static const RtObjectOps hook_ops = { .name = "hook", .no_close_handle = true };

static BOOL fail_bool(DWORD e) { rt_thread_set_last_error(e); return FALSE; }

static bool is_low_level(INT type) { return type == WH_KEYBOARD_LL || type == WH_MOUSE_LL; }

/* The four types whose events are real input (rt/window.h, rt_hook_set). */
static bool is_input_type(INT type) { return type == WH_KEYBOARD || type == WH_MOUSE || is_low_level(type); }

HHOOK rt_hook_set(INT type, HOOKPROC fn, HINSTANCE mod, DWORD thread_id)
{
    /* The four checks, in the order of rt/window.h. */
    DWORD e = 0;
    if (type < -1 || type > 14) e = ERROR_INVALID_HOOK_FILTER;
    else if (!fn) e = ERROR_INVALID_FILTER_PROC;
    else if (is_low_level(type) && thread_id) e = ERROR_GLOBAL_ONLY_HOOK;
    else if (!is_low_level(type) && !thread_id && !mod) e = ERROR_HOOK_NEEDS_HMOD;
    if (e) { rt_thread_set_last_error(e); return NULL; }

    /* Stops, made before anything is recorded. */
    if (!is_input_type(type))
        RT_WIN_STOP("rt_hook_set", "hook type %d: hooks of this type are not built (their events are windows, messages or queue reads)", (int)type);
    const DWORD self = rt_thread_self()->id;
    if (!is_low_level(type) && thread_id && thread_id != self)
        RT_WIN_STOP("rt_hook_set", "a thread hook on thread %u, not the calling thread %u: hooks on another thread are not built",
                    (unsigned)thread_id, (unsigned)self);

    RtHook *rec = (RtHook *)rt_object_new(RT_K_HOOK, sizeof *rec, &hook_ops, NULL, NULL);
    rec->type = type;
    rec->proc = fn;
    rec->mod = mod;
    rec->thread_id = thread_id;
    rt_win_lock();
    const HANDLE h = rt_htab_new(rt_win_table(), &rec->obj);
    rt_win_unlock();
    if (!h) {
        rt_object_unref(&rec->obj);
        RT_WIN_STOP("rt_hook_set", "the user handle table is full (no slot for a hook)");
    }
    return (HHOOK)h;
}

BOOL rt_hook_remove(HHOOK h)
{
    rt_win_lock();
    const BOOL closed = rt_htab_close(rt_win_table(), (HANDLE)h, RT_K_HOOK);
    rt_win_unlock();
    return closed ? TRUE : fail_bool(ERROR_INVALID_HOOK_HANDLE);
}
