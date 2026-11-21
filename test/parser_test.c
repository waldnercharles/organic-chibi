#define _DEFAULT_SOURCE

#include "../src/parser.h"
#include "../vendor/pico_unit/pico_unit.h"
#include "random/code.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *src;
    Tokens tokens;
    Nodes nodes;
    Parser parser;
    int ntokens;
    int count;
} Parse;

static Parse
parse(Arena *arena, const char *text)
{
    size_t len = strlen(text);
    Parse r = { .src = arena_alloc(arena, len + LEXER_SOURCE_PADDING, 16) };
    memset(r.src, 0, len + LEXER_SOURCE_PADDING);
    memcpy(r.src, text, len);

    Lexer lx;
    lexer_init(&lx, arena, len);
    r.tokens = lexer_alloc_tokens(arena, len);
    r.ntokens = lexer_tokenize(&lx, r.src, r.tokens);
    assert(r.ntokens > 0);

    r.nodes = parser_alloc_nodes(arena, r.ntokens);
    r.count = parser_parse(&r.parser, r.tokens, r.ntokens, r.nodes);
    return r;
}

static bool
tokens_used_at_most_twice(const char *text, Parse r)
{
    int n = r.count >= 0 ? r.count : r.parser.count;
    u8 *uses = calloc((size_t)r.ntokens, 1);
    bool ok = true;
    for (int i = 0; i < n; i++) {
        ok = ok && ++uses[r.nodes.tokens[i]] <= 2;
    }
    free(uses);

    if (!ok) {
        fprintf(stderr, "parse \"%s\"\n  a token is used by more than 2 nodes\n", text);
    }
    return ok;
}

static bool
parses_to(const char *text, const char *want)
{
    Arena arena = { 0 };
    Parse r = parse(&arena, text);

    char *got = NULL;
    size_t got_len = 0;
    FILE *out = open_memstream(&got, &got_len);
    if (r.count >= 0) {
        parser_dump(out, &arena, r.src, r.tokens, r.nodes, r.count);
    } else {
        fprintf(out, "error at byte %u: %s\n", r.parser.error_pos, r.parser.error);
    }
    fclose(out);

    if (got_len > 0 && got[got_len - 1] == '\n') {
        got_len--;
    }

    size_t want_len = strlen(want);
    bool same = got_len == want_len && memcmp(got, want, want_len) == 0 &&
                tokens_used_at_most_twice(text, r);
    if (!same) {
        fprintf(stderr, "parse \"%s\"\n  got:  %.*s\n  want: %s\n", text, (int)got_len, got, want);
    }

    free(got);
    arena_free(&arena);
    return same;
}

static bool
fails_with(const char *text, const char *error, u32 pos)
{
    Arena arena = { 0 };
    Parse r = parse(&arena, text);

    bool same = r.count < 0 && strcmp(r.parser.error, error) == 0 &&
                r.parser.error_pos == pos && tokens_used_at_most_twice(text, r);
    if (!same) {
        fprintf(
            stderr,
            "parse \"%s\"\n  got:  %s at byte %u\n  want: %s at byte %u\n",
            text,
            r.count < 0 ? r.parser.error : "no error",
            r.parser.error_pos,
            error,
            pos
        );
    }

    arena_free(&arena);
    return same;
}

