/* Raw input: the record of what the process registered (core/rawinput.c, built; unit
 * rt-census-rows).  The specification of user32!RegisterRawInputDevices, which the input
 * system fetches with GetProcAddress (inputsystem+0x783a) and calls once, right after it
 * subclasses the game's window: RegisterRawInputDevices(devices, 1, 16) = 1 at
 * inputsystem+0x6c77, 2.860 s of the recording (docs/handoff/gate3-run-3.md, section 5).
 *
 * THE MACHINE has a keyboard and a mouse and no game controller (the planner's decision), and
 * NO INPUT ARRIVES in this runtime yet: registration succeeds and is recorded, no WM_INPUT is
 * ever posted, and no HRAWINPUT exists, so GetRawInputData fails for every handle (its shim
 * needs no core: (UINT)-1 with ERROR_INVALID_HANDLE, Wine user32/tests/input.c,
 * test_GetRawInputData).  The record is what a later input source will read to know which
 * window's thread gets WM_INPUT.
 *
 * SOURCES.  [MS] Microsoft's pages nf-winuser-registerrawinputdevices and
 * ns-winuser-rawinputdevice, read 2026-10-11; [W] Wine's conformance test
 * dlls/user32/tests/input.c, test_RegisterRawInputDevices (wine-mirror master, read the same
 * day).  STOPS is the unimplemented report (rt_fatal, "<function>: not implemented: ...").
 */
#ifndef RT_RAWINPUT_H
#define RT_RAWINPUT_H

#include "rt/wintypes.h"
#include "rt/win/rawinput.h"

/* RegisterRawInputDevices(pRawInputDevices, uiNumDevices, cbSize).  The registrations are the
 * PROCESS's, one per top-level collection (usage page and usage): [MS] "Only one window per
 * raw input device class may be registered to receive raw input within a process (the window
 * passed in the last call to RegisterRawInputDevices)".  In this order:
 *   cb other than 16 (sizeof(RAWINPUTDEVICE)): FALSE with ERROR_INVALID_PARAMETER ([W]: a
 *       size of 0 with a good list, and (NULL, 0, 0)).
 *   an entry with RIDEV_INPUTSINK and no target window: FALSE with ERROR_INVALID_PARAMETER,
 *       and NOTHING of the call is recorded, earlier entries included ([W]: both entries
 *       carry the flag there; that an earlier good entry is not kept is THIS RUNTIME'S
 *       CHOICE, no test shows a mixed list).
 *   every entry in turn: flags 0, RIDEV_NOLEGACY, RIDEV_INPUTSINK and both together record
 *       (page, usage) -> flags, target, replacing a record of the same page and usage;
 *       RIDEV_REMOVE with no target drops the record.  Then TRUE; last error is left alone
 *       ([W]: gamepad and joystick usages register on a machine that has neither; it does not
 *       depend on a device being attached).
 * The target is NULL ("raw input events follow the keyboard focus", [MS]) or a window of
 * this process.  One line per process on the window channel says what was registered and
 * that no WM_INPUT will come.
 * STOPS: list NULL or count 0 with a good size (only (NULL, 0, 0) is asserted, and there the
 * size already fails); RIDEV_REMOVE with a target ([MS]: "parameter validation will fail",
 * no code) or of a collection never registered; any other flag (RIDEV_EXCLUDE or
 * RIDEV_PAGEONLY alone, RIDEV_CAPTUREMOUSE / RIDEV_NOHOTKEYS, RIDEV_APPKEYS,
 * RIDEV_EXINPUTSINK: each has rules of its own on the page; RIDEV_DEVNOTIFY: Windows then
 * posts WM_INPUT_DEVICE_CHANGE for the keyboard and mouse that exist, [W]); a target that is
 * not a window, or the desktop; a 17th collection. */
BOOL rt_raw_input_register(PCRAWINPUTDEVICE list, UINT count, UINT cb);

/* The record, for the runtime itself and for tests (GetRegisteredRawInputDevices is not an
 * import of the game).  rt_raw_input_count: how many collections are registered.
 * rt_raw_input_find: TRUE and *out the record of (page, usage) (cbSize-less: the four members
 * as registered), *thread the guest id of the thread that owns the target window (where
 * WM_INPUT would be posted), 0 for no target; FALSE when it is not registered.  `out` and
 * `thread` may be NULL. */
UINT rt_raw_input_count(void);
BOOL rt_raw_input_find(USHORT page, USHORT usage, RAWINPUTDEVICE *out, DWORD *thread);

/* ---- the packets (unit rt-input-script; docs/handoff/rt-contract-input.md) -----------------------
 * Only the scripted input source (rt/input.h) makes packets; with no script none exists.
 *
 * rt_raw_input_new: keep a copy of `packet` and return its handle, the lParam of the WM_INPUT
 * about to be posted.  Handles count up from RT_HRAWINPUT_BASE + 1 and are never reused; the
 * last 4096 packets are kept, an older one is overwritten.
 * rt_raw_input_read: 1 and *out the packet; 0 for a value this runtime never issued; -1 for a
 * handle it issued whose packet is gone (released, or overwritten).
 * rt_raw_input_release: DefWindowProc(WM_INPUT)'s cleanup (C11): the packet is gone.  A value
 * that is no live handle is ignored.
 * rt_raw_input_data: GetRawInputData once the shim has checked its arguments (C15 to C18):
 * `data` NULL writes the size to *size and returns 0; otherwise copies the header (RID_HEADER,
 * 24 bytes) or the packet (RID_INPUT, its dwSize) and returns the number of bytes, leaving
 * *size alone.  STOPS (rt_fatal, "rt_raw_input_data: not implemented: ..."): a handle never
 * issued; a handle whose packet is gone; a buffer smaller than what is copied (Windows
 * answers -1 with ERROR_INSUFFICIENT_BUFFER from memory, C18: not verified, so not given). */
#define RT_HRAWINPUT_BASE   0x00520000u
HRAWINPUT rt_raw_input_new(const RAWINPUT *packet);
int       rt_raw_input_read(HRAWINPUT h, RAWINPUT *out);
void      rt_raw_input_release(HRAWINPUT h);
UINT      rt_raw_input_data(HRAWINPUT h, UINT cmd, void *data, UINT *size);

/* The devices raw input comes from: none without a script; with one, a mouse and a keyboard
 * (the hDevice of every packet's header).  rt_raw_input_devices fills up to `cap` entries of
 * {handle, RIM_TYPE} and returns how many there are. */
#define RT_HRAWDEV_MOUSE     ((HANDLE)(uintptr_t)0x00530001u)
#define RT_HRAWDEV_KEYBOARD  ((HANDLE)(uintptr_t)0x00530002u)
typedef struct { HANDLE device; DWORD type; } RtRawDevice;
unsigned rt_raw_input_devices(RtRawDevice *out, unsigned cap);

#endif /* RT_RAWINPUT_H */
