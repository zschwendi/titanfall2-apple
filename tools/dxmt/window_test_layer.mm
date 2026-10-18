// window_test_layer.mm -- see window_test_layer.h.  Built with -fobjc-arc.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include <mach/mach.h>
#include <mutex>
#include <pthread.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

#include "window_test_layer.h"

double wl_now(void) { return CACurrentMediaTime(); }
int wl_is_main_thread(void) { return [NSThread isMainThread] ? 1 : 0; }

void wl_set_accessory_app(void) {
  @autoreleasepool {
    [[NSApplication sharedApplication] setActivationPolicy:NSApplicationActivationPolicyAccessory];
  }
}

uint32_t wl_window_display_id(void *nswindow) {
  @autoreleasepool {
    NSWindow *window = (__bridge NSWindow *)nswindow;
    NSNumber *number = window.screen.deviceDescription[@"NSScreenNumber"];
    return number ? number.unsignedIntValue : 0;
  }
}

// ---- the probe ------------------------------------------------------------------------------------------------------

static std::mutex g_mutex;
static std::vector<WlDrawable> g_records;
static const size_t kKept = 4;  // a CAMetalLayer cycles through at most 3 drawables
static struct {
  uint64_t index;
  id<MTLTexture> texture;
} g_kept[kKept];

@interface WlProbeLayer : CAMetalLayer
@end

@implementation WlProbeLayer
- (id<CAMetalDrawable>)nextDrawable {
  WlDrawable record = {};
  record.t_called = CACurrentMediaTime();
  id<CAMetalDrawable> drawable = [super nextDrawable];
  record.t_returned = CACurrentMediaTime();
  record.got_drawable = drawable != nil;
  record.on_main_thread = [NSThread isMainThread] ? 1 : 0;
  pthread_getname_np(pthread_self(), record.thread_name, sizeof(record.thread_name));
  id<MTLTexture> texture = drawable.texture;
  if (texture) {
    record.width = (uint32_t)texture.width;
    record.height = (uint32_t)texture.height;
    record.pixel_format = (uint32_t)texture.pixelFormat;
  }

  uint64_t index;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    index = g_records.size() + 1;
    record.index = index;
    g_records.push_back(record);
    g_kept[index % kKept].index = index;
    g_kept[index % kKept].texture = texture;
  }

  // The drawable itself is not kept: holding it would keep it out of the layer's pool.
  [drawable addPresentedHandler:^(id<MTLDrawable> shown) {
    double when = shown.presentedTime;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_records[index - 1].t_presented = when;
    g_records[index - 1].handler_ran = 1;
  }];
  return drawable;
}
@end

int wl_probe_install(void *metal_layer) {
  CAMetalLayer *layer = (__bridge CAMetalLayer *)metal_layer;
  if (!layer || object_getClass(layer) != [CAMetalLayer class])
    return 0;
  object_setClass(layer, [WlProbeLayer class]);
  return object_getClass(layer) == [WlProbeLayer class];
}

uint64_t wl_probe_count(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_records.size();
}

int wl_probe_get(uint64_t index, WlDrawable *out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (index == 0 || index > g_records.size())
    return 0;
  *out = g_records[index - 1];
  return 1;
}

int wl_probe_read_rgba(uint64_t index, uint8_t *rgba, uint32_t width, uint32_t height) {
  @autoreleasepool {
    id<MTLTexture> texture = nil;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_kept[index % kKept].index == index)
        texture = g_kept[index % kKept].texture;
    }
    if (!texture || texture.width != width || texture.height != height)
      return 0;
    MTLPixelFormat format = texture.pixelFormat;
    if (format != MTLPixelFormatBGRA8Unorm && format != MTLPixelFormatBGRA8Unorm_sRGB && format != MTLPixelFormatRGBA8Unorm)
      return 0;

    // A queue of our own on the same device.  The caller has already waited for DXMT's frame to finish on the GPU.
    static id<MTLCommandQueue> queue = nil;
    if (!queue || queue.device != texture.device)
      queue = [texture.device newCommandQueue];
    NSUInteger bytes = (NSUInteger)width * height * 4;
    id<MTLBuffer> buffer = [texture.device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> commands = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    [blit copyFromTexture:texture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(width, height, 1)
                        toBuffer:buffer
               destinationOffset:0
          destinationBytesPerRow:(NSUInteger)width * 4
        destinationBytesPerImage:bytes];
    [blit endEncoding];
    [commands commit];
    [commands waitUntilCompleted];
    if (commands.status != MTLCommandBufferStatusCompleted)
      return 0;

    const uint8_t *source = (const uint8_t *)buffer.contents;
    if (format == MTLPixelFormatRGBA8Unorm) {
      memcpy(rgba, source, bytes);
    } else {
      for (NSUInteger i = 0; i < bytes; i += 4) {
        rgba[i + 0] = source[i + 2];
        rgba[i + 1] = source[i + 1];
        rgba[i + 2] = source[i + 0];
        rgba[i + 3] = source[i + 3];
      }
    }
    return 1;
  }
}

