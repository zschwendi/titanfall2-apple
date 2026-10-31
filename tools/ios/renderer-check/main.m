#import <UIKit/UIKit.h>
#include "RendererCheck.h"

static void save_report(NSURL *url, NSDictionary *report) {
    NSData *json = [NSJSONSerialization dataWithJSONObject:report
        options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nil];
    [json writeToURL:url options:NSDataWritingAtomic error:nil];
}

@interface RendererScene : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation RendererScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session
      options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    UIWindowScene *windowScene = (UIWindowScene *)scene;
    self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
    UIViewController *controller = [UIViewController new];
    UITextView *text = [UITextView new]; text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:15 weight:UIFontWeightRegular];
    text.text = @"Testing the native DirectX-to-Metal renderer…\nThis is a graphics test, not gameplay.";
    controller.view = text; self.window.rootViewController = controller;
    [self.window makeKeyAndVisible];
    CGFloat scale = windowScene.screen.scale;
    uint32_t width = (uint32_t)(self.window.bounds.size.width * scale);
    uint32_t height = (uint32_t)(self.window.bounds.size.height * scale);
    uint32_t refresh = (uint32_t)windowScene.screen.maximumFramesPerSecond;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSThread *worker = [[NSThread alloc] initWithBlock:^{
            @autoreleasepool {
                NSFileManager *fm = NSFileManager.defaultManager;
                NSURL *documents = [fm URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
                NSURL *output = [documents URLByAppendingPathComponent:@"renderer-result.json"];
                save_report(output, @{@"complete": @NO, @"stage": @"D3D11 device and real shader test"});
                int result = tf_renderer_check(NSBundle.mainBundle.privateFrameworksPath.fileSystemRepresentation,
                    documents.fileSystemRepresentation, width, height, refresh);
                NSDictionary *report = @{@"complete": @YES, @"passed": @(result == 0), @"result": @(result),
                    @"purpose": @"Real D3D11 shader conversion, triangle draws and GPU pixel readback; no game scene",
                    @"system_version": UIDevice.currentDevice.systemVersion,
                    @"display_width": @(width), @"display_height": @(height), @"refresh_hz": @(refresh)};
                save_report(output, report);
                NSString *json = [[NSString alloc] initWithData:[NSData dataWithContentsOfURL:output]
                    encoding:NSUTF8StringEncoding];
                NSString *log = [NSString stringWithContentsOfURL:[documents URLByAppendingPathComponent:@"renderer.log"]
                    encoding:NSUTF8StringEncoding error:nil];
                dispatch_async(dispatch_get_main_queue(), ^{
                    text.text = [NSString stringWithFormat:@"%@\n\n%@", json, log ?: @""];
                });
            }
        }];
        worker.stackSize = 8 * 1024 * 1024; [worker start];
    });
}
@end

@interface RendererApp : UIResponder <UIApplicationDelegate>
@end
@implementation RendererApp
- (UISceneConfiguration *)application:(UIApplication *)app configurationForConnectingSceneSession:(UISceneSession *)session
    options:(UISceneConnectionOptions *)options {
    (void)app; (void)options;
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Renderer Check" sessionRole:session.role];
    config.delegateClass = RendererScene.class; return config;
}
@end
int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(RendererApp.class)); }
}
