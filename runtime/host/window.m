// window.m -- the host window (rt/host.h).  Objective-C with -fobjc-arc.  Plain AppKit: no runtime lock,
// no guest code, no logging through the runtime.  The events are translated by rt_host_event_translate
// (rt/hostevent.h) and handed to the sink.
/* rt/wintypes.h (through rt/input.h) typedefs BOOL as a 32-bit integer, and Objective-C's BOOL is bool:
 * the two cannot share a translation unit.  The runtime's headers are read first, with the Windows name
 * renamed, and AppKit after; this file uses no BOOL. */
#define BOOL RT_WIN_BOOL
#include "rt/host.h"
#include "rt/hostevent.h"
#undef BOOL
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreGraphics/CoreGraphics.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

// ---- state ----------------------------------------------------------------------------------------------------------
// Written on the first thread (the main queue) except where said; the flags are read from any thread.

static pthread_t g_first_thread;                 // the thread that called rt_host_main
static atomic_bool g_relative;                   // what the game wants: rt_host_set_relative
static atomic_bool g_applied;                    // what is applied: wanted AND window shown AND key (apply_relative)
static _Atomic(bool (*)(const RtHostInput *)) g_sink = rt_input_host_push;
static _Atomic(void (*)(void)) g_on_close;

static NSWindow *g_window;                       // main queue only
static CAMetalLayer *g_layer;                    // main queue only
static id g_delegate;                            // main queue only (the window holds its delegate weakly)
static bool g_hidden;                            // main queue only: the window was created hidden
static bool g_cursor_hidden;                     // main queue only: we hid the cursor, and must show it again
static int32_t g_origin_x, g_origin_y;           // main queue only, set when the window is made
static double g_scale = 2.0;                     // runtime points per host point: set when the window is made (main queue only)

static void run_on_main(void (^block)(void))
{
    if (pthread_main_np())
        block();
    else
        dispatch_sync(dispatch_get_main_queue(), block);
}

// Makes the applied flag (and the hidden pointer) match: applied = wanted AND the window exists AND is not
// hidden AND is the key window.  The pointer is hidden and detached only on a change, so the hide and the
// unhide stay balanced.  Main queue only.
static void apply_relative(void)
{
    bool key = g_window != nil && g_window.isKeyWindow;
    bool applied = atomic_load(&g_relative) && g_window != nil && !g_hidden && key;
    if (applied == atomic_load(&g_applied))
        return;
    atomic_store(&g_applied, applied);
    if (applied && !g_cursor_hidden) {
        CGAssociateMouseAndMouseCursorPosition(false);
        [NSCursor hide];
        g_cursor_hidden = true;
    } else if (!applied && g_cursor_hidden) {
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
        g_cursor_hidden = false;
    }
}

// ---- the view -------------------------------------------------------------------------------------------------------

// Translates one host event and hands the results to the sink.  Runs on the first thread.
static void deliver(RtHostEvent *e)
{
    RtHostView v = { .origin_x = g_origin_x, .origin_y = g_origin_y, .scale = g_scale, .move_scale = 2.0,
                     .relative = atomic_load(&g_applied) };
    RtHostInput out[4];
    unsigned n = rt_host_event_translate(e, &v, out, 4);
    bool (*push)(const RtHostInput *) = atomic_load(&g_sink);
    if (push == NULL)
        return;
    for (unsigned i = 0; i < n; i++)
        push(&out[i]);
}

// The game's close request (rt_host_on_close).  Used by the window's close button and by Command-Q.
static void request_close(void)
{
    void (*fn)(void) = atomic_load(&g_on_close);
    if (fn != NULL)
        fn();
}

// Pushes the window's key status to the game (RT_HI_ACTIVE).  Main queue only.
static void push_active(bool down)
{
    RtHostInput in = { .kind = RT_HI_ACTIVE, .down = down ? 1 : 0 };
    bool (*push)(const RtHostInput *) = atomic_load(&g_sink);
    if (push != NULL)
        push(&in);
}

static void fill_place(RtHostEvent *e, NSView *view, NSEvent *event)
{
    NSPoint p = [view convertPoint:event.locationInWindow fromView:nil];
    e->x = p.x;
    e->y = p.y;
    e->flags = (uint32_t)event.modifierFlags;
    e->dx = event.deltaX;
    e->dy = event.deltaY;
}

// The fit of a picture of width_px by height_px into an area of area_w by area_h (points): the largest
// size of the picture's own shape that fits, centred in the area.  (x, y) is its place from the area's
// origin.  Plain C, so that the test can call it (runtime/tools/hostwin/test.m).
void fit_rect(double area_w, double area_h, uint32_t width_px, uint32_t height_px,
              double *x, double *y, double *w, double *h);

