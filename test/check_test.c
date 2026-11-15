#define _DEFAULT_SOURCE

#include "../src/check.h"
#include "../vendor/pico_unit/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *src;
    const char *want;
} Case;

static char *
check_files(const char *const *texts, u32 n, u32 entry, bool ir)
{
    Arena arena = { 0 };
    size_t total = 0;
    for (u32 i = 0; i < n; i++) {
        total += strlen(texts[i]);
    }

    Lexer lx;
    lexer_init(&lx, &arena, total);
    ParsedFile *files = calloc(n, sizeof *files);
    for (u32 i = 0; i < n; i++) {
        size_t len = strlen(texts[i]);
        char *src = arena_alloc(&arena, len + LEXER_SOURCE_PADDING, 16);
        memset(src, 0, len + LEXER_SOURCE_PADDING);
        memcpy(src, texts[i], len);

        Tokens tokens = lexer_alloc_tokens(&arena, len);
        int ntokens = lexer_tokenize(&lx, src, tokens);
        assert(ntokens > 0);

        Nodes nodes = parser_alloc_nodes(&arena, ntokens);
        Parser p;
        int count = parser_parse(&p, tokens, ntokens, nodes);
        assert(count >= 0);
        files[i] = (ParsedFile){
            i == 0 ? "a" : "b", src,       tokens, nodes, (u32)count,
            (u32)p.top_count,   i == entry
        };
    }

    Checker c;
    check_init(&c, &lx.names);
    check_package(&c, files, n);

    char *got = NULL;
    size_t got_len = 0;
    FILE *out = open_memstream(&got, &got_len);
    check_print_diagnostics(out, &c);
    if (ir) {
        check_dump_ir(out, &c);
    } else {
        check_dump(out, &c);
    }
    fclose(out);

    check_free(&c);
    free(files);
    arena_free(&arena);
    return got;
}

static bool
compiles_to(const char *const *texts, u32 n, u32 entry, bool ir, const char *want)
{
    char *got = check_files(texts, n, entry, ir);
    bool same = strcmp(got, want) == 0;
    if (!same) {
        fprintf(stderr, "check \"%s\"\n  got:\n%s  want:\n%s", texts[0], got, want);
    }
    free(got);
    return same;
}

static bool
checks_to(const char *const *texts, u32 n, u32 entry, const char *want)
{
    return compiles_to(texts, n, entry, false, want);
}

static const Case DECLARATIONS[] = {
    { "const MAX := 100\nconst LIMIT: u8 = 2 * MAX + 1\n",
      "const MAX = 100\nconst LIMIT: u8 = 201\n" },
    { "const A := B + 1\nconst B := 0x10 - 0b11 * 0o7\n", "const A = -4\nconst B = -5\n" },
    { "const N := -(1 << 126) * 2 + 1 + ~0 + 1_000 % 7 / 2\n",
      "const N = -170141183460469231731687303715884105725\n" },
    { "const W: u16 = Ok\nconst Ok: u8 = 255\nconst S := Ok + W\n",
      "const W: u16 = 255\nconst Ok: u8 = 255\nconst S: u16 = 510\n" },
    { "const F: f32 = 16777216\nconst D: f64 = Ok\nconst Ok: i32 = -7\n",
      "const F: f32 = 16777216\nconst D: f64 = -7\nconst Ok: i32 = -7\n" },
    { "const A := Point\nstruct Point { x: int, y: int = 0 }\n",
      "const A = Point\nstruct Point { x: int, y: int = 0 }\n" },
    { "pub struct Node { next: ?*Node, value: [4]f32, parent: *Node, kids: "
      "[]Node }\n",
      "pub struct Node { next: ?*Node, value: [4]f32, parent: *Node, kids: "
      "[]Node }\n" },
    { "struct W { a: u8 = 255, b := 7, c: int = undefined }\n",
      "struct W { a: u8 = 255, b: int = 7, c: int = undefined }\n" },
    { "struct A { b: B }\nstruct B { c: [2]C, d: ?C, e: error!C }\nstruct C { "
      "x: i64 }\n",
      "struct A { b: B }\nstruct B { c: [2]C, d: ?C, e: error!C }\nstruct C { "
      "x: int }\n" },
    { "enum Color: u8 { red = 1, green, blue = 7, gray }\nenum Plain { a, b = "
      "-3, c }\n",
      "enum Color: u8 { red = 1, green = 2, blue = 7, gray = 8 }\nenum Plain { "
      "a = 0, b = -3, c = -2 }\n" },
    { "enum Kind: u8 { circle, none }\nunion Shape: Kind { circle: f32, none "
      "}\nunion Bits { f: f32, u: u32 }\n",
      "enum Kind: u8 { circle = 0, none = 1 }\nunion Shape: Kind { circle: "
      "f32, none }\nunion Bits { f: f32, u: u32 }\n" },
    { "fn area(p: Point, scale: f32) -> int { return 0 }\nstruct Point { x: "
      "int }\n",
      "fn area(p: Point, scale: f32) -> int\nstruct Point { x: int }\n" },
    { "pub fn load(path: []u8) -> !void {}\nfn quiet() {}\npub fn run() "
      "{}\nexport fn tick(dt: f32) {}\n",
      "pub fn load(path: []u8) -> !void\nfn quiet() -> void\npub fn run() -> "
      "void\nexport fn tick(dt: f32) -> void\n" },
    { "fn g(cb: fn(int, x: f32) -> u8, h: fn(), k: u8 = 9) => 0\n",
      "fn g(cb: fn(int, f32) -> u8, h: fn() -> void, k: u8 = 9) -> int\n" },
    { "struct S { f: fn(*int, []u8) -> ?int, g: fn(*int, []u8) -> ?int }\n",
      "struct S { f: fn(*int, []u8) -> ?int, g: fn(*int, []u8) -> ?int }\n" },
    { "package geo\nimport \"std/fmt\"\nconst X := 1\n", "const X = 1\n" },
};

