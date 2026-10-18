/* The MMDevice enumerator and its devices (runtime/xaudio/mmdev.c, the bodies in
 * runtime/xaudio/<Interface>/<Method>.c).  The Mac's microphone is never opened, so there is
 * no capture endpoint; there is exactly one output endpoint, made up: MM_ENDPOINT_ID. */
#ifndef RT_MMDEV_H
#define RT_MMDEV_H

#include <stdbool.h>

#include "rt/xaudio.h"
#include "com_ids.h"            /* generated: rt_iface_<Interface>, and the interface type names */

#ifndef E_NOTFOUND
#define E_NOTFOUND ((HRESULT)0x80070490)
#endif

/* EDataFlow, ERole, the state flags (mmdeviceapi.h). */
#define MM_E_RENDER          0
#define MM_E_CAPTURE         1
#define MM_E_ALL             2
#define MM_E_CONSOLE         0
#define MM_E_MULTIMEDIA      1
#define MM_E_COMMUNICATIONS  2
#define MM_DEVICE_STATE_ACTIVE     0x1
#define MM_DEVICE_STATEMASK_ALL    0xF

/* The one output endpoint's id, ASCII here; the guest holds it as UTF-16. */
#define MM_ENDPOINT_ID "{0.0.0.00000000}.{00000000-0000-0000-0000-000000000001}"

/* Compares a guest UTF-16 string with MM_ENDPOINT_ID; NULL is not the endpoint. */
bool rt_mm_id_is_endpoint(const void *guest_wstr);

/* The state of each class (bytes per object in rt_com_new). */
typedef struct MmDevice     { int nothing_yet; } MmDevice;     /* behind IMMDevice */
typedef struct MmCollection { unsigned count; } MmCollection;  /* behind IMMDeviceCollection: 0 or 1 */
extern const RtComClass rt_mm_device_class, rt_mm_collection_class;

/* A new device object (one reference), and a new collection of `count` devices. */
void *rt_mm_device_new(void);
void *rt_mm_collection_new(unsigned count);

/* The enumerator's notification list: up to 8 pointers per enumerator, kept in a table keyed by
 * the enumerator's state (its MmEnumerator).  The pointers are only remembered, never read.
 * add: false when the list already has 8.  remove: false when `cb` is not in the list. */
bool rt_mm_callbacks_add(const void *enumerator_state, const void *cb);
bool rt_mm_callbacks_remove(const void *enumerator_state, const void *cb);
/* The enumerator class's destroy: forgets its list. */
void rt_mm_enumerator_destroy(void *enumerator_state);

#endif
