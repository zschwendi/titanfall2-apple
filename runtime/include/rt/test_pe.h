/* Test support for the loader's tests: lifted modules a test controls.
 *
 * The test program's module list (runtime/tests/abi/modules.def) has, beside the corpus
 * DLL, three lifted modules that exist only for tests: t_lift_a.dll, t_lift_b.dll and
 * t_lift_c.dll ("slots" 'a', 'b', 'c'), each with SizeOfImage 0x10000 and an unchecked
 * identity.  A slot has no DLL file until a test makes one, so in every other test the
 * three are ABSENT (rt/module.h).  A fourth slot, 'd', is `t_ui(11).dll` in the directory
 * r2\paks\Win64 of the install root, a late row (RT_MODDEF_LATE) with the lifter module name
 * t_lift_d: the shape of the game's ui(11).dll.  Its file is written with rt_test_pe_write_in.  A test that wants one writes the file BEFORE the runtime
 * starts, in TEST_SETUP (rt/test.h), because rt_init reads every lifted module's file:
 *
 *     static void dllmain(Ctx *ctx) { ctx->r[0] = 1; GUEST_RET(ctx); }
 *
 *     TEST_SETUP(core, loader_example)
 *     {
 *         RtTestPe *pe = rt_test_pe_new();
 *         rt_test_pe_entry(pe, RT_TEST_LIFT_RVA(0));
 *         rt_test_pe_export(pe, "Answer", 1, RT_TEST_LIFT_RVA(1));
 *         rt_test_pe_import(pe, "kernel32.dll", "GetLastError", 0, 0x2000);
 *         rt_test_pe_write(pe, "t_lift_a.dll");
 *         rt_test_lift_set('a', 0, dllmain);
 *     }
 *
 * THE CODE OF A SLOT.  The "lifted functions" of a slot are eight host functions at the
 * RVAs RT_TEST_LIFT_RVA(0..7) = 0x1000, 0x1010, ... 0x1070, registered in the dispatch
 * table like any lifted function.  rt_test_lift_set(slot, i, fn) makes function i behave
 * as `fn`, which is written like a GUEST_FN body: arguments in ctx->r[1], r[2], r[8],
 * r[9], result in ctx->r[0], leave with GUEST_RET(ctx).  It may be set in TEST_SETUP or in
 * the test body, and changed at any time.  A function that is called without being set is
 * a fatal report "fixture function t_lift_<slot>.dll+0x<rva> is not set".
 *
 * THE IMAGE OF A SLOT is always laid out the same way, so a test only says what is in it:
 *     0x0000  headers            ImageBase 0x180000000, x64, DLL, timestamp 0x5A5A5A5A
 *     0x1000  .text   0x1000  R X   zero bytes (the code is the eight host functions)
 *     0x2000  .data   0x1000  R W   what the test puts there: import slots, the TLS index, data
 *     0x3000  .rdata  0x1000  R     what the test puts there: TLS template, callback array, data
 *     0x8000  .tables         R     written by rt_test_pe_write: export directory, import
 *                                   descriptors and name tables, TLS directory, exception
 *                                   directory (rt_test_pe_pdata), relocations
 * SizeOfImage is 0x10000.  The data directories point into .tables and exist only for
 * what the test asked for (no export directory if nothing was exported, and so on).
 *
 * Every function here aborts the test program with a message on misuse (an RVA outside the
 * section it belongs in, a slot letter that is not a, b or c, an import slot not 8-aligned).
 */
#ifndef RT_TEST_PE_H
#define RT_TEST_PE_H

#include "rt/test.h"
#include "rt/win/pe.h"

#define RT_TEST_LIFT_FNS      8
#define RT_TEST_LIFT_RVA(i)   (0x1000u + 0x10u * (unsigned)(i))
#define RT_TEST_PE_BASE       UINT64_C(0x180000000)      /* the preferred base of every slot image */
#define RT_TEST_PE_TIMESTAMP  0x5A5A5A5Au

void rt_test_lift_set(char slot, unsigned i, LiftedFn fn);

/* ---- files in the test's install root (C:\Titanfall2) -----------------------------------------
 * `rel` is a path under the install root with forward slashes ("bin/x64_retail/x.dll");
 * missing directories are created.  A lifted module's file belongs in bin/x64_retail. */
void rt_test_install_write(const char *rel, const void *bytes, size_t n);
void rt_test_install_copy(const char *host_src, const char *rel);

/* ---- building a slot's DLL file --------------------------------------------------------------- */

typedef struct RtTestPe RtTestPe;

/* A new image description with the layout above and nothing in it. */
RtTestPe *rt_test_pe_new(void);