static const Case ERRORS[] = {
    { "const A := B + 1\nconst B := A * 2\nconst C := C\n",
      "a:1:7: `A` depends on itself through `B`\n"
      "a:3:7: `C` depends on itself\n"
      "const A = ?\nconst B = ?\nconst C = ?\n" },
    { "struct S { t: T }\nstruct T { s: S, n: ?*T }\nstruct U { u: [2]U "
      "}\nstruct V { v: ?V }\n",
      "a:1:8: `S` depends on itself through `T`\n"
      "a:3:8: `U` depends on itself\n"
      "a:4:8: `V` depends on itself\n"
      "struct S { t: T }\nstruct T { s: S, n: ?*T }\nstruct U { u: [2]U "
      "}\nstruct V { v: ?V }\n" },
    { "const A := (1 << 511) >> 509\nconst B := 1 << 512\nconst C := "
      "0x1_0000000000000000_0000000000000000_0000000000000000_0000000000000000_"
      "0000000000000000_0000000000000000_0000000000000000_0000000000000000\n",
      "a:2:14: an integer constant must fit in 512 bits\n"
      "a:3:12: an integer constant must fit in 512 bits\n"
      "const A = 4\nconst B = ?\nconst C = ?\n" },
    { "const A: u8 = 256\nconst B: u8 = -1\nconst C: u8 = 255\nconst D := C + "
      "1\nconst E := -C\n",
      "a:1:7: 256 does not fit in `u8`\n"
      "a:2:7: -1 does not fit in `u8`\n"
      "a:4:14: 256 does not fit in `u8`\n"
      "a:5:12: -255 does not fit in `u8`\n"
      "const A = ?\nconst B = ?\nconst C: u8 = 255\nconst D = ?\nconst E = "
      "?\n" },
    { "const A := 1 / 0\nconst B := 5 % 0\nconst C := 1 << 600\nconst D: u8 = "
      "1\nconst E := D << 8\nconst F := 1 << -1\n",
      "a:1:14: division by zero\n"
      "a:2:14: division by zero\n"
      "a:3:14: an integer constant must fit in 512 bits\n"
      "a:5:14: shift count 8 is out of range\n"
      "a:6:14: shift count -1 is out of range\n"
      "const A = ?\nconst B = ?\nconst C = ?\nconst D: u8 = 1\nconst E = "
      "?\nconst F = ?\n" },
    { "const U: u8 = 1\nconst I: i8 = 1\nconst A := U + I\nconst B: i8 = "
      "U\nconst C: f32 = 16777217\nconst D: f32 = Big\nconst Big: i32 = 1\n",
      "a:3:14: `u8` and `i8` don't mix without a conversion\n"
      "a:4:7: `u8` and `i8` don't mix without a conversion\n"
      "a:5:7: 16777217 does not fit in `f32`\n"
      "a:6:7: `i32` and `f32` don't mix without a conversion\n"
      "const U: u8 = 1\nconst I: i8 = 1\nconst A = ?\nconst B = ?\nconst C = "
      "?\nconst D = ?\nconst Big: i32 = 1\n" },
    { "const M := -1\nstruct L { x: [M]u8, y: [P]u8, z: [Q]u8 }\nstruct P "
      "{}\nconst Q := P\n",
      "a:2:15: an array length can't be negative\n"
      "a:2:25: expected a constant\n"
      "a:2:35: expected a constant\n"
      "const M = -1\nstruct L { x: ?, y: ?, z: ? }\nstruct P {}\nconst Q = "
      "P\n" },
    { "struct S { x: Missing, y: MAX, z: f }\nfn f() {}\nconst K := f\nconst "
      "MAX := 5\n",
      "a:1:15: unknown name `Missing`\n"
      "a:1:27: expected a type\n"
      "a:1:35: `f` is a function, so it can't be used here\n"
      "a:3:12: `f` is a function, so it can't be used here\n"
      "struct S { x: ?, y: ?, z: ? }\nfn f() -> void\nconst K = ?\nconst MAX = "
      "5\n" },
    { "struct Dup { x: int, x: u8 }\nstruct Dup { y: int }\nfn f(a: int, a: "
      "u8, int: u8) => 0\nconst int := 5\n",
      "a:1:22: `x` is already a member at 1:14\n"
      "a:2:8: `Dup` is already declared at a:1:8\n"
      "a:3:14: `a` is already a parameter at 3:6\n"
      "a:3:21: `int` is a builtin type name\n"
      "a:4:7: `int` is a builtin type name\n"
      "struct Dup { x: int }\nstruct Dup { y: int }\nfn f(a: int, int: u8) -> "
      "int\n" },
    { "enum H { a = Missing, b }\n",
      "a:1:14: unknown name `Missing`\n"
      "enum H { a, b }\n" },
    { "enum Kind: u8 { circle, square }\nunion Shape: Kind { circle: f32, "
      "triangle: f32 }\nenum E: u8 { a = 254, b, c }\nenum F: f32 { a }\nunion "
      "G: Shape { a }\n",
      "a:2:7: the union has no member `square` of the tag enum `Kind`\n"
      "a:2:34: `triangle` is not a member of the tag enum `Kind`\n"
      "a:3:26: 256 does not fit in `u8`\n"
      "a:4:9: expected an integer type\n"
      "a:5:10: expected an integer type\n"
      "enum Kind: u8 { circle = 0, square = 1 }\nunion Shape: Kind { circle: "
      "f32, triangle: f32 }\nenum E: u8 { a = 254, b = 255, c }\nenum F { a = "
      "0 }\nunion G { a }\n" },
    { "x := 5\nif x {}\nconst F := 1.5\nstruct S { a: geo.Point, union { i: "
      "int } }\nfn h(x) {}\n",
      "a:1:1: the checker doesn't support this yet\n"
      "a:2:1: only the file you run may contain code\n"
      "a:4:15: unknown name `geo`\n"
      "a:4:19: the checker doesn't support this yet\n"
      "a:4:26: the checker doesn't support this yet\n"
      "a:5:6: the checker doesn't support this yet\n"
      "const F = 1.5\nstruct S { a: ? }\nfn h() -> void\n" },
};

