#import <UIKit/UIKit.h>
#include "EngineCheck.h"

static void save_report(NSDictionary *report, NSURL *destination) {
    NSError *error = nil;
    NSMutableDictionary *withStorage = [report mutableCopy];
    NSDictionary *filesystem = [NSFileManager.defaultManager
        attributesOfFileSystemForPath:destination.URLByDeletingLastPathComponent.path error:&error];
    if (filesystem) {
        withStorage[@"filesystem_free_bytes"] = filesystem[NSFileSystemFreeSize];
        withStorage[@"filesystem_total_bytes"] = filesystem[NSFileSystemSize];
    } else {
        withStorage[@"storage_query_error"] = error.localizedDescription ?: @"Unknown error";
    }
    if ([report[@"complete"] boolValue]) {
        error = nil;
        NSDictionary *capacity = [destination.URLByDeletingLastPathComponent resourceValuesForKeys:@[
            NSURLVolumeAvailableCapacityKey,
            NSURLVolumeAvailableCapacityForImportantUsageKey,
            NSURLVolumeAvailableCapacityForOpportunisticUsageKey] error:&error];
        if (capacity) {
            withStorage[@"volume_available_bytes"] = capacity[NSURLVolumeAvailableCapacityKey];
            withStorage[@"available_for_important_usage_bytes"] = capacity[NSURLVolumeAvailableCapacityForImportantUsageKey];
            withStorage[@"available_for_opportunistic_usage_bytes"] = capacity[NSURLVolumeAvailableCapacityForOpportunisticUsageKey];
        } else {
            withStorage[@"capacity_query_error"] = error.localizedDescription ?: @"Unknown error";
        }
    }
    error = nil;
    NSData *json = [NSJSONSerialization dataWithJSONObject:withStorage
        options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];
    if (![json writeToURL:destination options:NSDataWritingAtomic error:&error])
        NSLog(@"Engine check report error: %@", error);
}
static void save_stage(const char *stage, void *context) {
    save_report(@{@"stage": @(stage), @"complete": @NO}, (__bridge NSURL *)context);
}

@interface EngineScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation EngineScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *controller = [UIViewController new];
    UITextView *text = [UITextView new]; text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightRegular];
    text.text = @"Checking two native engine modules…\nThis is not gameplay.";
    controller.view = text; self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSThread *worker = [[NSThread alloc] initWithBlock:^{
            @autoreleasepool {
                NSFileManager *fm = NSFileManager.defaultManager;
                NSURL *documents = [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
                NSURL *state = [documents URLByAppendingPathComponent:@"engine-state" isDirectory:YES];
                NSURL *output = [documents URLByAppendingPathComponent:@"engine-smoke.json"];
                NSURL *game = [NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Game" isDirectory:YES];
                NSError *error = nil;
                if (![fm createDirectoryAtURL:state withIntermediateDirectories:YES attributes:nil error:&error]) {
                    save_report(@{@"complete": @YES, @"passed": @NO, @"error": error.localizedDescription}, output);
                    return;
                }
                TFEngineCheck r = tf_engine_check(state.fileSystemRepresentation, game.fileSystemRepresentation,
                                                  save_stage, (__bridge void *)output);
                NSDictionary *report = @{@"complete": @YES, @"passed": @(r.passed), @"error": @(r.error),
                    @"purpose": @"Two recompiled engine modules; no game renderer or campaign",
                    @"system_version": UIDevice.currentDevice.systemVersion,
                    @"tier0_loaded": @(r.tier0_loaded), @"vstdlib_loaded": @(r.vstdlib_loaded),
                    @"modules": @(r.modules), @"dispatch_entries": @(r.dispatch_entries),
                    @"page_size": @(r.page_size), @"mode_flags": @(r.mode_flags),
                    @"utf8_copy": @(r.utf8_copy), @"random": @(r.random), @"clock": @(r.clock)};
                save_report(report, output);
                NSData *json = [NSData dataWithContentsOfURL:output];
                NSString *message = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
                dispatch_async(dispatch_get_main_queue(), ^{ text.text = message; });
            }
        }];
        worker.stackSize = 8 * 1024 * 1024;
        [worker start];
    });
}
@end
@interface EngineApp : UIResponder <UIApplicationDelegate>
@end
@implementation EngineApp
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session
    options:(UISceneConnectionOptions *)options {
    (void)app; (void)options;
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Engine Check" sessionRole:session.role];
    config.delegateClass = EngineScene.class; return config;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(EngineApp.class)); }
}
