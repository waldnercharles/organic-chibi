#define _DEFAULT_SOURCE

#include "../src/exact.h"
#include "../vendor/pico_unit/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *a;
    const char *b;
    ExactOp op;
    u32 limit;
    ExactStatus status;
    const char *want;
} OpCase;

typedef struct
{
    const char *text;
    u32 width;
    u64 bits;
    ExactStatus status;
} FloatCase;

typedef struct
{
    u64 bits;
    u32 width;
    const char *want;
} ValueCase;

static const OpCase OPS[] = {
    { "9223372036854775807", "1", EXACT_ADD, EXACT_INT_BITS, EXACT_OK, "9223372036854775808" },
    { "0", "9223372036854775808", EXACT_SUB, EXACT_INT_BITS, EXACT_OK, "-9223372036854775808" },
    { "18446744073709551615",
      "18446744073709551615",
      EXACT_MUL,
      EXACT_INT_BITS,
      EXACT_OK,
      "340282366920938463426481119284349108225" },
    { "0x1_0000_0000_0000_0000", "3", EXACT_DIV, EXACT_INT_BITS, EXACT_OK, "6148914691236517205" },
    { "0x1_0000_0000_0000_0000", "3", EXACT_REM, EXACT_INT_BITS, EXACT_OK, "1" },
    { "123456789012345678901234567890123456789",
      "987654321987654321987",
      EXACT_DIV,
      EXACT_INT_BITS,
      EXACT_OK,
      "124999998748437501" },
    { "123456789012345678901234567890123456789",
      "987654321987654321987",
      EXACT_REM,
      EXACT_INT_BITS,
      EXACT_OK,
      "27878858264323822302" },
    { "0x7fffffffffffffff_fffffffffffffffe_0000000000000002_fffffffffffffffe",
      "0x7fffffffffffffff_fffffffffffffffe_0000000100000000",
      EXACT_DIV,
      EXACT_INT_BITS,
      EXACT_OK,
      "18446744073709551615" },
    { "0xffffffff_0000000000000000_00000000ffffffff_0000000000000002",
      "0x8000000000000000_0000000000000000_8000000000000001",
      EXACT_REM,
      EXACT_INT_BITS,
      EXACT_OK,
      "3138550867693340381917894711603833208060401094260282097669" },
    { "0xfffffffffffffffe_0000000000000001_0000000000000000",
      "0xfffffffffffffffe_7e465b195bf3f74d",
      EXACT_DIV,
      EXACT_INT_BITS,
      EXACT_OK,
      "18446744073709551615" },
    { "0x8000000000000000_a817f426543c859b_8000000000000001",
      "0x8000000000000000_ffffffffffffffff",
      EXACT_DIV,
      EXACT_INT_BITS,
      EXACT_OK,
      "18446744073709551615" },
    { "0x1_d707107e855c3844_8000000000000001_8000000000000001",
      "0x100000000_7fffffffffffffff",
      EXACT_DIV,
      EXACT_INT_BITS,
      EXACT_OK,
      "145775979981383368922910402558" },
    { "0x2_7fffffffffffffff_0000000000000002_0000000000000000",
      "0x8000000000000001_0000000000000000_8000000000000001",
      EXACT_REM,
      EXACT_INT_BITS,
      EXACT_OK,
      "3138550867693340380216482876999140890734304685073176199164" },
    { "7", "2", EXACT_DIV, EXACT_INT_BITS, EXACT_OK, "3" },
    { "7", "2", EXACT_REM, EXACT_INT_BITS, EXACT_OK, "1" },
    { "1", "100", EXACT_SHL, EXACT_INT_BITS, EXACT_OK, "1267650600228229401496703205376" },
    { "1267650600228229401496703205376", "90", EXACT_SHR, EXACT_INT_BITS, EXACT_OK, "1024" },
    { "1", "511", EXACT_SHL, EXACT_INT_BITS, EXACT_OK, "6703903964971298549787012499102923063739682910296196688861780721860882015036773488400937149083451713845015929093243025426876941405973284973216824503042048" },
    { "1", "512", EXACT_SHL, EXACT_INT_BITS, EXACT_TOO_LARGE, "" },
    { "3", "511", EXACT_SHL, EXACT_INT_BITS, EXACT_TOO_LARGE, "" },
    { "1", "4000000000", EXACT_SHL, EXACT_INT_BITS, EXACT_TOO_LARGE, "" },
    { "134078079299425970995740249982058461274793658205923933777235614437217640"
      "300735469768018742981669034276900318581864860508537538828119465699464336"
      "49006084095",
      "0",
      EXACT_ADD,
      EXACT_INT_BITS,
      EXACT_OK,
      "134078079299425970995740249982058461274793658205923933777235614437217640"
      "300735469768018742981669034276900318581864860508537538828119465699464336"
      "49006084095" },
    { "134078079299425970995740249982058461274793658205923933777235614437217640"
      "300735469768018742981669034276900318581864860508537538828119465699464336"
      "49006084095",
      "1",
      EXACT_ADD,
      EXACT_INT_BITS,
      EXACT_TOO_LARGE,
      "" },
    { "0xff_ff", "0b1010_1010", EXACT_AND, EXACT_INT_BITS, EXACT_OK, "170" },
    { "0o777", "0xffff_ffff_ffff_ffff_ffff", EXACT_XOR, EXACT_INT_BITS, EXACT_OK, "1208925819614629174705664" },
    { "340282366920938463463374607431768211457",
      "6",
      EXACT_OR,
      EXACT_INT_BITS,
      EXACT_OK,
      "340282366920938463463374607431768211463" },
    { "2", "0", EXACT_DIV, EXACT_INT_BITS, EXACT_DIVIDE_BY_ZERO, "" },
    { "2", "0", EXACT_QUO, EXACT_FRACTION_BITS, EXACT_DIVIDE_BY_ZERO, "" },
    { "1", "3", EXACT_QUO, EXACT_FRACTION_BITS, EXACT_OK, "1/3" },
    { "0.1", "0.2", EXACT_ADD, EXACT_FRACTION_BITS, EXACT_OK, "0.3" },
    { "1", "1024", EXACT_QUO, EXACT_FRACTION_BITS, EXACT_OK, "0.0009765625" },
    { "6.0", "4.0", EXACT_QUO, EXACT_FRACTION_BITS, EXACT_OK, "1.5" },
    { "1e300", "1e300", EXACT_MUL, EXACT_FRACTION_BITS, EXACT_OK, "1000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000" },
    { "1.5e3", "2.5e-3", EXACT_ADD, EXACT_FRACTION_BITS, EXACT_OK, "1500.0025" },
    { "1.5E3", "0", EXACT_ADD, EXACT_FRACTION_BITS, EXACT_OK, "1500" },
    { "1e19", "0", EXACT_ADD, EXACT_FRACTION_BITS, EXACT_OK, "10000000000000000000" },
    { "0.00000000000000000001", "0", EXACT_ADD, EXACT_FRACTION_BITS, EXACT_OK, "0.00000000000000000001" },
    { "100000000000000000000..5", "0", EXACT_ADD, EXACT_INT_BITS, EXACT_OK, "100000000000000000000" },
    { "3.14159265358979323846", "180", EXACT_QUO, EXACT_FRACTION_BITS, EXACT_OK, "157079632679489661923/9000000000000000000000" },
    { "1e-70", "0", EXACT_ADD, EXACT_FRACTION_BITS, EXACT_OK, "1/10000000000000000000000000000000000000000000000000000000000000000000000" },
    { "1e1233", "0", EXACT_ADD, EXACT_FRACTION_BITS, EXACT_TOO_LARGE, "" },
    { "1e1232", "0", EXACT_ADD, EXACT_FRACTION_BITS, EXACT_OK, "100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000" },
    { "0.000_001", "1_000_000", EXACT_MUL, EXACT_FRACTION_BITS, EXACT_OK, "1" },
    { "2.5", "10", EXACT_SUB, EXACT_FRACTION_BITS, EXACT_OK, "-7.5" },
    { "1", "3", EXACT_QUO, EXACT_FRACTION_BITS, EXACT_OK, "1/3" },
};

