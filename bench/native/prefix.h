/* Force-included (-include) when a corpus source is built natively for arm64 macOS.
 *
 * It stands in for tests/corpus/corpus.h (same include guard, so the corpus's own
 * #include "corpus.h" becomes a no-op) without editing the corpus:
 *   - EXPORT becomes default visibility (the dylib is built with -fvisibility=hidden, so
 *     exactly the functions the DLL exports are visible), NOINLINE stays noinline;
 *   - usize is `unsigned long`, which is size_t here: cpp2.cpp declares
 *     `operator new(usize, void *)` and virt.cpp `operator delete(void *, usize)`, and the
 *     compiler insists on size_t for those.  Same size and signedness as on Windows;
 *   - the Microsoft va_list builtins map to the native ones.  Every variadic argument in
 *     the corpus is read as a 4- or 8-byte integer or a double from an 8-byte slot, which
 *     is also how Apple's arm64 ABI passes variadic arguments.
 */
#ifndef CORPUS_H
#define CORPUS_H
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char i8;
typedef short i16;
typedef int i32;
typedef long long i64;
typedef unsigned long usize;
#define EXPORT __attribute__((visibility("default")))
#define NOINLINE __attribute__((noinline))
#define __builtin_ms_va_list  __builtin_va_list
#define __builtin_ms_va_start __builtin_va_start
#define __builtin_ms_va_end   __builtin_va_end
#ifdef __cplusplus
extern "C" {
#endif
void *memcpy(void *d, const void *s, usize n);
void *memset(void *d, int c, usize n);
void *memmove(void *d, const void *s, usize n);
#ifdef __cplusplus
}
#endif
#endif
