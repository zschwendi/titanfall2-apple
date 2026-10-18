/* Atomic operations: these compile to lock-prefixed instructions and xchg. */
#include "corpus.h"

EXPORT i32 atomic_inc(i32 *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
EXPORT i32 atomic_dec(i32 *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
EXPORT i64 atomic_add64(i64 *p, i64 v) { return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST); }
EXPORT u32 atomic_or(u32 *p, u32 v) { return __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST); }
EXPORT u32 atomic_and(u32 *p, u32 v) { return __atomic_and_fetch(p, v, __ATOMIC_SEQ_CST); }
EXPORT i32 atomic_swap(i32 *p, i32 v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
EXPORT void *atomic_swap_ptr(void **p, void *v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
EXPORT i32 atomic_cas(i32 *p, i32 expect, i32 want) { __atomic_compare_exchange_n(p, &expect, want, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return expect; }
EXPORT i32 atomic_cas64(i64 *p, i64 expect, i64 want) { return __atomic_compare_exchange_n(p, &expect, want, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
EXPORT u8 atomic_flag_set(u8 *p) { return __atomic_exchange_n(p, 1, __ATOMIC_SEQ_CST); }
EXPORT i32 spin_try(i32 *lock) { i32 z = 0; return __atomic_compare_exchange_n(lock, &z, 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
EXPORT u32 refcount_release(u32 *rc) { u32 n = __atomic_sub_fetch(rc, 1, __ATOMIC_SEQ_CST); return n == 0 ? 0xDEAD : n; }
EXPORT i16 atomic_add16(i16 *p, i16 v) { return __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST); }
