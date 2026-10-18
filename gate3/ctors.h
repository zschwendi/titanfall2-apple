/* Counting the static constructors a module's entry point runs (gate3/ctors.c).
 *
 * WHAT IS COUNTED.  The C runtime start-up that Visual Studio links into a DLL walks tables
 * of function pointers (the C initializers, then the C++ constructors of static objects),
 * calling every entry that is not null.  The driver finds those tables in the DLL, and puts
 * a counting function in front of each entry's row in the module's lifted function table, so
 * that the call the start-up makes through the table is seen.  "Constructors run" is the
 * number of distinct entries entered.
 *
 * HOW THE TABLES ARE FOUND (a rule about compiler output, not a fact about Windows): two
 * `lea reg, [rip+x]` instructions in a row in an executable section, whose targets lo < hi
 * are 8-aligned apart in a section that is not executable, where the first slot is null and
 * every other slot of [lo, hi) is null or the address of a function start the lifter found,
 * at least one of them not null.  That is the shape `_initterm(first, last)` has at its call
 * site.  Checked for tier0 and vstdlib against the tables finding 21 found by reading the
 * start-up code (docs/handoff/gate3-build.md).  A DLL built without that start-up has no such
 * table and is reported so; a table of the same shape that the start-up does not walk at
 * load (the terminators run at unload) is counted apart, as "not walked".
 *
 * The counting function is a host-side hop in front of the lifted function: the guest sees
 * no difference.  `gate3 load --no-count` loads without it.
 */
#ifndef GATE3_CTORS_H
#define GATE3_CTORS_H

#include "modes.h"

#define GATE3_MAX_TABLES 8
#define GATE3_CTOR_POOL  0x8000         /* counting functions: entries that can be counted, all modules together */

typedef struct {
    uint32_t  lo, hi;                   /* rvas: the slots are [lo, hi) */
    uint32_t  site;                     /* rva of the first of the two lea instructions */
    unsigned  nonnull;
} Gate3InitTable;

typedef struct {
    unsigned       ntables;
    Gate3InitTable tables[GATE3_MAX_TABLES];
    unsigned       nfn;                 /* entries that are not null, all tables */
    uint32_t      *fn_rva;              /* their targets (rva) ... */
    uint8_t       *fn_table;            /* ... and the table each is in */
} Gate3Ctors;

/* What the parent reads after a child has ended, however it ended.  Shared memory. */
typedef struct {
    double   t_start_ms;                /* when the load being measured began (gate2_now_ms) */
    double   ms;                        /* how long it took, when it returned */
    uint32_t returned;                  /* 1: rt_module_load came back */
    uint32_t loaded;                    /* 1: it gave a module */
    uint32_t error;                     /* last error when it did not */
    uint32_t hits[GATE2_MAX_LIFTED][GATE3_MAX_TABLES];     /* distinct entries entered, by module and table */
    uint8_t  seen[GATE3_CTOR_POOL];
} Gate3Shared;

extern Gate3Shared *gate3_shared;       /* the driver maps it, shared, before it starts a child */

/* After rt_init: find the initializer tables of lifted module `m`, reading the DLL through
 * the loader.  The arrays are never freed. */
void gate3_ctor_scan(RtModule *m, Gate3Ctors *out);

/* Before rt_init: a copy of `defs` (n rows, the generated module table) in which lifted
 * module i's function table has a counting function in the row of every entry of ctors[i]
 * (i counts lifted rows, in order).  Entries beyond the pool are left uncounted; *uncounted
 * gets how many.  gate3_shared must be mapped before any wrapped module is loaded. */
const RtModuleDef *gate3_ctor_wrap(const RtModuleDef *defs, unsigned n, const Gate3Ctors *ctors, unsigned nctors,
                                   unsigned *uncounted);

#endif /* GATE3_CTORS_H */