// clang-format off
static const struct
{
    const char *src;
    const char *want;
} TREES[] = {
    { "a", "a" },
    { "42", "42" },
    { "1.5", "1.5" },
    { "\"hi\"", "\"hi\"" },
    { "true", "true" },
    { "false", "false" },
    { "nil", "nil" },
    { "a + b", "(+ a b)" },
    { "a + b + c", "(+ (+ a b) c)" },
    { "a - b - c", "(- (- a b) c)" },
    { "a + b * c", "(+ a (* b c))" },
    { "a * b + c", "(+ (* a b) c)" },
    { "(a + b) * c", "(* (+ a b) c)" },
    { "a * (b + c)", "(* a (+ b c))" },
    { "((a))", "a" },
    { "-a", "(- a)" },
    { "- -a", "(- (- a))" },
    { "-a * b", "(* (- a) b)" },
    { "a - -b", "(- a (- b))" },
    { "!a and b", "(and (! a) b)" },
    { "~a & b", "(& (~ a) b)" },
    { "a % b / c", "(/ (% a b) c)" },
    { "a << b + c", "(+ (<< a b) c)" },
    { "a & b == c", "(== (& a b) c)" },
    { "a | b ^ c", "(^ (| a b) c)" },
    { "a >> b <= c", "(<= (>> a b) c)" },
    { "a == b and c != d", "(and (== a b) (!= c d))" },
    { "a or b and c", "(or a (and b c))" },
    { "a and b or c", "(or (and a b) c)" },
    { "a < b or c >= d and e", "(or (< a b) (and (>= c d) e))" },
    { "(a < b) == c", "(== (< a b) c)" },
    { "a < (b < c)", "(< a (< b c))" },
    { "a +\n  b", "(+ a b)" },
    { "f()", "(call f)" },
    { "f(a)", "(call f a)" },
    { "f(a, b)", "(call f a b)" },
    { "f(a, b,)", "(call f a b)" },
    { "f(\n  a,\n  b,\n)", "(call f a b)" },
    { "f(a + b, c * d)", "(call f (+ a b) (* c d))" },
    { "f(g(x))", "(call f (call g x))" },
    { "f(a)(b)", "(call (call f a) b)" },
    { "f()()", "(call (call f))" },
    { "(f)(x)", "(call f x)" },
    { "a[i]", "(index a i)" },
    { "a[i + 1]", "(index a (+ i 1))" },
    { "a[i][j]", "(index (index a i) j)" },
    { "a[f(i)]", "(index a (call f i))" },
    { "a.b", "(field a b)" },
    { "a.b.c", "(field (field a b) c)" },
    { "a.b(c)[d].e", "(field (index (call (field a b) c) d) e)" },
    { "(a + b).c", "(field (+ a b) c)" },
    { "\"s\".len", "(field \"s\" len)" },
    { "-a.b", "(- (field a b))" },
    { "-f(x)", "(- (call f x))" },
    { "!a[i]", "(! (index a i))" },
    { "a * f(b) + c", "(+ (* a (call f b)) c)" },
    { "&x", "(& x)" },
    { "&a.b", "(& (field a b))" },
    { "p.*", "(deref p)" },
    { "p.*.x", "(field (deref p) x)" },
    { "x.type", "(field x type)" },
    { "", "" },
    { "a\nb", "a\nb" },
    { "a; b", "a\nb" },
    { ";; a;; b;", "a\nb" },
    { "a\n- b", "(- a b)" },
    { "a\n* b", "(* a b)" },
    { "a\nand b", "(and a b)" },
    { "a or\nb", "(or a b)" },
    { "a\n.b", "(field a b)" },
    { "a\n!b", "a\n(! b)" },
    { "f\n(x)", "f\nx" },
    { "f(a\n(b))", "(call f (call a b))" },
    { "a[i\n[j]]", "(index a (index i j))" },
    { "x := 1", "(:= x 1)" },
    { "x: int", "(: x int)" },
    { "x: int = 1", "(: x int 1)" },
    { "x: int\n= 1", "(: x int 1)" },
    { "x: int\ny := 2", "(: x int)\n(:= y 2)" },
    { "a = b", "(= a b)" },
    { "a.b[i] += 1", "(+= (index (field a b) i) 1)" },
    { "a <<= 2", "(<<= a 2)" },
    { "a >>= b & c", "(>>= a (& b c))" },
    { "p.* = q.*", "(= (deref p) (deref q))" },
    { "x = 1\n+ 2", "(= x (+ 1 2))" },
    { "{}", "(block)" },
    { "{ a; b }", "(block a b)" },
    { "{\n  a\n  { b }\n}", "(block a (block b))" },
    { "if a { b }", "(if a (block b))" },
    { "if (a) {}", "(if a (block))" },
    { "if a { b } else { c }", "(if a (block b) (block c))" },
    { "if a {\n} \nelse {\n}", "(if a (block) (block))" },
    { "if a {} else if b {} else {}", "(if a (block) b (block) (block))" },
    { "if a {} else if b {}", "(if a (block) b (block))" },
    { "if a and b or c {}", "(if (or (and a b) c) (block))" },
    { "while a < b { a += 1 }", "(while (< a b) (block (+= a 1)))" },
    { "while a { break; continue }", "(while a (block break continue))" },
    { "return", "return" },
    { "return a + b", "(return (+ a b))" },
    { "return\nx", "return\nx" },
    { "{ return }", "(block return)" },
    { "defer f()", "(defer (call f))" },
    { "defer { a; b }", "(defer (block a b))" },
    { "defer x = 1; y", "(defer (= x 1))\ny" },
    { "fn f() {}", "(fn f (block))" },
    { "fn add(a: int, b: int) -> int { return a + b }",
      "(fn add (: a int) (: b int) (-> int) (block (return (+ a b))))" },
    { "fn f(a, b) {}", "(fn f a b (block))" },
    { "fn f(\n  a,\n  b,\n) {}", "(fn f a b (block))" },
    { "fn f(a: int = 1, b := 2) {}", "(fn f (: a int 1) (:= b 2) (block))" },
    { "fn square(x: int) -> int => x * x", "(fn square (: x int) (-> int) (return (* x x)))" },
    { "fn f()\n{\n}", "(fn f (block))" },
    { "fn f() { fn g() {} }", "(fn f (block (fn g (block))))" },
    { "f := fn(x) => x * 2", "(:= f (fn-literal x (return (* x 2))))" },
    { "g(fn(x: int) -> int { return x })", "(call g (fn-literal (: x int) (-> int) (block (return x))))" },
    { "g(fn() {\n  a\n  (b)\n})", "(call g (fn-literal (block a b)))" },
    { "g(fn() => a\n  (b))", "(call g (fn-literal (return (call a b))))" },
    { "const X := 10", "(const X 10)" },
    { "const X: int = 10", "(const X int 10)" },
    { "f(.a = 1)", "(call f (arg a 1))" },
    { "f(x, .b = 2, .c = y + 1)", "(call f x (arg b 2) (arg c (+ y 1)))" },
    { "f(\n  .a = 1,\n  .b = 2,\n)", "(call f (arg a 1) (arg b 2))" },
    { "f(.a = fn(x) => x)", "(call f (arg a (fn-literal x (return x))))" },
    { "x: *int", "(: x (ptr int))" },
    { "x: []*Node", "(: x (slice (ptr Node)))" },
    { "x: [*]u8", "(: x (many u8))" },
    { "argv: [*][*]u8", "(: argv (many (many u8)))" },
    { "x: ?[*]*Node", "(: x (optional (many (ptr Node))))" },
    { "x: [16]u8", "(: x (array 16 u8))" },
    { "m: [4, 4]f32", "(: m (array 4 4 f32))" },
    { "rows: [4][4]f32", "(: rows (array 4 (array 4 f32)))" },
    { "x: [N * 2]T", "(: x (array (* N 2) T))" },
    { "f: fn(int, x: *T) -> bool", "(: f (fn-type int (: x (ptr T)) (-> bool)))" },
    { "g: fn()", "(: g (fn-type))" },
    { "h: fn() -> fn(int) -> int", "(: h (fn-type (-> (fn-type int (-> int)))))" },
    { "c: extern fn(a: *u8, b: extern fn()) -> i32",
      "(: c (extern (fn-type (: a (ptr u8)) (: b (extern (fn-type))) (-> i32))))" },
    { "fn f(p: *Node) -> []int {}", "(fn f (: p (ptr Node)) (-> (slice int)) (block))" },
    { "m[i, j] = 1.0", "(= (index m i j) 1.0)" },
    { "m[i][j]", "(index (index m i) j)" },
    { "a[i, j, k, l]", "(index a i j k l)" },
    { "p := Point.{ 1, 2 }", "(:= p (literal Point 1 2))" },
    { "p := Point.{ .x = 1, .y = 2 }", "(:= p (literal Point (arg x 1) (arg y 2)))" },
    { "p: Point = .{ 1, 2 }", "(: p Point (literal 1 2))" },
    { "f(.{})", "(call f (literal))" },
    { "x := geo.Point.{\n  1,\n  2,\n}", "(:= x (literal (field geo Point) 1 2))" },
    { "if p == Point.{} {}", "(if (== p (literal Point)) (block))" },
    { "c := Color.red", "(:= c (field Color red))" },
    { "set(.green)", "(call set (. green))" },
    { "f(.a)", "(call f (. a))" },
    { "if c == .blue {}", "(if (== c (. blue)) (block))" },
    { "struct Point { x: int, y: int = 0 }", "(struct Point (: x int) (: y int 0))" },
    { "struct P {\n  x: *P\n  y := 1\n}", "(struct P (: x (ptr P)) (:= y 1))" },
    { "struct E {}", "(struct E)" },
    { "struct P {\n  a: int,\n  b: int,\n}", "(struct P (: a int) (: b int))" },
    { "enum Color: u8 {\n  red = 1\n  green\n  blue\n}", "(enum Color (: u8) (= red 1) green blue)" },
    { "enum E { a, b }", "(enum E a b)" },
    { "struct P { x: int; y: int; }", "(struct P (: x int) (: y int))" },
    { "struct P {\n  x: int;\n  y: int = 1;\n}", "(struct P (: x int) (: y int 1))" },
    { "enum E { a; b = 2; }", "(enum E a (= b 2))" },
    { "a := .[1, 2, 3]", "(:= a (array-literal 1 2 3))" },
    { "b := u8.[1, 2, 3]", "(:= b (array-literal u8 1 2 3))" },
    { "nodes := *Node.[x, y]", "(:= nodes (array-literal (ptr Node) x y))" },
    { "m := [2]f32.[.[1, 0], .[0, 1]]", "(:= m (array-literal (array 2 f32) (array-literal 1 0) (array-literal 0 1)))" },
    { "f(.[])", "(call f (array-literal))" },
    { "x := .[\n  1,\n  2,\n]", "(:= x (array-literal 1 2))" },
    { "s := []u8.{ data, len }", "(:= s (literal (slice u8) data len))" },
    { "struct Token { type: Kind, in: bool }", "(struct Token (: type Kind) (: in bool))" },
    { "enum Op { in, as }", "(enum Op in as)" },
    { "t.type = .in", "(= (field t type) (. in))" },
    { "f(.type = 1)", "(call f (arg type 1))" },
    { "for x in items {}", "(for items x (block))" },
    { "for x, i in items { f(x) }", "(for items x i (block (call f x)))" },
    { "for _, i in items {}", "(for items _ i (block))" },
    { "for i in 0..n {}", "(for (.. 0 n) i (block))" },
    { "for i in 1..=10 {}", "(for (..= 1 10) i (block))" },
    { "for i in a + 1..b * 2 {}", "(for (.. (+ a 1) (* b 2)) i (block))" },
    { "for x in xs { for y in ys { break } }", "(for xs x (block (for ys y (block break))))" },
    { "switch c { .red, .green => go(), _ => stop() }",
      "(switch c (case (. red) (. green) (call go)) (case _ (call stop)))" },
    { "switch n {\n  0..10 => { small() }\n  10 => ten()\n  _ => {}\n}",
      "(switch n (case (.. 0 10) (block (call small))) (case 10 (call ten)) (case _ (block)))" },
    { "switch x {}", "(switch x)" },
    { "switch x { 1 => return; 2 => break }", "(switch x (case 1 return) (case 2 break))" },
    { "switch a { 1 => switch b { 2 => c } }", "(switch a (case 1 (switch b (case 2 c))))" },
    { "switch x { 1 => y = 2, }", "(switch x (case 1 (= y 2)))" },
    { "_ = f()", "(= _ (call f))" },
    { "switch c {\n    .red => go()\n    .green => stop()\n}",
      "(switch c (case (. red) (call go)) (case (. green) (call stop)))" },
    { "switch n {\n  1 => go()\n  -1 => back()\n}", "(switch n (case 1 (call go)) (case (- 1) (call back)))" },
    { "switch x {\n  _ => {\n    a\n      .b()\n  }\n}", "(switch x (case _ (block (call (field a b)))))" },
    { "switch x {\n  1 => f(a,\n    b)\n}", "(switch x (case 1 (call f a b)))" },
    { "switch x { 1 => a }\nb\n  .c", "(switch x (case 1 a))\n(field b c)" },
    { "switch x { 1 => return, 2 => b }", "(switch x (case 1 return) (case 2 b))" },
    { "package geo\nstruct Point { x: int }", "(package geo)\n(struct Point (: x int))" },
    { "// header\npackage geo", "(package geo)" },
    { "import \"std/fmt\"", "(import \"std/fmt\")" },
    { "const f := import \"std/fmt\"", "(const f (import \"std/fmt\"))" },
    { "const SDL := import \"SDL3/SDL.h\"", "(const SDL (import \"SDL3/SDL.h\"))" },
    { "x := import \"std\".fmt.print", "(:= x (field (field (import \"std\") fmt) print))" },
    { "pub fn f() {}", "(pub (fn f (block)))" },
    { "pub struct P { x: int }", "(pub (struct P (: x int)))" },
    { "pub enum E { a }", "(pub (enum E a))" },
    { "pub const X := 1", "(pub (const X 1))" },
    { "pub x := 1", "(pub (:= x 1))" },
    { "pub x: int", "(pub (: x int))" },
    { "export fn f() {}", "(export (fn f (block)))" },
    { "pub export fn f() {}", "(pub (export (fn f (block))))" },
    { "extern fn f(x: int) -> int", "(extern (fn f (: x int) (-> int)))" },
    { "pub extern fn f()", "(pub (extern (fn f)))" },
    { "extern fn printf(fmt: *u8, ..) -> i32", "(extern (fn printf (: fmt (ptr u8)) .. (-> i32)))" },
    { "extern fn f(..)", "(extern (fn f ..))" },
    { "x: geo.Point", "(: x (field geo Point))" },
    { "p: *SDL.Window", "(: p (ptr (field SDL Window)))" },
    { "fn f(w: fmt.Writer) -> io.Error {}", "(fn f (: w (field fmt Writer)) (-> (field io Error)) (block))" },
    { "s := []geo.Point.{}", "(:= s (literal (slice (field geo Point))))" },
    { "x: a.type", "(: x (field a type))" },
    { "h = (h ^ b) *% 16777619", "(= h (*% (^ h b) 16777619))" },
    { "a +% b * c", "(+% a (* b c))" },
    { "a -% b -% c", "(-% (-% a b) c)" },
    { "a *% b +% c", "(+% (*% a b) c)" },
    { "-%x", "(-% x)" },
    { "a -% -%b", "(-% a (-% b))" },
    { "a\n+% b", "(+% a b)" },
    { "x +%= 1", "(+%= x 1)" },
    { "x -%= 1", "(-%= x 1)" },
    { "x *%= 2", "(*%= x 2)" },
    { "buf: [4096]u8 = undefined", "(: buf (array 4096 u8) undefined)" },
    { "buf: [4]u8 =\n  undefined", "(: buf (array 4 u8) undefined)" },
    { "struct F { s: [256]u8 = undefined }", "(struct F (: s (array 256 u8) undefined))" },
    { "a[1..4]", "(slice a 1 4)" },
    { "a[i..]", "(slice-from a i)" },
    { "a[..n]", "(slice-to a n)" },
    { "a[i + 1..n - 1]", "(slice a (+ i 1) (- n 1))" },
    { "a[1..][..2]", "(slice-to (slice-from a 1) 2)" },
    { "a[\n  1..\n  4\n]", "(slice a 1 4)" },
    { "a[1..3] = 0", "(= (slice a 1 3) 0)" },
    { "for x in a[1..] {}", "(for (slice-from a 1) x (block))" },
    { "a[1..=3]", "(slice-inclusive a 1 3)" },
    { "a[..=n]", "(slice-to-inclusive a n)" },
    { "a[i + 1..=n - 1]", "(slice-inclusive a (+ i 1) (- n 1))" },
    { "a[..=n][1..]", "(slice-from (slice-to-inclusive a n) 1)" },
    { "p!!.next", "(field (unwrap p) next)" },
    { "f(a!!, b!![0])", "(call f (unwrap a) (index (unwrap b) 0))" },
    { "a!!!!", "(unwrap (unwrap a))" },
    { "x!! = 1", "(= (unwrap x) 1)" },
    { "(a\n!!)", "(unwrap a)" },
    { "a ?? b + 1 == c", "(== (?? a (+ b 1)) c)" },
    { "a ?? b ?? c", "(?? (?? a b) c)" },
    { "a ?? b or c", "(or (?? a b) c)" },
    { "a\n?? b", "(?? a b)" },
    { "x ?\?= load()", "(?\?= x (call load))" },
    { "x: ?int", "(: x (optional int))" },
    { "next: ?*Node = nil", "(: next (optional (ptr Node)) nil)" },
    { "fn f() -> ?[]u8 {}", "(fn f (-> (optional (slice u8))) (block))" },
    { "x: []?u8", "(: x (slice (optional u8)))" },
    { "x: *?T", "(: x (ptr (optional T)))" },
    { "x: ??int", "(: x (optional (optional int)))" },
    { "f: fn(?int) -> ?a.B", "(: f (fn-type (optional int) (-> (optional (field a B)))))" },
    { "bits := bitcast(u32, f)", "(:= bits (bitcast u32 f))" },
    { "bitcast(*u8, &header)", "(bitcast (ptr u8) (& header))" },
    { "bitcast(*Header, raw).size", "(field (bitcast (ptr Header) raw) size)" },
    { "bitcast(?*T, p)", "(bitcast (optional (ptr T)) p)" },
    { "bitcast([4]u8, x)", "(bitcast (array 4 u8) x)" },
    { "bitcast(\n  u32,\n  a + b\n)", "(bitcast u32 (+ a b))" },
    { "n := u32(total)", "(:= n (call u32 total))" },
    { "union Bits {\n  f: f32\n  u: u32\n}", "(union Bits (: f f32) (: u u32))" },
    { "union Shape: u8 { circle: Circle, rect: Rect, none }",
      "(union Shape (: u8) (: circle Circle) (: rect Rect) none)" },
    { "union Event: EventKind {\n  key: KeyEvent\n  quit\n}", "(union Event (: EventKind) (: key KeyEvent) quit)" },
    { "pub union U { a: int }", "(pub (union U (: a int)))" },
    { "struct Value { kind: u8, union { i: i64, f: f64 } }", "(struct Value (: kind u8) (union (: i i64) (: f f64)))" },
    { "struct S { union: int }", "(struct S (: union int))" },
    { "switch v in e {\n  .key => press(v.code)\n  .quit => stop()\n}",
      "(switch e v (case (. key) (call press (field v code))) (case (. quit) (call stop)))" },
    { "switch &v in next() { .mouse => v.x += 1 }", "(switch (call next) (& v) (case (. mouse) (+= (field v x) 1)))" },
    { "switch v {}", "(switch v)" },
    { "if e is .key { press(e.key.code) }", "(if (is e (. key)) (block (call press (field (field e key) code))))" },
    { "ok := e is .quit and running", "(:= ok (and (is e (. quit)) running))" },
    { "outer: for x in xs { break outer }", "(label outer (for xs x (block (break outer))))" },
    { "outer: while a { continue outer }", "(label outer (while a (block (continue outer))))" },
    { "sw: switch s { .a => continue sw .b, _ => {} }",
      "(label sw (switch s (case (. a) (continue sw (. b))) (case _ (block))))" },
    { "blk: {\n  break blk\n}", "(label blk (block (break blk)))" },
    { "icon := found: {\n  if a { break found x }\n  break found y\n}",
      "(:= icon (label found (block (if a (block (break found x))) (break found y))))" },
    { "f(found: { break found 1 })", "(call f (label found (block (break found 1))))" },
    { "x := switch k { .a => 1, .b => 2 }", "(:= x (switch k (case (. a) 1) (case (. b) 2)))" },
    { "return switch k {\n  .a => 1\n  _ => 0\n}", "(return (switch k (case (. a) 1) (case _ 0)))" },
    { "break\nx", "break\nx" },
    { "break found\nx", "(break found)\nx" },
    { "x: int\ny: {}", "(: x int)\n(label y (block))" },
    { "cfg := try parse(try read_file(path))", "(:= cfg (try (call parse (try (call read_file path)))))" },
    { "n := try open(path).size()", "(:= n (try (call (field (call open path) size))))" },
    { "n := (try open(path)).size()", "(:= n (call (field (try (call open path)) size)))" },
    { "x := try a() + try b()", "(:= x (+ (try (call a)) (try (call b))))" },
    { "x := !try f()", "(:= x (! (try (call f))))" },
    { "try a() catch b", "(catch (try (call a)) b)" },
    { "x := f() catch 0", "(:= x (catch (call f) 0))" },
    { "x := f() catch err => log(err)", "(:= x (catch (call f) err (call log err)))" },
    { "x := f() catch err => done: {\n  log(err)\n  break done 1\n}",
      "(:= x (catch (call f) err (label done (block (call log err) (break done 1)))))" },
    { "f() catch { return }", "(catch (call f) (block return))" },
    { "cfg := try parse(read_file(path) catch text)", "(:= cfg (try (call parse (catch (call read_file path) text))))" },
    { "a ?? b catch c", "(catch (?? a b) c)" },
    { "if f() catch false { go() }", "(if (catch (call f) false) (block (call go)))" },
    { "x := f()\n  catch 0", "(:= x (catch (call f) 0))" },
    { "defer catch destroy_texture(atlas)", "(defer-catch (call destroy_texture atlas))" },
    { "defer catch err => log(err)", "(defer-catch err (call log err))" },
    { "fn load() -> !Level {}", "(fn load (-> (! Level)) (block))" },
    { "fn load() -> LoadError!Level {}", "(fn load (-> (! LoadError Level)) (block))" },
    { "fn save() -> !void {}", "(fn save (-> (! void)) (block))" },
    { "fn save() -> LoadError!void {}", "(fn save (-> (! LoadError void)) (block))" },
    { "save: fn() -> !Level\ncount := 0", "(: save (fn-type (-> (! Level))))\n(:= count 0)" },
    { "x: error!int", "(: x (! error int))" },
    { "f: fn() -> io.Error!?*Node", "(: f (fn-type (-> (! (field io Error) (optional (ptr Node))))))" },
    { "return error.not_found", "(return (field error not_found))" },
    { "const LoadError := FileError | ParseError | error.{ out_of_slots }",
      "(const LoadError (| (| FileError ParseError) (literal error out_of_slots)))" },
};

