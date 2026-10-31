/* The payload's entry point (see tf2_payload.h). Built into TF2Game.framework by tools/ios/build-payload.sh
 * together with the runtime, gate3/run.c, runtime/dxback, the module table and the recompiled modules. */
#import <UIKit/UIKit.h>
#include "tf2_payload.h"
#include "rt/host_ios.h"
#include "run.h"

#ifndef TF2_DXMT_FINGERPRINT
#error "build with -DTF2_DXMT_FINGERPRINT=\"...\" from tools/ios/dxmt-fingerprint.sh"
#endif
#ifndef TF2_PAYLOAD_SOURCE
#define TF2_PAYLOAD_SOURCE "unknown"
#endif

/* The runtime keeps the view for the life of the process, so an unretained pointer is safe to hand over. */
static void *payload_create_view(void) { return (__bridge void *)rt_host_ios_create_view(); }
static void payload_set_active(bool active) { rt_host_ios_set_active(active); }

static const struct TF2Payload payload = {
    .abi = TF2_PAYLOAD_ABI,
    .size = sizeof(struct TF2Payload),
    .dxmt = TF2_DXMT_FINGERPRINT,
    .source = TF2_PAYLOAD_SOURCE,
    .create_view = payload_create_view,
    .set_active = payload_set_active,
    .run = gate3_run_embedded,
    .presents = gate3_embedded_presents,
};

__attribute__((visibility("default"))) const struct TF2Payload *tf2_payload(void) { return &payload; }
