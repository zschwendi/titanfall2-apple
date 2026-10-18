// window_test_layer.h -- the Objective-C half of window_test, behind a plain C interface.
//
// window_test.cpp includes DXMT's native <windows.h>, whose BOOL and `interface` clash with Objective-C headers, so
// everything that needs AppKit, QuartzCore or Metal lives in window_test_layer.mm:
//   - a probe on the CAMetalLayer that DXMT presents into: every drawable the layer hands out is recorded (who asked,
//     how long it blocked, its texture size and format, and the time Core Animation says it was shown), and its
//     pixels can be copied back.  No screen capture, no permission prompt: only our own layer's own drawables.
//   - an optional view and layer of our own inside the SDL window (instead of SDL_Metal_CreateView);
//   - small facts about the layer and the window for the report.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Seconds on the clock Core Animation uses for presentedTime (mach_absolute_time).
double wl_now(void);
int wl_is_main_thread(void);

// Not a foreground app: no Dock icon, no menu bar, no activation.  Call after SDL_Init.
void wl_set_accessory_app(void);

// CGDirectDisplayID of the screen an NSWindow is on (main thread).
uint32_t wl_window_display_id(void *nswindow);

// ---- the probe -------------------------------------------------------------------------------------------------
// Makes `metal_layer` (a plain CAMetalLayer) an instance of a subclass that overrides -nextDrawable.  Main thread,
// before DXMT sees the layer.  Returns 1 on success, 0 if the layer is not exactly a CAMetalLayer.
int wl_probe_install(void *metal_layer);

typedef struct {
  uint64_t index;         // 1-based count of -nextDrawable calls on the probed layer
  int      got_drawable;  // 0: -nextDrawable returned nil
  double   t_called;      // wl_now() when -nextDrawable was called
  double   t_returned;    // ... and when it returned
  double   t_presented;   // drawable.presentedTime; 0 until the handler ran, and 0 if the frame was never shown
  int      handler_ran;   // the presented handler has been called
  uint32_t width, height; // drawable texture
  uint32_t pixel_format;  // MTLPixelFormat (80 = BGRA8Unorm)
  int      on_main_thread;
  char     thread_name[40];
} WlDrawable;

uint64_t wl_probe_count(void);                      // -nextDrawable calls so far
int wl_probe_get(uint64_t index, WlDrawable *out);  // 1 if `index` exists

// Copies the texture of drawable `index` to `rgba` (width*height*4 bytes, R,G,B,A order).  Only meaningful after the
// GPU has finished the frame and before the layer hands that drawable out again; only the last few are kept.
// Returns 1 on success.
int wl_probe_read_rgba(uint64_t index, uint8_t *rgba, uint32_t width, uint32_t height);

// Forget the textures kept for wl_probe_read_rgba (before checking that the layer is deallocated).
void wl_probe_forget(void);

// ---- what DXMT asks Metal to do with each drawable -----------------------------------------------------------------
// Hooks -[MTLCommandBuffer presentDrawable:], presentDrawable:afterMinimumDuration: and presentDrawable:atTime: on the
// command buffer class of the default device, and records each call before passing it on.  This is the request DXMT
// makes for pacing; it can be checked whether or not a display is awake to show the frames.  Returns 1 on success.
int wl_present_hook_install(void);

typedef struct {
  int    kind;      // 0 presentDrawable:, 1 ...afterMinimumDuration:, 2 ...atTime:
  double argument;  // the minimum duration or the time, in seconds; 0 for kind 0
  double t_called;  // wl_now()
  char   thread_name[40];
} WlPresent;

uint64_t wl_present_count(void);
int wl_present_get(uint64_t index, WlPresent *out);  // 1-based

// ---- autorelease pools ---------------------------------------------------------------------------------------------
// DXMT sends Objective-C messages on the thread that calls Present (it reads the layer's view, window and screen every
// frame); what those autorelease lives until the calling thread drains a pool.
void *wl_pool_push(void);
void wl_pool_pop(void *pool);
// Prints the calling thread's pending autoreleases to stderr, one line each (the Objective-C runtime's own debug
// output, lines "objc[pid]: [address]  address  Class").
void wl_pool_print(void);

// ---- the display and the window ------------------------------------------------------------------------------------
int wl_display_asleep(uint32_t display_id);  // CGDisplayIsAsleep
int wl_window_visible(void *nswindow);       // NSWindowOcclusionStateVisible (main thread)
int wl_screen_locked(void);                  // the login session's CGSSessionScreenIsLocked
long wl_retain_count(void *object);          // CFGetRetainCount, for the report only

// ---- a view and layer of our own ----------------------------------------------------------------------------------
// Adds a layer-backed NSView filling the NSWindow's content view, backed by a probed CAMetalLayer.  Main thread.
// pixels_per_point 0 = use the window's backing scale.  Returns the CAMetalLayer*, and the view in *out_view.
void *wl_ownview_create(void *nswindow, double pixels_per_point, void **out_view);
void wl_ownview_destroy(void *view);

// ---- facts ----------------------------------------------------------------------------------------------------------
typedef struct {
  char   class_name[48];
  double bounds_w, bounds_h, contents_scale, drawable_w, drawable_h;
  uint32_t pixel_format;
  int    opaque, framebuffer_only, display_sync, has_device, delegate_is_view, wants_edr;
  uint32_t maximum_drawable_count;
  char   colorspace[64];
} WlLayerInfo;
void wl_layer_info(void *metal_layer, WlLayerInfo *out);
// CAMetalLayer.maximumDrawableCount (2 or 3): the host owns the layer and may set it before DXMT sees it.
void wl_layer_set_maximum_drawable_count(void *metal_layer, unsigned count);

// The names of all threads in the process, one line per distinct name with a count ("  2 x dxmt-encode-thread").
void wl_thread_names(char *out, unsigned long size);

// A weak reference, to see whether the layer is deallocated once everything has let go of it.
void *wl_weak_new(void *object);
int wl_weak_alive(void *weak);  // 1 while the object exists

#ifdef __cplusplus
}
#endif