static const Case BODIES[] = {
    {
        "fn sum(n: int) -> int {\n"
        "    total := 0\n"
        "    for i in 0..n {\n"
        "        if i % 2 == 0 {\n"
        "            total += i\n"
        "        } else {\n"
        "            total -= 1\n"
        "        }\n"
        "    }\n"
        "    return total\n"
        "}\n",
        "fn sum(n: int) -> int\n"
        "    slots: n: int, total: int, i: int, $3: int\n"
        "    store int total, 0\n"
        "    %1 = load int n\n"
        "    store int i, 0\n"
        "    store int $3, %1\n"
        "    loop @4\n"
        "        %5 = load int i\n"
        "        %6 = load int $3\n"
        "        %7 = lt int %5, %6\n"
        "        if @8 %7\n"
        "            block @9\n"
        "                %10 = load int i\n"
        "                %11 = rem int %10, 2\n"
        "                %12 = eq int %11, 0\n"
        "                if @13 %12\n"
        "                    %14 = load int i\n"
        "                    %15 = load int total\n"
        "                    %16 = add int %15, %14\n"
        "                    store int total, %16\n"
        "                else\n"
        "                    %19 = load int total\n"
        "                    %20 = sub int %19, 1\n"
        "                    store int total, %20\n"
        "                end\n"
        "            end\n"
        "            %24 = load int i\n"
        "            %25 = add-wrap int %24, 1\n"
        "            store int i, %25\n"
        "            repeat @4\n"
        "        end\n"
        "    end\n"
        "    %30 = load int total\n"
        "    ret int %30\n",
    },
    {
        "fn find(limit: i32) -> i32 {\n"
        "    i: i32 = 0\n"
        "    while true {\n"
        "        i += 1\n"
        "        if i > limit or i == 100 {\n"
        "            break\n"
        "        }\n"
        "        if i % 3 != 0 and i > 2 {\n"
        "            continue\n"
        "        }\n"
        "    }\n"
        "    return i\n"
        "}\n"
        "\n"
        "fn sign(x: f64) -> i8 {\n"
        "    if x < 0 {\n"
        "        return -1\n"
        "    } else if x == 0 {\n"
        "        return 0\n"
        "    }\n"
        "    return 1\n"
        "}\n"
        "\n"
        "fn spin() -> int {\n"
        "    while true {\n"
        "    }\n"
        "}\n",
        "fn find(limit: i32) -> i32\n"
        "    slots: limit: i32, i: i32\n"
        "    store i32 i, 0\n"
        "    loop @1\n"
        "        if @2 true\n"
        "            %3 = load i32 i\n"
        "            %4 = add i32 %3, 1\n"
        "            store i32 i, %4\n"
        "            %6 = load i32 i\n"
        "            %7 = load i32 limit\n"
        "            %8 = gt i32 %6, %7\n"
        "            if @9 bool %8\n"
        "            else bool true\n"
        "                %11 = load i32 i\n"
        "                %12 = eq i32 %11, 100\n"
        "            %13 = end bool %12\n"
        "            if @14 %13\n"
        "                exit @1\n"
        "            end\n"
        "            %17 = load i32 i\n"
        "            %18 = rem i32 %17, 3\n"
        "            %19 = ne i32 %18, 0\n"
        "            if @20 bool %19\n"
        "                %21 = load i32 i\n"
        "                %22 = gt i32 %21, 2\n"
        "            else bool %22\n"
        "            %24 = end bool false\n"
        "            if @25 %24\n"
        "                repeat @1\n"
        "            end\n"
        "            repeat @1\n"
        "        end\n"
        "    end\n"
        "    %31 = load i32 i\n"
        "    ret i32 %31\n"
        "fn sign(x: f64) -> i8\n"
        "    slots: x: f64\n"
        "    %0 = load f64 x\n"
        "    %1 = lt f64 %0, 0\n"
        "    if @2 %1\n"
        "        ret i8 -1\n"
        "    else\n"
        "        %5 = load f64 x\n"
        "        %6 = eq f64 %5, 0\n"
        "        if @7 %6\n"
        "            ret i8 0\n"
        "        end\n"
        "    end\n"
        "    ret i8 1\n"
        "fn spin() -> int\n"
        "    slots:\n"
        "    loop @0\n"
        "        if @1 true\n"
        "            repeat @0\n"
        "        end\n"
        "    end\n",
    },
    {
        "fn scale(x: f32, k: f32 = 2) => x * k\n"
        "\n"
        "fn area(w: i32, h: i32) -> i64 {\n"
        "    a := i64(w) * h\n"
        "    return a\n"
        "}\n"
        "\n"
        "fn mix(a: u8, b: u32) -> u64 {\n"
        "    low := u8(b & 0xFF)\n"
        "    return u64(low + a) + u64(b) + u64(i8(a))\n"
        "}\n"
        "\n"
        "fn bits(n: u8) -> u32 {\n"
        "    mask: u32 = 1 << n\n"
        "    total: u32 = 0\n"
        "    for k in 0..=n {\n"
        "        total |= mask >> k\n"
        "    }\n"
        "    return total\n"
        "}\n"
        "\n"
        "fn run() {\n"
        "    s := scale(1.5)\n"
        "    t := scale(s, 0.1)\n"
        "    d: f64 = t\n"
        "    _ = area(2, 3)\n"
        "    big := 1 << 40\n"
        "    third := 1.0 / 3\n"
        "    flag := !(s > t) and true\n"
        "}\n",
        "fn scale(x: f32, k: f32 = 2) -> f32\n"
        "    slots: x: f32, k: f32\n"
        "    %0 = load f32 x\n"
        "    %1 = load f32 k\n"
        "    %2 = mul f32 %0, %1\n"
        "    ret f32 %2\n"
        "fn area(w: i32, h: i32) -> int\n"
        "    slots: w: i32, h: i32, a: int\n"
        "    %0 = load i32 w\n"
        "    %1 = convert int %0\n"
        "    %2 = load i32 h\n"
        "    %3 = convert int %2\n"
        "    %4 = mul int %1, %3\n"
        "    store int a, %4\n"
        "    %6 = load int a\n"
        "    ret int %6\n"
        "fn mix(a: u8, b: u32) -> u64\n"
        "    slots: a: u8, b: u32, low: u8\n"
        "    %0 = load u32 b\n"
        "    %1 = and u32 %0, 255\n"
        "    %2 = truncate u8 %1\n"
        "    store u8 low, %2\n"
        "    %4 = load u8 low\n"
        "    %5 = load u8 a\n"
        "    %6 = add u8 %4, %5\n"
        "    %7 = convert u64 %6\n"
        "    %8 = load u32 b\n"
        "    %9 = convert u64 %8\n"
        "    %10 = add u64 %7, %9\n"
        "    %11 = load u8 a\n"
        "    %12 = narrow i8 %11\n"
        "    %13 = narrow u64 %12\n"
        "    %14 = add u64 %10, %13\n"
        "    ret u64 %14\n"
        "fn bits(n: u8) -> u32\n"
        "    slots: n: u8, mask: u32, total: u32, k: u8, $4: u8\n"
        "    %0 = load u8 n\n"
        "    %1 = shl u32 1, %0\n"
        "    store u32 mask, %1\n"
        "    store u32 total, 0\n"
        "    %4 = load u8 n\n"
        "    store u8 k, 0\n"
        "    store u8 $4, %4\n"
        "    loop @7\n"
        "        %8 = load u8 k\n"
        "        %9 = load u8 $4\n"
        "        %10 = le u8 %8, %9\n"
        "        if @11 %10\n"
        "            block @12\n"
        "                %13 = load u32 mask\n"
        "                %14 = load u8 k\n"
        "                %15 = shr u32 %13, %14\n"
        "                %16 = load u32 total\n"
        "                %17 = or u32 %16, %15\n"
        "                store u32 total, %17\n"
        "            end\n"
        "            %20 = load u8 k\n"
        "            %21 = load u8 $4\n"
        "            %22 = eq u8 %20, %21\n"
        "            if @23 %22\n"
        "                exit @7\n"
        "            end\n"
        "            %26 = load u8 k\n"
        "            %27 = add-wrap u8 %26, 1\n"
        "            store u8 k, %27\n"
        "            repeat @7\n"
        "        end\n"
        "    end\n"
        "    %32 = load u32 total\n"
        "    ret u32 %32\n"
        "fn run() -> void\n"
        "    slots: s: f32, t: f32, d: f64, big: int, third: f32, flag: bool\n"
        "    %0 = call f32 scale(1.5, 2)\n"
        "    store f32 s, %0\n"
        "    %2 = load f32 s\n"
        "    %3 = call f32 scale(%2, 0.1)\n"
        "    store f32 t, %3\n"
        "    %5 = load f32 t\n"
        "    %6 = convert f64 %5\n"
        "    store f64 d, %6\n"
        "    %8 = call int area(2, 3)\n"
        "    drop %8\n"
        "    store int big, 1099511627776\n"
        "    store f32 third, 0.33333334\n"
        "    %12 = load f32 s\n"
        "    %13 = load f32 t\n"
        "    %14 = gt f32 %12, %13\n"
        "    %15 = not bool %14\n"
        "    if @16 bool %15\n"
        "    else bool true\n"
        "    %18 = end bool false\n"
        "    store bool flag, %18\n"
        "    ret void\n",
    },
    {
        "fn bad(x: i32) -> i64 {\n"
        "    y := x * x\n"
        "    z: i64 = y * 2\n"
        "    x := 1\n"
        "    w := 0.5 + x\n"
        "    if x {\n"
        "    }\n"
        "    5\n"
        "    break\n"
        "    return\n"
        "}\n"
        "\n"
        "pub fn none() {\n"
        "    return 1\n"
        "}\n"
        "\n"
        "fn missing(a: bool) -> int {\n"
        "    if a {\n"
        "        return 1\n"
        "    }\n"
        "}\n"
        "\n"
        "fn cyc() => cyc()\n"
        "\n"
        "fn calls() {\n"
        "    bad(1, 2)\n"
        "    g := calls\n"
        "    MAX = 3\n"
        "    1(2)\n"
        "    q := u8(300)\n"
        "    r := int(2.5)\n"
        "    s: u8 = -1\n"
        "    t := 1 << 600\n"
        "    u := true + 1\n"
        "    v := int\n"
        "    for i in 0.5..2 {\n"
        "    }\n"
        "}\n"
        "\n"
        "const MAX := 5\n",
        "a:3:16: `i32` and `int` don't mix without a conversion\n"
        "a:4:5: `x` is already a local at 1:8\n"
        "a:5:14: 0.5 does not fit in `i32`\n"
        "a:6:8: expected `bool`, found `i32`\n"
        "a:8:5: this value is not used; discard it with `_ = ...`\n"
        "a:9:5: `break` is only allowed inside a loop\n"
        "a:10:5: `return` needs a value of type `int`\n"
        "a:14:5: this function returns nothing, so `return` takes no value\n"
        "a:21:1: the function can reach its end without returning a value of "
        "type `int`\n"
        "a:23:4: `cyc` depends on itself\n"
        "a:26:5: wrong number of arguments: expected 1, found 2\n"
        "a:27:10: `calls` is a function, so it can't be used here\n"
        "a:28:5: only a local can be assigned to\n"
        "a:29:5: expected a function or a type\n"
        "a:30:13: 300 does not fit in `u8`\n"
        "a:31:14: 2.5 does not fit in `int`\n"
        "a:32:13: -1 does not fit in `u8`\n"
        "a:33:12: an integer constant must fit in 512 bits\n"
        "a:34:15: `untyped integer` and `bool` don't mix without a conversion\n"
        "a:35:10: expected a value\n"
        "a:36:17: `untyped float` doesn't support `..`\n"
        "fn bad(x: i32) -> int\n"
        "    slots: x: i32, y: i32, z: int, x: int, w: ?\n"
        "    %0 = load i32 x\n"
        "    %1 = load i32 x\n"
        "    %2 = mul i32 %0, %1\n"
        "    store i32 y, %2\n"
        "    %4 = load i32 y\n"
        "    %5 = mul i32 %4, 2\n"
        "    store int x, 1\n"
        "    %7 = load i32 x\n"
        "    %8 = load i32 x\n"
        "    if @9 false\n"
        "    end\n"
        "pub fn none() -> void\n"
        "    slots:\n"
        "fn missing(a: bool) -> int\n"
        "    slots: a: bool\n"
        "    %0 = load bool a\n"
        "    if @1 %0\n"
        "        ret int 1\n"
        "    end\n"
        "fn cyc() -> ?\n"
        "    slots:\n"
        "fn calls() -> void\n"
        "    slots: g: ?, q: ?, r: ?, s: u8, t: ?, u: ?, v: ?\n"
        "    loop @0\n"
        "        if @1 false\n"
        "            block @2\n"
        "            end\n"
        "        end\n"
        "    end\n"
        "    ret void\n",
    },
    {
        "fn first() => second() + 1\n"
        "\n"
        "fn second() => 2\n"
        "\n"
        "fn pick(a: bool) -> int {\n"
        "    if a {\n"
        "        x := 1\n"
        "        return x\n"
        "    } else {\n"
        "        x := 2\n"
        "        return x\n"
        "    }\n"
        "}\n"
        "\n"
        "fn count(n: int) -> int {\n"
        "    c := 0\n"
        "    for i in 0..n {\n"
        "        if i == 3 {\n"
        "            continue\n"
        "        }\n"
        "        if i == 7 {\n"
        "            break\n"
        "        }\n"
        "        c += 1\n"
        "    }\n"
        "    return c\n"
        "}\n",
        "fn first() -> int\n"
        "    slots:\n"
        "    %0 = call int second()\n"
        "    %1 = add int %0, 1\n"
        "    ret int %1\n"
        "fn second() -> int\n"
        "    slots:\n"
        "    ret int 2\n"
        "fn pick(a: bool) -> int\n"
        "    slots: a: bool, x: int, x: int\n"
        "    %0 = load bool a\n"
        "    if @1 %0\n"
        "        store int x, 1\n"
        "        %3 = load int x\n"
        "        ret int %3\n"
        "    else\n"
        "        store int x, 2\n"
        "        %7 = load int x\n"
        "        ret int %7\n"
        "    end\n"
        "fn count(n: int) -> int\n"
        "    slots: n: int, c: int, i: int, $3: int\n"
        "    store int c, 0\n"
        "    %1 = load int n\n"
        "    store int i, 0\n"
        "    store int $3, %1\n"
        "    loop @4\n"
        "        %5 = load int i\n"
        "        %6 = load int $3\n"
        "        %7 = lt int %5, %6\n"
        "        if @8 %7\n"
        "            block @9\n"
        "                %10 = load int i\n"
        "                %11 = eq int %10, 3\n"
        "                if @12 %11\n"
        "                    exit @9\n"
        "                end\n"
        "                %15 = load int i\n"
        "                %16 = eq int %15, 7\n"
        "                if @17 %16\n"
        "                    exit @4\n"
        "                end\n"
        "                %20 = load int c\n"
        "                %21 = add int %20, 1\n"
        "                store int c, %21\n"
        "            end\n"
        "            %24 = load int i\n"
        "            %25 = add-wrap int %24, 1\n"
        "            store int i, %25\n"
        "            repeat @4\n"
        "        end\n"
        "    end\n"
        "    %30 = load int c\n"
        "    ret int %30\n",
    },
    {
        "fn a() => b() + c()\n"
        "\n"
        "fn b() => c() * 2\n"
        "\n"
        "fn c() => 3\n"
        "\n"
        "fn d(x: int) -> int {\n"
        "    return a() + x\n"
        "}\n",
        "fn a() -> int\n"
        "    slots:\n"
        "    %0 = call int b()\n"
        "    %1 = call int c()\n"
        "    %2 = add int %0, %1\n"
        "    ret int %2\n"
        "fn b() -> int\n"
        "    slots:\n"
        "    %0 = call int c()\n"
        "    %1 = mul int %0, 2\n"
        "    ret int %1\n"
        "fn c() -> int\n"
        "    slots:\n"
        "    ret int 3\n"
        "fn d(x: int) -> int\n"
        "    slots: x: int\n"
        "    %0 = call int a()\n"
        "    %1 = load int x\n"
        "    %2 = add int %0, %1\n"
        "    ret int %2\n",
    },
    {
        "fn rules(n: int, _: bool, _: bool) -> int {\n"
        "    n = 2\n"
        "    n += 1\n"
        "    for i in 0..n {\n"
        "        i = 3\n"
        "        i += 1\n"
        "    }\n"
        "    for _ in 0..3 {\n"
        "    }\n"
        "    _ = n + 1\n"
        "    _ = 7\n"
        "    _ += 1\n"
        "    k := _\n"
        "    _ := 4\n"
        "    rules(1, true, false)\n"
        "    _ = rules(1, true, false)\n"
        "    return n\n"
        "}\n",
        "a:2:5: `n` is a parameter, so it can't be assigned\n"
        "a:3:5: `n` is a parameter, so it can't be assigned\n"
        "a:5:9: `i` is a loop variable, so it can't be assigned\n"
        "a:6:9: `i` is a loop variable, so it can't be assigned\n"
        "a:12:5: `_` can only discard a value, as in `_ = f()`\n"
        "a:13:10: `_` can only discard a value, as in `_ = f()`\n"
        "a:14:5: `_` can only discard a value, as in `_ = f()`\n"
        "a:15:5: this value is not used; discard it with `_ = ...`\n"
        "fn rules(n: int, _: bool, _: bool) -> int\n"
        "    slots: n: int, _: bool, _: bool, i: int, $4: int, _: int, $6: "
        "int, k: ?, _: int\n"
        "    %0 = load int n\n"
        "    store int i, 0\n"
        "    store int $4, %0\n"
        "    loop @3\n"
        "        %4 = load int i\n"
        "        %5 = load int $4\n"
        "        %6 = lt int %4, %5\n"
        "        if @7 %6\n"
        "            block @8\n"
        "            end\n"
        "            %10 = load int i\n"
        "            %11 = add-wrap int %10, 1\n"
        "            store int i, %11\n"
        "            repeat @3\n"
        "        end\n"
        "    end\n"
        "    store int _, 0\n"
        "    store int $6, 3\n"
        "    loop @18\n"
        "        %19 = load int _\n"
        "        %20 = load int $6\n"
        "        %21 = lt int %19, %20\n"
        "        if @22 %21\n"
        "            block @23\n"
        "            end\n"
        "            %25 = load int _\n"
        "            %26 = add-wrap int %25, 1\n"
        "            store int _, %26\n"
        "            repeat @18\n"
        "        end\n"
        "    end\n"
        "    %31 = load int n\n"
        "    %32 = add int %31, 1\n"
        "    drop %32\n"
        "    store int _, 4\n"
        "    %35 = call int rules(1, true, false)\n"
        "    %36 = call int rules(1, true, false)\n"
        "    drop %36\n"
        "    %38 = load int n\n"
        "    ret int %38\n",
    },
    {
        "fn mix(a: bool, b: bool, c: bool) -> bool {\n"
        "    return a and (b or c)\n"
        "}\n",
        "fn mix(a: bool, b: bool, c: bool) -> bool\n"
        "    slots: a: bool, b: bool, c: bool\n"
        "    %0 = load bool a\n"
        "    if @1 bool %0\n"
        "        %2 = load bool b\n"
        "        if @3 bool %2\n"
        "        else bool true\n"
        "            %5 = load bool c\n"
        "        %6 = end bool %5\n"
        "    else bool %6\n"
        "    %8 = end bool false\n"
        "    ret bool %8\n",
    },
    {
        "const OFF := false\n"
        "fn g() -> bool {\n"
        "    return true\n"
        "}\n"
        "fn skip() -> bool {\n"
        "    return OFF and g()\n"
        "}\n",
        "fn g() -> bool\n"
        "    slots:\n"
        "    ret bool true\n"
        "fn skip() -> bool\n"
        "    slots:\n"
        "    if @0 false\n"
        "        %1 = call bool g()\n"
        "        drop %1\n"
        "    end\n"
        "    ret bool false\n",
    },
};

