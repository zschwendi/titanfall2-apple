/* Native GameController -> XInput snapshot regression. This uses a synthetic
 * GCExtendedGamepad, not physical DualSense acceptance. */
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#include "rt/gamepad.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        if (GCController.controllers.count) {
            fprintf(stderr, "Run this deterministic adapter test with no controller connected to this Mac.\n");
            return 2;
        }
        rt_apple_gamepads_start();
        GCController *pad=[GCController controllerWithExtendedGamepad];
        GCExtendedGamepad *g=pad.extendedGamepad;
        [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidConnectNotification object:pad];
        [g.buttonA setValue:1]; [g.buttonX setValue:1]; [g.buttonMenu setValue:1];
        [g.leftThumbstick.xAxis setValue:-1]; [g.leftThumbstick.yAxis setValue:1];
        [g.rightThumbstick.xAxis setValue:0.5]; [g.rightThumbstick.yAxis setValue:-0.5];
        [g.leftTrigger setValue:1]; [g.rightTrigger setValue:0.5];
        assert(g.valueChangedHandler != nil);
        g.valueChangedHandler(g,g.buttonA);
        RtGamepadState state={0};
        assert(rt_gamepad_get_state(0,&state)==0);
        assert(state.gamepad.buttons==(RT_PAD_A|RT_PAD_X|RT_PAD_START));
        assert(state.gamepad.left_x==-32768 && state.gamepad.left_y==32767);
        assert(state.gamepad.right_x==16384 && state.gamepad.right_y==-16384);
        assert(state.gamepad.left_trigger==255 && state.gamepad.right_trigger==128);
        uint32_t packet=state.packet;
        g.valueChangedHandler(g,g.buttonA);
        assert(rt_gamepad_get_state(0,&state)==0 && state.packet==packet);
        rt_apple_gamepads_set_active(false);
        assert(rt_gamepad_get_state(0,&state)==0);
        assert(state.gamepad.buttons==0 && state.gamepad.left_x==0 && state.gamepad.left_trigger==0);
        rt_apple_gamepads_set_active(true);
        assert(rt_gamepad_get_state(0,&state)==0 && state.gamepad.left_x==-32768);
        GCController *second=[GCController controllerWithExtendedGamepad];
        [second.extendedGamepad.buttonB setValue:1];
        [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidConnectNotification object:second];
        [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidBecomeCurrentNotification object:second];
        assert(rt_gamepad_get_state(0,&state)==0 && state.gamepad.buttons==RT_PAD_B);
        assert(state.gamepad.left_x==0 && state.gamepad.left_trigger==0);
        [g.buttonX setValue:0];
        g.valueChangedHandler(g,g.buttonX);
        assert(rt_gamepad_get_state(1,&state)==0 && state.gamepad.buttons==(RT_PAD_A|RT_PAD_START));
        [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidDisconnectNotification object:second];
        assert(rt_gamepad_get_state(0,&state)==0 && state.gamepad.buttons==(RT_PAD_A|RT_PAD_START));
        assert(rt_gamepad_get_state(1,&state)==1167);
        [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidDisconnectNotification object:pad];
        assert(rt_gamepad_get_state(0,&state)==1167);
        assert(g.valueChangedHandler==nil);
        rt_apple_gamepads_stop();
        rt_apple_gamepads_start();
        [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidConnectNotification object:pad];
        assert(rt_gamepad_get_state(0,&state)==0 && state.gamepad.buttons==(RT_PAD_A|RT_PAD_START));
        rt_apple_gamepads_stop();
        assert(rt_gamepad_get_state(0,&state)==1167);
        puts("PASS GameController mapping, packet stability, focus, primary-controller handoff, hotplug, restart");
    }
    return 0;
}
