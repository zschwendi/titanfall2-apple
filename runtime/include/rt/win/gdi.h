/* gdi32.dll: the structures and constants the window contract uses
 * (docs/handoff/rt-contract-window.md).  The core that answers is rt/gdi.h.  Layouts are the
 * x64 ones of the Windows SDK; sizes are asserted below and were counted against the member
 * lists of Wine's include/wingdi.h, whose constants these equal (docs/reviews/contracts-check.md).
 */
#ifndef RT_WIN_GDI_H
#define RT_WIN_GDI_H

#include "rt/wintypes.h"
#include "rt/win/base.h"

typedef struct { LONG bmType, bmWidth, bmHeight, bmWidthBytes; WORD bmPlanes, bmBitsPixel; LPVOID bmBits; } BITMAP;
typedef struct { DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount; DWORD biCompression, biSizeImage;
                 LONG biXPelsPerMeter, biYPelsPerMeter; DWORD biClrUsed, biClrImportant; } BITMAPINFOHEADER;
typedef struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;
typedef struct { BITMAP dsBm; BITMAPINFOHEADER dsBmih; DWORD dsBitfields[3]; HANDLE dshSection; DWORD dsOffset; } DIBSECTION;
typedef struct { LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
                 BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality,
                      lfPitchAndFamily; CHAR lfFaceName[32]; } LOGFONTA;
typedef struct { LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
                 BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality,
                      lfPitchAndFamily; WCHAR lfFaceName[32]; } LOGFONTW;
typedef struct { UINT lopnStyle; POINT lopnWidth; COLORREF lopnColor; } LOGPEN;
typedef struct { UINT lbStyle; COLORREF lbColor; ULONG_PTR lbHatch; } LOGBRUSH;

_Static_assert(sizeof(BITMAP) == 32 && sizeof(BITMAPINFOHEADER) == 40 && sizeof(BITMAPINFO) == 44, "bitmaps");
_Static_assert(sizeof(DIBSECTION) == 104 && __builtin_offsetof(DIBSECTION, dshSection) == 88, "DIBSECTION");
_Static_assert(sizeof(LOGFONTA) == 60 && sizeof(LOGFONTW) == 92, "LOGFONT");
_Static_assert(sizeof(LOGPEN) == 16 && sizeof(LOGBRUSH) == 16, "LOGPEN, LOGBRUSH");

/* GetDeviceCaps indexes. */
#define HORZSIZE      4
#define VERTSIZE      6
#define HORZRES       8
#define VERTRES       10
#define BITSPIXEL     12
#define PLANES        14
#define LOGPIXELSX    88
#define LOGPIXELSY    90
#define VREFRESH      116
#define DESKTOPVERTRES 117
#define DESKTOPHORZRES 118

/* GetObjectType-style kinds of a GDI object record (the values of OBJ_*). */
#define OBJ_PEN     1
#define OBJ_BRUSH   2
#define OBJ_FONT    6
#define OBJ_BITMAP  7
#define OBJ_REGION  8

#define BI_RGB        0u
#define BI_BITFIELDS  3u
#define DIB_RGB_COLORS 0u
#define CLR_INVALID   0xFFFFFFFFu
#define TRANSPARENT   1
#define OPAQUE        2
#define PS_SOLID      0
#define PS_NULL       5
#define BS_SOLID      0
#define TA_LEFT       0u
#define TA_UPDATECP   1u
#define TA_RIGHT      2u
#define TA_CENTER     6u
#define TA_TOP        0u
#define TA_BOTTOM     8u
#define TA_BASELINE   24u
#define BLACKONWHITE  1
#define WHITEONBLACK  2
#define COLORONCOLOR  3
#define HALFTONE      4
#define NULLREGION    1
#define SIMPLEREGION  2
#define RGN_ERROR     0
#define HGDI_ERROR    ((HGDIOBJ)(intptr_t)-1)

#endif /* RT_WIN_GDI_H */