TEST_CASE(declarations)
{
    for (size_t i = 0; i < sizeof DECLARATIONS / sizeof DECLARATIONS[0]; i++) {
        REQUIRE(checks_to(&DECLARATIONS[i].src, 1, 1, DECLARATIONS[i].want));
    }
    return true;
}

TEST_CASE(errors)
{
    for (size_t i = 0; i < sizeof ERRORS / sizeof ERRORS[0]; i++) {
        REQUIRE(checks_to(&ERRORS[i].src, 1, 1, ERRORS[i].want));
    }
    return true;
}

TEST_CASE(bodies)
{
    for (size_t i = 0; i < sizeof BODIES / sizeof BODIES[0]; i++) {
        REQUIRE(compiles_to(&BODIES[i].src, 1, 1, true, BODIES[i].want));
    }
    return true;
}

TEST_CASE(files)
{
    const char *texts[] = {
        "const A := B * 2\nstruct S { t: T }\nconst Dup := 1\n",
        "const B := 21\nstruct T { s: S }\nconst Dup := 2\n",
    };
    REQUIRE(checks_to(
        texts,
        2,
        2,
        "a:2:8: `S` depends on itself through `T`\n"
        "b:3:7: `Dup` is already declared at a:3:7\n"
        "const A = 42\nstruct S { t: T }\nconst Dup = 1\nconst B = 21\nstruct "
        "T { s: S }\nconst Dup = 2\n"
    ));
    return true;
}

