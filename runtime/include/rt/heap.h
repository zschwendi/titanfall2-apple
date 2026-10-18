/* Heaps (docs/RUNTIME.md section 5.4).
 *
 * Built in runtime/core/heap.c (unit rt-core-heap).  The comments are the specification.
 *
 * GetProcessHeap is a dedicated host malloc zone (not the default zone); each HeapCreate
 * heap is another zone, destroyed whole by HeapDestroy.  The game brings its own allocator,
 * so this family mostly serves the C runtime's own bookkeeping.
 *
 * A heap handle is NOT in the handle table: it is the guest address of an RtHeap header,
 * checked by its magic.  Every block has a 16-byte header before the address the caller
 * gets, which is 16-byte aligned.
 *
 * FIXED-SIZE HEAPS.  HeapCreate with a nonzero maximum size makes a heap that cannot grow:
 *   - one block must be smaller than 0xFF000 bytes.  The HeapAlloc page gives 0x7FFF8, a 32-bit
 *     figure; the HeapCreate page says "slightly less than 1,024 KB" for a 64-bit process.  Wine
 *     kernel32/tests/heap.c test_HeapCreate (fixed-size heaps, 64-bit) shows that a block of
 *     0x80000 fits and one of 0x100000 does not; those are the only known points of Windows.
 *     0xFF000 (1,024 KB less one 4 KB page) is OUR figure, chosen inside that range, not
 *     Windows'; which figure Windows 7 enforces is not verified;
 *   - the heap's total is held to the maximum rounded up to a 4 KB page, each live block
 *     counted as 16 + its size rounded up to 16.  Windows also spends part of the maximum
 *     on its own structures, so it fills up sooner (Wine heap.c test_HeapCreate: in a 0x40000
 *     fixed heap 0x3F400 bytes fit and 0x3FA00 do not, on 64-bit).
 * A request that breaks either limit fails as an exhausted heap does: NULL, with last error
 * ERROR_NOT_ENOUGH_MEMORY from HeapAlloc and untouched from HeapReAlloc (or the fatal
 * report, with HEAP_GENERATE_EXCEPTIONS).  A heap created with
 * maximum size 0 grows without limit.
 *
 * CAPS OF THIS RUNTIME.  At most 1,024 heaps alive at once (one more HeapCreate is fatal:
 * that many is a leak).  A request of 4 TiB or more, in any heap, fails with NULL without
 * the host being asked.  The commit limit of Windows is not emulated: a smaller request
 * fails only when the host allocator refuses it.  Every block lies below the guest address
 * limit of 8 TB; a host zone that hands out a block above it is fatal.
 *
 * FLAGS.  Each function takes the HEAP_* bits listed with it.  Any other bit is FATAL, not
 * ignored (Windows masks some and rejects others; which is not verified), so a shim that
 * wants to report an unknown bit as its own unimplemented case checks before it calls.
 * HEAP_NO_SERIALIZE is accepted everywhere and changes nothing: every heap is thread-safe.
 * HEAP_GENERATE_EXCEPTIONS, in the call or in the heap's HeapCreate options, turns an
 * allocation failure into a fatal report (Windows raises STATUS_NO_MEMORY; exceptions are
 * plan A, RUNTIME.md 9.3).
 *
 * LAST ERROR.  rt_heap_create, rt_heap_destroy and rt_heap_alloc set it, through
 * rt_thread_set_last_error, and only when they fail.  rt_heap_alloc sets ERROR_NOT_ENOUGH_MEMORY
 * (from Wine kernel32/tests/heap.c test_HeapCreate: a failed HeapAlloc in a fixed-size heap
 * gives that code, on Windows 7 too; the Microsoft page says HeapAlloc does not call
 * SetLastError, and the test contradicts the page).  The other allocation functions never
 * touch it: HeapReAlloc as the Microsoft page says (the same test expects ERROR_NOT_ENOUGH_MEMORY
 * for an in-place failure too, but that check is marked todo_wine; ntdll may set it: not
 * verified).  The one exception is HeapReAlloc with a NULL block, which the HeapReAlloc shim
 * answers with NULL and last error ERROR_SUCCESS (see rt_heap_realloc).  Otherwise a shim
 * sets nothing.
 *
 * FATAL, in every function but rt_heap_validate: a heap handle that is NULL, is not the
 * address of a heap header, or names a destroyed heap.  LOCKS: none is held across a call;
 * all functions may be called from any thread, with any lock held.
 */
