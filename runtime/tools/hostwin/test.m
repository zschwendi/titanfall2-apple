// test.m -- the host window's test (rt/host.h, runtime/host/window.m).  It shows nothing: the window is
// hidden, the activation policy stays Prohibited, the cursor is never hidden or moved.  The events are
// synthetic, handed straight to the view's handlers on the main queue.
/* rt/wintypes.h (through rt/input.h) typedefs BOOL as a 32-bit integer, and Objective-C's BOOL is bool:
 * the two cannot share a translation unit.  The runtime's headers are read first, with the Windows name
 * renamed, and AppKit after; this file uses no BOOL. */
#define BOOL RT_WIN_BOOL
#include "rt/host.h"
#include "rt/hostevent.h"
#include "rt/input.h"
#undef BOOL
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/QuartzCore.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

// The fit of a picture into an area (runtime/host/window.m); the test calls it directly.
void fit_rect(double area_w, double area_h, uint32_t width_px, uint32_t height_px,
              double *x, double *y, double *w, double *h);

static int g_passed, g_failed;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (cond) {                                                                 \
            g_passed++;                                                             \
        } else {                                                                    \
            g_failed++;                                                             \
            fprintf(stderr, "hostwin-test: FAILED line %d: %s\n", __LINE__, #cond); \
        }                                                                           \
    } while (0)

// The sink: what the window hands on.  Written on the main queue; read by the driver after a dispatch_sync.
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static RtHostInput g_got[64];
static unsigned g_ngot;

static bool sink(const RtHostInput *e)
{
    pthread_mutex_lock(&g_mu);
    if (g_ngot < 64)
        g_got[g_ngot] = *e;
    g_ngot++;
    pthread_mutex_unlock(&g_mu);
    return true;
}

static unsigned count(void)
{
    pthread_mutex_lock(&g_mu);
    unsigned n = g_ngot;
    pthread_mutex_unlock(&g_mu);
    return n;
}

static RtHostInput got(unsigned i)
{
    pthread_mutex_lock(&g_mu);
    RtHostInput e = g_got[i];
    pthread_mutex_unlock(&g_mu);
    return e;
}

static bool near(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }

// The fit: the largest of the picture's shape that fits the area, centred.  Pure C: no window.
static void fit_checks(void)
{
    double x, y, w, h;
    fit_rect(1512, 950, 1920, 1080, &x, &y, &w, &h);       // 16:9 limited by the width
    CHECK(near(w, 1512) && near(h, 850.5) && near(x, 0) && near(y, 49.75));
    fit_rect(1000, 950, 1920, 1080, &x, &y, &w, &h);       // 16:9 limited by the width, narrower area
    CHECK(near(w, 1000) && near(h, 562.5) && near(x, 0) && near(y, 193.75));
    fit_rect(1512, 950, 1440, 1080, &x, &y, &w, &h);       // 4:3 limited by the height
    CHECK(near(h, 950) && near(w, 1266.0 + 2.0 / 3.0) && near(x, (1512 - w) / 2) && near(y, 0));
}

static NSWindow *find_window(void)
{
    for (NSWindow *w in [NSApp windows])
        if ([w.title isEqualToString:@"hostwin-test"])
            return w;
    return nil;
}

// The full screen, hidden, in the child process (one window per process): the scale is the fit's
// on a 1512 by 982 screen, and the window is today's hidden one.
static void *fs_driver(void *arg)
{
    (void)arg;
    RtHostWindowDesc d = { .width_px = 1920, .height_px = 1080, .origin_x = 5, .origin_y = 28,
                           .title = "hostwin-test", .hidden = true, .fullscreen = true };
    void *layer = rt_host_window_open(&d);
    CHECK(layer != NULL);
    CHECK(near(rt_host_window_scale(), 1920.0 / 1512.0));
    __block NSSize s = NSZeroSize;
    dispatch_sync(dispatch_get_main_queue(), ^{
        NSWindow *w = find_window();
        CHECK(w != nil && w.isVisible == NO);
        if (w != nil) s = w.contentView.frame.size;
    });
    CHECK(s.width == 960.0 && s.height == 540.0);
    rt_host_quit();
    return NULL;
}

