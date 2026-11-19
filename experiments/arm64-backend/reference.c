#include <stdbool.h>
#include <stdint.h>
#define NAME(x) REF_##x
int64_t
NAME(fib)(int64_t n)
{
    if (n < 2) {
        return n;
    }
    return NAME(fib)(n - 1) + NAME(fib)(n - 2);
}
int64_t
NAME(sum)(int64_t n)
{
    int64_t total = 0;
    for (int64_t i = 0; i < n; i++) {
        if (__builtin_add_overflow(total, i ^ (i >> 3), &total)) {
            __builtin_trap();
        }
    }
    return total;
}
int64_t
NAME(collatz)(int64_t limit)
{
    int64_t steps = 0;
    for (int64_t n = 1; n < limit; n++) {
        int64_t x = n;
        while (x != 1) {
            if (x % 2 == 0) {
                x = x / 2;
            } else {
                x = 3 * x + 1;
            }
            steps += 1;
        }
    }
    return steps;
}
int64_t
NAME(primes)(int64_t limit)
{
    int64_t count = 0;
    for (int64_t n = 2; n < limit; n++) {
        bool prime = true;
        for (int64_t d = 2; d * d <= n; d++) {
            if (n % d == 0) {
                prime = false;
                break;
            }
        }
        if (prime) {
            count += 1;
        }
    }
    return count;
}
