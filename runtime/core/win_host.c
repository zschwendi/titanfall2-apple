/* The host window (rt/window.h, "the host window"): the game's top-level window gets a real macOS
 * window when RT_HOST_WINDOW says so (rt/host.h does the AppKit part).  This file holds only the
 * handle and the layer of the one window it opened, under its own mutex; the window record is
 * not changed.
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "rt/host.h"
#include "rt/input.h"
#include "rt/log.h"
#include "rt/window.h"

#define HOST_TITLE "Titanfall 2"

typedef enum { HOST_NONE = 0, HOST_SHOWN, HOST_HIDDEN, HOST_FULLSCREEN } HostMode;

static pthread_once_t g_mode_once = PTHREAD_ONCE_INIT;
static HostMode       g_mode;                   /* set once by mode_once */
static bool           g_builtin_screen;         /* RT_HOST_SCREEN=builtin: full screen on the built-in display */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static HWND           g_hwnd;                   /* the window with the host window; NULL until opened */
static void          *g_layer;                  /* its CAMetalLayer, or NULL */
static bool           g_second_warned;

static void mode_once(void)
{
    const char *v = getenv("RT_HOST_WINDOW");
    if (!v || strcmp(v, "none") == 0) g_mode = HOST_NONE;
    else if (strcmp(v, "appkit") == 0) g_mode = HOST_SHOWN;
    else if (strcmp(v, "appkit-hidden") == 0) g_mode = HOST_HIDDEN;
    else if (strcmp(v, "appkit-fullscreen") == 0) g_mode = HOST_FULLSCREEN;
    else rt_fatal("RT_HOST_WINDOW=%s: not a value; the values are appkit, appkit-hidden, appkit-fullscreen, none (or unset)", v);

    const char *s = getenv("RT_HOST_SCREEN");
    if (!s || strcmp(s, "main") == 0) g_builtin_screen = false;
    else if (strcmp(s, "builtin") == 0) g_builtin_screen = true;
    else rt_fatal("RT_HOST_SCREEN=%s: not a value; the values are builtin, main (or unset)", s);
}

bool rt_host_window_wanted(void)
{
    pthread_once(&g_mode_once, mode_once);
    return g_mode != HOST_NONE;
}

void *rt_window_host_layer(HWND h)
{
    if (!rt_host_window_wanted()) return NULL;

    pthread_mutex_lock(&g_lock);
    if (g_hwnd) {
        void *layer = (h == g_hwnd) ? g_layer : NULL;
        if (h != g_hwnd && !g_second_warned) {
            g_second_warned = true;
            rt_log(RT_CH_WINDOW, RT_LL_WARN, "host window: a second window has no host window (one per process)");
        }
        pthread_mutex_unlock(&g_lock);
        return layer;
    }

    UINT w = 0, hgt = 0;
    POINT origin = { 0, 0 };
    if (!rt_window_client_size(h, &w, &hgt) || !rt_window_client_to_screen(h, &origin)) {
        pthread_mutex_unlock(&g_lock);
        rt_log(RT_CH_WINDOW, RT_LL_WARN, "host window: the window is not one, no host window");
        return NULL;
    }

    RtHostWindowDesc d;
    memset(&d, 0, sizeof d);
    d.width_px = w;
    d.height_px = hgt;
    d.origin_x = origin.x;
    d.origin_y = origin.y;
    d.title = HOST_TITLE;
    d.hidden = (g_mode == HOST_HIDDEN);
    d.fullscreen = (g_mode == HOST_FULLSCREEN);
    d.builtin_screen = g_builtin_screen;

    void *layer = rt_host_window_open(&d);   /* waits for the process's first thread; no core lock held */
    g_hwnd = h;
    g_layer = layer;
    rt_input_host_attach();
    rt_log(RT_CH_WINDOW, RT_LL_INFO, "host window: opened %s, %u by %u, origin %d,%d",
           d.hidden ? "hidden" : (d.fullscreen ? "shown full screen" : "shown"), (unsigned)w, (unsigned)hgt, (int)origin.x, (int)origin.y);
    if (!layer)
        rt_log(RT_CH_WINDOW, RT_LL_WARN, "host window: the host returned no layer");
    pthread_mutex_unlock(&g_lock);
    return layer;
}

bool rt_window_is_host_window(HWND h)
{
    pthread_mutex_lock(&g_lock);
    bool yes = g_hwnd != NULL && h == g_hwnd;
    pthread_mutex_unlock(&g_lock);
    return yes;
}

bool rt_window_host_open(void)
{
    pthread_mutex_lock(&g_lock);
    bool yes = g_hwnd != NULL;
    pthread_mutex_unlock(&g_lock);
    return yes;
}