static const struct
{
    const char *src;
    const char *error;
    u32 pos;
} ERRORS[] = {
    { "a +", "expected an expression", 3 },
    { "a + * b", "use `.*` to dereference", 4 },
    { "* a", "use `.*` to dereference", 0 },
    { "()", "expected an expression", 1 },
    { "(a", "expected `)`", 2 },
    { "a)", "expected `;` or a new line", 1 },
    { "a b", "expected `;` or a new line", 2 },
    { "a < b < c", "comparisons cannot be chained; add parentheses", 6 },
    { "a == b != c", "comparisons cannot be chained; add parentheses", 7 },
    { "a < b + c > d", "comparisons cannot be chained; add parentheses", 10 },
    { "f(", "expected an expression", 2 },
    { "f(a", "expected `,` or `)`", 3 },
    { "f(a b)", "expected `,` or `)`", 4 },
    { "f(,)", "expected an expression", 2 },
    { "f(a,,)", "expected an expression", 4 },
    { "f(a))", "expected `;` or a new line", 4 },
    { "a[", "expected an expression", 2 },
    { "a[]", "expected an expression", 2 },
    { "a[i", "expected `]`", 3 },
    { "a.", "expected a field name after `.`", 2 },
    { "a.(b)", "expected a field name after `.`", 2 },
    { "a.+", "expected a field name after `.`", 2 },
    { "a && b", "use `and` instead of `&&`", 2 },
    { "a || b", "use `or` instead of `||`", 2 },
    { "(a && b)", "use `and` instead of `&&`", 3 },
    { "a = b = c", "assignments cannot be chained", 6 },
    { "a := b := c", "expected `;` or a new line", 7 },
    { "x: 1", "expected a type", 3 },
    { "x :=", "expected an expression", 4 },
    { "a\n[i]", "expected a type", 5 },
    { "[4]int", "expected `.{` or `.[` after a type", 6 },
    { ".[1, 2", "expected `,` or `]`", 6 },
    { "u8.[1 2]", "expected `,` or `]`", 6 },
    { "{ a", "expected `}`", 3 },
    { "a }", "unmatched `}`", 2 },
    { "}", "unmatched `}`", 0 },
    { "if a b", "expected `{`", 5 },
    { "if a { b", "expected `}`", 8 },
    { "if a {} b", "expected `;` or a new line", 8 },
    { "if a {} else b", "expected `{`", 13 },
    { "if a {} else if {}", "expected an expression", 16 },
    { "while a", "expected `{`", 7 },
    { "return a b", "expected `;` or a new line", 9 },
    { "else {}", "expected an expression", 0 },
    { "fn f {}", "expected `(`", 5 },
    { "fn f(1) {}", "expected a parameter name", 5 },
    { "fn f(a b) {}", "expected `,` or `)`", 7 },
    { "fn f()", "expected `{` or `=>`", 6 },
    { "fn f() int {}", "expected `{` or `=>`", 7 },
    { "fn f() -> {}", "expected a type", 10 },
    { "fn f() => {}", "expected an expression", 10 },
    { "fn(", "expected a parameter name", 3 },
    { "const X = 1", "expected `:=` or `:`", 8 },
    { "const X: int", "expected `=`", 12 },
    { "const 1 := 2", "expected a name", 6 },
    { "f(.a = )", "expected an expression", 7 },
    { "f(.", "expected an expression", 2 },
    { "x: *", "expected a type", 4 },
    { "x: [*]", "expected a type", 6 },
    { "x: 1", "expected a type", 3 },
    { "x: [4 int", "expected `]`", 6 },
    { "x: [1, 2, 3, 4, 5]int", "at most 4 dimensions", 14 },
    { "m[1, 2, 3, 4, 5]", "at most 4 dimensions", 12 },
    { "f: fn(int", "expected `,` or `)`", 9 },
    { "f: fn int", "expected `(`", 6 },
    { "P.{ 1", "expected `,` or `}`", 5 },
    { ".{ 1 2 }", "expected `,` or `}`", 5 },
    { "struct {}", "expected a name", 7 },
    { "struct P", "expected `{`", 8 },
    { "struct P: u8 {}", "expected `{`", 8 },
    { "struct P { x }", "expected `:` or `:=`", 13 },
    { "struct P { 1 }", "expected a field name", 11 },
    { "struct P { x: int y: int }", "expected `,`, `;` or a new line", 18 },
    { "struct P { x: int", "expected `}`", 17 },
    { "struct P { x: int;; }", "expected a field name", 18 },
    { "enum E { 1 }", "expected a name", 9 },
    { "enum E { a b }", "expected `,`, `;` or a new line", 11 },
    { "enum E: { a }", "expected a type", 8 },
    { "for in a {}", "expected a name", 4 },
    { "for x a {}", "expected `in`", 6 },
    { "for x, in a {}", "expected a name", 7 },
    { "for x in {}", "expected an expression", 9 },
    { "for x in a", "expected `{`", 10 },
    { "for i in 0..1..2 {}", "expected `{`", 13 },
    { "x := 0..n", "expected `;` or a new line", 6 },
    { "for i in 0..<n {}", "expected an expression", 12 },
    { "switch n { 0..<10 => a }", "expected an expression", 14 },
    { "switch x 1 => a", "expected `{`", 9 },
    { "switch x { 1 a }", "expected `,` or `=>`", 13 },
    { "x := switch k { _ => y = 1 }", "expected `,`, `;` or a new line", 23 },
    { "switch x { 1 => a b }", "expected `,`, `;` or a new line", 18 },
    { "switch x { 1 => a", "expected `}`", 17 },
    { "switch x { => a }", "expected an expression", 11 },
    { "switch n {\n  2 => p\n  .* => q\n}", "expected an expression", 22 },
    { "x := 1\npackage geo", "`package` must be the first line", 7 },
    { "fn f() { package geo }", "`package` must be the first line", 9 },
    { "package 1", "expected a package name", 8 },
    { "import fmt", "expected a path in quotes", 7 },
    { "x := import fmt", "expected a path in quotes", 12 },
    { "pub x = 1", "`pub` must come before a declaration", 0 },
    { "pub pub fn f() {}", "`pub` must come before a declaration", 0 },
    { "pub import \"a\"", "`pub` must come before a declaration", 0 },
    { "export struct P {}", "`export` must come before `fn`", 0 },
    { "export fn() {}", "`export` must come before `fn`", 0 },
    { "extern struct P {}", "`extern` must come before `fn`", 0 },
    { "extern fn() {}", "`extern` must come before `fn`", 0 },
    { "extern fn f() {}", "an `extern fn` has no body", 14 },
    { "extern fn f() => 1", "an `extern fn` has no body", 14 },
    { "fn f(x: int, ..) {}", "only an `extern fn` can end with `..`", 13 },
    { "f := fn(..) {}", "only an `extern fn` can end with `..`", 8 },
    { "extern fn f(.., x: int)", "`..` must be the last parameter", 12 },
    { "extern fn f(x: int, .., )", "`..` must be the last parameter", 20 },
    { "import \"a\".b", "expected `;` or a new line", 10 },
    { "*%x", "expected an expression", 0 },
    { "a +% * b", "use `.*` to dereference", 5 },
    { "x := undefined", "use `undefined` only as `name: Type = undefined`", 5 },
    { "x = undefined", "use `undefined` only as `name: Type = undefined`", 4 },
    { "const N: int = undefined", "use `undefined` only as `name: Type = undefined`", 15 },
    { "fn f(x: int = undefined) {}", "use `undefined` only as `name: Type = undefined`", 14 },
    { "f(undefined)", "use `undefined` only as `name: Type = undefined`", 2 },
    { "x: int = undefined + 1", "expected `;` or a new line", 19 },
    { "a[..]", "expected an expression", 4 },
    { "a[1..<4]", "expected an expression", 5 },
    { "a[..<4]", "expected an expression", 4 },
    { "a[i..=]", "expected an expression", 6 },
    { "a[..=]", "expected an expression", 5 },
    { "!!x", "use `!!` only after a value on the same line", 0 },
    { "x := a\n!!", "use `!!` only after a value on the same line", 7 },
    { "a ??", "expected an expression", 4 },
    { "x ?= 1", "use `?\?=` instead of `?=`", 2 },
    { "MAX :: 5", "use `const name := value` instead of `::`", 4 },
    { "a?.b", "expected `;` or a new line", 1 },
    { "a ?: b", "expected `;` or a new line", 2 },
    { "x: ?", "expected a type", 4 },
    { "x: ?= 1", "expected a type", 3 },
    { "x := ?int", "expected an expression", 5 },
    { "x := try", "expected an expression", 8 },
    { "x := f() catch", "expected an expression", 14 },
    { "x := f() catch err =>", "expected an expression", 21 },
    { "defer catch", "expected an expression", 11 },
    { "x: int!", "expected a type after `!`, such as `void`", 6 },
    { "x: extern int", "expected `fn` after `extern`", 10 },
    { "fn save() -> ! {}", "expected a type after `!`, such as `void`", 13 },
    { "fn save() -> LoadError! {}", "expected a type after `!`, such as `void`", 22 },
    { "save: fn() -> !\ncount := 0", "expected a type after `!`, such as `void`", 14 },
    { "try := 1", "expected an expression", 4 },
    { "bitcast u32", "expected `(`", 8 },
    { "bitcast(u32)", "expected `,`", 11 },
    { "bitcast(u32, f, g)", "expected `)`", 14 },
    { "bitcast(, f)", "expected a type", 8 },
    { "x := cast(u32) f", "expected `;` or a new line", 15 },
    { "x as u32", "expected `;` or a new line", 2 },
    { "union { a: int }", "expected a name", 6 },
    { "union U { a }", "expected `:`", 12 },
    { "union U: { a: int }", "expected a type", 9 },
    { "union U { a: int = 1 }", "expected `,`, `;` or a new line", 17 },
    { "struct S { union { a } }", "expected `:`", 21 },
    { "switch v in {}", "expected an expression", 12 },
    { "a is .x == b", "comparisons cannot be chained; add parentheses", 8 },
    { "break 5", "expected a label", 6 },
    { "x := a: 1", "expected `;` or a new line", 6 },
    { "a.b: {}", "expected `;` or a new line", 3 },
    { "outer: x := 1", "expected `;` or a new line", 9 },
    { "a[1..2, 3]", "expected `]`", 6 },
    { "m[i, 1..2]", "expected `]`", 6 },
};
// clang-format on

