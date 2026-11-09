#ifndef CHIBI_BASE_H
#define CHIBI_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define ALWAYS_INLINE __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))
#define NORETURN __attribute__((noreturn))

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// Emits no instructions, but the compiler forgets what it knows about x. It then
// cannot fold x, turn a select on x into a branch, or merge loads around it.
#define OPAQUE(x) __asm__("" : "+r"(x))

#endif