#ifndef RT_HEAP_H
#define RT_HEAP_H

#include "rt/wintypes.h"
#include "rt/win/memory.h"     /* the HEAP_* flags */

#define RT_HEAP_MAGIC        0x70616548u      /* "Heap": first word of the header a heap handle points to */
#define RT_HEAP_BLOCK_MAGIC  0x6B6C4248u      /* "HBlk" */

/* The 16 bytes before every block.  HeapSize returns `size` exactly: the C runtime's
 * _recalloc zero-fills from the old HeapSize, so a size rounded up by the host allocator
 * would leave garbage where Windows has zeros. */
typedef struct {
    uint32_t magic;         /* RT_HEAP_BLOCK_MAGIC; cleared on free so a double free is caught */
    uint32_t heap_id;       /* id of the owning heap */
    uint64_t size;          /* bytes the caller asked for */
} RtHeapBlock;
_Static_assert(sizeof(RtHeapBlock) == 16, "RtHeapBlock");

/* Called once by rt_init: create the process heap and store its handle in the process
 * block (RT_PEB_PROCESS_HEAP). */
void rt_heap_init(void);

/* GetProcessHeap: the process heap's handle, which grows without limit and is never
 * destroyed.  Fatal before rt_heap_init. */
HANDLE rt_heap_process(void);

/* HeapCreate.
 *   options       HEAP_NO_SERIALIZE, HEAP_GENERATE_EXCEPTIONS (applies to every later call
 *                 on the heap), HEAP_CREATE_ENABLE_EXECUTE (accepted and logged once: guest
 *                 bytes are never executed)
 *   initial_size  a hint only; nothing is committed up front
 *   maximum_size  0: the heap grows.  Otherwise a fixed-size heap (see the top)
 * Returns the handle, or NULL with last error ERROR_NOT_ENOUGH_MEMORY if either size is
 * above the guest address limit (which code Windows gives is not verified) or the host
 * refuses a new malloc zone.  Fatal: an initial size larger than a nonzero maximum (the
 * page says it "must be smaller"; what Windows does is not verified; equal is accepted);
 * the 1,025th live heap. */
HANDLE rt_heap_create(DWORD options, SIZE_T initial_size, SIZE_T maximum_size);

/* HeapDestroy: free every block of the heap, without HeapFree, and retire the handle (its
 * later use is fatal).  TRUE.  For the process heap: FALSE with last error
 * ERROR_INVALID_HANDLE, and nothing is destroyed (decided by the planner 2026-10-09; the
 * code is from memory of kernel32, not verified). */
BOOL   rt_heap_destroy(HANDLE heap);

/* HeapAlloc.  flags: HEAP_NO_SERIALIZE, HEAP_GENERATE_EXCEPTIONS, HEAP_ZERO_MEMORY (zeroes
 * exactly `size` bytes).  Size 0 gives a valid, unique, freeable pointer whose HeapSize is
 * 0.  NULL, with last error ERROR_NOT_ENOUGH_MEMORY, when the request cannot be met (the host
 * is out of memory, a fixed-size heap is full or the block too large for one, 4 TiB or more);
 * logged. */
LPVOID rt_heap_alloc(HANDLE heap, DWORD flags, SIZE_T size);

