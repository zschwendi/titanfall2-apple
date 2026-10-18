/* What the Gate 2 driver (gate2/driver.c) and the Gate 3 driver (gate3/driver.c) share: the
 * list of lifted modules of a started runtime, the `registry` and `imports` modes over it,
 * and a child process whose fatal report the parent keeps.  gate2/modes.c.
 */
#ifndef GATE2_MODES_H
#define GATE2_MODES_H

#include <stdbool.h>
#include <stdint.h>

#include "rt/module.h"
#include "peek.h"

#define GATE2_MAX_LIFTED 32

/* The lifted modules of the registry, in list order, each with the scan of its DLL. */
typedef struct {
    const char *prog;                       /* the driver's name in what it prints: "gate2" */
    RtModule   *lifted[GATE2_MAX_LIFTED];
    Gate2Scan   scans[GATE2_MAX_LIFTED];
    unsigned    nlifted;
} Gate2Mods;

/* After rt_init: fill `g` from the registry (gate2_scan of every lifted module).  Exits with
 * status 2 when there are more than GATE2_MAX_LIFTED. */
void gate2_mods_collect(Gate2Mods *g, const char *prog);

const Gate2Scan *gate2_scan_of(const Gate2Mods *g, const RtModule *m);

/* Has the module's lifted table a function at `rva`?  A row that is the stub of a function
 * the lifter skipped does not count (rt/module.h). */
bool gate2_has_lifted_entry(const RtModule *m, uint32_t rva);

const Gate2Export *gate2_find_export(const Gate2Scan *s, const char *name);

double      gate2_now_ms(void);
const char *gate2_error_name(uint32_t win32_error);

/* `registry`: one line per module (present or absent, base, size, lifted functions, exports,
 * imports, entry point), then the exported functions with no lifted entry; with `all`, every
 * export.  Returns the exit status. */
int gate2_mode_registry(const Gate2Mods *g, bool all);

/* `imports`: every import of each module and what it binds to, computed from the tables;
 * with `loaded` (the caller has loaded every module), compared with what each slot holds. */
int gate2_mode_imports(const Gate2Mods *g, bool loaded);

/* A child process: fn(arg) with descriptor 2 going to a file, ended by the driver after
 * `timeout_s` seconds.  `first` gets the first line of a fatal report ("FATAL ..."), or else
 * the first line the child wrote there, with the last gate2_progress lines after it;
 * `report` gets the child's output from that first line on, as far as it fits. */
typedef struct { int status; char first[1000]; char report[3000]; } Gate2Child;
void gate2_run_child(void (*fn)(void *), void *arg, unsigned timeout_s, Gate2Child *res);

/* THE QUIET STOP, for both drivers.  A process that a driver starts so that a stop can end it
 * (each child of gate2_run_child: a rank of `gate2 call`, a module of `gate3 load`; and the
 * run of `gate3 run`) asks the runtime for RT_QUIET_STOPS (rt/crash.h, THE QUIET END): a
 * stop prints its report as before and then EXITS WITH STATUS 134 (128 + SIGABRT) instead of
 * dying of SIGABRT, so that macOS writes no crash report for it.  A host fault or a host
 * library's abort() in that process still ends in SIGABRT and a crash report.  On unless
 * GATE_QUIET_STOP=0, which gives every stop its signal back.  The modes that stop in the
 * driver's own process (`gate2 load`, `gate3 load --in-process`, `imports --load`, `words`)
 * do not ask: there a stop is abort() and SIGABRT, as it was. */
bool gate2_quiet_stop(void);            /* the switch: true unless GATE_QUIET_STOP=0 */

#endif /* GATE2_MODES_H */
