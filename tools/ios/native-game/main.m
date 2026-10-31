#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#import <AVFAudio/AVFAudio.h>
#include <os/proc.h>
#import "TFNative-Swift.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifndef TF_ASSET_STAGE_ONLY
#include "rt/host_ios.h"
#include "dxmt_native_display.h"
#include "run.h"
#endif

static void saveProgress(NSString *stage, NSDictionary *details) {
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSMutableDictionary *report = [details mutableCopy] ?: [NSMutableDictionary new];
    report[@"stage"] = stage;
    report[@"timestamp"] = @([NSDate date].timeIntervalSince1970);
    [[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil]
        writeToURL:[documents URLByAppendingPathComponent:@"game-progress.json"] atomically:YES];
}

#ifndef TF_ASSET_STAGE_ONLY
// Full-screen play: no status bar over the letterbox, and edge swipes need a second swipe.
@interface GameViewController : UIViewController
@end
@implementation GameViewController
- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures { return UIRectEdgeAll; }
@end

// Controller evidence for acceptance runs: which pads the game could see, and when.
static void logController(NSString *event, GCController *controller) {
    NSLog(@"TFNative controller %@: %@ (%@), extended %d, current %d", event, controller.vendorName ?: @"?",
          controller.productCategory, controller.extendedGamepad != nil, controller == GCController.current);
}

// Audio evidence: where iOS is sending output. A connected route is not proof of sound;
// the runtime's "audio: coreaudio output" lines show what the game actually wrote.
static void logAudioRoute(NSString *why) {
    AVAudioSession *session = AVAudioSession.sharedInstance;
    NSMutableArray *outputs = [NSMutableArray new];
    for (AVAudioSessionPortDescription *port in session.currentRoute.outputs) [outputs addObject:port.portType];
    NSLog(@"TFNative audio %@: category %@, outputs %@, output volume %.2f, %.0f Hz, other audio %d", why,
          session.category, [outputs componentsJoinedByString:@","], session.outputVolume, session.sampleRate, session.isOtherAudioPlaying);
}

static NSString *thermalName(NSProcessInfoThermalState state) {
    switch (state) {
    case NSProcessInfoThermalStateNominal: return @"nominal";
    case NSProcessInfoThermalStateFair: return @"fair";
    case NSProcessInfoThermalStateSerious: return @"serious";
    case NSProcessInfoThermalStateCritical: return @"critical";
    }
    return @"unknown";
}

// The screen in landscape pixels, and the launcher's render size clamped to it.
static CGSize screenPixels(UIScreen *screen) {
    CGSize native = screen.nativeBounds.size;
    return CGSizeMake(MAX(native.width, native.height), MIN(native.width, native.height));
}
static CGSize renderSize(CGSize screen) {
    NSString *choice = [NSUserDefaults.standardUserDefaults stringForKey:@"TFRenderResolution"] ?: @"1920x1080";
    unsigned w = 0, h = 0;
    if ([choice isEqualToString:@"native"]) return screen;
    if (sscanf(choice.UTF8String, "%ux%u", &w, &h) != 2 || w < 640 || h < 360 || w > screen.width || h > screen.height)
        return CGSizeMake(MIN(1280, screen.width), MIN(720, screen.height));
    return CGSizeMake(w, h);
}

static void writeDeviceInfo(UIWindowScene *scene) {
    NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSNumber *capacity = nil;
    [documents getResourceValue:&capacity forKey:NSURLVolumeAvailableCapacityForImportantUsageKey error:nil];
    CGSize screen = screenPixels(scene.screen);
    BOOL onVision = NO;
    if (@available(iOS 26.1, *)) onVision = NSProcessInfo.processInfo.isiOSAppOnVision;
    NSDictionary *info = @{@"model": UIDevice.currentDevice.model, @"system": UIDevice.currentDevice.systemName,
        @"version": UIDevice.currentDevice.systemVersion, @"ios_app_on_vision": @(onVision),
        @"screen_pixels": @[@(screen.width), @(screen.height)], @"max_fps": @(scene.screen.maximumFramesPerSecond),
        @"available_important_bytes": capacity ?: @0, @"physical_memory": @(NSProcessInfo.processInfo.physicalMemory),
        @"available_memory": @(os_proc_available_memory()), @"thermal": thermalName(NSProcessInfo.processInfo.thermalState),
        @"render_resolution": [NSUserDefaults.standardUserDefaults stringForKey:@"TFRenderResolution"] ?: @"default"};
    [[NSJSONSerialization dataWithJSONObject:info options:NSJSONWritingPrettyPrinted error:nil]
        writeToURL:[documents URLByAppendingPathComponent:@"device-info.json"] atomically:YES];
}
#endif

