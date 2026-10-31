/* The one interface between the public TF2 shell app and the game payload (TF2Game.framework).
 *
 * The shell contains the launcher and the DXMT frameworks and no game code. The payload is built on the
 * player's own Mac from their own copy of the game: the runtime, the game driver, the Direct3D back end, the
 * module table and the recompiled game modules, linked into one framework. The shell loads it with dlopen
 * and calls only what this table holds, so every hot path stays inside the payload.
 *
 * Change TF2_PAYLOAD_ABI whenever this structure, the meaning of a member, or what the shell does before
 * calling run() (its environment variables and the order of the calls) changes. */
#ifndef TF2_PAYLOAD_H
#define TF2_PAYLOAD_H

#include <stdbool.h>
#include <stdint.h>

#define TF2_PAYLOAD_ABI 1
#define TF2_PAYLOAD_SYMBOL "tf2_payload"
#define TF2_PAYLOAD_PATH "TF2Game.framework/TF2Game"

struct TF2Payload {
    uint32_t abi;                   /* TF2_PAYLOAD_ABI of the payload's build */
    uint32_t size;                  /* sizeof(struct TF2Payload) of the payload's build */
    const char *dxmt;               /* TF2_DXMT_FINGERPRINT the runtime's Direct3D back end was built against */
    const char *source;             /* the payload builder's description of its source, for logs */
    void *(*create_view)(void);     /* the game's UIView (rt_host_ios_create_view); call on the main thread */
    void (*set_active)(bool active);                /* rt_host_ios_set_active; main thread */
    int (*run)(int argc, char **argv);              /* gate3_run_embedded; on a worker thread */
    unsigned long long (*presents)(void);           /* gate3_embedded_presents */
};

/* Exported by the payload; the only symbol it exports. */
const struct TF2Payload *tf2_payload(void);

#endif
