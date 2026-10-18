/* Declarations private to runtime/core: how the core files call each other during start-up
 * and in fatal reports.  Nothing outside runtime/core includes this.
 */
#ifndef RT_CORE_H
#define RT_CORE_H

#include "rt/shim.h"
#include "rt/init.h"
#include "rt/dispatch.h"

/* ---- start-up, in the order rt_init calls them ------------------------------------------- */

/* thread.c: create the process block (needs the machine description). */
void rt_process_block_init(void);

/* module.c: build the registry from rt_module_defs and cfg->extra_modules, assign every
 * base, create the native modules with their shim stubs and dispatch entries. */
void rt_module_registry_init(const RtConfig *cfg);

/* guestcall.c: create the return-to-host stub (needs ntdll.dll's native module). */
void rt_guest_call_init(void);

/* thread.c: attach the calling host thread as guest thread 1. */
void rt_thread_attach_main(RtTsoMode tso);

/* ---- traps ---------------------------------------------------------------------------------- */

#define RT_TRAP_POOL 4096       /* distinct trap stubs a process can have */

/* trap.c: the host function behind trap stub number i (0 <= i < RT_TRAP_POOL). */
LiftedFn rt_trap_pool_fn(unsigned i);

/* core/trap.c: the stop of rt_module_export for a look-up in a stubbed module (RT_MOD_STUBBED)
 * that finds no shim: "FATAL unimplemented import GetProcAddress(<module>, <name or #ordinal>)"
 * and the lines of rt_report_context for the calling thread.  Does not return. */
_Noreturn void rt_export_unimplemented(const RtModule *m, const char *name, uint32_t ordinal);

/* module.c: which import trap stub number i stands for.  False if i was never handed out. */
bool rt_native_trap_info(unsigned i, const RtModule **module, const char **name);

/* trap.c: append to `buf` the lines every fatal report shares: the guest caller, the
 * thread, the argument registers, the guest stack.  `ret_addr_at` is the guest address
 * that holds the return address of the call being reported, or 0 if there is none. */
size_t rt_report_context(char *buf, size_t n, Ctx *ctx, uint64_t ret_addr_at);

/* Fatal reports are written by rt_report_die and the other functions of core/report.h,
 * which take no lock (review finding R14). */

#endif /* RT_CORE_H */
