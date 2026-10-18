/* The test class of the interface layer's tests (runtime/tests/core/com_*.c): one object with
 * two faces, IRtTestShape (which derives from IRtTestNamed) and IRtTestCounter.  The class is
 * defined in runtime/tests/com_fixtures.c; the method bodies are runtime/tests/com/bodies. */
#ifndef RTTEST_COM_H
#define RTTEST_COM_H

#include "rttest_com_ids.h"

typedef struct RtTestShapeState {
    UINT tag;
    UINT next;
    int *destroyed;         /* if not NULL: incremented by the class's destroy */
} RtTestShapeState;

extern const RtComClass rttest_shape_class;        /* faces: IRtTestShape, IRtTestCounter */
extern const GUID rttest_iid_refused;              /* a shape whose tag is not 0 refuses this IID */
extern const RtComClass rttest_counter_class;      /* one face: IRtTestCounter; no state */

/* A root object: one face, IRtTestRoot, which has no IUnknown (its slots are its own methods,
 * no QueryInterface, AddRef or Release, and no IID).  Ended from C (rt_com_release). */
typedef struct RtTestRootState {
    UINT value;             /* what Value returns */
    int *destroyed;         /* if not NULL: incremented by the class's destroy */
} RtTestRootState;

extern const RtComClass rttest_root_class;         /* one face: IRtTestRoot; state RtTestRootState */

#endif