static const FloatCase FLOATS[] = {
    { "0.1", 32, 0x3DCCCCCDull, EXACT_OK },
    { "0.1", 64, 0x3FB999999999999Aull, EXACT_OK },
    { "16777217", 32, 0x4B800000ull, EXACT_OK },
    { "16777219", 32, 0x4B800002ull, EXACT_OK },
    { "3.4028235e38", 32, 0x7F7FFFFFull, EXACT_OK },
    { "3.4028236e38", 32, 0x0ull, EXACT_TOO_LARGE },
    { "3.40282356779733661637539395458142568447e38", 32, 0x7F7FFFFFull, EXACT_OK },
    { "3.40282356779733661637539395458142568448e38", 32, 0x0ull, EXACT_TOO_LARGE },
    { "1e-45", 32, 0x1ull, EXACT_OK },
    { "7e-46", 32, 0x0ull, EXACT_OK },
    { "7.1e-46", 32, 0x1ull, EXACT_OK },
    { "1.1754942e-38", 32, 0x7FFFFFull, EXACT_OK },
    { "4.9406564584124654e-324", 64, 0x1ull, EXACT_OK },
    { "2."
      "470328229206232720882843964341106861825299013071623822127928412503377536"
      "351043759326499181808179961898982823477228588654633283551779698981993873"
      "980053909390631503565951557022639229085839244910518443593180284993653615"
      "250031937045767824921936562366986365848075700158576926990370631192827955"
      "855133292783433840935197801553124659726357957462276646527282722005637400"
      "648549997709659947045402082816622623785739345073633900796776193057750674"
      "017632467360096895134053553745851666113422376667860416215968046191446729"
      "184030053005753084904876539171138659164623952491262365388187963623937328"
      "042389101867234849766823508986338858792562830275599565752445550725518931"
      "369083625477918694866799496832404970582102851318545139621383772282614543"
      "7693412532098591327667236328125e-324",
      64,
      0x0ull,
      EXACT_OK },
    { "2.4703282292062328e-324", 64, 0x1ull, EXACT_OK },
    { "1.7976931348623157e308", 64, 0x7FEFFFFFFFFFFFFFull, EXACT_OK },
    { "1.7976931348623159e308", 64, 0x0ull, EXACT_TOO_LARGE },
    { "3.14159265358979323846", 64, 0x400921FB54442D18ull, EXACT_OK },
    { "3.14159265358979323846", 32, 0x40490FDBull, EXACT_OK },
    { "9007199254740993", 64, 0x4340000000000000ull, EXACT_OK },
    { "0.3", 64, 0x3FD3333333333333ull, EXACT_OK },
    { "123456789012345678901234567890", 64, 0x45F8EE90FF6C373Eull, EXACT_OK },
    { "1e23", 64, 0x44B52D02C7E14AF6ull, EXACT_OK },
    { "8.589973e9", 32, 0x50000026ull, EXACT_OK },
};