/* Copy `n` bytes to RVA `rva`; [rva, rva + n) must lie inside .text, .data or .rdata. */
void rt_test_pe_put(RtTestPe *pe, uint32_t rva, const void *bytes, uint32_t n);
void rt_test_pe_put32(RtTestPe *pe, uint32_t rva, uint32_t v);
void rt_test_pe_put64(RtTestPe *pe, uint32_t rva, uint64_t v);

/* Store the virtual address of `target_rva` at `rva`, as a linker does: the 8 bytes hold
 * RT_TEST_PE_BASE + target_rva and a DIR64 relocation is recorded for `rva`, so after
 * loading they hold base + target_rva. */
void rt_test_pe_put_va(RtTestPe *pe, uint32_t rva, uint32_t target_rva);

/* Write no relocation directory, whatever rt_test_pe_put_va recorded (an image linked
 * without /dynamicbase).  The stored addresses then stay RT_TEST_PE_BASE + target. */
void rt_test_pe_no_relocs(RtTestPe *pe);

/* AddressOfEntryPoint.  Without this call the image has none (0). */
void rt_test_pe_entry(RtTestPe *pe, uint32_t rva);

/* Add an export.  `name` NULL makes it reachable by ordinal only.  `ordinal` is the real
 * ordinal (the directory's Base is the smallest ordinal given; gaps get function RVA 0).
 * `rva` is any RVA of the image (a function or data).  rt_test_pe_forward adds a forwarded
 * export instead: its RVA points at the string `forwarder` ("OTHER.Name") inside the export
 * directory, which is how a forwarder is recognised. */
void rt_test_pe_export(RtTestPe *pe, const char *name, uint32_t ordinal, uint32_t rva);
void rt_test_pe_forward(RtTestPe *pe, const char *name, uint32_t ordinal, const char *forwarder);

/* Add an import of `name` (or, with name NULL, of `ordinal`) from `dll`, whose slot is the
 * 8 bytes at `slot_rva` (in .data, 8-aligned).  Imports of one DLL must be added one after
 * another with slots 8 bytes apart, ascending: they become one descriptor whose FirstThunk
 * is the first slot; the test leaves 8 zero bytes after the last slot of a DLL.  The file
 * holds 0xBAD0BAD0BAD0BAD0 in every slot, so a slot the loader did not fill is visible.
 * Descriptors are written in the order the DLLs first appear. */
void rt_test_pe_import(RtTestPe *pe, const char *dll, const char *name, uint32_t ordinal, uint32_t slot_rva);

/* Give the image a TLS directory: template = the `template_size` bytes at `template_rva`
 * (in .rdata), then `zero_fill` zero bytes; AddressOfIndex = `index_rva` (4 bytes in
 * .data); AddressOfCallBacks = `callbacks_rva`, a 0-terminated array of virtual addresses
 * the test wrote with rt_test_pe_put_va (0: no callbacks).  The four addresses in the
 * directory are virtual addresses with relocations, as a linker writes them. */
void rt_test_pe_tls(RtTestPe *pe, uint32_t template_rva, uint32_t template_size, uint32_t zero_fill,
                    uint32_t index_rva, uint32_t callbacks_rva);

/* Add one row (RUNTIME_FUNCTION) to the image's exception directory: the function at
 * [begin_rva, end_rva) has its UNWIND_INFO at `unwind_rva`.  If `unwind_info` is not NULL its
 * `n` bytes are also copied to `unwind_rva`, which must then lie in .rdata; with NULL the row
 * only refers to bytes put there some other way (two rows may share one UNWIND_INFO, and a
 * malformed row may point anywhere).  The bytes are the test's own: nothing is checked or
 * padded.  Rows may be added in any order; rt_test_pe_write sorts them by begin_rva.  The
 * code bytes of the function, if a test needs any (an epilogue), go to .text with
 * rt_test_pe_put.  Without this call the image has no exception directory. */
void rt_test_pe_pdata(RtTestPe *pe, uint32_t begin_rva, uint32_t end_rva, uint32_t unwind_rva,
                      const void *unwind_info, uint32_t n);

/* Lay the image out, write it as <install root>/bin/x64_retail/<file> ("t_lift_a.dll") and
 * free `pe`.  Returns the number of bytes written. */
size_t rt_test_pe_write(RtTestPe *pe, const char *file);

/* The same into another directory of the install root (forward slashes): slot d's file is
 * rt_test_pe_write_in(pe, "r2/paks/Win64", "t_ui(11).dll"). */
size_t rt_test_pe_write_in(RtTestPe *pe, const char *dir, const char *file);

#endif /* RT_TEST_PE_H */
