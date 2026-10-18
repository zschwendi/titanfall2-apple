/* The classes of the interface layer's tests (runtime/tests/abi/rttest_com.h). */
#include <string.h>

#include "rttest_com.h"

static void shape_destroy(void *state) {
    RtTestShapeState *s = state;
    if (s->destroyed) ++*s->destroyed;
}

static RtComIface *const shape_faces[] = { &rt_iface_IRtTestShape, &rt_iface_IRtTestCounter };
/* The one IID a shape is "documented" to refuse, and only while its tag is not 0. */
const GUID rttest_iid_refused = { 0x7e57c0de, 0x00fe, 0x4000, { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xde } };
static bool shape_refuses(const void *state, const GUID *iid) {
    const RtTestShapeState *s = state;
    return s->tag != 0 && !memcmp(iid, &rttest_iid_refused, sizeof *iid);
}
const RtComClass rttest_shape_class = { "RtTestShape", shape_faces, 2, sizeof(RtTestShapeState), shape_destroy, shape_refuses };

static RtComIface *const counter_faces[] = { &rt_iface_IRtTestCounter };
const RtComClass rttest_counter_class = { "RtTestCounter", counter_faces, 1, 0, NULL, NULL };

static void root_destroy(void *state) {
    RtTestRootState *s = state;
    if (s->destroyed) ++*s->destroyed;
}

static RtComIface *const root_faces[] = { &rt_iface_IRtTestRoot };
const RtComClass rttest_root_class = { "RtTestRoot", root_faces, 1, sizeof(RtTestRootState), root_destroy, NULL };
