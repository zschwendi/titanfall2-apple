#import "GameModule.h"
#include <dlfcn.h>

#ifndef TF2_DXMT_FINGERPRINT
#error "build with -DTF2_DXMT_FINGERPRINT=\"...\" from tools/ios/dxmt-fingerprint.sh"
#endif

const struct TF2Payload *tf2_game;

BOOL TF2LoadGameModule(NSString **problem) {
    if (tf2_game) return YES;
    NSString *path = [NSBundle.mainBundle.privateFrameworksPath stringByAppendingPathComponent:@TF2_PAYLOAD_PATH];
    if (![NSFileManager.defaultManager fileExistsAtPath:path]) {
        *problem = @"Game module not installed. Build it on your Mac from your own game files (see the README).";
        return NO;
    }
    void *handle = dlopen(path.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char *error = dlerror();
        NSLog(@"TF2 game module: %s", error ? error : "dlopen failed");
        *problem = @"The game module could not be loaded. Rebuild it and sign the whole app with your own team.";
        return NO;
    }
    const struct TF2Payload *(*entry)(void) = (const struct TF2Payload *(*)(void))dlsym(handle, TF2_PAYLOAD_SYMBOL);
    const struct TF2Payload *payload = entry ? entry() : NULL;
    if (!payload || payload->abi != TF2_PAYLOAD_ABI || payload->size < sizeof(struct TF2Payload)) {
        NSLog(@"TF2 game module: interface %u, this app needs %u", payload ? payload->abi : 0, TF2_PAYLOAD_ABI);
        *problem = @"The game module was built for a different version of this app. Rebuild it from the matching release.";
        return NO;
    }
    if (!payload->dxmt || strcmp(payload->dxmt, TF2_DXMT_FINGERPRINT) != 0) {
        NSLog(@"TF2 game module: built for DXMT %s, this app has %s", payload->dxmt ? payload->dxmt : "?", TF2_DXMT_FINGERPRINT);
        *problem = @"The game module was built for a different renderer. Rebuild it from the matching release.";
        return NO;
    }
    NSLog(@"TF2 game module loaded: %s", payload->source ? payload->source : "?");
    tf2_game = payload;
    return YES;
}
