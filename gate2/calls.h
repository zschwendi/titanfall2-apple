/* The first-call table of the Gate 2 driver: the interface between gate2/driver.c, which
 * loads the modules and runs each entry in a child process, and gate2/calls.c, which holds
 * the 30 entries of docs/findings/21-gate2-exports.md section 3.
 *
 * An entry is a function `void run(Call *c)`.  It looks its exports up with EXPORT, calls
 * them with GUEST_CALL (rt/shim.h) and compares with EXPECT.  It ends by returning: with
 * nothing recorded it PASSes; the first EXPECT that does not hold records FAIL with what was
 * got and what was expected and returns; SKIP(reason) records why the entry cannot run in
 * this driver yet.  A fatal report of the runtime ends the child, and the driver prints STOP
 * with the report's first line.
 *
 *     static void run_1(Call *c) {
 *         EXPORT(f, c->tier0, "Plat_GetMemPageSize");
 *         EXPECT("Plat_GetMemPageSize()", (uint32_t)GUEST_CALL(c->ctx, f), 4);
 *     }
 *
 * Rules for an entry:
 *  - memory the guest reads or writes comes from gate2_mem (zeroed, never freed);
 *  - a 32-bit signed argument is passed sign-extended: S32(x);
 *  - a guest function's bool or int result is the low 8 or 32 bits of rax: cast it.
 */
#ifndef GATE2_CALLS_H
#define GATE2_CALLS_H

#include "rt/module.h"
#include "rt/shim.h"
#include "values.h"             /* gate2_stream, for an entry that records a digest of many values */

enum { GATE2_PASS = 0, GATE2_FAIL, GATE2_SKIP };

typedef struct Call {
    Ctx      *ctx;
    RtModule *tier0, *vstdlib;
    bool      map_only;         /* the images are mapped, nothing else: no imports, no TLS, no entry point */
    int       result;           /* GATE2_* */
    char      msg[600];         /* FAIL: what, got, expected.  SKIP: the reason */
} Call;

/* What an entry needs beyond code and its own data; `call --map-only` skips entries with any. */
#define NEEDS_IMPORTS  0x1u     /* calls a Windows function through an import slot */
#define NEEDS_TLS      0x2u     /* reads or writes a thread-local */
#define NEEDS_ENTRY    0x4u     /* a static constructor or the C runtime start-up must have run */

typedef struct Entry {
    int         rank;           /* 1 to 30, the finding's numbering */
    const char *name;           /* the finding's title of the item */
    unsigned    needs;          /* NEEDS_* */
    const char *call;           /* "Call and pass", from the finding */
    const char *check;          /* "Check", from the finding */
    const char *also;           /* "Also needs", from the finding */
    const char *blocked;        /* NULL, or the finding's "skipped inside": functions of the item's static
                                   closure that the lifter skipped when the finding was written */
    void      (*run)(Call *c);  /* NULL: not typed in yet (SKIP) */
} Entry;

extern const Entry    gate2_calls[];

/* The entries of ranks 10 to 30 that are typed in (calls_more.c). */
void gate2_run_10(Call *c);  void gate2_run_11(Call *c);  void gate2_run_12(Call *c);
void gate2_run_13(Call *c);  void gate2_run_14(Call *c);  void gate2_run_15(Call *c);
void gate2_run_17(Call *c);  void gate2_run_18(Call *c);  void gate2_run_19(Call *c);
void gate2_run_20(Call *c);  void gate2_run_21(Call *c);  void gate2_run_22(Call *c);
void gate2_run_23(Call *c);  void gate2_run_24(Call *c);  void gate2_run_25(Call *c);
void gate2_run_26(Call *c);  void gate2_run_27(Call *c);  void gate2_run_28(Call *c);
void gate2_run_29(Call *c);  void gate2_run_30(Call *c);

extern const unsigned gate2_ncalls;

/* The wide set, ranks 31 and up: rows and code in calls_wide.c (not from the finding; each
 * row's `also` text says where its expected values come from). */
extern const Entry    gate2_calls_wide[];
extern const unsigned gate2_ncalls_wide;

/* The address of export `name` of `m`, or 0 after recording FAIL ("no export ..."). */
GUESTFN gate2_export(Call *c, RtModule *m, const char *name);
void    gate2_fail(Call *c, const char *what, uint64_t got, uint64_t want);
/* What EXPECT and EXPECT_TRUE do: record the value under `what` (gate2/values.h: the record is
 * what a run against the real DLLs is compared with), then compare; false after recording FAIL. */
bool    gate2_check(Call *c, const char *what, uint64_t got, uint64_t want);
bool    gate2_check_true(Call *c, const char *what, int holds);
/* Record a value that has no exact expectation, for that comparison only: it cannot fail. */
void    gate2_value(Call *c, const char *what, uint64_t value);
void    gate2_fail_text(Call *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void    gate2_skip(Call *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* A remark printed after PASS: a part of the finding's item that the entry does not run. */
void    gate2_note(Call *c, const char *text);
void   *gate2_mem(size_t size);
/* Say what the entry is about to do: a STOP report then names it ("[during: ...]"). */
void    gate2_progress(const char *what);
/* End the child process now with what `c` has recorded, as if the entry had returned.  For an
 * entry whose result is known on a host thread while its guest call cannot return (rank 29). */
void    gate2_end(Call *c) __attribute__((noreturn));

#define EXPORT(var, mod, name)  GUESTFN var = gate2_export(c, (mod), (name)); if (!var) return
#define EXPECT(what, got, want) \
    do { if (!gate2_check(c, (what), (uint64_t)(got), (uint64_t)(want))) return; } while (0)
#define EXPECT_TRUE(what, cond) do { if (!gate2_check_true(c, (what), (cond) ? 1 : 0)) return; } while (0)
#define SKIP(...)               do { gate2_skip(c, __VA_ARGS__); return; } while (0)
#define S32(x)                  ((uint64_t)(int64_t)(int32_t)(x))

#endif /* GATE2_CALLS_H */
