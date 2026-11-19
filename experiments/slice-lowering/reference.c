#include "kernels.h"

int64_t
sum_ref(Ints xs)
{
    int64_t total = 0;
    for (int64_t i = 0; i < xs.len; i++) {
        if ((uint64_t)i >= (uint64_t)xs.len) {
            __builtin_trap();
        }
        if (__builtin_add_overflow(total, xs.ptr[i], &total)) {
            __builtin_trap();
        }
    }
    return total;
}

int64_t
count_ref(Bytes s, uint8_t c)
{
    int64_t n = 0;
    Bytes rest = s;
    while (rest.len > 0) {
        if (rest.len == 0) {
            __builtin_trap();
        }
        if (rest.ptr[0] == c && __builtin_add_overflow(n, 1, &n)) {
            __builtin_trap();
        }
        if ((uint64_t)rest.len < 1) {
            __builtin_trap();
        }
        rest.ptr += 1;
        rest.len -= 1;
    }
    return n;
}

__attribute__((noinline)) int64_t
first_ref(Bytes s)
{
    if (s.len == 0) {
        __builtin_trap();
    }
    return s.ptr[0];
}

int64_t (*slot_first_ref)(Bytes) = first_ref;

int64_t
total_ref(Bytes s)
{
    int64_t n = 0;
    Bytes rest = s;
    while (rest.len > 0) {
        if (__builtin_add_overflow(n, slot_first_ref(rest), &n)) {
            __builtin_trap();
        }
        if ((uint64_t)rest.len < 1) {
            __builtin_trap();
        }
        rest.ptr += 1;
        rest.len -= 1;
    }
    return n;
}