static const ValueCase VALUES[] = {
    { 0x3DCCCCCDull, 32, "0.100000001490116119384765625" },
    { 0x3FB999999999999Aull, 64, "0.1000000000000000055511151231257827021181583404541015625" },
    { 0x1ull, 32, "1/713623846352979940529142984724747568191373312" },
    { 0x4B800000ull, 32, "16777216" },
    { 0x44B52D02C7E14AF6ull, 64, "99999999999999991611392" },
};

static char *
printed(const Exacts *e, u32 a)
{
    char *text = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&text, &len);
    exact_print(out, e, a);
    fclose(out);
    return text;
}

static bool
prints_as(const Exacts *e, u32 a, const char *want, const char *what)
{
    char *got = printed(e, a);
    bool same = strcmp(got, want) == 0;
    if (!same) {
        fprintf(stderr, "%s\n  got:  %s\n  want: %s\n", what, got, want);
    }
    free(got);
    return same;
}

TEST_CASE(operations)
{
    Exacts e;
    exacts_init(&e);
    bool ok = true;
    for (size_t i = 0; i < sizeof OPS / sizeof OPS[0]; i++) {
        OpCase t = OPS[i];
        ExactStatus sa, sb, status;
        u32 a = exact_parse(&e, t.a, EXACT_FRACTION_BITS, &sa);
        u32 b = exact_parse(&e, t.b, EXACT_FRACTION_BITS, &sb);
        u32 r = exact_binary(&e, t.op, a, b, t.limit, &status);
        status = sa != EXACT_OK ? sa : sb != EXACT_OK ? sb : status;
        if (status != t.status) {
            fprintf(stderr, "%s op %d %s: status %d, want %d\n", t.a, t.op, t.b, status, t.status);
            ok = false;
        } else if (status == EXACT_OK) {
            ok &= prints_as(&e, r, t.want, t.a);
        }
    }
    exacts_free(&e);
    REQUIRE(ok);
    return true;
}