TEST_CASE(entry_file)
{
    const char *texts[] = {
        "import \"std/fmt\"\nfmt.print(1)\nif ready { go() }\ntotal += "
        "1\nconst A := 1\n",
        "struct Player { hp: int }\nfmt.print(2)\nscore := 5\n",
    };
    REQUIRE(checks_to(
        texts,
        2,
        0,
        "a:2:1: the checker doesn't support this yet\n"
        "a:3:1: the checker doesn't support this yet\n"
        "a:4:1: the checker doesn't support this yet\n"
        "b:2:1: only the file you run may contain code\n"
        "b:3:1: the checker doesn't support this yet\n"
        "const A = 1\nstruct Player { hp: int }\n"
    ));
    return true;
}

TEST_CASE(depth_limit)
{
    u32 n = 1100;
    size_t cap = (size_t)n * 32;
    char *text = malloc(cap);
    size_t len = 0;
    for (u32 i = 0; i < n; i++) {
        len += (size_t)snprintf(text + len, cap - len, "const C%u := C%u + 1\n", i, i + 1);
    }
    snprintf(text + len, cap - len, "const C%u := 0\n", n);

    const char *texts[] = { text };
    char *got = check_files(texts, 1, 1, false);
    free(text);

    const char
        *want = "a:1000:15: this needs more than 1000 nested declarations\n";
    bool ok = strncmp(got, want, strlen(want)) == 0 &&
              strstr(got, "const C1099 = 1\n") != NULL;
    if (!ok) {
        fprintf(stderr, "depth limit\n  got: %.200s\n", got);
    }
    free(got);
    REQUIRE(ok);
    return true;
}

TEST_SUITE(check_tests)
{
    RUN_TEST_CASE(declarations);
    RUN_TEST_CASE(errors);
    RUN_TEST_CASE(bodies);
    RUN_TEST_CASE(files);
    RUN_TEST_CASE(entry_file);
    RUN_TEST_CASE(depth_limit);
}