@interface GameScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) UIViewController *gameController;
@property(nonatomic) BOOL gameStarted;
@property(nonatomic) BOOL gameRunning;
@property(nonatomic) CGSize render;
@end
@implementation GameScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    UIWindowScene *windowScene = (UIWindowScene *)scene;
    self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
#ifdef TF_ASSET_STAGE_ONLY
    UIViewController *controller = [UIViewController new];
    controller.view.backgroundColor = UIColor.blackColor;
#else
    UIViewController *controller = [GameViewController new];
    controller.view = rt_host_ios_create_view();
    for (GCController *pad in GCController.controllers) logController(@"present", pad);
    [NSNotificationCenter.defaultCenter addObserverForName:GCControllerDidConnectNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { logController(@"connected", n.object); }];
    [NSNotificationCenter.defaultCenter addObserverForName:GCControllerDidDisconnectNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { logController(@"disconnected", n.object); }];
    writeDeviceInfo(windowScene);
    [NSNotificationCenter.defaultCenter addObserverForName:AVAudioSessionRouteChangeNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { (void)n; logAudioRoute(@"route changed"); }];
    [NSNotificationCenter.defaultCenter addObserverForName:AVAudioSessionInterruptionNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) {
            BOOL ended = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeEnded;
            // The runtime restarts its queue once the session is active again.
            if (ended && UIApplication.sharedApplication.applicationState == UIApplicationStateActive)
                [AVAudioSession.sharedInstance setActive:YES error:nil];
            logAudioRoute(ended ? @"interruption ended" : @"interrupted");
        }];
    [NSNotificationCenter.defaultCenter addObserverForName:NSProcessInfoThermalStateDidChangeNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) {
            (void)n; NSLog(@"TFNative thermal state: %@", thermalName(NSProcessInfo.processInfo.thermalState));
        }];
#endif
    self.status = [UILabel new]; self.status.numberOfLines = 0;
    self.status.textAlignment = NSTextAlignmentCenter; self.status.textColor = UIColor.whiteColor;
    self.status.font = [UIFont systemFontOfSize:20 weight:UIFontWeightMedium];
    self.status.translatesAutoresizingMaskIntoConstraints = NO;
    [controller.view addSubview:self.status];
    [NSLayoutConstraint activateConstraints:@[
        [self.status.centerYAnchor constraintEqualToAnchor:controller.view.centerYAnchor],
        [self.status.leadingAnchor constraintEqualToAnchor:controller.view.leadingAnchor constant:30],
        [self.status.trailingAnchor constraintEqualToAnchor:controller.view.trailingAnchor constant:-30]]];
    self.gameController = controller;
    __weak GameScene *weakSelf = self;
#ifdef TF_ASSET_STAGE_ONLY
    self.window.rootViewController = [[TFLauncherController alloc] initWithEngineReady:NO launch:^{}];
#else
    self.window.rootViewController = [[TFLauncherController alloc] initWithEngineReady:YES launch:^{ [weakSelf beginGame]; }];
#endif
    [self.window makeKeyAndVisible];
#ifndef TF_ASSET_STAGE_ONLY
    if (getenv("TF_AUTOSTART")) dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf beginGame]; });
