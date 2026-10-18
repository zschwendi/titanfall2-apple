/* RT-STATUS: full
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-createfonta
 * RT-CASES:  creates a font record from the fourteen arguments; a NULL face name is an empty face name; a face name of 40 bytes is cut to its first 31; success leaves last error alone
 * RT-TRAPS:  none
 */
#include "rt/shim.h"
#include "rt/gdi.h"

SHIM(gdi32, HFONT, CreateFontA, INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
{
    /* The core takes a LOGFONTA: the five INTs into the LONG members, the low byte of each
     * DWORD into its BYTE member, and the face name up to 31 bytes (rt/gdi.h, rt_gdi_create_font_a).
     * The core never fails, so the shim sets no last error. */
    LOGFONTA lf = {0};
    int i;

    lf.lfHeight = cHeight;
    lf.lfWidth = cWidth;
    lf.lfEscapement = cEscapement;
    lf.lfOrientation = cOrientation;
    lf.lfWeight = cWeight;
    lf.lfItalic = (BYTE)bItalic;
    lf.lfUnderline = (BYTE)bUnderline;
    lf.lfStrikeOut = (BYTE)bStrikeOut;
    lf.lfCharSet = (BYTE)iCharSet;
    lf.lfOutPrecision = (BYTE)iOutPrecision;
    lf.lfClipPrecision = (BYTE)iClipPrecision;
    lf.lfQuality = (BYTE)iQuality;
    lf.lfPitchAndFamily = (BYTE)iPitchAndFamily;
    if (pszFaceName) {
        for (i = 0; i < 31 && pszFaceName[i]; i++)
            lf.lfFaceName[i] = pszFaceName[i];
    }
    return rt_gdi_create_font_a(&lf);
}
