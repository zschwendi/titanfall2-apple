#include "EngineCheck.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "rt/init.h"
#include "rt/dispatch.h"
#include "rt/shim.h"
#include "rt/thread.h"
#include "rt/vm.h"
#include "gate2_ids.h"

/* Tests execute ARM64 functions generated from the owner's two DLLs. Their
 * original PE bytes are data for relocation/export lookup, never executable. */
TFEngineCheck tf_engine_check(const char *state, const char *game,
                             TFEngineStage stage, void *context) {
    TFEngineCheck r = {0};
    stage("runtime-init", context);
    RtConfig cfg = {.state_dir = state, .install_root = game, .tso = RT_TSO_OFF,
        .extra_modules = gate2_module_defs, .n_extra_modules = gate2_module_def_count,
        .extra_shims = gate2_shim_table, .n_extra_shims = gate2_shim_count};
    rt_init(&cfg);
    r.modules = rt_module_count(); r.dispatch_entries = rt_disp_count();
    Ctx *ctx = rt_main_ctx();
    stage("load-tier0", context);
    RtModule *tier0 = rt_module_load_a(ctx, "tier0.dll", 0);
    r.tier0_loaded = tier0 != NULL;
    if (!tier0) { snprintf(r.error, sizeof r.error, "tier0 load failed: %u", rt_thread_get_last_error()); return r; }
    stage("load-vstdlib", context);
    RtModule *vstdlib = rt_module_load_a(ctx, "vstdlib.dll", 0);
    r.vstdlib_loaded = vstdlib != NULL;
    if (!vstdlib) { snprintf(r.error, sizeof r.error, "vstdlib load failed: %u", rt_thread_get_last_error()); return r; }

#define EXPORT(var, module, name) \
    GUESTFN var = rt_module_export(module, name, 0); \
    if (!var) { snprintf(r.error, sizeof r.error, "Missing export: %s", name); return r; }
    EXPORT(page, tier0, "Plat_GetMemPageSize");
    EXPORT(set_mode, tier0, "Plat_SetToolMode");
    EXPORT(get_mode, tier0, "Plat_IsInToolMode");
    EXPORT(copy, vstdlib, "V_UTF8_strncpy");
    EXPORT(seed, vstdlib, "RandomSeed");
    EXPORT(get_seed, vstdlib, "GetCurrentRandomSeed");
    EXPORT(random_int, vstdlib, "RandomInt");
    EXPORT(clock, tier0, "Plat_FloatTime");
#undef EXPORT

    stage("check-page-size", context);
    r.page_size = (uint32_t)GUEST_CALL(ctx, page) == 4;
    stage("check-mode-flags", context);
    GUEST_CALL(ctx, set_mode, 1);
    int enabled = (uint8_t)GUEST_CALL(ctx, get_mode) == 1;
    GUEST_CALL(ctx, set_mode, 0);
    r.mode_flags = enabled && (uint8_t)GUEST_CALL(ctx, get_mode) == 0;

    stage("check-utf8-copy", context);
    uint64_t memory = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    if (!memory) { snprintf(r.error, sizeof r.error, "Test buffer allocation failed"); return r; }
    char *source = (char *)(uintptr_t)memory, *destination = source + 128;
    strcpy(source, "A\xc3\xa9\xe2\x82\xacZ");
    memset(destination, 0x7F, 128);
    uint64_t returned = GUEST_CALL(ctx, copy, destination, source, 6);
    r.utf8_copy = returned == (uint64_t)(uintptr_t)destination &&
        memcmp(destination, "A\xc3\xa9", 3) == 0 && destination[3] == 0;
    rt_vm_free(memory, 0, MEM_RELEASE);

    stage("check-random", context);
    GUEST_CALL(ctx, seed, 1337);
    int seeded = (int32_t)GUEST_CALL(ctx, get_seed) == 1337;
    r.random = seeded && (int32_t)GUEST_CALL(ctx, random_int, 7, 7) == 7;
    stage("check-clock", context);
    double t0 = rt_guest_call_fp(ctx, clock, NULL, 0);
    double t1 = rt_guest_call_fp(ctx, clock, NULL, 0);
    r.clock = isfinite(t0) && isfinite(t1) && t0 >= 0 && t1 >= t0;
    r.passed = r.page_size && r.mode_flags && r.utf8_copy && r.random && r.clock;
    if (!r.passed) snprintf(r.error, sizeof r.error, "One or more engine export checks failed");
    return r;
}
