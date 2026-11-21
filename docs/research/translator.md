# The C-to-chibi translator

Updated 2026-11-21. Item 5 of the self-hosting order ([README.md](README.md)) translates the compiler's C into chibi. The translator reads clang's JSON AST of one C file and writes a chibi file. `make translated` translates the files listed in the Makefile's `TRANSLATED`, builds each one through `chibi c`, and runs the test suite with them in place of the C files, on x86-64 and on arm64 under qemu.

## Steps

- **3t.1 The reader and `src/arena.c`: built 2026-11-21.** The translator is written in C, the language recommended to the owner on 2026-11-21, so it follows the compiler's rules and can be translated with it later.
  - `translate/ast.c` streams the output of `clang -std=c23 -Xclang -ast-dump=json -fsyntax-only` into flat columns, one row per node in pre-order: the end of the node's subtree, its kind, location, type spelling, name, a detail (cast kind, operator, value or storage class), the declaration it refers to, and flags. clang leaves out a location's file and line when they repeat, so the reader carries them over from the previous location in the dump. The 500 MB dump of `src/lexer.c` loads in 0.9 s.
  - `translate/translate.c` parses type spellings, which are C declarators, into its own type table. It links each declaration to the first one and to the definition, so a prototype and its body share one set of pointer facts. It infers what each pointer can hold, then writes declarations, statements and expressions, adding `!!`, `bitcast`, conversions and parentheses where chibi needs them.
  - A translated file includes the structs it uses from project headers, an `extern fn` for each C function it calls, and any `static inline` function from a header that it calls.
  - Checked against a hand translation of `src/arena.c`: the structure is the same. The differences are the wrapping operators of rule 2, and `!!` on reads of locals where the hand translation relied on narrowing.
  - Tests: `make translated` passes with the translated `src/arena.c` on x86-64 under ASan and UBSan and on arm64 under qemu. The generic-SIMD and bulk-lexer builds pass too, run by hand.

## Rules chosen, open to review

1. **Integers** map by width and signedness. `char` becomes `u8`, unsigned as on arm64. `long` becomes `int`. `size_t` and `uintptr_t` become `u64`, not `uintptr`, so pointer-sized integers mix with other `u64` values without conversions.
2. **Unsigned arithmetic wraps.** Unsigned `+`, `-`, `*` and unary `-` become `+%`, `-%`, `*%` and `-%`, because C wraps them. Signed operators stay checked, because C leaves their overflow undefined. So the compiler's unsigned arithmetic, which is most of it, loses chibi's overflow checks; a later pass can bring checks back where the tests show that nothing wraps.
3. **Conversions follow clang's casts.** A narrowing becomes `T(x & mask)`, which the checker emits as a truncation with no check. A sign change at the same width becomes `bitcast`. A signed value converted to a wider unsigned type becomes `bitcast(U, S(x))`, with `S` signed at the new width. A widening becomes `T(x)`. A constant whose value changes in the conversion, such as C's `-1` as a `u32`, is written as the converted value.
4. **Nullable pointers.** A pointer is nullable if it is compared with `NULL`, tested as a condition, assigned `NULL`, zero-initialized (a global without an initializer, or a field left out of a literal), or given a value that may be nil. The facts flow along assignments, initializations, arguments and returns until nothing changes. A C function declared without a body returns nullable pointers and takes nullable pointers.
5. **Many-pointers.** A pointer becomes `[*]T` if it is indexed, offset with `+` or `-`, incremented, subtracted from another pointer, or compared with `<`. The kind spreads both ways along plain copies, so one value keeps one kind. `(U *)(p + 1)`, the idiom that steps over a header, doesn't make `p` a `[*]T`; the translation offsets a `bitcast` of it.
6. **Dereferenced parameters stay non-nullable.** A parameter that the function dereferences before its first branch or `return` isn't made nullable by the arguments; the caller unwraps the argument with `!!` instead. That turns `use_chunk(a, c)` into `use_chunk(a, c!!)`, as in the hand translation, rather than adding `!!` to every use inside `use_chunk`.
7. **Uses.** A nullable pointer gets `!!` wherever it is dereferenced, offset, or passed where a non-nullable pointer is expected; on a narrowed local, `!!` emits no check. A field read through a `[*]T` is `p[0].f`. A value that moves between `*T` and `[*]T`, or to another pointee type, goes through `bitcast`.
8. **`void *`** becomes a pointer to `u8`, and so does a pointer to a struct from a system header, such as `FILE`.
9. **Pointer arithmetic that chibi lacks.** `p - q` becomes `(bitcast(int, p) - bitcast(int, q)) / size_of(T)`, and `p < q` compares `bitcast(u64, p)` with `bitcast(u64, q)`. `p - n` becomes `p + -%n`, or `p + -%u64(k)` for a constant `k`, because chibi has no `-` on a `[*]T` and refuses a constant negative offset.
10. **Names.** A parameter that the body assigns, or whose address it takes, is renamed `name_in` and copied into a local `name`. A local that would shadow another gets a number. A C name that is a chibi keyword or type name gets a trailing `_`.
11. **Statements.**
    - A local without an initializer gets `= undefined`.
    - `for (T i = a; i < b; i++)`, where `b` is a constant or a local that the body doesn't write, becomes `for i in a..b`. Any other `for` becomes a `while` with the step at the end of the body, inside a block when the `for` declares a variable. If the body has a `continue`, a labeled block wraps it and `continue` becomes `break nextN`.
    - `do … while (c)` becomes `while true { …; if !(c) { break } }`.
    - A `switch` arm drops its final `break`. Any other `break` out of a `switch` uses a label on the switch, because an unlabeled `break` in a chibi arm leaves the enclosing loop. An arm that falls into the next is refused. `_ => {}` is added when there is no `default`.
12. **`?:`** in an assignment, a declaration or a `return` becomes `if` and `else`. Elsewhere it is refused until the owner decides on conditional expressions (asked 2026-11-21).
13. **Builtins.** `assert(c)` becomes `if !c { abort() }`, with `extern fn abort()`. `__builtin_expect(x, y)` becomes `x`. A `bool` used as an integer becomes a labeled block that breaks with `T(1)` or `T(0)`.
14. **Functions** that aren't `static` become `export fn`, so C callers in the mixed build keep their symbols.
15. **Layout.** Blank lines between statements come from the source file. Comments are dropped. Macros appear expanded, so `ARENA_MAX_ALLOC` is written `u64(1) << 62`.

## Not built yet

- Globals and constant tables, which every other file has.
- Strings, `?:` inside expressions, and character literals, which wait on the owner's answer (asked 2026-11-21). `src/arena.c` needs none of them.
- Unions, nested and unnamed records, function pointers, variadic C functions such as `fprintf`, `static` locals, compound assignments that promote their target (`u8 += int`), `++` used as a value, `switch` fallthrough, SIMD intrinsics and `__int128`.
- Each translated file carries its own copy of chibi's runtime, `chibi_trap` and `chibi_context`, so the mixed build renames them per file with `-D`. Once the whole compiler is translated, it is one program and the renames go away.