static void *driver(void *arg)
{
    (void)arg;
    RtHostWindowDesc d = { .width_px = 1920, .height_px = 1080, .origin_x = 5, .origin_y = 28,
                           .title = "hostwin-test", .hidden = true };

    // 1. the window, hidden; a second call returns the same layer.
    void *layer = rt_host_window_open(&d);
    CHECK(layer != NULL);
    CHECK(rt_host_window_open(&d) == layer);

    // 2. the layer, the window's state, the view's size, the policy.
    __block NSWindow *w = nil;
    dispatch_sync(dispatch_get_main_queue(), ^{
        w = find_window();
        CHECK(w != nil);
        CALayer *l = (__bridge CALayer *)layer;
        CHECK([l isKindOfClass:[CAMetalLayer class]]);
        CHECK(l == w.contentView.layer);
        CHECK(l.contentsScale == 2.0);
        CHECK(w.isVisible == NO);
        NSSize s = w.contentView.frame.size;
        CHECK(s.width == 960.0 && s.height == 540.0);
        CHECK([NSApp activationPolicy] == NSApplicationActivationPolicyProhibited);
    });
    if (w == nil) {
        rt_host_quit();
        return NULL;
    }

    // 3. the letter A, down and up, straight to the view's handlers.
    NSView *view = w.contentView;
    unsigned before = count();
    dispatch_sync(dispatch_get_main_queue(), ^{
        NSEvent *down = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
                                        timestamp:0 windowNumber:w.windowNumber context:nil
                                        characters:@"a" charactersIgnoringModifiers:@"a"
                                        isARepeat:NO keyCode:0];
        [view keyDown:down];
    });
    CHECK(count() == before + 1);
    if (count() == before + 1) {
        RtHostInput e = got(before);
        CHECK(e.kind == RT_HI_KEY && e.a == 0x41 && e.down == 1);
    }
    before = count();
    dispatch_sync(dispatch_get_main_queue(), ^{
        NSEvent *up = [NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint modifierFlags:0
                                      timestamp:0 windowNumber:w.windowNumber context:nil
                                      characters:@"a" charactersIgnoringModifiers:@"a"
                                      isARepeat:NO keyCode:0];
        [view keyUp:up];
    });
    CHECK(count() == before + 1);
    if (count() == before + 1) {
        RtHostInput e = got(before);
        CHECK(e.kind == RT_HI_KEY && e.a == 0x41 && e.down == 0);
    }

    // 4. a left mouse down at the view's point (100, 50): a move to the place, then the button.
    before = count();
    dispatch_sync(dispatch_get_main_queue(), ^{
        NSPoint at = [view convertPoint:NSMakePoint(100, 50) toView:nil];
        NSEvent *md = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:at modifierFlags:0
                                        timestamp:0 windowNumber:w.windowNumber context:nil
                                      eventNumber:1 clickCount:1 pressure:1.0];
        [view mouseDown:md];
    });
    CHECK(count() == before + 2);
    if (count() == before + 2) {
        RtHostInput m = got(before), b = got(before + 1);
        CHECK(m.kind == RT_HI_MOVETO && m.a == 5 + 200 && m.b == 28 + 100);
        CHECK(b.kind == RT_HI_BUTTON && b.a == 0 && b.down == 1);
    }

    // 5. relative on a hidden window: the game wants the mouse, but it is not applied (the window is not shown
    // and not key), so the move is still absolute (MOVETO) and the real cursor stays visible.
    /* The owner's own session may already have the cursor hidden (a game running in front), so the
     * check is that the call left the real cursor as it was, not that it is visible. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const bool cursor_before = CGCursorIsVisible();
#pragma clang diagnostic pop
    rt_host_set_relative(true);
    before = count();
    dispatch_sync(dispatch_get_main_queue(), ^{
        CGEventRef ce = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, CGPointMake(0, 0), kCGMouseButtonLeft);
        CGEventSetIntegerValueField(ce, kCGMouseEventDeltaX, 3);
        CGEventSetIntegerValueField(ce, kCGMouseEventDeltaY, -2);
        NSEvent *mv = [NSEvent eventWithCGEvent:ce];
        CFRelease(ce);
        [view mouseMoved:mv];
    });
    CHECK(count() == before + 1);
    if (count() == before + 1) {
        RtHostInput e = got(before);
        CHECK(e.kind == RT_HI_MOVETO);
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CHECK(CGCursorIsVisible() == cursor_before);
#pragma clang diagnostic pop

    // 6. the Command key's own code gives nothing.
    before = count();
    dispatch_sync(dispatch_get_main_queue(), ^{
        NSEvent *cmd = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
                                       timestamp:0 windowNumber:w.windowNumber context:nil
                                       characters:@"" charactersIgnoringModifiers:@""
                                       isARepeat:NO keyCode:0x37];
        [view keyDown:cmd];
    });
    CHECK(count() == before);

    // 7. the end.
    rt_host_quit();
    return NULL;
}

int main(void)
{
    alarm(30);
    fit_checks();
    // The full-screen case needs a window of its own: a child process, forked before any thread or AppKit.
    pid_t child = fork();
    const bool is_child = child == 0;
    if (is_child)
        alarm(30);
    rt_host_set_sink(sink);
    pthread_t t;
    if (pthread_create(&t, NULL, is_child ? fs_driver : driver, NULL) != 0) {
        fprintf(stderr, "hostwin-test: FAILED: no driver thread\n");
        return 1;
    }
    rt_host_main();
    pthread_join(t, NULL);
    if (is_child) {
        if (g_failed == 0) {
            printf("hostwin-test (full screen, hidden): %d checks passed\n", g_passed);
            fflush(stdout);
            _exit(0);
        }
        printf("hostwin-test (full screen, hidden): %d checks passed, %d FAILED\n", g_passed, g_failed);
        fflush(stdout);
        _exit(1);
    }
    if (child > 0) {
        int status = 0;
        waitpid(child, &status, 0);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    } else {
        g_failed++;
        fprintf(stderr, "hostwin-test: FAILED: no child for the full-screen case\n");
    }
    if (g_failed == 0) {
        printf("hostwin-test: %d checks passed\n", g_passed);
        return 0;
    }
    printf("hostwin-test: %d checks passed, %d FAILED\n", g_passed, g_failed);
    return 1;
}
