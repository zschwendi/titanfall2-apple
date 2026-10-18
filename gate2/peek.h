/* What the Gate 2 driver needs from inside the loader, and nothing else of it.
 *
 * gate2/driver.c uses the runtime's public headers only.  Two things it must do have no public
 * interface, because they happen before a module is loaded: listing the exports and imports
 * of a DLL the loader has only verified (the `registry` and `imports` modes), and mapping an
 * image without running its entry point (`call --map-only`).  peek.c does both through the
 * loader's private header (runtime/core/loader.h) and is the only file here that includes it.
 * The DLL is read by the loader's own reader (rt_ldr_image_scan: identity checked), never by
 * the driver.
 */
#ifndef GATE2_PEEK_H
#define GATE2_PEEK_H

#include "rt/module.h"

enum { GATE2_EXP_CODE = 0, GATE2_EXP_DATA, GATE2_EXP_FORWARDED };

typedef struct {
    const char *name;           /* NULL: exported by ordinal only */
    uint32_t    ordinal;        /* biased by the directory's Base */
    uint32_t    rva;
    uint8_t     kind;           /* GATE2_EXP_*: CODE = the rva lies in an executable section */
} Gate2Export;

typedef struct {
    const char *dll;            /* as the import descriptor spells it */
    const char *name;           /* NULL: by ordinal */
    uint32_t    ordinal;
    uint32_t    slot_rva;       /* the import address table slot */
} Gate2Import;

typedef struct {
    bool         present;       /* false: no DLL file at start-up; nothing below is filled in */
    const char  *host_path;
    uint32_t     entry_rva;     /* 0: no entry point */
    int32_t      tls_index;     /* the static TLS index the pre-scan gave the module; -1: no TLS directory */
    unsigned     nsections;
    unsigned     nexports, nimports;
    Gate2Export *exports;       /* in ordinal order */
    Gate2Import *imports;       /* in file order */
} Gate2Scan;

/* Read lifted module `m`'s DLL through the loader and list its exports and imports.  The
 * strings point into a buffer that is never freed.  Fatal (rt_fatal) on a malformed table. */
void gate2_scan(RtModule *m, Gate2Scan *out);

/* Build the image of `m` at its base and give its pages their protections, without binding
 * imports, without TLS and without calling the entry point (steps 1 and 4 of rt_module_load).
 * Code that touches no import, no thread-local and nothing a static constructor set up can
 * then be called.  Not to be mixed with rt_module_load in one process. */
void gate2_map_only(RtModule *m);

/* Which parts of the loader's pre-scan are the driver's own stand-ins in this build, as text
 * for a report line; "" when the runtime's are used. */
const char *gate2_standins(void);

#endif /* GATE2_PEEK_H */
