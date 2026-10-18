/* Classes and registered messages (specification: rt/window.h; private side: core/win_priv.h).
 * BUILT: rt_wndclass_register, rt_wndclass_unregister, rt_wndmsg_register, and the two
 * functions the other pieces find a class with (rt_win_text_key, rt_win_class_lookup).
 * Tests: tests/core/win_class.c.
 *
 * State, all under the win lock: rt_win.classes (256 slots, core/win_priv.h) and the name
 * table below.  Guest text is read before the lock is taken and nothing is allocated, logged
 * or reported while it is held.
 *
 * THE NAME TABLE.  Class atoms and registered message numbers are one series, 0xC000 to
 * 0xFFFF, as on Windows: a name gets the next free number the first time either function
 * sees it, and keeps it for the life of the process.  A name is never removed, so a class
 * that is unregistered and registered again has the atom it had before (this runtime's
 * choice: Windows may give the number out again once nothing holds the atom).
 */
#include <stdlib.h>
#include <string.h>

#include "win_priv.h"
#include "rt/locale.h"
#include "rt/module.h"

#define ATOM_FIRST  0xC000u
#define ATOM_COUNT  0x4000u                     /* 0xC000 to 0xFFFF */
#define NAME_MAX_CH 255u                        /* characters, without the terminator */

static WCHAR   *atom_name[ATOM_COUNT];          /* win lock; [i] is the name of ATOM_FIRST + i */
static unsigned atom_used;

static WCHAR fold(WCHAR c) { return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c; }

static bool same_name(const WCHAR *a, const WCHAR *b) {
    for (; fold(*a) == fold(*b); a++, b++)
        if (!*a) return true;
    return false;
}

/* The number of `name`; a name the table has not seen takes the next number and the table
 * keeps `*copy` (set to NULL then).  0 if the table is full.  Win lock held. */
static ATOM atom_intern(const WCHAR *name, WCHAR **copy) {
    for (unsigned i = 0; i < atom_used; i++)
        if (same_name(atom_name[i], name)) return (ATOM)(ATOM_FIRST + i);
    if (atom_used == ATOM_COUNT) return 0;
    atom_name[atom_used] = *copy;
    *copy = NULL;
    return (ATOM)(ATOM_FIRST + atom_used++);
}

static WCHAR *name_copy(const WCHAR *name, const char *fn) {
    size_t n = 0;
    while (name[n]) n++;
    WCHAR *c = malloc((n + 1) * sizeof(WCHAR));
    if (!c) rt_fatal("%s: no host memory for a name of %zu characters", fn, n);
    memcpy(c, name, (n + 1) * sizeof(WCHAR));
    return c;
}

RtKeyKind rt_win_text_key(RtText t, RtClassKey *key) {
    const uintptr_t v = t.w ? (uintptr_t)t.w : (uintptr_t)t.a;
    key->atom = 0;
    key->name[0] = 0;
    if (!v) return RT_KEY_NULL;
    if (v < 0x10000u) { key->atom = (ATOM)v; return RT_KEY_ATOM; }
    size_t n = 0;
    if (t.w) {
        while (n <= NAME_MAX_CH && t.w[n]) n++;
        if (n > NAME_MAX_CH) return RT_KEY_LONG;
        memcpy(key->name, t.w, n * sizeof(WCHAR));
    } else {
        while (n <= NAME_MAX_CH && t.a[n]) n++;
        if (n > NAME_MAX_CH) return RT_KEY_LONG;
        /* Code page 1252 is one byte a character and defines every byte here (rt/locale.h),
         * so n bytes give n characters and the call cannot fail. */
        if (n && rt_mb_to_wide(1252, 0, t.a, (int)n, key->name, (int)n) != (int)n)
            rt_fatal("rt_win_text_key: code page 1252 did not convert %zu bytes to as many characters", n);
    }
    key->name[n] = 0;
    return n ? RT_KEY_NAME : RT_KEY_EMPTY;
}

RtWndClass *rt_win_class_lookup(const RtClassKey *key) {
    for (unsigned i = 0; i < RT_WIN_MAX_CLASSES; i++) {
        RtWndClass *c = &rt_win.classes[i];
        if (!c->used) continue;
        if (key->atom ? c->atom == key->atom : same_name(c->name, key->name)) return c;
    }
    return NULL;
}

static ATOM fail_atom(DWORD e) { rt_thread_set_last_error(e); return 0; }
static BOOL fail_bool(DWORD e) { rt_thread_set_last_error(e); return FALSE; }