#define NODE_KIND_NAME(name, ...) [name] = #name,
static const char *const node_kind_names[NNODES] = {
    CHIBI_NODES(NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME, NODE_KIND_NAME)
};

static bool
postorder_is(const char *text, const char *want)
{
    Arena arena = { 0 };
    Parse r = parse(&arena, text);

    char got[512] = "";
    size_t got_len = 0;
    for (int i = 0; i < r.count; i++) {
        const char *name = node_kind_names[r.nodes.kinds[i]] + strlen("NODE_");
        got_len += (size_t)
            snprintf(got + got_len, sizeof got - got_len, "%s%s", i ? " " : "", name);
    }

    bool same = r.count >= 0 && strcmp(got, want) == 0 &&
                tokens_used_at_most_twice(text, r);
    if (!same) {
        fprintf(stderr, "parse \"%s\"\n  got:  %s\n  want: %s\n", text, got, want);
    }

    arena_free(&arena);
    return same;
}

// clang-format off
static const struct
{
    const char *src;
    const char *want;
} POSTORDERS[] = {
    { "a and b", "NAME AND_LEFT NAME AND EXPR_STMT" },
    { "a or b and c", "NAME OR_LEFT NAME AND_LEFT NAME AND OR EXPR_STMT" },
    { "f(x)", "NAME CALL_ARGS NAME CALL EXPR_STMT" },
    { "{ a }", "BLOCK_START NAME EXPR_STMT BLOCK" },
    { "if a {}", "IF_START NAME IF_THEN BLOCK_START BLOCK IF" },
    { "if a {} else if b {} else {}",
      "IF_START NAME IF_THEN BLOCK_START BLOCK ELSE_IF NAME IF_THEN BLOCK_START BLOCK ELSE BLOCK_START BLOCK IF" },
    { "while a {}", "WHILE_START NAME WHILE_BODY BLOCK_START BLOCK WHILE" },
    { "defer f()", "DEFER_START NAME CALL_ARGS CALL EXPR_STMT DEFER" },
    { "x := 1", "INT VAR_INIT" },
    { "x: T", "NAME VAR_TYPE" },
    { "x: T = 1", "NAME VALUE INT VAR_TYPE_INIT" },
    { "a.b = c", "NAME FIELD VALUE NAME ASSIGN" },
    { "fn f(a, b: T = 1) -> R {}",
      "FN_START PARAM NAME VALUE INT PARAM_TYPE_INIT NAME FN_RETURN FN_BODY BLOCK_START BLOCK FN" },
    { "fn(x) => x", "FN_START PARAM FN_BODY RETURN_START NAME RETURN_VALUE FN_LITERAL EXPR_STMT" },
    { "f(.a = 1)", "NAME CALL_ARGS ARG_NAME VALUE INT NAMED_ARG CALL EXPR_STMT" },
    { "const X := 1", "INT CONST_INIT" },
    { "const X: T = 1", "NAME VALUE INT CONST_TYPE_INIT" },
    { "return a", "RETURN_START NAME RETURN_VALUE" },
    { "x: *T", "NAME POINTER_TYPE VAR_TYPE" },
    { "x: [4, 4]T", "INT INT NAME ARRAY2_TYPE VAR_TYPE" },
    { "f: fn(int, x: T) -> R", "FN_START NAME NAME PARAM_TYPE NAME FN_RETURN FN_TYPE VAR_TYPE" },
    { "m[i, j]", "NAME NAME NAME INDEX2 EXPR_STMT" },
    { "P.{ 1, .x = 2 }", "NAME LITERAL_START INT ARG_NAME VALUE INT NAMED_ARG LITERAL EXPR_STMT" },
    { ".{}", "ANON_LITERAL_START ANON_LITERAL EXPR_STMT" },
    { "x == .red", "NAME ENUM_LITERAL EQ EXPR_STMT" },
    { "struct P { x: T }", "STRUCT_START NAME MEMBER_TYPE STRUCT" },
    { "enum C: u8 { a, b = 1 }", "ENUM_START NAME ENUM_BACKING ENUM_MEMBER INT ENUM_VALUE ENUM" },
    { ".[1]", "ANON_LITERAL_START INT ANON_ARRAY_LITERAL EXPR_STMT" },
    { "*T.[x]", "NAME POINTER_TYPE LITERAL_START NAME ARRAY_LITERAL EXPR_STMT" },
    { "for x, i in a {}", "FOR_START NAME FOR_VALUE FOR_INDEX BLOCK_START BLOCK FOR" },
    { "for i in 0..n {}", "FOR_START INT NAME RANGE FOR_VALUE BLOCK_START BLOCK FOR" },
    { "switch x { 1, 2 => a }", "SWITCH_START NAME CASE_START INT INT CASE_BODY NAME EXPR_STMT CASE SWITCH" },
    { "package p", "PACKAGE" },
    { "import \"a\"", "IMPORT_DECL" },
    { "const f := import \"a\"", "IMPORT CONST_INIT" },
    { "pub export fn f() {}", "FN_START FN_BODY BLOCK_START BLOCK FN EXPORT PUB" },
    { "pub extern fn f() -> int", "FN_START NAME FN_RETURN FN EXTERN PUB" },
    { "extern fn f(x: int, ..) -> int", "FN_START NAME PARAM_TYPE VARIADIC NAME FN_RETURN FN EXTERN" },
    { "x: a.B", "NAME FIELD VAR_TYPE" },
    { "a[1..4]", "NAME INT INT SLICE EXPR_STMT" },
    { "a[i..]", "NAME NAME SLICE_FROM EXPR_STMT" },
    { "a[..n]", "NAME NAME SLICE_TO EXPR_STMT" },
    { "a[1..=3]", "NAME INT INT SLICE_INCLUSIVE EXPR_STMT" },
    { "a[..=n]", "NAME NAME SLICE_TO_INCLUSIVE EXPR_STMT" },
    { "a!!", "NAME UNWRAP EXPR_STMT" },
    { "a ?? b", "NAME OR_ELSE_LEFT NAME OR_ELSE EXPR_STMT" },
    { "x ?\?= y", "NAME VALUE NAME OR_ELSE_ASSIGN" },
    { "x: ?T", "NAME OPTIONAL_TYPE VAR_TYPE" },
    { "x: ??T", "NAME OPTIONAL_TYPE OPTIONAL_TYPE VAR_TYPE" },
    { "bitcast(u32, f)", "NAME NAME BITCAST EXPR_STMT" },
    { "union U: u8 { a: int, b }", "UNION_START NAME UNION_TAG NAME MEMBER_TYPE EMPTY_MEMBER UNION" },
    { "struct S { union { a: int } }", "STRUCT_START UNION_START NAME MEMBER_TYPE ANON_UNION STRUCT" },
    { "switch v in e {}", "SWITCH_START NAME SWITCH_VALUE SWITCH" },
    { "switch &v in e {}", "SWITCH_START NAME SWITCH_REF SWITCH" },
    { "e is .a", "NAME ENUM_LITERAL IS EXPR_STMT" },
    { "x := try f()", "NAME CALL_ARGS CALL TRY VAR_INIT" },
    { "f() catch 0", "NAME CALL_ARGS CALL CATCH_LEFT INT CATCH EXPR_STMT" },
    { "f() catch e => 0", "NAME CALL_ARGS CALL CATCH_LEFT CATCH_NAME INT CATCH_NAMED EXPR_STMT" },
    { "defer catch e => f()", "DEFER_START CATCH_NAME NAME CALL_ARGS CALL EXPR_STMT DEFER_CATCH_NAMED" },
    { "x: E!T", "NAME NAME ERROR_UNION VAR_TYPE" },
    { "a: for x in b {}", "LABEL_START FOR_START NAME FOR_VALUE BLOCK_START BLOCK FOR LABELED" },
    { "break a 1", "INT BREAK_VALUE" },
    { "x := b: { break b 1 }", "VALUE_LABEL_START BLOCK_START INT BREAK_VALUE BLOCK LABELED_VALUE VAR_INIT" },
    { "f(b: { break b 1 }, c: {} + 1)", "NAME CALL_ARGS VALUE_LABEL_START BLOCK_START INT BREAK_VALUE BLOCK LABELED_ITEM VALUE_LABEL_START BLOCK_START BLOCK LABELED_VALUE INT ADD CALL EXPR_STMT" },
    { "x := switch k { .a => 1, _ => b: { break b 2 } }", "VALUE_SWITCH_START NAME CASE_START ENUM_LITERAL CASE_BODY INT CASE CASE_START NAME CASE_BODY VALUE_LABEL_START BLOCK_START INT BREAK_VALUE BLOCK LABELED_VALUE CASE VALUE_SWITCH VAR_INIT" },
    { "x := switch k { _ => return }", "VALUE_SWITCH_START NAME CASE_START NAME CASE_BODY RETURN CASE VALUE_SWITCH VAR_INIT" },
    { "f(switch k { _ => 1 }, switch k { _ => 1 } + 1)", "NAME CALL_ARGS VALUE_SWITCH_START NAME CASE_START NAME CASE_BODY INT CASE SWITCH_ITEM VALUE_SWITCH_START NAME CASE_START NAME CASE_BODY INT CASE VALUE_SWITCH INT ADD CALL EXPR_STMT" },
    { "x: T = undefined", "NAME VALUE UNDEFINED VAR_TYPE_INIT" },
    { "a +% -%b", "NAME NAME NEG_WRAP ADD_WRAP EXPR_STMT" },
    { "x *%= 2", "NAME VALUE INT MUL_WRAP_ASSIGN" },
};
// clang-format on

