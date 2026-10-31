#import <UIKit/UIKit.h>
#import "Probe.h"

@interface ProbeScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation ProbeScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *controller = [UIViewController new];
    UITextView *text = [UITextView new];
    text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightRegular];
    controller.view = text;
    self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];

    TFVMProbe results[TF_VM_PROBE_COUNT];
    size_t count = tf_vm_probe(results, TF_VM_PROBE_COUNT);
    NSMutableArray *rows = [NSMutableArray new];
    for (size_t i = 0; i < count; ++i) {
        TFVMProbe r = results[i];
        [rows addObject:@{@"layout": @(r.name), @"method": @(r.method),
            @"error_domain": r.fixed ? @"kern_return_t" : @"errno",
            @"exact": @(r.exact != 0),
            @"image_requested": @(r.image_base), @"image_size": @(r.image_size),
            @"image_actual": @(r.image_actual), @"image_error": @(r.image_error),
            @"vm_requested": @(r.vm_base), @"vm_size": @(r.vm_size),
            @"vm_actual": @(r.vm_actual), @"vm_error": @(r.vm_error)}];
    }
    NSDictionary *report = @{@"purpose": @"Address reservations only; no game or GPU test",
        @"configuration": @TF_PROBE_CONFIGURATION,
        @"system_version": UIDevice.currentDevice.systemVersion,
        @"physical_memory": @(NSProcessInfo.processInfo.physicalMemory),
        @"results": rows};
    NSError *error;
    NSData *json = [NSJSONSerialization dataWithJSONObject:report
        options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:&error];
    NSURL *documents = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
        inDomains:NSUserDomainMask].firstObject;
    BOOL saved = [json writeToURL:[documents URLByAppendingPathComponent:@"vm-probe.json"]
        options:NSDataWritingAtomic error:&error];
    text.text = [NSString stringWithFormat:@"Titanfall port memory diagnostic\n\n%@\n\n%@",
        [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding],
        saved ? @"Report saved." : error.localizedDescription];
    NSLog(@"TF_VM_PROBE %@", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding]);
}
@end

@interface ProbeApp : UIResponder <UIApplicationDelegate>
@end
@implementation ProbeApp
- (UISceneConfiguration *)application:(UIApplication *)application
    configurationForConnectingSceneSession:(UISceneSession *)session
    options:(UISceneConnectionOptions *)options {
    (void)application; (void)options;
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc]
        initWithName:@"Diagnostic" sessionRole:session.role];
    configuration.delegateClass = ProbeScene.class;
    return configuration;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(ProbeApp.class)); }
}
