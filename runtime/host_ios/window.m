#define BOOL RT_WIN_BOOL
#include "rt/host.h"
#include "rt/log.h"
#undef BOOL
#include "rt/host_ios.h"
#include "rt/gamepad.h"
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AVFAudio/AVFAudio.h>
#include <stdatomic.h>

static UIView *game_view;
static CAMetalLayer *game_layer;
static _Atomic double window_scale = 1;
static _Atomic(bool (*)(const RtHostInput *)) input_sink = rt_input_host_push;
static _Atomic(void (*)(void)) on_close;
static uint32_t render_width, render_height;

@interface RTGameMetalView : UIView
@end
@implementation RTGameMetalView
+ (Class)layerClass { return CAMetalLayer.class; }
- (void)layoutSubviews {
    [super layoutSubviews];
    /* Keep the game aspect ratio when the scene/view resizes. UIKit remains
     * responsible for laying out this view; CAMetalLayer scales the drawable. */
    if (render_width && self.bounds.size.width > 0)
        atomic_store(&window_scale, render_width / self.bounds.size.width);
}
@end

UIView *rt_host_ios_create_view(void) {
    NSCAssert(NSThread.isMainThread, @"Create the game view on the main thread");
    if (!game_view) {
        game_view=[[RTGameMetalView alloc] initWithFrame:CGRectZero];
        game_view.backgroundColor=UIColor.blackColor;
        game_view.layer.contentsGravity=kCAGravityResizeAspect;
        game_layer=(CAMetalLayer *)game_view.layer;
        game_layer.opaque=YES;
        rt_apple_gamepads_start();
    }
    return game_view;
}

void rt_host_ios_set_active(bool value) {
    NSCAssert(NSThread.isMainThread, @"Forward scene activity on the main thread");
    rt_apple_gamepads_set_active(value);
    bool (*sink)(const RtHostInput *)=atomic_load(&input_sink);
    RtHostInput input={ .kind=RT_HI_ACTIVE, .down=value };
    if (sink) sink(&input);
    NSError *error=nil;
    AVAudioSession *session=AVAudioSession.sharedInstance;
    if (value) {
        if (![session setCategory:AVAudioSessionCategoryPlayback error:&error] ||
            ![session setActive:YES error:&error])
            NSLog(@"Titanfall audio session activation: %@", error);
    } else {
        [session setActive:NO withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:&error];
    }
}

void *rt_host_window_open(const RtHostWindowDesc *desc) {
    if (!desc || !desc->width_px || !desc->height_px) return NULL;
    if (NSThread.isMainThread) rt_fatal("UIKit game startup must run on a guest thread");
    RtHostWindowDesc copy=*desc;
    __block void *result=NULL;
    dispatch_sync(dispatch_get_main_queue(), ^{
        if (!game_view) return; /* The real application must attach its view first. */
        render_width=copy.width_px; render_height=copy.height_px;
        game_layer.drawableSize=CGSizeMake(render_width, render_height);
        game_view.hidden=copy.hidden;
        [game_view setNeedsLayout];
        result=(__bridge void *)game_layer;
    });
    return result;
}

void rt_host_main(void) {
    rt_fatal("UIKit owns the main event loop: create the game view and launch the guest on a background thread");
}
void rt_host_quit(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        rt_host_ios_set_active(false);
        rt_apple_gamepads_stop();
        void (*callback)(void)=atomic_load(&on_close);
        if (callback) callback();
    });
}
void rt_host_set_relative(bool on) { (void)on; /* DualSense is independent of pointer capture. */ }
void rt_host_set_sink(bool (*sink)(const RtHostInput *)) { atomic_store(&input_sink, sink); }
void rt_host_on_close(void (*fn)(void)) { atomic_store(&on_close, fn); }
double rt_host_window_scale(void) { return atomic_load(&window_scale); }
