/* The thread block (Windows x64 TEB) and the process block (PEB): the offsets the runtime
 * maintains.  docs/RUNTIME.md section 6.2; what the guest reads is in docs/findings/17.
 *
 * ctx->gs_base points at the thread block.  The lifter turns `gs:[x]` into a load from
 * gs_base + x with no hook, so the runtime keeps its own per-thread state in these fields
 * at these offsets and reads it back from there (LD32(ctx->gs_base + RT_TEB_LAST_ERROR)),
 * never from a host-side copy.  Every byte not listed is zero.
 *
 * These are offsets, not structs: the runtime uses a dozen fields out of several hundred.
 * Planner-owned.
 */
#ifndef RT_WIN_TEB_H
#define RT_WIN_TEB_H

/* ---- thread block --------------------------------------------------------------------- */
#define RT_TEB_STACK_BASE        0x0008   /* u64: top of the guest stack (section 5.3) */
#define RT_TEB_STACK_LIMIT       0x0010   /* u64: lowest usable stack address; the C runtime's stack probe reads it */
#define RT_TEB_SELF              0x0030   /* u64: address of this block */
#define RT_TEB_PROCESS_ID        0x0040   /* u64 slot, 32-bit value: GetCurrentProcessId */
#define RT_TEB_THREAD_ID         0x0048   /* u64 slot, 32-bit value: GetCurrentThreadId */
#define RT_TEB_TLS_POINTER       0x0058   /* u64: static TLS pointer array, one slot per manifest module */
#define RT_TEB_PEB               0x0060   /* u64: the process block */
#define RT_TEB_LAST_ERROR        0x0068   /* u32: GetLastError */
#define RT_TEB_DEALLOC_STACK     0x1478   /* u64: base of the guest stack region */
#define RT_TEB_TLS_SLOTS         0x1480   /* u64[64]: TlsGetValue for indices 0 to 63 */
#define RT_TEB_TLS_EXPANSION     0x1780   /* u64: pointer to 1,024 more slots, allocated on first use */
#define RT_TEB_SIZE              0x1838   /* bytes the runtime treats as the TEB: no smaller than any Windows x64 TEB */

#define RT_TLS_MINIMUM_SLOTS     64
#define RT_TLS_EXPANSION_SLOTS   1024

/* The thread block region is 16 KB (section 6.1).  The runtime puts the static TLS pointer
 * array in the same region, above the TEB proper. */
#define RT_TEB_REGION_SIZE       0x4000
#define RT_TEB_STATIC_TLS_ARRAY  0x2000   /* offset in the region of the array RT_TEB_TLS_POINTER points to */
#define RT_STATIC_TLS_SLOTS      1024     /* (RT_TEB_REGION_SIZE - RT_TEB_STATIC_TLS_ARRAY) / 8 */

/* ---- process block -------------------------------------------------------------------- */
#define RT_PEB_BEING_DEBUGGED    0x0002   /* u8: 0 (section 9.4); IsDebuggerPresent reads it */
#define RT_PEB_IMAGE_BASE        0x0010   /* u64: base of the executable's native module */
#define RT_PEB_PROCESS_HEAP      0x0030   /* u64: GetProcessHeap; written by rt_heap_init */
#define RT_PEB_NUMBER_OF_PROCESSORS 0x00B8 /* u32 */
#define RT_PEB_OS_MAJOR_VERSION  0x0118   /* u32 */
#define RT_PEB_OS_MINOR_VERSION  0x011C   /* u32 */
#define RT_PEB_OS_BUILD_NUMBER   0x0120   /* u16 */
#define RT_PEB_OS_CSD_VERSION    0x0122   /* u16: service pack major << 8 | minor */
#define RT_PEB_OS_PLATFORM_ID    0x0124   /* u32: 2 = VER_PLATFORM_WIN32_NT */
#define RT_PEB_REGION_SIZE       0x4000   /* the process block is one zero-filled 16 KB region */

#endif /* RT_WIN_TEB_H */
