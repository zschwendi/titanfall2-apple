/* Start-up (docs/RUNTIME.md appendix A).  Interface: rt/init.h.
 *
 * rt_init is a fixed sequence of the subsystems' own init functions.  A subsystem that is
 * not built yet still has its init function, in its own file, doing nothing; every other
 * entry point of such a subsystem is fatal.  So a unit that builds a subsystem replaces
 * that subsystem's file and does not edit this one.
 */
#include "core.h"
#include "rt/com.h"
#include "rt/handle.h"
#include "rt/heap.h"
#include "rt/machine.h"
#include "rt/path.h"
#include "rt/time.h"
#include "rt/vm.h"

static RtConfig config;
static bool     started;

/* Subsystems that have no file yet.  Each gets its place in the start-up sequence now, as a
 * weak function that does nothing; the unit that builds the subsystem defines the real one
 * in its own file and does not edit this one.  (The linker prefers the strong definition
 * whenever that file is linked, which it is as soon as any shim uses the subsystem.) */
__attribute__((weak)) void rt_crash_init(void)    { }
__attribute__((weak)) void rt_env_init(void)      { }
__attribute__((weak)) void rt_file_init(void)     { }
__attribute__((weak)) void rt_registry_init(void) { }
__attribute__((weak)) void rt_locale_init(void)   { }
__attribute__((weak)) void rt_unwind_init(void)   { }

const RtConfig *rt_config(void) {
    if (!started) rt_fatal("rt_config before rt_init");
    return &config;
}

void rt_init(const RtConfig *cfg) {
    rt_log_init();
    if (started) rt_fatal("rt_init called twice");
    if (!cfg) rt_fatal("rt_init: no configuration");
    config = *cfg;
    started = true;

    rt_vm_init();                       /* reserve the two arenas before anything else maps memory */
    rt_crash_init();                    /* fault handlers and the alternate stack   [rt-core-crash]    */
    rt_machine_init();
    rt_process_block_init();
    rt_time_init();                     /* clocks                                    [rt-core-time]     */
    rt_disp_init();

    rt_heap_init();                     /* process heap                              [rt-core-heap]     */
    rt_handle_init();                   /* handle table and name space               [rt-core-handle]   */
    rt_path_init();                     /* roots of the virtual C: drive             [rt-core-path]     */
    rt_file_init();                     /* standard handles                          [rt-core-file]     */
    rt_env_init();                      /* environment block and command line        [rt-core-env]      */
    rt_registry_init();                 /* registry tree from the state directory    [rt-core-registry] */
    rt_locale_init();                   /* code page tables                          [rt-core-locale]   */

    rt_module_registry_init(&config);   /* every base; native modules; shim stubs */
    rt_guest_call_init();               /* the return-to-host stub */
    rt_com_init();                      /* vtables and method stubs of the described interfaces (rt/com.h) */
    rt_loader_prescan();                /* trap stubs and lifted functions           [rt-core-loader]   */
    rt_unwind_init();                   /*                                           [rt-core-unwind]   */
    if (config.before_seal) config.before_seal(config.user);
    rt_disp_seal();

    rt_thread_attach_main(config.tso);  /* thread block, guest stack, ordering mode */

    rt_log(RT_CH_LOADER, RT_LL_INFO, "runtime started: %u modules, %zu dispatch entries",
           rt_module_count(), rt_disp_count());
}