TEST_CASE(postorders)
{
    for (size_t i = 0; i < sizeof POSTORDERS / sizeof POSTORDERS[0]; i++) {
        REQUIRE(postorder_is(POSTORDERS[i].src, POSTORDERS[i].want));
    }
    return true;
}

static bool
top_ends_cover_nodes(Parse r)
{
    u32 start = 0;
    for (int i = 0; i < r.parser.top_count; i++) {
        if (r.nodes.top_ends[i] <= start) {
            return false;
        }
        start = r.nodes.top_ends[i];
    }
    return start == (u32)r.count;
}

static bool
top_level_is(const char *text, const char *want)
{
    Arena arena = { 0 };
    Parse r = parse(&arena, text);

    char got[512] = "";
    size_t got_len = 0;
    int top = 0;
    for (int i = 0; i < r.count; i++) {
        const char *gap = i == 0 ? "" : " ";
        if (top < r.parser.top_count && (u32)i == r.nodes.top_ends[top]) {
            gap = " | ";
            top++;
        }
        const char *name = node_kind_names[r.nodes.kinds[i]] + strlen("NODE_");
        got_len += (size_t)snprintf(got + got_len, sizeof got - got_len, "%s%s", gap, name);
    }

    bool same = r.count >= 0 && top_ends_cover_nodes(r) && strcmp(got, want) == 0;
    if (!same) {
        fprintf(stderr, "parse \"%s\"\n  got:  %s\n  want: %s\n", text, got, want);
    }

    arena_free(&arena);
    return same;
}

