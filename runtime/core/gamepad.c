#include "rt/gamepad.h"
#include <pthread.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(RtGamepad) == 12, "XINPUT_GAMEPAD ABI");
_Static_assert(sizeof(RtGamepadState) == 16, "XINPUT_STATE ABI");
_Static_assert(offsetof(RtGamepadState, gamepad) == 4, "XINPUT_STATE offset");
_Static_assert(sizeof(RtGamepadCapabilities) == 20, "XINPUT_CAPABILITIES ABI");
_Static_assert(sizeof(RtGamepadVibration) == 4, "XINPUT_VIBRATION ABI");

static pthread_mutex_t pad_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { bool connected; RtGamepadState state; } slots[4];
enum { PAD_OK=0, PAD_UNSUPPORTED=50, PAD_BAD_ARGUMENT=87, PAD_DISCONNECTED=1167 };

void rt_gamepad_publish(unsigned slot, const RtGamepad *pad) {
    if (slot >= 4 || !pad) return;
    pthread_mutex_lock(&pad_lock);
    if (!slots[slot].connected || memcmp(&slots[slot].state.gamepad, pad, sizeof *pad)) {
        slots[slot].state.packet++;
        slots[slot].state.gamepad = *pad;
    }
    slots[slot].connected = true;
    pthread_mutex_unlock(&pad_lock);
}

void rt_gamepad_disconnect(unsigned slot) {
    if (slot >= 4) return;
    pthread_mutex_lock(&pad_lock);
    if (slots[slot].connected) slots[slot].state.packet++;
    slots[slot].connected = false;
    memset(&slots[slot].state.gamepad, 0, sizeof(RtGamepad));
    pthread_mutex_unlock(&pad_lock);
}

uint32_t rt_gamepad_get_state(unsigned slot, void *state) {
    if (slot >= 4 || !state) return PAD_BAD_ARGUMENT;
    pthread_mutex_lock(&pad_lock);
    bool connected = slots[slot].connected;
    if (connected) memcpy(state, &slots[slot].state, sizeof(RtGamepadState));
    pthread_mutex_unlock(&pad_lock);
    return connected ? PAD_OK : PAD_DISCONNECTED;
}

uint32_t rt_gamepad_get_capabilities(unsigned slot, void *capabilities) {
    if (slot >= 4 || !capabilities) return PAD_BAD_ARGUMENT;
    pthread_mutex_lock(&pad_lock);
    bool connected = slots[slot].connected;
    if (connected) {
        /* XINPUT_DEVTYPE_GAMEPAD / XINPUT_DEVSUBTYPE_GAMEPAD. Capabilities
         * describe supported controls, not their present positions. Reserved
         * button bits and rumble capability are deliberately absent. */
        RtGamepadCapabilities caps = { .type=1, .subtype=1,
            .gamepad={ .buttons=0xf3ff, .left_trigger=255, .right_trigger=255,
                       .left_x=32767, .left_y=32767, .right_x=32767, .right_y=32767 } };
        memcpy(capabilities, &caps, sizeof caps);
    }
    pthread_mutex_unlock(&pad_lock);
    return connected ? PAD_OK : PAD_DISCONNECTED;
}

uint32_t rt_gamepad_set_vibration(unsigned slot, const void *vibration) {
    if (slot >= 4 || !vibration) return PAD_BAD_ARGUMENT;
    pthread_mutex_lock(&pad_lock);
    bool connected = slots[slot].connected;
    pthread_mutex_unlock(&pad_lock);
    if (!connected) return PAD_DISCONNECTED;
    RtGamepadVibration requested;
    memcpy(&requested, vibration, sizeof requested);
    return (requested.left_motor || requested.right_motor) ? PAD_UNSUPPORTED : PAD_OK;
}
