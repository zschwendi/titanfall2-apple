/* The macOS key codes (NSEvent keyCode, the kVK_ constants of HIToolbox's Events.h) to the
 * runtime's Windows virtual keys (core/mac_keys.c).  Plain C, no macOS framework.
 */
#ifndef RT_MACKEYS_H
#define RT_MACKEYS_H

#include <stdbool.h>
#include <stdint.h>

/* macOS virtual key code (NSEvent keyCode) to the Windows virtual key of the same
 * physical key on a US keyboard.  false: the runtime's key table has no such key. */
bool rt_mac_key_to_vk(uint16_t mac_key_code, uint32_t *vk);
/* How many rows the table has (for tests). */
unsigned rt_mac_key_count(void);
/* Row i of the table (for tests): false past the end. */
bool rt_mac_key_row(unsigned i, uint16_t *mac_key_code, uint32_t *vk);

#endif
