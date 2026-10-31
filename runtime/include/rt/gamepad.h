/* Native controller snapshots in the Windows XInput wire layout. No Apple/Windows
 * headers: usable by the native input adapter and the guest ABI shims. */
#ifndef RT_GAMEPAD_H
#define RT_GAMEPAD_H
#include <stdbool.h>
#include <stdint.h>

enum {
    RT_PAD_UP=0x0001, RT_PAD_DOWN=0x0002, RT_PAD_LEFT=0x0004, RT_PAD_RIGHT=0x0008,
    RT_PAD_START=0x0010, RT_PAD_BACK=0x0020, RT_PAD_L3=0x0040, RT_PAD_R3=0x0080,
    RT_PAD_L1=0x0100, RT_PAD_R1=0x0200, RT_PAD_A=0x1000, RT_PAD_B=0x2000,
    RT_PAD_X=0x4000, RT_PAD_Y=0x8000
};
typedef struct {
    uint16_t buttons;
    uint8_t left_trigger, right_trigger;
    int16_t left_x, left_y, right_x, right_y;
} RtGamepad;
typedef struct { uint32_t packet; RtGamepad gamepad; } RtGamepadState;
typedef struct { uint16_t left_motor, right_motor; } RtGamepadVibration;
typedef struct {
    uint8_t type, subtype;
    uint16_t flags;
    RtGamepad gamepad;
    RtGamepadVibration vibration;
} RtGamepadCapabilities;

/* Thread-safe snapshots. Disconnect clears held inputs. Reconnect retains the
 * packet sequence so a guest cannot mistake new input for an old cached sample. */
void rt_gamepad_publish(unsigned slot, const RtGamepad *pad);
void rt_gamepad_disconnect(unsigned slot);
uint32_t rt_gamepad_get_state(unsigned slot, void *state);
uint32_t rt_gamepad_get_capabilities(unsigned slot, void *capabilities);
uint32_t rt_gamepad_set_vibration(unsigned slot, const void *vibration);

/* Main thread only; observes existing controllers and subsequent hotplug events.
 * DualSense is delivered through GCExtendedGamepad. Rumble is not implemented yet. */
void rt_apple_gamepads_start(void);
void rt_apple_gamepads_stop(void);
void rt_apple_gamepads_set_active(bool active);
#endif