// clang-format off
static const struct
{
    const char *src;
    const char *want;
} TOP_LEVEL[] = {
    { "", "" },
    { "x := 1\ny := 2", "INT VAR_INIT | INT VAR_INIT" },
    { ";\nx := 1;;\n;", "INT VAR_INIT" },
    { "fn f() {\n    a := 1\n    b := 2\n}\nconst N := 4",
      "FN_START FN_BODY BLOCK_START INT VAR_INIT INT VAR_INIT BLOCK FN | INT CONST_INIT" },
    { "package main\nimport \"fmt\"\npub fn f() {}",
      "PACKAGE | IMPORT_DECL | FN_START FN_BODY BLOCK_START BLOCK FN PUB" },
    { "struct S { x: int }\nif a {}\nf()",
      "STRUCT_START NAME MEMBER_TYPE STRUCT | IF_START NAME IF_THEN BLOCK_START BLOCK IF | NAME CALL_ARGS CALL EXPR_STMT" },
};
// clang-format on

TEST_CASE(top_level)
{
    for (size_t i = 0; i < sizeof TOP_LEVEL / sizeof TOP_LEVEL[0]; i++) {
        REQUIRE(top_level_is(TOP_LEVEL[i].src, TOP_LEVEL[i].want));
    }
    return true;
}

