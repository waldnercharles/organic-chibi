#define _DEFAULT_SOURCE
#define PICO_UNIT_IMPLEMENTATION

#include "../vendor/pico_unit/pico_unit.h"

#include <stdio.h>
#include <unistd.h>

#ifdef __aarch64__
#define ARCH "arm64"
#else
#define ARCH "x86_64"
#endif

#ifdef CHIBI_SIMD_GENERIC
#define SIMD ", generic SIMD"
#else
#define SIMD ""
#endif

#ifdef CHIBI_LEXER_BULK
#define PATH ", bulk lexer"
#else
#define PATH ""
#endif

TEST_SUITE(lexer_tests);

int
main(void)
{
    alarm(30);
    pu_display_quiet(true);

    RUN_TEST_SUITE(lexer_tests);

    printf("test: %s (" ARCH SIMD PATH ")\n", pu_test_failed() ? "FAILED" : "passed");
    return pu_test_failed();
}
