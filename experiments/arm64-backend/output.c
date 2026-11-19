#define _DEFAULT_SOURCE
#include "../../src/arm64.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
typedef int64_t I1(int64_t);
I1 O0_fib, O0_sum, O0_collatz, O0_primes, O2_fib, O2_sum, O2_collatz, O2_primes;
static double
now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static const char
    *text = "fn fib(n: int) -> int {\n    if n < 2 {\n        return n\n    "
            "}\n    return fib(n - 1) + fib(n - 2)\n}\n"
            "fn sum(n: int) -> int {\n    total := 0\n    for i in 0..n {\n    "
            "    total += i ^ (i >> 3)\n    }\n    return total\n}\n"
            "fn collatz(limit: int) -> int {\n    steps := 0\n    for n in "
            "1..limit {\n        x := n\n        while x != 1 {\n            "
            "if x % 2 == 0 {\n                x = x / 2\n            } else "
            "{\n                x = 3 * x + 1\n            }\n            "
            "steps += 1\n        }\n    }\n    return steps\n}\n"
            "fn primes(limit: int) -> int {\n    count := 0\n    for n in "
            "2..limit {\n        prime := true\n        d := 2\n        while "
            "d * d <= n {\n            if n % d == 0 {\n                prime "
            "= false\n                break\n            }\n            d += "
            "1\n        }\n        if prime {\n            count += 1\n        "
            "}\n    }\n    return count\n}\n";
int
main(void)
{
    Arena arena = { 0 };
    size_t len = strlen(text);
    Lexer lx;
    lexer_init(&lx, &arena, len);
    char *src = arena_alloc(&arena, len + LEXER_SOURCE_PADDING, 16);
    memset(src, 0, len + LEXER_SOURCE_PADDING);
    memcpy(src, text, len);
    Tokens t = lexer_alloc_tokens(&arena, len);
    int nt = lexer_tokenize(&lx, src, t);
    Nodes o = parser_alloc_nodes(&arena, nt);
    Parser p;
    int c = parser_parse(&p, t, nt, o);
    ParsedFile f = { "a", src, t, o, c, p.top_count, true };
    Checker ch;
    check_init(&ch, &lx.names);
    check_package(&ch, &f, 1);
    if (ch.ndiags) {
        check_print_diagnostics(stderr, &ch);
        return 1;
    }
    Native n;
    if (!arm64_compile(&n, &ch)) {
        printf("error %s\n", n.error);
        return 1;
    }
    const char *names[] = { "fib", "sum", "collatz", "primes" };
    int64_t args[] = { 32, 100000000, 300000, 2000000 };
    I1 *o0[] = { O0_fib, O0_sum, O0_collatz, O0_primes },
       *o2[] = { O2_fib, O2_sum, O2_collatz, O2_primes };
    printf("%-8s %10s %10s %10s   chibi/O0  chibi/O2\n", "", "chibi ms", "O0 ms", "O2 ms");
    for (int k = 0; k < 4; k++) {
        u32 id = names_intern(&lx.names, names[k], strlen(names[k]));
        I1 *jit = (I1 *)native_entry(&n, ch.package_names[id]);
        double best[3] = { 1e9, 1e9, 1e9 };
        int64_t r[3];
        I1 *fs[3] = { jit, o0[k], o2[k] };
        for (int rep = 0; rep < 3; rep++) {
            for (int m = 0; m < 3; m++) {
                double t0 = now();
                r[m] = fs[m](args[k]);
                double d = now() - t0;
                if (d < best[m]) {
                    best[m] = d;
                }
            }
        }
        if (r[0] != r[1] || r[1] != r[2]) {
            printf("MISMATCH %lld %lld %lld\n", (long long)r[0], (long long)r[1], (long long)r[2]);
        }
        printf(
            "%-8s %10.1f %10.1f %10.1f   %8.2f  %8.2f\n",
            names[k],
            best[0] * 1e3,
            best[1] * 1e3,
            best[2] * 1e3,
            best[0] / best[1],
            best[0] / best[2]
        );
    }
}
