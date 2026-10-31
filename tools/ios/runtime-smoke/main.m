#import <UIKit/UIKit.h>
#include "RuntimeCheck.h"

/* An isolated runtime check, without game DLLs, assets, rendering or network.
 * It uses only its own app container and touches one 64 KiB guest allocation. */
static NSDictionary *run_check(NSURL *documents) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSURL *state = [documents URLByAppendingPathComponent:@"runtime-state" isDirectory:YES];
    NSURL *install = [documents URLByAppendingPathComponent:@"empty-game-root" isDirectory:YES];
    NSError *error = nil;
    if (![fm createDirectoryAtURL:state withIntermediateDirectories:YES attributes:nil error:&error] ||
        ![fm createDirectoryAtURL:install withIntermediateDirectories:YES attributes:nil error:&error])
        return @{@"passed": @NO, @"error": error.localizedDescription ?: @"Cannot create test directories"};
    TFRuntimeCheck r = tf_runtime_check(state.fileSystemRepresentation, install.fileSystemRepresentation);
    return @{@"runtime_started": @(r.runtime_started), @"native_module_count": @(r.native_module_count),
        @"dispatch_entries": @(r.dispatch_entries), @"image_base": @(r.image_base), @"vm_base": @(r.vm_base),
        @"vm_size": @(r.vm_size), @"physical_memory": @(NSProcessInfo.processInfo.physicalMemory),
        @"purpose": @"Native runtime initialization and memory lifecycle only; no game or GPU test",
        @"allocated": @(r.allocated), @"zero_filled": @(r.zero_filled), @"read_write": @(r.read_write),
        @"read_only_protection": @(r.read_only_protection), @"query_matches": @(r.query_matches),
        @"decommit": @(r.decommit), @"recommit_zero_filled": @(r.recommit_zero_filled),
        @"release": @(r.release), @"passed": @(r.passed)};
}

@interface SmokeScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation SmokeScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *controller = [UIViewController new];
    UITextView *text = [UITextView new]; text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightRegular];
    text.text = @"Checking the native runtime…";
    controller.view = text; self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSURL *documents = [NSFileManager.defaultManager URLsForDirectory:NSDocumentDirectory
            inDomains:NSUserDomainMask].firstObject;
        NSDictionary *report = run_check(documents);
        NSError *error = nil;
        NSData *json = [NSJSONSerialization dataWithJSONObject:report
            options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];
        BOOL saved = [json writeToURL:[documents URLByAppendingPathComponent:@"runtime-smoke.json"]
            options:NSDataWritingAtomic error:&error];
        text.text = [NSString stringWithFormat:@"Titanfall runtime check\n\n%@\n\n%@",
            [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding],
            saved ? @"Report saved." : error.localizedDescription];
    });
}
@end
@interface SmokeApp : UIResponder <UIApplicationDelegate>
@end
@implementation SmokeApp
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session
    options:(UISceneConnectionOptions *)options {
    (void)app; (void)options;
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Runtime Check" sessionRole:session.role];
    config.delegateClass = SmokeScene.class; return config;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(SmokeApp.class)); }
}