TEST_CASE(trees)
{
    for (size_t i = 0; i < sizeof TREES / sizeof TREES[0]; i++) {
        REQUIRE(parses_to(TREES[i].src, TREES[i].want));
    }
    return true;
}

TEST_CASE(errors)
{
    for (size_t i = 0; i < sizeof ERRORS / sizeof ERRORS[0]; i++) {
        REQUIRE(fails_with(ERRORS[i].src, ERRORS[i].error, ERRORS[i].pos));
    }
    return true;
}

static char *
nested(const char *open, int depth, const char *close, const char *prefix)
{
    size_t open_len = strlen(open), close_len = strlen(close),
           prefix_len = strlen(prefix);
    char *s = malloc(prefix_len + (open_len + close_len) * (size_t)depth + 2);
    memcpy(s, prefix, prefix_len);
    char *p = s + prefix_len;
    for (int i = 0; i < depth; i++, p += open_len) {
        memcpy(p, open, open_len);
    }
    *p++ = 'x';
    for (int i = 0; i < depth; i++, p += close_len) {
        memcpy(p, close, close_len);
    }
    *p = '\0';
    return s;
}

static bool
depth_ok(const char *open, const char *close, u32 at, const char *prefix)
{
    char *deepest = nested(open, PARSER_MAX_DEPTH, close, prefix);
    char *too_deep = nested(open, PARSER_MAX_DEPTH + 1, close, prefix);

    Arena arena = { 0 };
    bool ok = parse(&arena, deepest).count >= 0;
    u32 failing_byte = (u32)strlen(prefix) + PARSER_MAX_DEPTH * (u32)strlen(open) + at;
    ok = ok && fails_with(too_deep, "nested too deeply", failing_byte);

    arena_free(&arena);
    free(deepest);
    free(too_deep);
    return ok;
}

