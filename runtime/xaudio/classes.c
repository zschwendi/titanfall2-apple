/* The sound classes and CoCreateInstance's class table (runtime/include/rt/xaudio.h).
 * Each class presents one face; no destroy function yet, since no object holds anything.
 * Every method of their interfaces has no body, so a call stops with the method's name. */
#include <string.h>

#include "rt/xaudio.h"
#include "rt/mmdev.h"
#include "com_ids.h"            /* generated: rt_iface_<Interface> */

#define CLASS(var, text, record, ...) \
    static RtComIface *const var##_faces[] = { __VA_ARGS__ }; \
    const RtComClass var = { text, var##_faces, sizeof var##_faces / sizeof var##_faces[0], record, NULL, NULL }

CLASS(rt_xaudio_engine_class, "XAudio2",            sizeof(XaEngine),     &rt_iface_IXAudio2);
/* The enumerator's destroy forgets its notification list (runtime/xaudio/mmdev.c). */
static RtComIface *const rt_mm_enumerator_faces[] = { &rt_iface_IMMDeviceEnumerator };
const RtComClass rt_mm_enumerator_class = { "MMDeviceEnumerator", rt_mm_enumerator_faces, 1,
                                            sizeof(MmEnumerator), rt_mm_enumerator_destroy, NULL };

/* The table.  XAudio2 2.6 ({3EDA9B49-2085-498B-9BB2-39A6778493DE}) is NOT in it: the game asks
 * for 2.6 only after 2.7 has failed, and it is answered REGDB_E_CLASSNOTREG like any unknown class. */
static const struct { GUID clsid; const RtComClass *cls; } table[] = {
    { { 0x5A508685, 0xA254, 0x4FBA, { 0x9B, 0x82, 0x9A, 0x24, 0xB0, 0x03, 0x06, 0xAF } }, &rt_xaudio_engine_class },   /* XAudio2 2.7 */
    { { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } }, &rt_mm_enumerator_class },   /* MMDevice enumerator */
};

bool rt_sound_classes = true;                       /* rt/xaudio.h */

const RtComClass *rt_com_class_by_clsid(const GUID *clsid) {
    if (!rt_sound_classes) return NULL;
    for (unsigned i = 0; i < sizeof table / sizeof table[0]; i++)
        if (!memcmp(&table[i].clsid, clsid, sizeof *clsid)) return table[i].cls;
    return NULL;
}

HRESULT rt_com_class_create(Ctx *ctx, const GUID *clsid, const GUID *iid, void **ppv) {
    (void)ctx;
    const RtComClass *cls = rt_com_class_by_clsid(clsid);
    if (!cls) return REGDB_E_CLASSNOTREG;

    void *obj = rt_com_new(cls);                    /* one reference: the game's */
    /* IUnknown is answered by face 0, the object's identity (rt_com_QueryInterface's rule).  Its
     * IID is the root of face 0's chain, so no IID literal is written here. */
    const RtComAncestor *root = &cls->faces[0]->chain[cls->faces[0]->nchain - 1];
    void *face;
    if (!memcmp(iid, &root->iid, sizeof *iid)) face = obj;
    else {
        const RtComIface *want = rt_com_iface_by_iid(iid);
        face = want ? rt_com_face(obj, want) : NULL;
    }
    if (!face) {                                    /* not a face of this class: nothing left alive */
        rt_com_release(obj);
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    *ppv = face;
    return S_OK;
}
