#ifndef TF_ENGINE_CHECK_H
#define TF_ENGINE_CHECK_H
#include <stdint.h>
typedef struct {
    int tier0_loaded, vstdlib_loaded, page_size, mode_flags, utf8_copy, random, clock, passed;
    unsigned modules;
    uint64_t dispatch_entries;
    char error[160];
} TFEngineCheck;
typedef void (*TFEngineStage)(const char *stage, void *context);
TFEngineCheck tf_engine_check(const char *state, const char *game,
                             TFEngineStage stage, void *context);
#endif
