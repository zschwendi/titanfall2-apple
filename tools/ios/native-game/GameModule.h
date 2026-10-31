/* The shell side of tools/ios/payload/tf2_payload.h: load TF2Game.framework from the app's Frameworks and
 * call the game through its table. Built only into the TF2Shell target (TF_SPLIT_PAYLOAD). The wrappers carry
 * the names of the runtime functions that the monolithic TFNative target links directly, so main.m calls the
 * same names in both targets. */
#ifndef TF2_GAME_MODULE_H
#define TF2_GAME_MODULE_H

#import <UIKit/UIKit.h>
#include "tf2_payload.h"

/* The loaded payload, or NULL. */
extern const struct TF2Payload *tf2_game;

/* Load the payload once. On failure returns NO and sets *problem to a sentence for the launcher. */
BOOL TF2LoadGameModule(NSString **problem);

static inline UIView *rt_host_ios_create_view(void) { return (__bridge UIView *)tf2_game->create_view(); }
static inline void rt_host_ios_set_active(bool active) { if (tf2_game) tf2_game->set_active(active); }
static inline int gate3_run_embedded(int argc, char **argv) { return tf2_game->run(argc, argv); }
static inline unsigned long long gate3_embedded_presents(void) { return tf2_game ? tf2_game->presents() : 0; }

#endif