#endif
}
#ifndef TF_ASSET_STAGE_ONLY
- (void)showFailure:(NSString *)message {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.gameRunning = NO;
        rt_host_ios_set_active(NO);
        UIApplication.sharedApplication.idleTimerDisabled = NO;
        self.status.hidden = NO;
        self.status.text = message;
    });
}
- (void)beginGame {
    if (self.gameStarted) return;
    self.gameStarted = YES;
    self.gameRunning = YES;
    UIApplication.sharedApplication.idleTimerDisabled = YES;
    self.window.rootViewController = self.gameController;
    [self.window makeKeyAndVisible];
    rt_host_ios_set_active(YES);
    logAudioRoute(@"game start");
    // The game sees the whole panel as its monitor (DXMT lists the usual sizes that fit)
    // and draws at the launcher's size; the layer scales it to the screen.
    CGSize screen = screenPixels(self.window.windowScene.screen);
    self.render = renderSize(screen);
    const struct DXMTNativeDisplay display = {(uint32_t)screen.width, (uint32_t)screen.height,
        (uint32_t)self.window.windowScene.screen.maximumFramesPerSecond, 1};
    if (!DXMTSetNativeDisplay(&display)) { [self showFailure:@"Could not configure the game display."]; return; }
    self.status.text = @"Checking Titanfall 2 files…";
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSThread *worker = [[NSThread alloc] initWithBlock:^{
            @autoreleasepool { [self runGame]; }
        }];
        worker.stackSize = 32 * 1024 * 1024; [worker start];
    });
    [NSTimer scheduledTimerWithTimeInterval:0.5 repeats:YES block:^(NSTimer *timer) {
        if (!self.gameRunning) { [timer invalidate]; return; }
        unsigned long long presents = gate3_embedded_presents();
        if (presents > 0) {
            saveProgress(@"first present observed", @{@"present_calls": @(presents)});
            self.status.hidden = YES; [timer invalidate];
        }
    }];
}
- (void)runGame {
    saveProgress(@"validating game files", nil);
    NSFileManager *fm = NSFileManager.defaultManager;
    NSURL *documents = [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL *game = [documents URLByAppendingPathComponent:@"Game" isDirectory:YES];
    NSURL *manifestURL = [documents URLByAppendingPathComponent:@"game-files-ready.json"];
    NSData *manifestData = [NSData dataWithContentsOfURL:manifestURL];
    NSDictionary *manifest = manifestData ? [NSJSONSerialization JSONObjectWithData:manifestData options:0 error:nil] : nil;
    NSString *failure = nil;
    NSArray *files = [manifest isKindOfClass:NSDictionary.class] ? manifest[@"files"] : nil;
    if (![files isKindOfClass:NSArray.class] || files.count == 0) {
        failure = @"Game files are still being transferred. Open this app again once the transfer is complete.";
        files = nil;
    }
    for (id file in files) {
        if (![file isKindOfClass:NSDictionary.class] || ![file[@"size"] isKindOfClass:NSNumber.class]) {
            failure = @"The game file list is invalid."; break;
        }
        NSString *name = file[@"path"];
        if (![name isKindOfClass:NSString.class] || [name hasPrefix:@"/"] || [name.pathComponents containsObject:@".."]) {
            failure = @"The game file list is invalid."; break;
        }
        NSDictionary *attributes = [fm attributesOfItemAtPath:[game URLByAppendingPathComponent:name].path error:nil];
        if (!attributes || [attributes[NSFileSize] unsignedLongLongValue] != [file[@"size"] unsignedLongLongValue]) {
            failure = [NSString stringWithFormat:@"Game file is missing or incomplete: %@", name]; break;
        }
    }
    if (failure) {
        saveProgress(@"file validation failed", @{@"error": failure});
        [self showFailure:failure];
        return;
    }
    // Owned game files are replaceable downloads; player saves remain backed up.
    [game setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
    NSURL *support = [fm URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL *state = [support URLByAppendingPathComponent:@"PlayerState" isDirectory:YES];
    NSError *error = nil;
    if (![fm createDirectoryAtURL:state withIntermediateDirectories:YES attributes:nil error:&error]) {
        saveProgress(@"startup failed", @{@"error": error.localizedDescription ?: @"Could not create save directory"});
        [self showFailure:error.localizedDescription ?: @"Could not create the save directory."]; return;
    }
    if (chdir(documents.fileSystemRepresentation) != 0) {
        saveProgress(@"startup failed", @{@"error": @"Could not open app Documents directory"});
        [self showFailure:@"Could not open the game directory."];
        return;
    }
    NSURL *caches = [fm URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL *shaderCache = [caches URLByAppendingPathComponent:@"DXMT" isDirectory:YES];
    [fm createDirectoryAtURL:shaderCache withIntermediateDirectories:YES attributes:nil error:nil];
    setenv("DXMT_SHADER_CACHE_PATH", shaderCache.fileSystemRepresentation, 1);
    freopen([documents URLByAppendingPathComponent:@"game-console.log"].fileSystemRepresentation, "w", stdout);
    dup2(fileno(stdout), STDERR_FILENO); setvbuf(stdout, NULL, _IONBF, 0);
    setenv("GATE3_ROOT", game.fileSystemRepresentation, 1);
    setenv("GATE3_STATE", state.fileSystemRepresentation, 1);
    setenv("GATE3_SOUND", "on", 1);
    setenv("RT_HOST_AUDIO", "coreaudio", 1);    // the runtime's default sink is silent
    setenv("GATE3_FRAMES", [documents URLByAppendingPathComponent:@"frame-times.txt"].fileSystemRepresentation, 1);
    setenv("DXMT_FRAME_STATS", "300", 1);
    setenv("RT_LOG_FILE", [documents URLByAppendingPathComponent:@"game-runtime.log"].fileSystemRepresentation, 1);
    setenv("RT_DXMT_DIR", NSBundle.mainBundle.privateFrameworksPath.fileSystemRepresentation, 1);
    setenv("RT_HOST_WINDOW", "uikit", 1);
    setenv("RT_D3D_BACKEND", "dxmt", 1);
    setenv("RT_D3D_SHOT", "0,60,300", 1);
    setenv("RT_D3D_SHOT_DIR", [documents URLByAppendingPathComponent:@"frames"].fileSystemRepresentation, 1);
    static char width[16], height[16];
    snprintf(width, sizeof width, "%u", (unsigned)self.render.width);
    snprintf(height, sizeof height, "%u", (unsigned)self.render.height);
    char *args[] = {"-noorigin", "-novid", "-numworkerthreads", "1", "-numreservedcores", "0",
        "-threads", "1", "-noasync", "+async_mode", "1", "-NoQueuedPacketThread",
        "+net_queued_packet_thread", "0", "+host_thread_mode", "0", "-windowed", "-w", width, "-h", height};
    // Device diagnostics can request the installed training map. A startup +map is
    // ignored, so it goes to the engine's command buffer once the main menu is up.
    if (getenv("TF_PILOT_TRAINING")) setenv("GATE3_CONSOLE", "3:map sp_training", 1);
    int argc = (int)(sizeof args / sizeof *args);
    dispatch_async(dispatch_get_main_queue(), ^{ self.status.text = @"Starting Titanfall 2…"; });
    saveProgress(@"entering native game driver", @{@"validated_files": @(files.count),
        @"render": [NSString stringWithFormat:@"%ux%u", (unsigned)self.render.width, (unsigned)self.render.height]});
    int result = gate3_run_embedded(argc, args);
    NSDictionary *report = @{@"launcher_returned": @YES, @"result": @(result),
        @"present_calls": @(gate3_embedded_presents()), @"system_version": UIDevice.currentDevice.systemVersion};
    [[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:nil]
        writeToURL:[documents URLByAppendingPathComponent:@"game-result.json"] atomically:YES];
    saveProgress(@"game driver returned", report);
    [self showFailure:[NSString stringWithFormat:@"Titanfall 2 exited (%d).", result]];
}
- (void)sceneDidBecomeActive:(UIScene *)scene { (void)scene; rt_host_ios_set_active(self.gameRunning); UIApplication.sharedApplication.idleTimerDisabled = self.gameRunning; }
- (void)sceneWillResignActive:(UIScene *)scene { (void)scene; rt_host_ios_set_active(NO); UIApplication.sharedApplication.idleTimerDisabled = NO; }
#endif
@end
@interface GameApp : UIResponder <UIApplicationDelegate>
@end
@implementation GameApp
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)app; (void)options;
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Titanfall 2" sessionRole:session.role];
    config.delegateClass = GameScene.class; return config;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(GameApp.class)); }
}