TEST_CASE(floats)
{
    Exacts e;
    exacts_init(&e);
    bool ok = true;
    for (size_t i = 0; i < sizeof FLOATS / sizeof FLOATS[0]; i++) {
        FloatCase t = FLOATS[i];
        ExactStatus status;
        u32 a = exact_parse(&e, t.text, EXACT_FRACTION_BITS, &status);
        u64 bits = exact_float(&e, a, t.width, &status);
        if (status != t.status || (status == EXACT_OK && bits != t.bits)) {
            fprintf(
                stderr,
                "%.60s as f%u: 0x%llX status %d, want 0x%llX status %d\n",
                t.text,
                t.width,
                (unsigned long long)bits,
                status,
                (unsigned long long)t.bits,
                t.status
            );
            ok = false;
        }
    }

    ExactStatus status;
    u32 tenth = exact_parse(&e, "0.1", EXACT_FRACTION_BITS, &status);
    u32 minus = exact_binary(&e, EXACT_SUB, EXACT_ZERO, tenth, EXACT_FRACTION_BITS, &status);
    ok &= exact_float(&e, minus, 32, &status) == 0xBDCCCCCDull;
    u32 tiny = exact_parse(&e, "1e-60", EXACT_FRACTION_BITS, &status);
    u32 minus_tiny = exact_binary(&e, EXACT_SUB, EXACT_ZERO, tiny, EXACT_FRACTION_BITS, &status);
    ok &= exact_float(&e, minus_tiny, 32, &status) == 0x80000000ull;

    for (size_t i = 0; i < sizeof VALUES / sizeof VALUES[0]; i++) {
        ok &= prints_as(
            &e,
            exact_from_float(&e, VALUES[i].bits, VALUES[i].width),
            VALUES[i].want,
            "exact_from_float"
        );
    }
    exacts_free(&e);
    REQUIRE(ok);
    return true;
}

static __int128
random_int(u64 *state, u32 bits)
{
    u64 x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    u64 y = x;
    y ^= y << 13;
    y ^= y >> 7;
    y ^= y << 17;
    *state = y;

    unsigned __int128 v = ((unsigned __int128)x << 64 | y) >> (128 - bits);
    return (y & 1) ? -(__int128)v : (__int128)v;
}

static void
print_int128(char *buf, __int128 v)
{
    char tmp[48];
    char *p = tmp + sizeof tmp;
    *--p = 0;
    unsigned __int128 u = v < 0 ? -(unsigned __int128)v : (unsigned __int128)v;
    do {
        *--p = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    if (v < 0) {
        *--p = '-';
    }
    strcpy(buf, p);
}

static u32
make(Exacts *e, __int128 v)
{
    char text[48];
    print_int128(text, v < 0 ? -v : v);
    ExactStatus status;
    u32 m = exact_parse(e, text, EXACT_INT_BITS, &status);
    return v < 0 ? exact_binary(e, EXACT_SUB, EXACT_ZERO, m, EXACT_INT_BITS, &status)
                 : m;
}

TEST_CASE(against_int128)
{
    static const ExactOp ops[] = { EXACT_ADD, EXACT_SUB, EXACT_MUL, EXACT_DIV,
                                   EXACT_REM, EXACT_AND, EXACT_OR,  EXACT_XOR,
                                   EXACT_SHL, EXACT_SHR };
    Exacts e;
    exacts_init(&e);
    u64 state = 0x9E3779B97F4A7C15ull;
    bool ok = true;
    for (u32 i = 0; i < 20000 && ok; i++) {
        ExactOp op = ops[i % (sizeof ops / sizeof ops[0])];
        u32 abits = 1 + (u32)(state % 100);
        u32 bbits = op == EXACT_MUL
                      ? 1 + (u32)((state >> 8) % (126 - abits > 100 ? 100 : 126 - abits))
                      : 1 + (u32)((state >> 8) % 100);
        __int128 x = random_int(&state, abits);
        __int128 y = random_int(&state, bbits);
        if (op == EXACT_SHL || op == EXACT_SHR) {
            y = (y < 0 ? -y : y) % (op == EXACT_SHL ? 126 - abits + 1 : 130);
        }
        if ((op == EXACT_DIV || op == EXACT_REM) && y == 0) {
            continue;
        }

        __int128 r;
        switch (op) {
            case EXACT_ADD: r = x + y; break;
            case EXACT_SUB: r = x - y; break;
            case EXACT_MUL:
                r = (__int128)((unsigned __int128)x * (unsigned __int128)y);
                break;
            case EXACT_DIV: r = x / y; break;
            case EXACT_REM: r = x % y; break;
            case EXACT_AND: r = x & y; break;
            case EXACT_OR: r = x | y; break;
            case EXACT_XOR: r = x ^ y; break;
            case EXACT_SHL: r = (__int128)((unsigned __int128)x << y); break;
            default: r = y >= 127 ? (x < 0 ? -1 : 0) : x >> y; break;
        }

        ExactStatus status;
        u32 got = exact_binary(&e, op, make(&e, x), make(&e, y), EXACT_INT_BITS, &status);
        char want[48];
        print_int128(want, r);
        ok = status == EXACT_OK && prints_as(&e, got, want, "against int128");
    }
    exacts_free(&e);
    REQUIRE(ok);
    return true;
}

TEST_SUITE(exact_tests)
{
    RUN_TEST_CASE(operations);
    RUN_TEST_CASE(floats);
    RUN_TEST_CASE(against_int128);
}
