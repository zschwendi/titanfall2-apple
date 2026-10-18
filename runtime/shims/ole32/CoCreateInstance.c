/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/combaseapi/nf-combaseapi-cocreateinstance
 * RT-CASES:  class not registered returns REGDB_E_CLASSNOTREG and sets *ppv to NULL; class not registered logs the class and the interface; ppv NULL returns E_POINTER; leaves last error alone; the thread's COM state is not looked at (Windows gives CO_E_NOTINITIALIZED without COM); a class of the table makes an object and writes its face for the IID through *ppv (S_OK); a class of the table asked for an interface it does not present gives E_NOINTERFACE and *ppv NULL with nothing left alive; a non-NULL outer for a class of the table gives CLASS_E_NOAGGREGATION and makes nothing; the XAudio2 2.6 class is not in the table: REGDB_E_CLASSNOTREG (the game asks for it only after 2.7 failed); the XAudio2 object: AddRef and Release twice give 2, 1, 0; calling IXAudio2::Initialize traps with the method's name (it has no body yet); LoadLibraryA of xaudio2_7.dll gives a module
 * RT-TRAPS:  rclsid NULL; riid NULL
 */
#include "rt/shim.h"
#include "rt/win/com.h"
#include "rt/xaudio.h"          /* the class table: rt_com_class_by_clsid, rt_com_class_create */

/* Not in rt/win: the one error this shim needs of its own (winerror.h: CLASS_E_NOAGGREGATION). */
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110)

/* Writes a GUID in the registry's form, 36 characters and a NUL (rt/win/com.h says which
 * member goes where).  Two GUIDs have the same text only if they have the same 16 bytes. */
static char *put_hex(char *p, uint64_t v, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = digits - 1; i >= 0; i--) *p++ = hex[(v >> (4 * i)) & 15];
    return p;
}

static void guid_text(char out[37], const GUID *g)
{
    char *p = out;
    p = put_hex(p, g->Data1, 8);    *p++ = '-';
    p = put_hex(p, g->Data2, 4);    *p++ = '-';
    p = put_hex(p, g->Data3, 4);    *p++ = '-';
    p = put_hex(p, g->Data4[0], 2);
    p = put_hex(p, g->Data4[1], 2); *p++ = '-';
    for (int i = 2; i < 8; i++) p = put_hex(p, g->Data4[i], 2);
    *p = '\0';
}

SHIM(ole32, HRESULT, CoCreateInstance, REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv)
{
    char clsid[37], iid[37];

    /* No class is provided, so the aggregate and the context play no part. */
    (void)dwClsContext;

    if (!ppv) return E_POINTER;
    if (!rclsid) RT_UNIMPLEMENTED(ctx, "CoCreateInstance with rclsid NULL");
    if (!riid) RT_UNIMPLEMENTED(ctx, "CoCreateInstance with riid NULL");

    *ppv = NULL;
    guid_text(clsid, rclsid);
    guid_text(iid, riid);
    const RtComClass *cls = rt_com_class_by_clsid(rclsid);
    if (!cls) {
        /* Logged once per class: the key is the CLSID's text, which holds its 16 bytes. */
        rt_log_once(RT_CH_IMPORT, RT_LL_INFO, clsid,
                    "CoCreateInstance: class {%s} is not registered (interface {%s})", clsid, iid);
        return REGDB_E_CLASSNOTREG;
    }
    /* An aggregate is refused before anything is made: the table's classes do not aggregate. */
    HRESULT hr = pUnkOuter ? CLASS_E_NOAGGREGATION : rt_com_class_create(ctx, rclsid, riid, ppv);
    rt_log(RT_CH_IMPORT, RT_LL_INFO, "CoCreateInstance: class %s {%s} interface {%s}: result 0x%08lX",
           cls->name, clsid, iid, (unsigned long)hr);
    return hr;
}