void wl_probe_forget(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (size_t i = 0; i < kKept; i++) {
    g_kept[i].index = 0;
    g_kept[i].texture = nil;
  }
}

// ---- what DXMT asks Metal to do with each drawable ------------------------------------------------------------------

static std::vector<WlPresent> g_presents;

static void record_present(int kind, double argument) {
  WlPresent present = {kind, argument, CACurrentMediaTime(), {0}};
  pthread_getname_np(pthread_self(), present.thread_name, sizeof(present.thread_name));
  std::lock_guard<std::mutex> lock(g_mutex);
  g_presents.push_back(present);
}

int wl_present_hook_install(void) {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> queue = [device newCommandQueue];
    Class cls = object_getClass([queue commandBuffer]);
    Method plain = class_getInstanceMethod(cls, @selector(presentDrawable:));
    Method after = class_getInstanceMethod(cls, @selector(presentDrawable:afterMinimumDuration:));
    Method at = class_getInstanceMethod(cls, @selector(presentDrawable:atTime:));
    if (!plain || !after || !at)
      return 0;
    typedef void (*PlainFn)(id, SEL, id);
    typedef void (*TimedFn)(id, SEL, id, CFTimeInterval);
    PlainFn plain_original = (PlainFn)method_getImplementation(plain);
    TimedFn after_original = (TimedFn)method_getImplementation(after);
    TimedFn at_original = (TimedFn)method_getImplementation(at);
    method_setImplementation(plain, imp_implementationWithBlock(^(id self, id drawable) {
      record_present(0, 0);
      plain_original(self, @selector(presentDrawable:), drawable);
    }));
    method_setImplementation(after, imp_implementationWithBlock(^(id self, id drawable, CFTimeInterval duration) {
      record_present(1, duration);
      after_original(self, @selector(presentDrawable:afterMinimumDuration:), drawable, duration);
    }));
    method_setImplementation(at, imp_implementationWithBlock(^(id self, id drawable, CFTimeInterval time) {
      record_present(2, time);
      at_original(self, @selector(presentDrawable:atTime:), drawable, time);
    }));
    return 1;
  }
}

uint64_t wl_present_count(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_presents.size();
}

int wl_present_get(uint64_t index, WlPresent *out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (index == 0 || index > g_presents.size())
    return 0;
  *out = g_presents[index - 1];
  return 1;
}

// ---- autorelease pools, display, window -----------------------------------------------------------------------------

extern "C" void *objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void *pool);

extern "C" void _objc_autoreleasePoolPrint(void);

void wl_pool_print(void) { _objc_autoreleasePoolPrint(); }
void *wl_pool_push(void) { return objc_autoreleasePoolPush(); }
void wl_pool_pop(void *pool) { objc_autoreleasePoolPop(pool); }

int wl_display_asleep(uint32_t display_id) { return CGDisplayIsAsleep(display_id) ? 1 : 0; }

int wl_window_visible(void *nswindow) {
  @autoreleasepool {
    NSWindow *window = (__bridge NSWindow *)nswindow;
    return (window.occlusionState & NSWindowOcclusionStateVisible) ? 1 : 0;
  }
}

int wl_screen_locked(void) {
  @autoreleasepool {
    NSDictionary *session = CFBridgingRelease(CGSessionCopyCurrentDictionary());
    return [session[@"CGSSessionScreenIsLocked"] boolValue] ? 1 : 0;
  }
}

long wl_retain_count(void *object) { return object ? (long)CFGetRetainCount((CFTypeRef)object) : 0; }

// ---- a view and layer of our own --------------------------------------------------------------------------------------

@interface WlOwnView : NSView
@property double pixelsPerPoint;
@end

@implementation WlOwnView
- (instancetype)initWithFrame:(NSRect)frame pixelsPerPoint:(double)scale {
  if ((self = [super initWithFrame:frame])) {
    _pixelsPerPoint = scale;
    self.wantsLayer = YES;
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  }
  return self;
}
- (CALayer *)makeBackingLayer {
  WlProbeLayer *layer = [WlProbeLayer layer];
  layer.contentsScale = _pixelsPerPoint;
  return layer;
}
- (BOOL)wantsUpdateLayer {
  return YES;
}
- (void)updateLayer {
}
// mouse events go to the view underneath (SDL's)
- (NSView *)hitTest:(NSPoint)point {
  return nil;
}
// the scale is ours to decide, not the window's
- (BOOL)layer:(CALayer *)layer shouldInheritContentsScale:(CGFloat)newScale fromWindow:(NSWindow *)window {
  return NO;
}
@end