void fit_rect(double area_w, double area_h, uint32_t width_px, uint32_t height_px,
              double *x, double *y, double *w, double *h)
{
    double sx = area_w / width_px, sy = area_h / height_px;
    double s = sx < sy ? sx : sy;
    *w = width_px * s;
    *h = height_px * s;
    *x = (area_w - *w) / 2.0;
    *y = (area_h - *h) / 2.0;
}

// The full screen's window: a borderless window may not become key or main unless it says so.
@interface RtHostFullWindow : NSWindow
@end

@implementation RtHostFullWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

@interface RtHostContentView : NSView
@end

@implementation RtHostContentView

- (CALayer *)makeBackingLayer
{
    return [CAMetalLayer layer];
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

// Command-Q closes the window the way the close button does (no menu exists to do it).
- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    NSEventModifierFlags f = event.modifierFlags;
    if (event.keyCode == 0x0C && (f & NSEventModifierFlagCommand) != 0
        && (f & (NSEventModifierFlagControl | NSEventModifierFlagOption)) == 0) {
        request_close();
        return YES;
    }
    return NO;
}

- (void)keyDown:(NSEvent *)event
{
    RtHostEvent e = {0};
    e.kind = RT_HE_KEY_DOWN;
    e.key_code = event.keyCode;
    e.flags = (uint32_t)event.modifierFlags;
    deliver(&e);
}

- (void)keyUp:(NSEvent *)event
{
    RtHostEvent e = {0};
    e.kind = RT_HE_KEY_UP;
    e.key_code = event.keyCode;
    e.flags = (uint32_t)event.modifierFlags;
    deliver(&e);
}

- (void)flagsChanged:(NSEvent *)event
{
    RtHostEvent e = {0};
    e.kind = RT_HE_FLAGS;
    e.key_code = event.keyCode;
    e.flags = (uint32_t)event.modifierFlags;
    deliver(&e);
}

- (void)moved:(NSEvent *)event
{
    RtHostEvent e = {0};
    e.kind = RT_HE_MOUSE_MOVE;
    fill_place(&e, self, event);
    deliver(&e);
}

- (void)mouseMoved:(NSEvent *)event { [self moved:event]; }
- (void)mouseDragged:(NSEvent *)event { [self moved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self moved:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self moved:event]; }

- (void)pressed:(NSEvent *)event button:(uint8_t)button down:(bool)down
{
    RtHostEvent e = {0};
    e.kind = down ? RT_HE_BUTTON_DOWN : RT_HE_BUTTON_UP;
    e.button = button;
    fill_place(&e, self, event);
    deliver(&e);
}

- (void)mouseDown:(NSEvent *)event { [self pressed:event button:0 down:true]; }
- (void)mouseUp:(NSEvent *)event { [self pressed:event button:0 down:false]; }
- (void)rightMouseDown:(NSEvent *)event { [self pressed:event button:1 down:true]; }
- (void)rightMouseUp:(NSEvent *)event { [self pressed:event button:1 down:false]; }
- (void)otherMouseDown:(NSEvent *)event { [self pressed:event button:(uint8_t)event.buttonNumber down:true]; }
- (void)otherMouseUp:(NSEvent *)event { [self pressed:event button:(uint8_t)event.buttonNumber down:false]; }

- (void)scrollWheel:(NSEvent *)event
{
    RtHostEvent e = {0};
    e.kind = RT_HE_WHEEL;
    fill_place(&e, self, event);
    deliver(&e);
}

@end

// ---- the window's delegate --------------------------------------------------------------------------------------

@interface RtHostWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation RtHostWindowDelegate

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    request_close();
    return NO;   // the window stays; the runtime decides what a close means
}

- (void)windowDidResignKey:(NSNotification *)notification
{
    (void)notification;
    apply_relative();
    push_active(false);
}

- (void)windowDidBecomeKey:(NSNotification *)notification
{
    (void)notification;
    apply_relative();
    push_active(true);
}

@end

// ---- the public functions ----------------------------------------------------------------------------------------

void rt_host_main(void)
{
    @autoreleasepool {
        g_first_thread = pthread_self();
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyProhibited];
        [app run];
    }
}

void rt_host_quit(void)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp stop:nil];
        NSEvent *wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                           location:NSZeroPoint
                                      modifierFlags:0
                                          timestamp:0
                                       windowNumber:0
                                            context:nil
                                            subtype:0
                                              data1:0
                                              data2:0];
        [NSApp postEvent:wake atStart:NO];
    });
}

// The built-in display's screen: the first of [NSScreen screens] whose display is built in; nil if none is.
static NSScreen *builtin_screen(void)
{
    for (NSScreen *s in [NSScreen screens]) {
        NSNumber *n = s.deviceDescription[@"NSScreenNumber"];
        if (n != nil && CGDisplayIsBuiltin((CGDirectDisplayID)[n unsignedIntValue]))
            return s;
    }
    return nil;
}