TEST_CASE(depth_limit)
{
    REQUIRE(depth_ok("(", ")", 0, ""));
    REQUIRE(depth_ok("-", "", 0, ""));
    REQUIRE(depth_ok("! ", "", 0, ""));
    REQUIRE(depth_ok("f(", ")", 1, ""));
    REQUIRE(depth_ok("a[", "]", 1, ""));
    REQUIRE(depth_ok("a[..", "]", 1, ""));
    REQUIRE(depth_ok("a[", "..]", 1, ""));
    REQUIRE(depth_ok("{", "}", 0, ""));
    REQUIRE(depth_ok("defer ", "", 0, ""));
    REQUIRE(depth_ok("if a {", "}", 5, ""));
    REQUIRE(depth_ok("fn() => ", "", 0, ""));
    REQUIRE(depth_ok(".{", "}", 0, ""));
    REQUIRE(depth_ok("P.{", "}", 1, ""));
    REQUIRE(depth_ok(".[", "]", 0, ""));
    REQUIRE(depth_ok("for x in a {", "}", 11, ""));
    REQUIRE(depth_ok("switch a { 1 => ", "}", 0, ""));
    REQUIRE(depth_ok("*", "", 0, "y: "));
    REQUIRE(depth_ok("[]", "", 0, "y: "));
    REQUIRE(depth_ok("[1]", "", 0, "y: "));
    REQUIRE(depth_ok("fn() -> ", "", 0, "y: "));
    return true;
}

TEST_CASE(long_chains)
{
    char *chain = nested("", PARSER_MAX_DEPTH + 1, ".b(c)[d]", "");

    Arena arena = { 0 };
    bool ok = parse(&arena, chain).count >= 0;

    arena_free(&arena);
    free(chain);
    REQUIRE(ok);
    return true;
}

TEST_CASE(long_else_if)
{
    const char *link = " else if a {}";
    size_t link_len = strlen(link);
    int nlinks = 10 * PARSER_MAX_DEPTH;

    char *chain = malloc(strlen("if a {}") + link_len * (size_t)nlinks + 1);
    char *p = stpcpy(chain, "if a {}");
    for (int i = 0; i < nlinks; i++) {
        p = stpcpy(p, link);
    }

    Arena arena = { 0 };
    bool ok = parse(&arena, chain).count >= 0;

    arena_free(&arena);
    free(chain);
    REQUIRE(ok);
    return true;
}

TEST_CASE(generated_code)
{
    enum
    {
        NPROGRAMS = 200,
        PROGRAM_SIZE = 8192,
    };

    Arena arena = { 0 };
    u64 rng = 1;
    Vocabulary v;
    vocabulary_init(&rng, &v, &arena);
    char *text = arena_alloc(&arena, PROGRAM_SIZE + CODE_SLACK + 1, 1);

    bool ok = true;
    for (int i = 0; i < NPROGRAMS && ok; i++) {
        size_t len = random_code(&rng, text, &v, PROGRAM_SIZE);
        text[len] = '\0';

        ArenaMark mark = arena_mark(&arena);
        Parse r = parse(&arena, text);
        ok = r.count >= 0;
        if (!ok) {
            fprintf(stderr, "generated program #%d\n", i);
            parser_print_error(&r.parser, r.src);
        }
        if (ok && !top_ends_cover_nodes(r)) {
            fprintf(stderr, "generated program #%d: top-level ends do not cover the nodes\n", i);
            ok = false;
        }
        arena_restore(&arena, mark);
    }

    arena_free(&arena);
    REQUIRE(ok);
    return true;
}

TEST_SUITE(parser_tests)
{
    RUN_TEST_CASE(trees);
    RUN_TEST_CASE(postorders);
    RUN_TEST_CASE(top_level);
    RUN_TEST_CASE(errors);
    RUN_TEST_CASE(depth_limit);
    RUN_TEST_CASE(long_chains);
    RUN_TEST_CASE(long_else_if);
    RUN_TEST_CASE(generated_code);
}