ATOM rt_wndclass_register(const WNDCLASSEXW *wc, bool unicode)
{
    static const char fn[] = "rt_wndclass_register";
    if (!wc || wc->cbSize != sizeof *wc) return fail_atom(ERROR_INVALID_PARAMETER);
    if (wc->cbClsExtra < 0 || wc->cbWndExtra < 0) return fail_atom(ERROR_INVALID_PARAMETER);
    RtClassKey key;
    const RtKeyKind kind = rt_win_text_key(unicode ? RT_TEXT_W(wc->lpszClassName)
                                                   : RT_TEXT_A((const char *)wc->lpszClassName), &key);
    if (kind == RT_KEY_NULL || kind == RT_KEY_LONG) return fail_atom(ERROR_INVALID_PARAMETER);
    if (kind == RT_KEY_ATOM)
        RT_WIN_STOP("rt_wndclass_register", "a class name that is an atom (0x%04x)", (unsigned)key.atom);
    if (kind == RT_KEY_EMPTY)
        RT_WIN_STOP("rt_wndclass_register", "an empty class name (what Windows answers is not verified)");
    if (!wc->lpfnWndProc)
        RT_WIN_STOP("rt_wndclass_register", "a class with no window procedure (lpfnWndProc 0)");
    if (wc->lpszMenuName)
        RT_WIN_STOP("rt_wndclass_register", "a class with a menu name (%p): menus are not built",
                    (const void *)wc->lpszMenuName);
    if (wc->cbClsExtra > RT_WIN_MAX_EXTRA || wc->cbWndExtra > RT_WIN_MAX_EXTRA)
        RT_WIN_STOP("rt_wndclass_register", "more than %d extra bytes (cbClsExtra %d, cbWndExtra %d)",
                    RT_WIN_MAX_EXTRA, (int)wc->cbClsExtra, (int)wc->cbWndExtra);

    /* hInstance NULL is the main module (Wine user32/tests/class.c, test_instances; review D6
     * of docs/reviews/contracts-check-2.md): stored and compared as the executable's handle. */
    const HINSTANCE inst = wc->hInstance ? wc->hInstance : (HINSTANCE)(uintptr_t)rt_module_exe()->base;
    WCHAR *copy = name_copy(key.name, fn);
    enum { OK, EXISTS, OTHER_MODULE, NO_SLOT, NO_ATOM } how = OK;
    ATOM atom = 0;
    HINSTANCE other = NULL;
    rt_win_lock();
    RtWndClass *c = rt_win_class_lookup(&key);
    if (c) {
        how = c->instance == inst ? EXISTS : OTHER_MODULE;
        other = c->instance;
    } else {
        for (unsigned i = 0; i < RT_WIN_MAX_CLASSES && !c; i++)
            if (!rt_win.classes[i].used) c = &rt_win.classes[i];
        if (!c) how = NO_SLOT;
        else if (!(atom = atom_intern(key.name, &copy))) how = NO_ATOM;
        else {
            memset(c, 0, sizeof *c);
            c->used = true;
            c->unicode = unicode;
            c->atom = atom;
            memcpy(c->name, key.name, sizeof c->name);
            c->style = wc->style;
            c->proc = wc->lpfnWndProc;
            c->cls_extra = wc->cbClsExtra;
            c->wnd_extra = wc->cbWndExtra;
            c->instance = inst;
            c->icon = wc->hIcon;
            c->icon_sm = wc->hIconSm;
            c->cursor = wc->hCursor;
            c->background = wc->hbrBackground;
        }
    }
    rt_win_unlock();
    free(copy);
    switch (how) {
    case OK:      return atom;
    case EXISTS:  return fail_atom(ERROR_CLASS_ALREADY_EXISTS);
    case OTHER_MODULE:
        RT_WIN_STOP("rt_wndclass_register", "a class name that is registered with another hInstance "
                    "(%p, now %p): per-module classes of one name are not built",
                    (void *)other, (void *)inst);
    case NO_SLOT: RT_WIN_STOP("rt_wndclass_register", "more than %d classes", RT_WIN_MAX_CLASSES);
    default:      RT_WIN_STOP("rt_wndclass_register", "the table of class and message names is full (%u names)", ATOM_COUNT);
    }
}

BOOL rt_wndclass_unregister(RtText name, HINSTANCE inst)
{
    (void)inst;                                 /* one class per name: rt/window.h */
    RtClassKey key;
    const RtKeyKind kind = rt_win_text_key(name, &key);
    if (kind != RT_KEY_ATOM && kind != RT_KEY_NAME) return fail_bool(ERROR_CLASS_DOES_NOT_EXIST);
    DWORD e = 0;
    rt_win_lock();
    RtWndClass *c = rt_win_class_lookup(&key);
    if (!c) e = ERROR_CLASS_DOES_NOT_EXIST;
    else if (c->windows) e = ERROR_CLASS_HAS_WINDOWS;
    else c->used = false;
    rt_win_unlock();
    return e ? fail_bool(e) : TRUE;
}

UINT rt_wndmsg_register(RtText name)
{
    static const char fn[] = "rt_wndmsg_register";
    RtClassKey key;
    switch (rt_win_text_key(name, &key)) {
    case RT_KEY_NAME:  break;
    case RT_KEY_NULL:  RT_WIN_STOP("rt_wndmsg_register", "a NULL message name");
    case RT_KEY_EMPTY: RT_WIN_STOP("rt_wndmsg_register", "an empty message name");
    case RT_KEY_LONG:  RT_WIN_STOP("rt_wndmsg_register", "a message name longer than %u characters", NAME_MAX_CH);
    default:           RT_WIN_STOP("rt_wndmsg_register", "a message name that is an atom (0x%04x)", (unsigned)key.atom);
    }
    WCHAR *copy = name_copy(key.name, fn);
    rt_win_lock();
    const ATOM atom = atom_intern(key.name, &copy);
    rt_win_unlock();
    free(copy);
    if (!atom) RT_WIN_STOP("rt_wndmsg_register", "the table of class and message names is full (%u names)", ATOM_COUNT);
    return atom;
}