void *wl_ownview_create(void *nswindow, double pixels_per_point, void **out_view) {
  @autoreleasepool {
    NSWindow *window = (__bridge NSWindow *)nswindow;
    NSView *content = window.contentView;
    if (!content)
      return NULL;
    double scale = pixels_per_point > 0 ? pixels_per_point : window.backingScaleFactor;
    WlOwnView *view = [[WlOwnView alloc] initWithFrame:content.bounds pixelsPerPoint:scale];
    [content addSubview:view];
    CALayer *layer = view.layer;
    if (![layer isKindOfClass:[CAMetalLayer class]]) {
      [view removeFromSuperview];
      return NULL;
    }
    *out_view = (__bridge_retained void *)view;
    return (__bridge void *)layer;
  }
}

void wl_ownview_destroy(void *view_pointer) {
  @autoreleasepool {
    WlOwnView *view = (__bridge_transfer WlOwnView *)view_pointer;
    [view removeFromSuperview];
  }
}

// ---- facts ------------------------------------------------------------------------------------------------------------

void wl_layer_info(void *metal_layer, WlLayerInfo *out) {
  @autoreleasepool {
    memset(out, 0, sizeof(*out));
    CAMetalLayer *layer = (__bridge CAMetalLayer *)metal_layer;
    if (!layer)
      return;
    strlcpy(out->class_name, object_getClassName(layer), sizeof(out->class_name));
    out->bounds_w = layer.bounds.size.width;
    out->bounds_h = layer.bounds.size.height;
    out->contents_scale = layer.contentsScale;
    out->drawable_w = layer.drawableSize.width;
    out->drawable_h = layer.drawableSize.height;
    out->pixel_format = (uint32_t)layer.pixelFormat;
    out->opaque = layer.opaque;
    out->framebuffer_only = layer.framebufferOnly;
    out->display_sync = layer.displaySyncEnabled;
    out->has_device = layer.device != nil;
    out->delegate_is_view = [layer.delegate isKindOfClass:[NSView class]];
    out->wants_edr = layer.wantsExtendedDynamicRangeContent;
    out->maximum_drawable_count = (uint32_t)layer.maximumDrawableCount;
    CGColorSpaceRef space = layer.colorspace;
    NSString *name = space ? CFBridgingRelease(CGColorSpaceCopyName(space)) : nil;
    strlcpy(out->colorspace, name ? name.UTF8String : "(none)", sizeof(out->colorspace));
  }
}

void wl_layer_set_maximum_drawable_count(void *metal_layer, unsigned count) {
  ((__bridge CAMetalLayer *)metal_layer).maximumDrawableCount = count;
}

void wl_thread_names(char *out, unsigned long size) {
  out[0] = 0;
  thread_act_array_t threads = NULL;
  mach_msg_type_number_t count = 0;
  if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
    return;
  std::vector<std::pair<std::string, int>> names;
  for (mach_msg_type_number_t i = 0; i < count; i++) {
    char name[64] = "";
    pthread_t thread = pthread_from_mach_thread_np(threads[i]);
    if (thread)
      pthread_getname_np(thread, name, sizeof(name));
    if (thread && pthread_main_np() && pthread_equal(thread, pthread_self()) && !name[0])
      strlcpy(name, "(main thread)", sizeof(name));
    std::string key = name[0] ? name : "(unnamed)";
    bool found = false;
    for (auto &entry : names)
      if (entry.first == key) {
        entry.second++;
        found = true;
      }
    if (!found)
      names.push_back({key, 1});
    mach_port_deallocate(mach_task_self(), threads[i]);
  }
  vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(thread_act_t));
  size_t used = 0;
  for (auto &entry : names) {
    int n = snprintf(out + used, size - used, "  %d x %s\n", entry.second, entry.first.c_str());
    if (n < 0 || used + (size_t)n >= size)
      break;
    used += (size_t)n;
  }
}

@interface WlWeak : NSObject
@property(weak) id object;
@end
@implementation WlWeak
@end

void *wl_weak_new(void *object) {
  WlWeak *weak = [WlWeak new];
  weak.object = (__bridge id)object;
  return (__bridge_retained void *)weak;
}

int wl_weak_alive(void *weak_pointer) {
  @autoreleasepool {
    WlWeak *weak = (__bridge WlWeak *)weak_pointer;
    return weak.object != nil;
  }
}
