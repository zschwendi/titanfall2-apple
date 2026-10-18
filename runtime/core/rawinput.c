/* Raw input: the process's registrations.  Interface and specification: rt/rawinput.h (every
 * case, its source and every stop is there).  Built by unit rt-census-rows.  One mutex guards
 * the table; nothing here posts a message. */
#include <pthread.h>
#include <string.h>

#include "rt/rawinput.h"
#include "rt/input.h"
#include "rt/window.h"
#include "rt/thread.h"
#include "rt/log.h"
#include "rt/win/error.h"

#define STOP(fmt, ...)  rt_fatal("rt_raw_input_register: not implemented: " fmt, ##__VA_ARGS__)

enum { MAX_DEVICES = 16 };
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct { RAWINPUTDEVICE d; DWORD thread; } table[MAX_DEVICES];
static unsigned used;

static int find(USHORT page, USHORT usage) {
    for (unsigned i = 0; i < used; i++)
        if (table[i].d.usUsagePage == page && table[i].d.usUsage == usage) return (int)i;
    return -1;
}

BOOL rt_raw_input_register(PCRAWINPUTDEVICE list, UINT count, UINT cb) {
    if (cb != sizeof(RAWINPUTDEVICE)) {
        rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!list || !count) STOP("%s with a good size", list ? "a count of 0" : "a NULL list");
    DWORD threads[MAX_DEVICES] = { 0 };
    if (count > MAX_DEVICES) STOP("%u entries in one call", (unsigned)count);
    /* Check every entry before anything is recorded. */
    for (UINT i = 0; i < count; i++)
        if ((list[i].dwFlags & RIDEV_INPUTSINK) && !list[i].hwndTarget) {
            rt_thread_set_last_error(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    for (UINT i = 0; i < count; i++) {
        const RAWINPUTDEVICE *d = &list[i];
        if (d->dwFlags & ~(RIDEV_REMOVE | RIDEV_NOLEGACY | RIDEV_INPUTSINK)
            || ((d->dwFlags & RIDEV_NOLEGACY) && (d->dwFlags & RIDEV_NOLEGACY) != RIDEV_NOLEGACY)
            || ((d->dwFlags & RIDEV_REMOVE) && d->dwFlags != RIDEV_REMOVE))
            STOP("flags 0x%x for usage page 0x%x, usage 0x%x", (unsigned)d->dwFlags, d->usUsagePage, d->usUsage);
        if ((d->dwFlags & RIDEV_REMOVE) && d->hwndTarget)
            STOP("RIDEV_REMOVE with a target window (%p)", d->hwndTarget);
        if (d->hwndTarget) {
            if (d->hwndTarget == RT_HWND_DESKTOP) STOP("the desktop window as the target");
            threads[i] = rt_window_thread(d->hwndTarget, NULL);
            if (!threads[i]) STOP("target %p is not a window", d->hwndTarget);
        }
    }
    pthread_mutex_lock(&lock);
    for (UINT i = 0; i < count; i++) {
        const RAWINPUTDEVICE *d = &list[i];
        int at = find(d->usUsagePage, d->usUsage);
        if (d->dwFlags & RIDEV_REMOVE) {
            if (at < 0) STOP("RIDEV_REMOVE of usage page 0x%x, usage 0x%x, which is not registered", d->usUsagePage, d->usUsage);
            table[at] = table[--used];
            continue;
        }
        if (at < 0) {
            if (used == MAX_DEVICES) STOP("more than %d collections", MAX_DEVICES);
            at = (int)used++;
        }
        table[at].d = *d;
        table[at].thread = threads[i];
    }
    pthread_mutex_unlock(&lock);
    rt_log_once(RT_CH_WINDOW, RT_LL_INFO, "user32.dll!RegisterRawInputDevices",
                "RegisterRawInputDevices: %u collection(s), the first usage page 0x%x usage 0x%x flags 0x%x target %p: "
                "recorded; %s",
                (unsigned)count, list[0].usUsagePage, list[0].usUsage, (unsigned)list[0].dwFlags, list[0].hwndTarget,
                rt_input_active() ? "an input script is loaded: its events are posted as WM_INPUT"
                                  : "no input arrives in this runtime, so no WM_INPUT is posted");
    return TRUE;
}

UINT rt_raw_input_count(void) {
    pthread_mutex_lock(&lock);
    UINT n = used;
    pthread_mutex_unlock(&lock);
    return n;
}

BOOL rt_raw_input_find(USHORT page, USHORT usage, RAWINPUTDEVICE *out, DWORD *thread) {
    pthread_mutex_lock(&lock);
    int at = find(page, usage);
    if (at >= 0) {
        if (out) *out = table[at].d;
        if (thread) *thread = table[at].thread;
    }
    pthread_mutex_unlock(&lock);
    return at >= 0;
}

/* ---- the packets (rt/rawinput.h; unit rt-input-script) ----------------------------------------- */
enum { RING = 4096 };
static struct { uint64_t seq; bool live; RAWINPUT p; } ring[RING];
static uint64_t issued;                 /* how many handles were given out; guarded by `lock` */

HRAWINPUT rt_raw_input_new(const RAWINPUT *packet) {
    pthread_mutex_lock(&lock);
    const uint64_t seq = ++issued;
    ring[seq % RING].seq = seq;
    ring[seq % RING].live = true;
    ring[seq % RING].p = *packet;
    pthread_mutex_unlock(&lock);
    return (HRAWINPUT)(uintptr_t)(RT_HRAWINPUT_BASE + seq);
}

/* 1 live (and *out), 0 never issued, -1 issued and gone.  `drop`: release it. */
static int packet_of(HRAWINPUT h, RAWINPUT *out, bool drop) {
    const uint64_t v = (uint64_t)(uintptr_t)h;
    int r = 0;
    pthread_mutex_lock(&lock);
    if (v > RT_HRAWINPUT_BASE && v - RT_HRAWINPUT_BASE <= issued) {
        const uint64_t seq = v - RT_HRAWINPUT_BASE;
        if (ring[seq % RING].seq == seq && ring[seq % RING].live) {
            r = 1;
            if (out) *out = ring[seq % RING].p;
            if (drop) ring[seq % RING].live = false;
        } else r = -1;
    }
    pthread_mutex_unlock(&lock);
    return r;
}

int rt_raw_input_read(HRAWINPUT h, RAWINPUT *out) { return packet_of(h, out, false); }
void rt_raw_input_release(HRAWINPUT h) { (void)packet_of(h, NULL, true); }

UINT rt_raw_input_data(HRAWINPUT h, UINT cmd, void *data, UINT *size) {
    RAWINPUT p;
    const int r = rt_raw_input_read(h, &p);
    if (r == 0) rt_fatal("rt_raw_input_data: not implemented: handle %p, which this runtime did not issue (the handles of "
                         "WM_INPUT's lParam start at 0x%x)", (void *)h, RT_HRAWINPUT_BASE + 1);
    if (r < 0) rt_fatal("rt_raw_input_data: not implemented: handle %p, whose packet is gone (DefWindowProc got its WM_INPUT, "
                        "or %d newer packets were posted since): what Windows answers is not known", (void *)h, RING);
    const UINT need = cmd == RID_HEADER ? (UINT)sizeof(RAWINPUTHEADER) : p.header.dwSize;
    if (!data) { *size = need; return 0; }                              /* C16 */
    if (*size < need)
        rt_fatal("rt_raw_input_data: not implemented: a buffer of %u bytes for %u (Windows: -1 with ERROR_INSUFFICIENT_BUFFER "
                 "from memory, not verified)", (unsigned)*size, (unsigned)need);
    memcpy(data, &p, need);                                             /* C17: *size is left alone */
    return need;
}

unsigned rt_raw_input_devices(RtRawDevice *out, unsigned cap) {
    if (!rt_input_active()) return 0;
    const RtRawDevice both[2] = { { RT_HRAWDEV_MOUSE, RIM_TYPEMOUSE }, { RT_HRAWDEV_KEYBOARD, RIM_TYPEKEYBOARD } };
    for (unsigned i = 0; i < 2 && i < cap; i++) out[i] = both[i];
    return 2;
}
