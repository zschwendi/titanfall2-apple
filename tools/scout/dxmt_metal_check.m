// dxmt_metal_check.m -- what DXMT's native build asks of Metal, checked on this Mac (finding 35).
//
// Build + run (needs only Xcode's clang; installs nothing):
//   clang -fobjc-arc -Wno-deprecated-declarations -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics \
//         tools/scout/dxmt_metal_check.m -o /tmp/dxmt_metal_check && /tmp/dxmt_metal_check
//
// 1. The GPU facts that DXMT's device and feature-level code read:
//      third_party/dxmt/src/d3d11/d3d11.cpp:59            supportsFamily(Apple7) -> feature level 11_1, else 11_0
//      third_party/dxmt/src/dxmt/dxmt_device.cpp:45       supportsFamily(Apple7) else "Experimental non-Apple GPU support"
//      third_party/dxmt/src/d3d11/d3d11_pipeline_ts.cpp:55,136   Apple9 / Apple7 for tessellation and geometry via mesh shaders
//      third_party/dxmt/src/dxgi/dxgi_adapter.cpp:155-186 name, unified memory, recommendedMaxWorkingSetSize
// 2. Whether the non-public Apple interfaces that DXMT uses by default exist on this macOS:
//      -[MTLRenderPipelineDescriptor setLogicOperationEnabled:] / setLogicOperation:   (winemetal_unix.c:482-516, 548, 615)
//      -[MTLMeshRenderPipelineDescriptor ...] same selectors
//      class MTLHUDService (macOS 27+) / _CADeveloperHUDProperties (earlier)            (winemetal_unix.c:1533-1550)
//      C symbols MTLSetShaderCachePath / MTLGetShaderCachePath                          (winemetal_unix.c / cache.c:224-232)
//      C symbol bootstrap_register2                                                     (winemetal_unix.c:2737)
// Nothing here creates a window, a queue or a pipeline.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <dlfcn.h>
#include <objc/runtime.h>

int main(void) {
  @autoreleasepool {
    NSOperatingSystemVersion v = [NSProcessInfo processInfo].operatingSystemVersion;
    printf("macOS %ld.%ld.%ld (DXMT picks Metal 3.2 when major >= 15, else 3.1: dxmt_device.cpp:39-45)\n",
           (long)v.majorVersion, (long)v.minorVersion, (long)v.patchVersion);

    NSArray<id<MTLDevice>> *devs = MTLCopyAllDevices();
    printf("MTLCopyAllDevices: %lu device(s)\n", (unsigned long)devs.count);
    for (id<MTLDevice> d in devs) {
      printf("  name=\"%s\" unifiedMemory=%d lowPower=%d recommendedMaxWorkingSetSize=%llu MiB registryID=0x%llx\n",
             d.name.UTF8String, d.hasUnifiedMemory, d.isLowPower,
             (unsigned long long)(d.recommendedMaxWorkingSetSize >> 20), d.registryID);
      printf("  supportsFamily: Apple7=%d Apple8=%d Apple9=%d Mac2=%d Metal3=%d\n",
             [d supportsFamily:MTLGPUFamilyApple7], [d supportsFamily:MTLGPUFamilyApple8],
             [d supportsFamily:MTLGPUFamilyApple9], [d supportsFamily:MTLGPUFamilyMac2],
             [d supportsFamily:MTLGPUFamilyMetal3]);
      printf("  supportsBCTextureCompression=%d supports32BitFloatFiltering=%d\n",
             d.supportsBCTextureCompression, d.supports32BitFloatFiltering);
    }

    // DXMT calls these on instances it gets from [[MTLRenderPipelineDescriptor alloc] init], which may be a private
    // subclass of the public class, so ask an instance (creating a descriptor needs no device).
    MTLRenderPipelineDescriptor *rpd = [[MTLRenderPipelineDescriptor alloc] init];
    MTLMeshRenderPipelineDescriptor *mpd = [[MTLMeshRenderPipelineDescriptor alloc] init];
    printf("MTLRenderPipelineDescriptor instance (%s) responds to setLogicOperationEnabled:=%d setLogicOperation:=%d\n",
           class_getName(object_getClass(rpd)), [rpd respondsToSelector:@selector(setLogicOperationEnabled:)],
           [rpd respondsToSelector:@selector(setLogicOperation:)]);
    printf("MTLMeshRenderPipelineDescriptor instance (%s) responds to setLogicOperationEnabled:=%d setLogicOperation:=%d\n",
           class_getName(object_getClass(mpd)), [mpd respondsToSelector:@selector(setLogicOperationEnabled:)],
           [mpd respondsToSelector:@selector(setLogicOperation:)]);
    printf("class MTLHUDService=%p  class _CADeveloperHUDProperties=%p\n",
           (__bridge void *)objc_lookUpClass("MTLHUDService"),
           (__bridge void *)objc_lookUpClass("_CADeveloperHUDProperties"));
    void *self = dlopen(NULL, RTLD_NOW);
    printf("dlsym MTLSetShaderCachePath=%p MTLGetShaderCachePath=%p bootstrap_register2=%p\n",
           dlsym(self, "MTLSetShaderCachePath"), dlsym(self, "MTLGetShaderCachePath"), dlsym(self, "bootstrap_register2"));
  }
  return 0;
}
