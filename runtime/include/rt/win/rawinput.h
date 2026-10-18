/* Raw input (winuser.h): the types RegisterRawInputDevices and GetRawInputData name.  A file of
 * its own so that it does not meet the window unit's rt/win/user.h in a merge (unit
 * rt-census-rows).  RAWINPUTDEVICE and the RIDEV_ values are Microsoft's page
 * ns-winuser-rawinputdevice, the RID_ values the page nf-winuser-getrawinputdata (both read
 * 2026-10-11); RAWINPUTHEADER's layout is from memory of winuser.h (its size, 24 on x64, is
 * what a caller passes as cbSizeHeader). */
#ifndef RT_WIN_RAWINPUT_H
#define RT_WIN_RAWINPUT_H

#include "rt/wintypes.h"

typedef struct { USHORT usUsagePage, usUsage; DWORD dwFlags; HWND hwndTarget; } RAWINPUTDEVICE;
typedef RAWINPUTDEVICE *PRAWINPUTDEVICE;
typedef const RAWINPUTDEVICE *PCRAWINPUTDEVICE;
typedef struct { DWORD dwType, dwSize; HANDLE hDevice; WPARAM wParam; } RAWINPUTHEADER;
typedef HANDLE HRAWINPUT;
_Static_assert(sizeof(RAWINPUTDEVICE) == 16 && sizeof(RAWINPUTHEADER) == 24, "raw input structures");

#define RIDEV_REMOVE        0x00000001u
#define RIDEV_EXCLUDE       0x00000010u
#define RIDEV_PAGEONLY      0x00000020u
#define RIDEV_NOLEGACY      0x00000030u
#define RIDEV_INPUTSINK     0x00000100u
#define RIDEV_CAPTUREMOUSE  0x00000200u
#define RIDEV_NOHOTKEYS     0x00000200u
#define RIDEV_APPKEYS       0x00000400u
#define RIDEV_EXINPUTSINK   0x00001000u
#define RIDEV_DEVNOTIFY     0x00002000u

#define RID_INPUT           0x10000003u
#define RID_HEADER          0x10000005u

/* The packets (unit rt-input-script; docs/handoff/rt-contract-input.md, C1 to C9: Microsoft's
 * pages RAWMOUSE, RAWKEYBOARD and RAWINPUTHEADER, read 2026-10-11).  RAWINPUT itself, the
 * header followed by the union, is from memory of winuser.h; its size for a mouse packet, 48,
 * is what the recorded game was answered 119,601 times (docs/trace/input.md). */
typedef struct {
    USHORT usFlags;
    union { ULONG ulButtons; struct { USHORT usButtonFlags, usButtonData; }; };
    ULONG  ulRawButtons;
    LONG   lLastX, lLastY;
    ULONG  ulExtraInformation;
} RAWMOUSE;
typedef struct { USHORT MakeCode, Flags, Reserved, VKey; UINT Message; ULONG ExtraInformation; } RAWKEYBOARD;
typedef struct { DWORD dwSizeHid, dwCount; BYTE bRawData[1]; } RAWHID;
typedef struct { RAWINPUTHEADER header; union { RAWMOUSE mouse; RAWKEYBOARD keyboard; RAWHID hid; } data; } RAWINPUT;
_Static_assert(sizeof(RAWMOUSE) == 24 && sizeof(RAWKEYBOARD) == 16 && sizeof(RAWINPUT) == 48, "raw input packets");

#define RIM_INPUT           0u          /* WM_INPUT's wParam: in the foreground */
#define RIM_INPUTSINK       1u          /* in the background (RIDEV_INPUTSINK) */
#ifndef RIM_TYPEMOUSE                   /* rt/win/user.h has the two as plain 0 and 1 */
#define RIM_TYPEMOUSE       0
#define RIM_TYPEKEYBOARD    1
#endif
#define MOUSE_MOVE_RELATIVE 0u
#define RI_MOUSE_LEFT_BUTTON_DOWN   0x0001u
#define RI_MOUSE_LEFT_BUTTON_UP     0x0002u
#define RI_MOUSE_RIGHT_BUTTON_DOWN  0x0004u
#define RI_MOUSE_RIGHT_BUTTON_UP    0x0008u
#define RI_MOUSE_MIDDLE_BUTTON_DOWN 0x0010u
#define RI_MOUSE_MIDDLE_BUTTON_UP   0x0020u
#define RI_KEY_MAKE         0u
#define RI_KEY_BREAK        1u
#define RI_KEY_E0           2u

#endif /* RT_WIN_RAWINPUT_H */
