#import <GameController/GameController.h>
#include "rt/gamepad.h"
#include <math.h>

static GCController *pads[4];
static id connected_observer, disconnected_observer, current_observer;
static bool active = true;

static int16_t axis(float v) {
    if (!isfinite(v)) return 0;
    v = fmaxf(-1, fminf(1, v));
    return (int16_t)lroundf(v * (v < 0 ? 32768 : 32767));
}
static uint8_t trigger(float v) {
    return isfinite(v) ? (uint8_t)lroundf(fmaxf(0, fminf(1, v)) * 255) : 0;
}
static void publish(unsigned slot) {
    RtGamepad out = {0};
    if (active) {
        GCExtendedGamepad *g = [pads[slot] capture].extendedGamepad;
        bool touchpad = false;
        if ([g isKindOfClass:GCDualSenseGamepad.class])
            touchpad = ((GCDualSenseGamepad *)g).touchpadButton.pressed;
        else if ([g isKindOfClass:GCDualShockGamepad.class])
            touchpad = ((GCDualShockGamepad *)g).touchpadButton.pressed;
        out.buttons = (g.dpad.up.pressed ? RT_PAD_UP : 0) |
            (g.dpad.down.pressed ? RT_PAD_DOWN : 0) |
            (g.dpad.left.pressed ? RT_PAD_LEFT : 0) |
            (g.dpad.right.pressed ? RT_PAD_RIGHT : 0) |
            (g.buttonMenu.pressed ? RT_PAD_START : 0) |
            ((g.buttonOptions.pressed || touchpad) ? RT_PAD_BACK : 0) |
            (g.leftThumbstickButton.pressed ? RT_PAD_L3 : 0) |
            (g.rightThumbstickButton.pressed ? RT_PAD_R3 : 0) |
            (g.leftShoulder.pressed ? RT_PAD_L1 : 0) |
            (g.rightShoulder.pressed ? RT_PAD_R1 : 0) |
            (g.buttonA.pressed ? RT_PAD_A : 0) | (g.buttonB.pressed ? RT_PAD_B : 0) |
            (g.buttonX.pressed ? RT_PAD_X : 0) | (g.buttonY.pressed ? RT_PAD_Y : 0);
        out.left_trigger=trigger(g.leftTrigger.value); out.right_trigger=trigger(g.rightTrigger.value);
        out.left_x=axis(g.leftThumbstick.xAxis.value); out.left_y=axis(g.leftThumbstick.yAxis.value);
        out.right_x=axis(g.rightThumbstick.xAxis.value); out.right_y=axis(g.rightThumbstick.yAxis.value);
    }
    rt_gamepad_publish(slot, &out);
}
static void connect_pad(GCController *controller) {
    if (!controller.extendedGamepad) return;
    for (unsigned i=0; i<4; i++) if (pads[i] == controller) return;
    for (unsigned i=0; i<4; i++) {
        if (pads[i]) continue;
        pads[i]=controller;
        controller.handlerQueue=dispatch_get_main_queue();
        __weak GCController *weak = controller;
        controller.extendedGamepad.valueChangedHandler = ^(GCExtendedGamepad *g, GCControllerElement *e) {
            (void)g; (void)e;
            for (unsigned slot=0; slot<4; slot++)
                if (weak && pads[slot] == weak) { publish(slot); break; }
        };
        publish(i);
        return;
    }
}
/* Titanfall is single-player here. Keep the pad used in the launcher in slot
 * zero; swapping complete snapshots also releases the previous pad's inputs. */
static void prefer_pad(GCController *controller) {
    if (!controller.extendedGamepad) return;
    connect_pad(controller);
    for (unsigned i=1; i<4; i++) if (pads[i] == controller) {
        GCController *old = pads[0];
        pads[0]=controller; pads[i]=old;
        publish(0);
        if (old) publish(i); else rt_gamepad_disconnect(i);
        break;
    }
}
void rt_apple_gamepads_start(void) {
    NSCAssert([NSThread isMainThread], @"GameController setup must run on main");
    if (connected_observer) return;
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    connected_observer=[center addObserverForName:GCControllerDidConnectNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { connect_pad(n.object); }];
    disconnected_observer=[center addObserverForName:GCControllerDidDisconnectNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) {
            for (unsigned i=0; i<4; i++) if (pads[i] == n.object) {
                pads[i].extendedGamepad.valueChangedHandler=nil;
                pads[i]=nil;
                rt_gamepad_disconnect(i);
            }
            for (GCController *controller in GCController.controllers) connect_pad(controller);
            if (!pads[0]) for (unsigned i=1; i<4; i++)
                if (pads[i]) { prefer_pad(pads[i]); break; }
        }];
    current_observer=[center addObserverForName:GCControllerDidBecomeCurrentNotification object:nil
        queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *n) { prefer_pad(n.object); }];
    for (GCController *controller in GCController.controllers) connect_pad(controller);
    prefer_pad(GCController.current);
}
void rt_apple_gamepads_set_active(bool value) {
    NSCAssert([NSThread isMainThread], @"GameController focus must run on main");
    active=value;
    for (unsigned i=0; i<4; i++) if (pads[i]) publish(i);
}
void rt_apple_gamepads_stop(void) {
    NSCAssert([NSThread isMainThread], @"GameController teardown must run on main");
    if (connected_observer) [NSNotificationCenter.defaultCenter removeObserver:connected_observer];
    if (disconnected_observer) [NSNotificationCenter.defaultCenter removeObserver:disconnected_observer];
    if (current_observer) [NSNotificationCenter.defaultCenter removeObserver:current_observer];
    connected_observer=nil; disconnected_observer=nil; current_observer=nil;
    for (unsigned i=0; i<4; i++) {
        pads[i].extendedGamepad.valueChangedHandler=nil;
        pads[i]=nil;
        rt_gamepad_disconnect(i);
    }
    active=true;
}
