/* UIKit integration. The app owns UIApplication and its scene lifecycle. Attach
 * the real game view on the main thread before starting the guest thread.
 * The returned view's layer is a CAMetalLayer handed to the game's DXMT backend. */
#ifndef RT_HOST_IOS_H
#define RT_HOST_IOS_H
#ifdef __OBJC__
@class UIView;
UIView *rt_host_ios_create_view(void);
#endif
void rt_host_ios_set_active(_Bool active);
#endif