void *rt_host_window_open(const RtHostWindowDesc *d)
{
    if (d == NULL)
        return NULL;
    if (pthread_main_np() || pthread_equal(pthread_self(), g_first_thread)) {
        fprintf(stderr, "rt_host_window_open: called on the first thread, which runs the event loop; "
                        "it must be called from another thread\n");
        abort();
    }
    __block void *result = NULL;
    RtHostWindowDesc desc = *d;
    dispatch_sync(dispatch_get_main_queue(), ^{
        if (g_window != nil) {
            result = (__bridge void *)g_layer;
            return;
        }
        NSScreen *screen = [NSScreen mainScreen];
        // The full screen: a borderless window over the main screen.  A hidden one keeps today's window.
        bool full = desc.fullscreen && !desc.hidden && screen != nil;
        // RT_HOST_SCREEN=builtin: the full screen goes to the built-in display; the main screen if there is none.
        NSScreen *fs = screen;
        if (full && desc.builtin_screen) {
            NSScreen *b = builtin_screen();
            if (b != nil)
                fs = b;
            else
                fprintf(stderr, "host window: RT_HOST_SCREEN=builtin, but no display is built in; using the main screen\n");
        }
        CGFloat cw = desc.width_px / 2.0, ch = desc.height_px / 2.0;
        NSRect content;
        NSWindowStyleMask style;
        if (full) {
            content = fs.frame;
            style = NSWindowStyleMaskBorderless;
        } else {
            // The origin is the client area's top left on the runtime's screen, which is top left; AppKit's is bottom left.
            CGFloat screen_h = screen != nil ? screen.frame.size.height : 0;
            content = NSMakeRect(desc.origin_x, screen_h - desc.origin_y - ch, cw, ch);
            style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
        }
        Class wclass = full ? [RtHostFullWindow class] : [NSWindow class];
        NSWindow *w = [[wclass alloc] initWithContentRect:content styleMask:style
                                                  backing:NSBackingStoreBuffered defer:NO];
        w.releasedWhenClosed = NO;
        w.title = [NSString stringWithUTF8String:desc.title != NULL ? desc.title : ""];
        w.acceptsMouseMovedEvents = YES;

        // The picture's place: the largest of its shape that fits the screen less the camera housing, centred.
        // A hidden full-screen window (for the test) is laid out as if the screen were 1512 by 982, no inset.
        NSRect viewFrame = NSMakeRect(0, 0, cw, ch);
        g_scale = 2.0;
        if (full) {
            CGFloat inset = 0;
            if (@available(macOS 12.0, *))
                inset = fs.safeAreaInsets.top;
            double x, y, fw, fh;
            fit_rect(content.size.width, content.size.height - inset, desc.width_px, desc.height_px, &x, &y, &fw, &fh);
            viewFrame = NSMakeRect(x, y, fw, fh);
            g_scale = desc.width_px / fw;
        } else if (desc.fullscreen && desc.hidden) {
            double x, y, fw, fh;
            fit_rect(1512, 982, desc.width_px, desc.height_px, &x, &y, &fw, &fh);
            g_scale = desc.width_px / fw;
        }

        RtHostContentView *view = [[RtHostContentView alloc] initWithFrame:viewFrame];
        view.wantsLayer = YES;
        CAMetalLayer *layer = (CAMetalLayer *)view.layer;
        layer.contentsScale = 2.0;
        if (full) {
            NSView *black = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, content.size.width, content.size.height)];
            black.wantsLayer = YES;
            black.layer.backgroundColor = [NSColor blackColor].CGColor;
            [black addSubview:view];
            w.backgroundColor = [NSColor blackColor];
            w.movable = NO;
            w.contentView = black;
        } else {
            w.contentView = view;
        }

        RtHostWindowDelegate *delegate = [[RtHostWindowDelegate alloc] init];
        w.delegate = delegate;

        g_window = w;
        g_layer = layer;
        g_delegate = delegate;
        g_hidden = desc.hidden;
        g_origin_x = desc.origin_x;
        g_origin_y = desc.origin_y;

        if (!desc.hidden) {
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            if (full)
                NSApp.presentationOptions = NSApplicationPresentationHideDock | NSApplicationPresentationHideMenuBar;
            [NSApp activateIgnoringOtherApps:YES];
            [w makeFirstResponder:view];
            [w makeKeyAndOrderFront:nil];
        }
        result = (__bridge void *)layer;
    });
    return result;
}

void rt_host_set_relative(bool on)
{
    atomic_store(&g_relative, on);
    run_on_main(^{ apply_relative(); });   // a hidden window or one not key: the real cursor is never touched
}

double rt_host_window_scale(void)
{
    __block double s = 0;
    run_on_main(^{ s = g_scale; });
    return s;
}

void rt_host_set_sink(bool (*push)(const RtHostInput *e))
{
    atomic_store(&g_sink, push);
}

void rt_host_on_close(void (*fn)(void))
{
    atomic_store(&g_on_close, fn);
}
