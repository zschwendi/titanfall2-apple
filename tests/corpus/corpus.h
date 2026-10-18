/* Shared declarations for the test corpus. Freestanding: no system headers. */
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
typedef u64 usize;
#define EXPORT __declspec(dllexport)
#define NOINLINE __declspec(noinline)
void *memcpy(void *d, const void *s, usize n);
void *memset(void *d, int c, usize n);
void *memmove(void *d, const void *s, usize n);
#endif