/* HeapReAlloc: the block at its new size, moved or not, with its contents up to the smaller
 * of the two sizes.  flags: those of HeapAlloc and HEAP_REALLOC_IN_PLACE_ONLY.
 * HEAP_ZERO_MEMORY zeroes the growth, from the old requested size.  Shrinking never fails.
 * SIZE 0 SUCCEEDS: the result is a live block of size 0, as HeapAlloc(0) gives; the block
 * is not freed and must still be freed (decided by the planner 2026-10-09; as Windows
 * does: Wine kernel32/tests/heap.c test_HeapCreate, HeapSize of the result is 0).
 * NULL, with last error untouched and the block unchanged and still live, when the request
 * cannot be met (as HeapAlloc), and with HEAP_REALLOC_IN_PLACE_ONLY when the block cannot
 * stay where it is: here it can stay only when it shrinks or grows within the slack the
 * host allocator gave it, so this fails in cases where Windows might grow into a free
 * neighbour.
 * Fatal: a NULL block (this function takes only a live block; C realloc(NULL, n) is not
 * HeapReAlloc), and a pointer that is not a live block of this heap, as for rt_heap_free.
 * The HeapReAlloc shim never passes a NULL block here: it returns NULL with last error
 * ERROR_SUCCESS and changes nothing, as Windows does (Wine kernel32/tests/heap.c test_HeapCreate:
 * NULL returned; the last error check is todo_wine, so Windows does it and Wine did not yet). */
LPVOID rt_heap_realloc(HANDLE heap, DWORD flags, LPVOID block, SIZE_T size);

/* HeapFree.  flags: HEAP_NO_SERIALIZE.  A NULL block is accepted and returns TRUE.  With a
 * NULL heap handle as well, the handle is not checked (Windows: Wine kernel32/tests/heap.c
 * test_HeapCreate, HeapFree(NULL, 0, NULL) is TRUE).  With any other handle the handle is
 * checked first, so a bad handle is fatal even with a NULL block (no source seen for that
 * case).  A pointer that is not a live block of this heap (misaligned, outside
 * guest memory, not a block of the heap's zone, already freed, a block of another heap) is
 * fatal: it names a bug that Windows would turn into heap corruption.  The check never
 * faults, whatever the pointer is.  Always TRUE when it returns. */
BOOL rt_heap_free(HANDLE heap, DWORD flags, LPVOID block);

/* HeapSize.  flags: HEAP_NO_SERIALIZE.  The requested size, or (SIZE_T)-1 for a pointer
 * that is not a live block of this heap (NULL included); no last error, as documented. */
SIZE_T rt_heap_size(HANDLE heap, DWORD flags, LPCVOID block);

/* HeapValidate: the check of rt_heap_free without the fatal report.  flags:
 * HEAP_NO_SERIALIZE.
 *   heap   TRUE needs a live heap.  A handle that is NULL, is not a heap header, or names a
 *          destroyed heap gives FALSE: this is the one function here for which a bad heap
 *          handle is not fatal ("if the specified heap or memory block is invalid, the
 *          return value is zero")
 *   block  not NULL: TRUE exactly when it is the address of a live block of this heap, by
 *          the test rt_heap_free applies; never faults, whatever the pointer is.
 *          NULL ("validate the whole heap"): TRUE for a live heap.  NOTHING IS WALKED: the
 *          runtime keeps no list of a heap's blocks, so a block whose header was
 *          overwritten is found only when that block itself is validated, freed or
 *          reallocated.
 * Never sets last error (the page: "there is no extended error information for this
 * function").  Fatal only for a flag bit it does not take.  The contents of a block are
 * not examined: an overrun that stays inside the host allocator's slack is not seen. */
BOOL rt_heap_validate(HANDLE heap, DWORD flags, LPCVOID block);

/* Guest-visible memory for the runtime's own use, from the process heap: anything the
 * guest may later hand to HeapFree, LocalFree or CoTaskMemFree, and native objects
 * (section 3.7).  rt_galloc zeroes; fatal when memory is exhausted.  Never use plain
 * malloc for memory whose address the guest sees. */
void *rt_galloc(SIZE_T size);
void  rt_gfree(void *p);

#endif /* RT_HEAP_H */
