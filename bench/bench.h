/* Shared between bench/driver.c and the C that bench/gen.py generates from
 * the tables in bench/functions/.  See bench/README.md. */
#ifndef BENCH_H
#define BENCH_H

#include <stddef.h>
#include <stdint.h>

#define BENCH_MAXARGS 12

/* ---- how one argument is described ---------------------------------------------------- */

enum { A_INT, A_BUF, A_STRUCT, A_U128, A_CB };            /* ArgDesc.kind */
enum { D_MIXED, D_LT, D_MASK, D_EQ, D_PICK };             /* ArgDesc.dist (integers) */
enum { F_BYTES, F_STR, F_HEX, F_I32SORTED, F_ZERO, F_LIST16, F_SMALL, F_SMALL32, F_SMALL64 };  /* ArgDesc.fill */

typedef struct ArgDesc {
    uint8_t  kind;
    uint8_t  bits;          /* A_INT: 8, 16, 32 or 64 */
    uint8_t  is_signed;     /* A_INT */
    uint8_t  dist;          /* A_INT: how values are drawn */
    uint64_t p0;            /* D_LT: bound, D_MASK: mask, D_EQ: the value */
    const uint64_t *picks;  /* D_PICK */
    uint32_t npicks;
    uint32_t size;          /* A_BUF: bytes; A_STRUCT: sizeof */
    uint8_t  align;         /* A_STRUCT */
    uint8_t  fill;          /* A_BUF */
    uint8_t  nocmp;         /* A_BUF: contents are not compared (layout is ABI-specific) */
    uint8_t  mut;           /* A_BUF with copyof: change one byte in three cases of four */
    int8_t   copyof;        /* A_BUF: starts as a copy of this argument's buffer, or -1 */
    int8_t   alias;         /* A_BUF: no storage of its own, points into this argument's buffer, or -1 */
    int32_t  alias_off;
    const uint8_t *offs;    /* A_BUF: the pointer is the buffer start plus one of these */
    uint32_t noffs;
    uint8_t  variadic;      /* passed after the "..." of a variadic prototype */
} ArgDesc;

enum { R_VOID, R_INT, R_STRUCT, R_U128 };                 /* FuncDesc.ret_kind */

/* One prepared input.  v[i] is the argument value: an integer already reduced to its type
 * (zero- or sign-extended to 64 bits), a pointer to a buffer, or for a struct or 128-bit
 * argument a pointer to its bytes. */
typedef struct Case {
    uint64_t v[BENCH_MAXARGS];
    uint8_t *chunk;         /* all buffers of this case, with guard bytes between them */
    uint8_t *pristine;      /* the chunk as generated */
    uint32_t chunk_size;
    uint32_t garbage;       /* seed for the undefined upper bits of narrow register arguments */
    struct { uint32_t off, size; uint8_t nocmp; } buf[BENCH_MAXARGS];
} Case;

/* Calls the native function once.  Returns the integer result zero-extended from its type,
 * or copies a struct result to ret and returns 0. */
typedef uint64_t (*NativeCall)(void *fn, const uint64_t *v, void *ret);
/* The timed native loop: reps passes over n cases, optionally restoring each case's buffers
 * first.  Returns a checksum of the results so the calls cannot be optimised away. */
typedef uint64_t (*NativeLoop)(void *fn, const Case *cs, int n, long reps, int restore);

typedef struct FuncDesc {
    const char *file;       /* corpus source without extension: the module is one_<file> */
    const char *name;       /* exported name */
    const char *kind;       /* arith, mem, call, switch, virtual, atomic, probe */
    uint8_t  ret_kind;
    uint8_t  ret_bits;      /* R_INT */
    uint32_t ret_size;      /* R_STRUCT, R_U128 */
    uint8_t  nargs;
    const ArgDesc *args;
    uint8_t  restore;       /* timing: restore the buffers before every call */
    uint8_t  notime;        /* compared but not timed */
    NativeCall native_call;
    NativeLoop native_loop;
    int line;               /* line in bench/functions/<file>.tbl */
} FuncDesc;

extern const FuncDesc bench_funcs[];
extern const int bench_nfuncs;

/* ---- one lifted module ---------------------------------------------------------------- */

struct Ctx;
typedef void (*BenchLiftedFn)(struct Ctx *);
typedef struct { uint32_t rva; BenchLiftedFn fn; } BenchLiftedEntry;   /* = LiftedEntry of the lifted C */

typedef struct ModuleDesc {
    const char *file;                   /* "abi" */
    const char *dll;                    /* path of one_<file>_O2.dll */
    const char *dylib;                  /* path of the native build */
    const void *table;                  /* one_<file>_table: {rva, fn}, sorted, {0,0}-terminated */
    const size_t *table_count;
    uint64_t *g_base;                   /* &g_base_one_<file> */
    const uint32_t *skipped;            /* entry RVAs the lifter skipped (from its report) */
    uint32_t nskipped;
    const char *native_note;            /* non-NULL when the native side is not the corpus source */
} ModuleDesc;

extern const ModuleDesc bench_modules[];
extern const int bench_nmodules;

/* The host callback (argument kind cb0): the same function on both sides.  Natively it is
 * called directly; from lifted code it is reached through rt_dispatch_fn at a guest address
 * that belongs to no module, the way an import implemented by the runtime would be. */
int64_t bench_cb0(int32_t v, void *user);

#endif
