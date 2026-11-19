# Syntax options: leftovers from C, tasks, and C interop

This note holds option sheets 4, 9 and 10 for chibi-v2 syntax that the parser thread has not reached: the small constructs C has and chibi lacks, tasks and coroutines, and the parts of C interop that are still open. Every example builds on the forms settled in [decisions.md](../decisions.md) as of 2026-11-10 and adds only the syntax its own option proposes, and every option states what it costs chibi-v2's lexer and postorder parser (a parser that writes each node after its children, into flat arrays). Sources are primary and pinned: Zig's language reference at tag `0.15.2` (byte-identical to the local `/tmp/zig-langref.html`) and at `0.10.1` for the removed `async`; Odin's docs at `1d272b93` and compiler at `85afcb38`; C3's docs at `d1088c19` and compiler at `b2e1e99b`; Carbon at `ca189850`; Zen C's `main` at `b3f550e9` (2026-10-25; the last release is v0.4.4) with docs at `60115e02`. Jai has no public docs, so Jai rows cite the owner's copies in `~/Personal/jai-refs` and are marked (community source). Other agents are writing sheets 1–3 (unions, casts, size queries) and 5–8 (methods, generics, compile-time code, reflection); this note names its dependencies on them and decides none of them.

---

## 4. Leftovers from C

C has six small features that chibi-v2 has not decided: `++` and `--`, a conditional expression, character literals, `goto`, variadic functions and bitfields. Each is rare in new code but common in C, so each choice decides how much the C-to-chibi translator ([c-corpus.md](c-corpus.md)) must rewrite, and how closely translated lines keep the shape of the C they came from. Counts below are uses per 1,000 code lines of the corpus mix, from c-corpus.md section 4.

### 4a. `++` and `--`

C uses `++` and `--` 19.4 times per 1,000 lines. Assignment is already settled as a statement, so the only open question is whether `x++` exists as a statement at all. Today the lexer has no `++` or `--` token, so `--a` lexes as two minus signs and means `-(-a)`.

#### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | None. `++` is array concatenation: `const together = array1 ++ array2;` | [langref L1785-L1801](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L1785-L1801) |
| Odin | None. "`x += 1` is slightly longer but it is unambiguous." | [faq.md L566-L569](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/faq.md#L566-L569) |
| Jai | None: "there are no increment or decrement operators" (community source) | `~/Personal/jai-refs/JCL-wiki/Getting-Started.md` L154 |
| C3 | Prefix and postfix expressions, `a[i++] = x`, with left-to-right evaluation defined so that `i = i++ + i++` has one result | [spec L1636-L1638, L1689, L1844-L1847](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/implementation-details/specification.md#L1636-L1638) |
| Carbon | Prefix statements only: `++a;`, `--a;`. They "can only be used as complete statements, not as subexpressions" | [assignment.md L48-L72](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/assignment.md#L48-L72) |
| ZenC | Prefix and postfix expressions, as in C | [expr_prec.c L596-L600, L884-L896](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/expr/expr_prec.c#L596-L600); [lex.md L130-L144](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/lex.md#L130-L144) |

#### Options

- **A. None** (Zig, Odin, Jai). Programs write `x += 1`, and nothing is added to the lexer or parser. The translator turns `i++;` into `i += 1`, which is one more token per use and is already counted in c-corpus.md's estimate of 6.4 tokens per line. It hoists `a[i++] = x` into two statements, as c2go did. chibi-v2's own C already follows the convention that `++` appears only as a whole statement (c-corpus.md section 6.2), so its translation stays mechanical, and `--a` keeps meaning `-(-a)`.
  ```
  count += 1
  for i in 0..n {
      total += xs[i]
  }
  ```
- **B. Statement only** (Carbon, prefix; Go, postfix). This adds two two-byte tokens, which the lexer's two-byte lookup table decides at full speed ([front-end-optimizations.md](front-end-optimizations.md), F7), and one statement node that follows its operand in postorder, as `+=` does. The prefix form fits the line-break rule unchanged, because `++` cannot continue an expression, so a line that starts with `++` always starts a new statement. The postfix form needs a same-line rule like the one `(` and `[` have, or `x` on one line followed by `++y` on the next reads as `x++` followed by a stray `y`. Either form is a second spelling of `+= 1`, and the new `--` token silently changes `--a` from double negation to a decrement. The benefit is that translated `i++;` lines keep their shape.
  ```
  ++count
  count++
  ```
  The first line is Carbon's form and the second is Go's, which must end its line.
- **C. Expression** (C3, ZenC). `a[i++] = x` would translate one to one. It would also reopen the settled rule that assignment is a statement, because `i++` assigns, and it brings back the evaluation-order rules C3 had to write down. It is listed only to complete the comparison.
  ```
  // not available without reopening "assignment is a statement"
  ```

#### Recommendation

**I recommend A (no `++` or `--`),** because the settled assignment rule already removes the one thing `++` does better than `+= 1`, which is use inside an expression. Range loops (`for i in 0..n`) remove its most common use, and B buys one token per use at the price of a second spelling and the `--a` trap.

#### Questions for you

1. Drop `++` and `--`, and write `x += 1`?
2. If you want them anyway, should the statement be prefix (`++x`, Carbon) or postfix (`x++`, Go, with a same-line rule)?

### 4b. Conditional expression

C's `?:` appears 8.0 times per 1,000 lines, and 19 times in chibi-v2's own C. A conditional expression keeps a choice between two values on one line, where an `if` statement takes four. Three settled or pending facts shape the choice: an `if` at the start of a statement is the `if` statement; `=>` already means "this expression is the body" after `fn` and "this is the arm" in `switch`; and `?` may be reserved for optionals.

#### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | `const result = if (a != b) 47 else 3089;` The parentheses are part of the syntax. "If expressions are used instead of a ternary expression." | [test_if.zig L8-L14](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/test_if.zig#L8-L14) |
| Odin | `x if cond else y`, `cond ? x : y`, and the compile-time `x when cond else y` | [overview.md L1496-L1502](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L1496-L1502) |
| Jai | `c := ifx a > b then 10 else 1000;` where `then` is optional (`ifx a > b 10 else 1000`), and `y := ifx x else 1` (community source) | `~/Personal/jai-refs/The_Way_to_Jai/book/14A_Branching_with_if_else.md` L89-L171 |
| C3 | `c ? a : b`, plus Elvis `a ?: b` and optional-else `a ?? b`; `?` also marks optional types such as `int?` | [spec L1774-L1797](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/implementation-details/specification.md#L1774-L1797) |
| Carbon | `var a: i32 = if cond then 1 + 1 else 2 + 4 * 6;` It has very low precedence, and "an `if` keyword at the start of a statement is always interpreted as an `if` statement" | [if.md L25-L62](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/expressions/if.md#L25-L62) |
| ZenC | `let y = x > 10 ? 1 : 0;` and `if (x > 100) { "huge" } else { "small" }`. The postfix try operator `res?` and the ternary `?` are told apart by peeking at the token after `?` | [05-control-flow.md L19-L23](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/05-control-flow.md#L19-L23); [expr_prec.c L1015-L1064](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/expr/expr_prec.c#L1015-L1064) |

#### Options

- **A. None** (Go). Programs use an `if` statement, and the translator hoists each `?:` into one with a temporary, as c2go did. Nothing changes in the parser.
  ```
  sign := 1
  if x < 0 {
      sign = -1
  }
  ```
- **B. C's `c ? a : b`** (C3, Odin, ZenC). `?` and `:` are already tokens. In postorder the parser writes the condition, then a marker node (a node that carries no value and only tells later passes where a branch starts, like the existing `AND_LEFT`), then the first value, a second marker, the second value and the conditional node, so one forward pass can emit the jumps. `?` and `:` can continue a line, so multi-line forms work under the line-break rule. The conflicts come from optionals. If optionals get a postfix `?` (Zen C's `res?`), every `?` needs the lookahead heuristic Zen C uses, which means looking at the token after the current one before deciding. If `?.` (safe navigation) becomes a token, `c ?.red : .blue` written without a space lexes as `?.` followed by `red`, and chibi writes enum values as `.red` everywhere. Zen C, where `.a` is shorthand for `self.a`, had to fix the same shape, `cond ? .a : .b` ([CHANGELOG L58](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/CHANGELOG.md#L58)). The `:` also reads close to `x: T` declarations.
  ```
  fn sign(x: int) -> int => x < 0 ? -1 : 1
  ```
- **C. Zig's `if (c) a else b`** (Zig). It needs no new keyword, and the first token decides it, because an `if` in operand position can only start this form. The parentheses must be required here, because without them `if x -y else z` reads `x - y` as the condition. That breaks the symmetry with the statement `if`, whose parentheses are optional, so in `x := if (a + b) * c > 0 ...` the condition would stop at `(a + b)`. Postorder works as in B.
  ```
  fn abs(x: int) -> int => if (x < 0) -x else x
  ```
- **D. Odin's and Python's `a if c else b`** (Odin). `if` becomes an infix operator, which costs chibi twice. In postorder, `a` is written before `c` but must run after it, so a one-pass consumer must buffer `a`, or the parser must re-tag (change the kind of) nodes it has already written. Under the TypeScript-like line rule, `x := a` followed by a line that starts `if ready {` would continue into an infix `if`, unless infix `if` gets a same-line exception.
  ```
  sign := -1 if x < 0 else 1
  ```
- **E. Jai's `ifx c then a else b`** (Jai). It adds the keyword `ifx`. Jai's optional `then` brings back C's ambiguity (`ifx a -b else c`), so chibi would have to require `then`, which makes this F with an extra keyword. How Jai finds the end of the condition without `then` is not in the community sources (unverified).
  ```
  sign := ifx x < 0 then -1 else 1
  ```
- **F. Carbon's `if c then a else b`** (Carbon). It adds one keyword, `then`, and needs no parentheses, so it matches chibi's statement `if`. In operand position `if` can only start this form, and at the start of a statement it stays the `if` statement, so it needs no lookahead and no re-tagging. Its precedence is the lowest, so it can't be an operand of `+` without parentheses, and the `else` value extends as far right as possible, as in Carbon. Postorder works as in B, and `?` stays free for optionals. In a `switch` arm the body is a statement, so an arm writes `1 => y = if c then a else b`; a bare `1 => if c then a else b` starts an `if` statement and fails.
  ```
  fn abs(x: int) -> int => if x < 0 then -x else x
  label := if hp > 0 then "alive" else "dead"
  ```

#### Recommendation

**I recommend F (`if c then a else b`),** because it is decided by its first token, it needs no parentheses rule that differs from the statement `if`, it leaves `?` and `?.` free for optionals, and translating `c ? a : b` stays mechanical at two extra tokens per use. If optionals end up with no `?` in expression position, B becomes the cheaper and more familiar choice.

#### Questions for you

1. Add a conditional expression, or keep only `if` statements (A)?
2. If you add one, `if c then a else b` (F) or `c ? a : b` (B)?
3. Will optionals use `?` inside expressions, as postfix try or as `?.`? The answer decides whether B is safe.

### 4c. Character literals

C uses character literals 7.3 times per 1,000 lines, often as `'\0'`. Three things need deciding: the spelling, the type, and the escapes. chibi-v2's strings accept only JSON's escapes today (`\" \\ \/ \b \f \n \r \t \uXXXX`, in `escape_len` in `src/lexer.c`), so C's `\0`, `\x41` and `\'` are errors. Below, a code point is the number Unicode gives a character (U+20AC for €), and a UTF-8 code unit is one byte of its encoding (€ takes three).

#### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | `'a'` is a code point literal of type `comptime_int`, the same type as integer literals. Escapes are `\n \r \t \\ \' \" \xNN \u{NNNNNN}`, with no `\0` | [langref L672-L744](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L672-L744) |
| Odin | `'g'` is an untyped rune with default type `rune`, the size of `i32`, and "can convert to any number type". Octal escapes need three digits, so `'\0'` is rejected | [faq.md L210-L214](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/faq.md#L210-L214); [overview.md L92-L151](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L92-L151); [tokenizer.cpp L625-L660](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/tokenizer.cpp#L625-L660) |
| Jai | No character type. `f := #char "1";` gives 49 as `s64`, and single quotes are kept free "for some special operation" later (community source) | `~/Personal/jai-refs/The_Way_to_Jai/book/05A_Constants_variables_types_and_operations.md` L87-L88; `JCL-wiki/Philosophy-of-Jai.md` L313-L315 |
| C3 | `'a'` is a `char` (unsigned 8-bit); `'C3'`, `'TEST'` and 8-byte literals become `ushort`, `uint` and `ulong`. Escapes include `\0 \a \e \v \xNN \uNNNN \UNNNNNNNN` | [parse_expr.c L1873-L1907](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/src/compiler/parse_expr.c#L1873-L1907); [spec L255-L320](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/implementation-details/specification.md#L255-L320) |
| Carbon | `var a: char = 'a';` A literal holds one code point and has type `Core.CharLiteral`, which converts to `char`, an 8-bit unsigned UTF-8 code unit. `\xHH` stops at `7F`; `\0` and `\u{…}` exist | [character_literals.md L20-L65](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/lexical_conventions/character_literals.md#L20-L65); [p6710 L43-L53](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p006710-char-redesign.md#L43-L53) |
| ZenC | `'a'` is a `char`, but a non-ASCII literal or `'\u{2764}'` is a 32-bit `rune`, so the type depends on the contents. Escapes are `\n \r \t \\ \' \" \0 \u{…}` | [expr_literal.c L769-L850](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/expr/expr_literal.c#L769-L850); [02-primitive-types.md L23-L44](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/02-primitive-types.md#L23-L44) |

#### Options

- **A. An integer constant typed by context** (Zig, Odin, Carbon). `'a'` behaves exactly like the literal `97`, so `c: u8 = 'a'`, `c - '0'` and `x == 'a'` need no cast, and `'€'` assigned to a `u8` is a compile error because 8364 doesn't fit. It reuses whatever rule gives number literals their types instead of adding a type rule.
  ```
  digit := c - '0'
  if c == '\n' {
      line += 1
  }
  ```
- **B. Always `u8`** (C3, and Zen C for ASCII). It matches how C code treats `char` as a byte. A code point then needs another form, and Zen C's answer, which switches the type by value, makes a literal's type depend on its contents.
  ```
  quote: u8 = '"'
  ```
- **C. Always a 32-bit code point** (Odin's default, Go). It suits text processing, but every byte comparison then needs a conversion if chibi has no implicit numeric conversions (sheet 2).
  ```
  euro := '€'
  ```
- **D. No character literal** (Jai). A compile-time call or a named constant replaces it, the translator writes numbers, and `'` stays unused.
  ```
  if c == 10 {
      line += 1
  }
  ```

Options A to C share a lexer cost. The block lexer finds strings 64 bytes at a time with a prefix XOR over the positions of `"` (each bit says "inside a string" when an odd number of quotes precede it), and it assumes every backslash is inside a string (`resolve_literals`, `src/lexer.c` L257-L305 today). A character literal breaks both assumptions, because `'"'` holds a quote that opens a fake string and `'\n'` puts a backslash outside one. Most cases fail the mask checks and fall back to the scalar path, which handles the block one byte at a time, but two `'"'` on one line pair up into a fake string and pass, so a cheap safe rule is to send any block holding both `'` and `"` to the scalar path. That rule also catches strings such as `"don't"`, so its rate should be measured; the lasting fix is to resolve `'…'` spans in the same fix-up loop the lexer uses for comments. This is analysis, not a measurement.

#### Recommendation

**I recommend A, with C's common escapes,** because an integer constant needs no new type rule (it is `97` spelled differently), works with bytes and code points alike, and translates C's `'a'` one to one. The escape set should grow by `\0`, `\xNN` and `\'`, and strings and character literals should share it.

#### Questions for you

1. Add `'a'` (A), or follow Jai and leave `'` unused (D)?
2. Add `\0`, `\xNN` and `\'` to the escapes that strings already accept?
3. Keep JSON's fixed-width `\uXXXX`, or switch to `\u{…}` (Zig, Carbon, Zen C), which reaches code points above U+FFFF without surrogate pairs?
4. What type does an unconstrained `c := 'a'` get: the same type as `c := 97`, `u8`, or a 32-bit code point?

### 4d. `goto` and labels

`goto` is a reserved keyword in chibi-v2's lexer (`TK_GOTO`), but nothing parses it. C uses it 2.7 times per 1,000 lines, often to jump to cleanup code; the corpus mix has about 2,800, with 703 in flecs and 871 in SQLite. Go's compiler had 1,032 `goto`s and 241 labels, and c2go translated only the trivial ones while people rewrote the rest ([c2go.slide L329-L345](https://github.com/golang/website/blob/eaf4e1aa02a736d035f64e9a0449cace03bc407f/_content/talks/2014/c2go.slide#L329-L345)). Zig's translate-c gives up on any function that contains one and keeps only its declaration ([langref L6801-L6816](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L6801-L6816)).

#### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | No `goto`. Labeled blocks (`blk: { y += 1; break :blk y; }`), labeled loops, and a labeled `switch` whose `continue :sw 4` jumps straight to another arm | [langref L2524-L2536, L2589-L2634, L2699-L2704](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L2589-L2634); [test_switch_continue.zig](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/test_switch_continue.zig) |
| Odin | No `goto` keyword. `loop: for cond1 { for cond2 { break loop } }`, and labels on `if` and plain blocks | [tokenizer.cpp L86-L126](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/tokenizer.cpp#L86-L126); [overview.md L731-L765](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L731-L765) |
| Jai | No `goto`. `break i` and `continue i` name a loop by its iteration variable, and a `while` can name its condition (community source) | `~/Personal/jai-refs/The_Way_to_Jai/book/14A_Branching_with_if_else.md` L6; `15A_Looping_with_while_and_for.md` L235-L362 |
| C3 | `goto` removed. A label goes after the keyword: `do FAIL: { … break FAIL; };`. `nextcase 3` jumps to another case | [c-to-c3 guide L539-L680](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/c-to-c3/a-guide-for-c-programmers.md#L539-L680); [spec L1365-L1375, L2039-L2055](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/implementation-details/specification.md#L2039-L2055) |
| Carbon | No `goto` and no labels: `break;` and `continue;` only | [loops.md L133-L172](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/control_flow/loops.md#L133-L172); [words.md L35-L118](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/lexical_conventions/words.md#L35-L118) |
| ZenC | `goto cleanup;` with `cleanup:`, plus GNU's computed `goto *ptr;`; `goto` is rejected inside `defer`. Labeled loops: `outer: loop { break outer; }` | [parser_stmt.c L619-L700](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/parser_stmt.c#L619-L700); [05-control-flow.md L92-L95](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/05-control-flow.md#L92-L95) |

#### Options

- **A. No `goto`; labels on loops, `switch` and blocks** (Zig, Odin). A label is `name:` in front of `for`, `while`, `switch` or `{`, and `break name` and `continue name` target it. None of those four tokens can start a type, so the parser tells a label from an `x: T` declaration by checking a third token, and it runs that check only after it has seen `name :`. It already looks three tokens ahead for `.a =` in named arguments, and nothing needs re-tagging. Like `return`, `break name` takes its name only from its own line. For translation, a cleanup `goto` becomes a labeled block plus `break` or a `defer`, a retry `goto` becomes `continue` on a labeled loop, and the rest (jumps into a block, loops with two entries) become a `switch` inside a loop or a hand edit.
  ```
  outer: for row in rows {
      for cell in row {
          if cell == target {
              found = true
              break outer
          }
      }
  }

  done: {
      if !open_file() { break done }
      if !read_header() { break done }
      ok = true
  }
  ```
- **B. Name loops by their variable** (Jai). `break x` leaves the `for` loop whose value or index is named `x`, so no label syntax is needed. `while` loops and plain blocks have no name, a loop whose value is `_` must use its index name, and cleanup `goto`s have nothing to map to.
  ```
  for row, r in rows {
      for cell in row {
          if cell == target { break r }
      }
  }
  ```
- **C. Restricted `goto`** (Go). `goto done` is allowed, but not into a block or over a declaration ([Go spec](https://go.dev/ref/spec#Goto_statements)), so most C `goto`s translate one to one. A Go-style label statement `done:` collides with the settled `x: T` form, because `done:` at the end of a line followed by `cleanup()` reads as the declaration `done: cleanup`. Labels would need another spelling, `goto` across a `defer` needs a rule (Zen C forbids `goto` inside `defer`), and the result is a second way to do what `defer` and labeled `break` already do.
  ```
  goto done
  ```
- **D. Full C `goto`, computed `goto` included** (ZenC). Translation fidelity is perfect. It also brings irreducible control flow (loops with more than one entry) into the IR, the compiler's internal code form, which the one-pass backend and the coroutine lowering must then handle.
  ```
  goto done
  ```

An add-on to A is Zig's labeled `switch` with `continue :sw value`, or C3's `nextcase value`, in which each arm jumps straight to the arm for the next value. Zig says each such jump gets its own branch, which the CPU predicts separately, so state machines and interpreters dispatch faster than through one loop head ([langref L2611-L2630](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L2611-L2630)). chibi's compile-time interpreter (H9) is that kind of loop.

#### Recommendation

**I recommend A (labels on `for`, `while`, `switch` and blocks, and no `goto`),** because every one of the six except Zen C dropped `goto`, `defer` covers C's main use of it, the label fits the declaration grammar with one extra token check, and the translator needs restructuring rules anyway for the `goto`s labels can't express, as Go did. `goto` should stay reserved only to produce an error that suggests a labeled `break`, `continue` or `defer`, as `&&` already suggests `and`.

#### Questions for you

1. Labels with no `goto` (A), or a restricted `goto` (C)?
2. Spell labels `outer: for` (Zig, Odin), or name loops by their variable (Jai)?
3. Inside a `switch` arm, does a plain `break` leave the `switch` (C, Go, Odin) or the enclosing loop (Zig, where an unlabeled `switch` is not a `break` target, by my reading of the langref)? Arms don't fall through, so leaving the `switch` is only an early exit.
4. Add `switch` re-dispatch (`continue sw value`) now for interpreter loops, or later?

### 4e. Variadic functions

Two questions are open: how chibi's own functions take a variable number of arguments, and how chibi calls C's variadic functions, such as `SDL_Log(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)` ([SDL_log.h L248](https://github.com/libsdl-org/SDL/blob/6f45f97af0ebaa963f2efb971540b08b199e264c/include/SDL3/SDL_log.h#L248)). The second is required, because the decided header generator must write a declaration for every such function. C defines variadic functions only 0.3 times per 1,000 lines, but `printf`-style calls are common (not measured). The lexer already has an unused `..` token, while `...` would be a three-byte operator, which the block lexer sends to its slow path (front-end-optimizations.md, F7).

#### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | No variadic Zig functions: a tuple goes to an `anytype` parameter, `print("{s} {}\n", .{ a_string, a_number })`. C variadics appear only on `extern` declarations: `pub extern "c" fn printf(format: [*:0]const u8, ...) c_int;`. Defining one uses `@cVaStart` and related builtins | [print.zig L7](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/print.zig#L7); [poc_print_fn.zig L3](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/poc_print_fn.zig#L3); [test_variadic_function.zig L4](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/test_variadic_function.zig#L4); [langref L6892-L6901](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L6892-L6901) |
| Odin | `sum :: proc(nums: ..int)`, called as `sum(1, 2)` or `sum(..odds)`. C variadics are marked in foreign blocks: `Log :: proc(fmt: cstring, #c_vararg args: ..any) ---`. A `proc "c"` can define one with the `c_va_*` intrinsics | [overview.md L850-L868, L3964-L3983](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L850-L868); [sdl3_log.odin L52-L67](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/vendor/sdl3/sdl3_log.odin#L52-L67) |
| Jai | `var_args :: (args: ..int)` and `print :: (fmt: string, args: ..Any)`, with spread `var_args(..args)`. Generated C bindings write `TraceLog :: (logLevel: s32, text: *u8, args: ..*u8) #foreign raylib_native;` (community source; how Jai passes these to C is unverified) | `~/Personal/jai-refs/JCL-wiki/Getting-Started.md` L1034-L1056; `The_Way_to_Jai/examples/35/raylib/module.jai` L714 |
| C3 | `fn void f(int... args)` (a slice), `any... args`, `args...` (implicitly `any`), and `extern fn void va_untyped(...);`, "only used for extern C functions". Splat with `f(...xs)` | [functions.md L91-L142](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/language-fundamentals/functions.md#L91-L142) |
| Carbon | Compile-time packs: `fn Zip[... each ElementType: type](... each vector: Vector(each ElementType))`. Calling an imported C variadic function fails with "semantics TODO: Unsupported: Variadic function" | [variadics.md L41-L82](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/variadics.md#L41-L82); [function.carbon L133-L157](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/testdata/interop/cpp/function/import/function.carbon#L133-L157) |
| ZenC | C's own mechanism: `fn log(lvl: int, fmt: char*, ...)` with `va_list` and `va_start`, and `extern fn printf(fmt: char*, ...) -> c_int;` | [04-functions-lambdas.md L104-L113](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/04-functions-lambdas.md#L104-L113); [14-interop.md L32-L41](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/14-interop.md#L32-L41) |

#### Options

For chibi's own functions:

- **A. Typed slice variadics** (Odin, Jai, C3). Inside `fn sum(xs: ..int) -> int` the parameter `xs` is a `[]int`; the caller builds the array on its own stack, and `sum(..list)` passes an existing slice. There is one compiled body, no generics and no allocation. The parser gains `..T` as one more prefix type form, valid only on the last parameter, and `..x` as a prefix inside argument lists; `..` is already a token. A `print` needs `..any`, which depends on reflection's `any` (sheet 8), in which each argument becomes a pointer plus a type ID that C3's `printf` switches on ([comptime-generics-reflection.md](comptime-generics-reflection.md), C3 section).
  ```
  fn sum(xs: ..int) -> int {
      total := 0
      for x in xs {
          total += x
      }
      return total
  }

  a := sum(1, 2, 3)
  b := sum(..scores)
  ```
- **B. Tuples and compile-time parameters** (Zig). There is no variadic syntax: `print("x = {}", .{ x })` passes an anonymous tuple to a parameter that accepts any type and is compiled once per type it receives (Zig's `anytype`). Format strings can then be checked at compile time, but every distinct pair of format string and argument types becomes a separate compiled instance (comptime-generics-reflection.md section 3.1 and hypothesis C9). It depends on sheets 6 and 7, and `.{ x }` with no known type also needs anonymous tuple types.
  ```
  print("x = {}, y = {}", .{ x, y })
  ```
- **C. Compile-time packs** (Carbon). It is the most general form and much the most complex, out of proportion to chibi's needs.
  ```
  // no settled chibi spelling; see Carbon's `... each`
  ```
- **D. C's `va_list`** (ZenC). Arguments are unchecked, and it is useful only for defining C-variadic functions that C calls.
  ```
  // only for export fn; needs va_list support in both backends
  ```

For C functions:

- **E. A bare `..` at the end of a C declaration** (Zig's and C3's `...`, spelled with chibi's existing token). The generator writes `..` wherever C has `...`. At each call the compiler applies C's default argument promotions (integers smaller than `int` become `int`, and `f32` becomes `f64`), and on Apple arm64 it passes the variadic arguments on the stack ([hypotheses.md](../hypotheses.md), I1). The `extern fn` spelling is sheet 10a's open question.
  ```
  extern fn SDL_Log(fmt: *u8, ..)

  SDL.SDL_Log("hp = %d", hp)
  ```
- **F. A marked, typed tail** (Odin's `#c_vararg args: ..any`, Jai's `args: ..*u8`). It reuses A's syntax with a marker, but the type after `..` means nothing to C.
  ```
  extern fn SDL_Log(fmt: *u8, args: ..any)
  ```

#### Recommendation

**I recommend A for chibi functions and E for C functions,** because a slice variadic compiles once, allocates nothing and needs neither generics nor compile-time code, while the C tail is a single token the generator can write for every `...` it meets. B can be added later beside A if sheets 6 and 7 make a compile-time-checked `print` (C9) worth it.

#### Questions for you

1. Typed slice variadics `xs: ..T` (A) for chibi functions?
2. A bare `..` (E) for C's variadic tail?
3. Should chibi code be able to define a C-variadic function, for `export fn`, as Odin and Zig allow? It needs `va_list` support in both backends.

### 4f. Bitfields

C bitfields pack several small integers into one machine word. They are rare in the corpus (about 0.2 per 1,000 lines) and absent from chibi-v2's own C, and their layout is set by each platform's C ABI (the rules for laying out data and passing arguments), not by the C standard. The decided C layout for structs doesn't cover them, yet the header generator will meet them in any C struct that has one.

#### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | `packed struct { half1: u8, quarter3: u4, quarter4: u4 }` with a backing integer, laid out from the least significant bit; translate-c turns C structs with bitfields into `opaque{}` | [langref L2231-L2316, L6801-L6812](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L2231-L2316); [test_packed_structs.zig L5-L12](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/test_packed_structs.zig#L5-L12) |
| Odin | `Foo :: bit_field u16 { x: i32 \| 3, y: u16 \| 2 + 3 }` with a required backing type; "Odin's `bit_field` and C's bit-fields might not be compatible" | [overview.md L2496-L2528](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L2496-L2528) |
| Jai | None: "Bitfields are not useful for Jon and game development. Bitfields are terrible for multi-threading." (community source) | `~/Personal/jai-refs/JCL-wiki/Philosophy-of-Jai.md` L301-L303 |
| C3 | `bitstruct Foo : char { int a : 0..2; int b : 4..6; bool c : 7; }` with exact bit ranges and no address of a field; "C bit fields must be manually converted to a C3 bitstruct with the correct layout for each target platform" | [bitstructs.md L6-L42](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/language-common/bitstructs.md#L6-L42); [cinterop.md L76-L79](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/language-common/cinterop.md#L76-L79) |
| Carbon | None. A C++ struct with bit-fields still imports, but using a bit-field member is "semantics TODO" | [field.carbon L94-L168](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/check/testdata/interop/cpp/class/import/field.carbon#L94-L168) |
| ZenC | C's own form, passed through to the C compiler: `valid: U8 : 1;` | [03-aggregate-types.md L52-L67](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/03-aggregate-types.md#L52-L67) |

#### Options

- **A. None; the generator stubs bitfield members** (Jai; Carbon today). clang reports the struct's size, alignment and every field offset, so the generator keeps the struct, fills the bitfield storage with opaque bytes, and makes each bitfield member a stub that errors only when used, under the decided stub rule. chibi code writes packed formats with shifts and masks, or with accessors that compile-time code generates (sheet 7). No syntax is added.
  ```
  kind := header & 0xF
  len := header >> 4
  ```
- **B. Sequential widths** (Zig, Odin). Zig's form needs arbitrary-width integer types such as `u3`, which is a type-system decision of its own; Odin's `| width` keeps ordinary field types. Fields take the next bits in order, so a C layout with gaps (MSVC starts a new storage unit when the declared field type changes size) needs padding fields.
  ```
  struct Header: u16 {
      kind: u8 | 4
      len: u16 | 12
  }
  ```
- **C. Explicit bit ranges, using the settled range syntax** (C3). The backing type after `:` mirrors `enum Color: u8`, and `in` is already a keyword. The generator can write any C layout exactly, per target, because clang reports each field's bit offset. The parser gains an optional `: Type` after a struct name (today `struct P: u8 {}` is an error), an optional `in` range after a member's type, and ranges in a new position (today they appear only in `for` and `switch`). The checker verifies that ranges fit the backing type and don't overlap, and rejects `&h.len`.
  ```
  struct Header: u16 {
      kind: u8 in 0..4
      len: u16 in 4..16
  }
  ```
- **D. C-compatible bitfields inside ordinary structs** (ZenC). This extends the decided C-layout promise to bitfields, but chibi's own backend must then implement each target ABI's bitfield rules, and the generated headers can't check them with `offsetof` as hypothesis I5 plans, because C forbids `offsetof` on a bitfield.
  ```
  struct Flags {
      valid: u8 : 1
      mode: u8 : 3
  }
  ```

#### Recommendation

**I recommend A (no bitfields yet; the generator stubs bitfield members),** because bitfields are rare in C and absent from chibi-v2's C, and the stub rule already keeps C structs that contain them usable. If a real program needs packed fields, add C, because only explicit ranges let the generator reproduce every C layout exactly.

#### Questions for you

1. Leave bitfields out for now (A), with generated stubs for C bitfield members?
2. If you want them now, explicit ranges (C) or widths (B)?

---

## 9. Tasks and coroutines

Tasks and coroutines are required, hot reload must keep their state, and `yield` is already a reserved keyword. This sheet decides how a function is marked as a task, what suspending looks like, and how a task is started, resumed, cancelled and stored. The research recommends stackless coroutines, where each task's live locals sit in a record called a frame and resuming jumps to a numbered suspension point (a place where the task can stop and later continue), because only those can be mapped onto new code after a reload and run on WebAssembly ([metaprogramming-reload-coroutines.md](metaprogramming-reload-coroutines.md), sections 5.3 and 6.12). Stackful coroutines, where each task owns a machine stack as Lua threads and fibers do, are still open, and the syntax decides how much of that choice shows in source.

### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | Removed. Up to 0.10: `var frame = async func();`, `suspend {}`, `resume frame;`, `await frame`, with frame types `@Frame(func)` and `anyframe`; "Zig infers that a function is `async` when it observes that the function contains a suspension point." 0.11 shipped without it, `async` and `await` stopped being keywords on 2025-07-07, and 0.15.2 still parses `suspend`, `resume` and `nosuspend` | [0.10.1 langref L7591-L7816](https://github.com/ziglang/zig/blob/0.10.1/doc/langref.html.in#L7591-L7816); [commit abfed17](https://github.com/ziglang/zig/commit/abfed1708862716b7b54ce8ea349ce7215a9fab4); [0.15.2 langref L4259-L4263, L8316-L8348](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L4259-L4263) |
| Odin | None in the language: no `yield`, `async` or `await` keyword; threads live in `core:thread` | [tokenizer.cpp L86-L126](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/src/tokenizer.cpp#L86-L126) |
| Jai | None: "Jai does not and will not support coroutines"; a community module, `osor_coroutine`, exists (community source) | `~/Personal/jai-refs/JCL-wiki/Overview.md` L85, L255-L256; `JCL-wiki/References.md` L104 |
| C3 | None; no such keyword | [grammar.md L8-L36](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/implementation-details/grammar.md#L8-L36) |
| Carbon | None in the design; no such keyword | [words.md L35-L118](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/lexical_conventions/words.md#L35-L118) |
| ZenC | Had `async fn fetch() -> int` with `let r = await fetch();`, documented as "Built on pthreads" and later compiled to a polled state machine. Removed on `main` on 2026-10-24: the parser now says "'async' was removed; call the function directly or use std/thread.zc". [memory-models.md](memory-models.md) describes the earlier state machine | [11-concurrency.md L1-L21](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/11-concurrency.md#L1-L21); [commit d6502e8f](https://github.com/Zuhaitz-dev/zenc-lang/commit/d6502e8fb56d284b12bcdc80609bd18296f802fa); [err_async_removed.expected](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/tests/compiler/diagnostics_output/err_async_removed.expected) |

### What other languages do

| Language | Form | Source |
|---|---|---|
| C# iterators | `IEnumerable<int> F(int upto) { … yield return i; }`. "Calling an iterator doesn't execute it immediately"; each step resumes after the last `yield return`, and `yield break` ends it | [yield statement](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/statements/yield) |
| Unity | "Coroutines are methods with an IEnumerator return type and a yield return statement." `StartCoroutine(Fade())` starts one, `yield return new WaitForSeconds(.1f)` waits, `StopCoroutine` stops it, and a coroutine also stops when its GameObject becomes inactive or its script is destroyed. "Each coroutine creates an IEnumerator state machine," and `new WaitForSeconds` allocates | [Unity manual, Coroutines](https://docs.unity.com/en-us/engine/current/manual/scripting/coroutines-section/coroutines) |
| Lua | `co = coroutine.create(f)`; `coroutine.resume(co, …)` "Starts or continues the execution"; `coroutine.yield(…)` works at any call depth because each coroutine is "an independent thread of execution" (stackful); `coroutine.close(co)` closes its pending to-be-closed variables | [Lua 5.4 §2.6](https://www.lua.org/manual/5.4/manual.html#2.6), [§6.2](https://www.lua.org/manual/5.4/manual.html#6.2) |
| Kotlin | `suspend fun greet()` marks the function, and calls to other suspend functions suspend with no `await`. `val job = launch { delay(…) }` starts one; `job.cancel()` cancels, and "coroutine cancellation is cooperative", checked at suspension points. `sequence { yield(1) }` builds generators | [coroutines basics](https://kotlinlang.org/docs/coroutines-basics.html); [cancellation](https://kotlinlang.org/docs/cancellation-and-timeouts.html) ([source L20-L28, L157-L159](https://github.com/Kotlin/kotlinx.coroutines/blob/cb891528b2841c7da18f01a790339d0f3fd26056/docs/topics/coroutines-cancellation.md#L20-L28)); [sequences](https://kotlinlang.org/docs/sequences.html) |
| Rust | `async fn` returns a future and does "no work when called"; `fut.await` suspends until it is ready and is legal only inside async code. There is no separate cancel: a future that is dropped is never polled again (my inference from the poll model). `gen` and `yield` are reserved but not stable | [async functions](https://doc.rust-lang.org/reference/items/functions.html#async-functions); [await](https://doc.rust-lang.org/reference/expressions/await-expr.html); [keywords](https://doc.rust-lang.org/reference/keywords.html) |

### Options

Every example is the same game script. An enemy walks to each of four waypoints and waits 2 seconds at each one, forever. `frame_dt()` returns the last frame's duration, `step_toward(id, p, dt)` moves the enemy and returns `true` on arrival, and `EnemyId` is a handle (an index plus a generation number into a pool), because under memory design A a handle survives a reload and a raw pointer may not.

- **A. A library, with nothing in the language** (Odin, Jai, C3, Carbon; Zen C since 2026-10-24; Zig 0.16's experimental `Io.Evented`). Fibers are stackful, so any function can suspend by calling the library, and `fiber.yield()` parses because keywords may be member names. The compiler and parser stay unchanged, and `yield` could even be un-reserved. Each fiber needs its own stack, and Naughty Dog used 64 KiB and 512 KiB stacks, so 1,000 patrolling enemies need about 62 MiB of stack, against roughly 80 bytes of live state per stackless frame here (an estimate). A reload must let every fiber finish in old code, because its stack holds raw return addresses, WebAssembly can't switch stacks, and stopping a fiber mid-function can't run its `defer`s without stack unwinding (walking the fiber's stack frame by frame to run cleanup). Starting a fiber with arguments needs a capturing `fn` literal, and whether `fn() => …` captures is undecided.
  ```
  fn wait(seconds: f32) {
      t: f32 = 0.0
      while t < seconds {
          fiber.yield()
          t += frame_dt()
      }
  }

  fn patrol(id: EnemyId, points: [4]Vec2) {
      while true {
          for p in points {
              while !step_toward(id, p, frame_dt()) {
                  fiber.yield()
              }
              wait(2.0)
          }
      }
  }

  f := fiber.start(64 * 1024, fn() => patrol(id, points))
  fiber.resume(f)
  fiber.stop(f)
  ```
- **B. Inferred, with no marker** (Zig up to 0.10; Lua is the stackful analogue). A function that contains `yield`, or that calls a function that does, becomes a task, and starting one at the top needs a call-site keyword, as Zig's `async` was. With stackful coroutines this is free, because any function can suspend. With stackless ones the compiler must propagate "suspends" through the whole call graph before it knows any function's type, and a function's frame layout depends on its callees. Zig never handled function pointers to async functions correctly ([#6025](https://github.com/ziglang/zig/issues/6025#issuecomment-1744116798)). For chibi, adding a `yield` to a leaf function would change the type and frame of every caller, which defeats parallel checking, incremental builds and per-function hot reload.
  ```
  fn wait(seconds: f32) {
      t: f32 = 0.0
      while t < seconds {
          yield
          t += frame_dt()
      }
  }

  fn patrol(id: EnemyId, points: [4]Vec2) {
      while true {
          for p in points {
              while !step_toward(id, p, frame_dt()) {
                  yield
              }
              wait(2.0)
          }
      }
  }

  t := async patrol(id, points)
  resume t
  ```
- **C. A marker on the declaration, with implicit suspension at calls** (Kotlin's `suspend fun`). The marker is local, so separate compilation works. Inside a task, a call to another task runs it and waits, with no keyword at the call; outside a task the same call must be an error, or a builder must start it, as Kotlin's `launch` does, and `spawn` below stands for such a builder. Suspension points don't show in source, so a reader can't see which calls suspend, and the checker can't point at a visible token when a raw pointer would be held across one. The marker makes the frame layout computable per function in the stackless case and is documentation only in the stackful case. `task` can be a contextual keyword, one that is a keyword only directly before `fn`, which the parser checks with a two-token test and which leaves `task` usable as a variable name.
  ```
  task fn wait(seconds: f32) {
      t: f32 = 0.0
      while t < seconds {
          yield
          t += frame_dt()
      }
  }

  task fn patrol(id: EnemyId, points: [4]Vec2) {
      while true {
          for p in points {
              while !step_toward(id, p, frame_dt()) {
                  yield
              }
              wait(2.0)
          }
      }
  }

  t := spawn patrol(id, points)
  resume(&t)
  ```
- **D. A marker on the declaration, with explicit `await`** (Rust's `async fn` and `.await`, C#'s `async` and `await`, Zen C before the removal). Each form has one meaning. Calling a task anywhere makes a frame and runs nothing, `await` runs a frame inside the current task until it finishes, and `yield` suspends. Every suspension point is a visible token, `yield` or `await`, which gives each one a stable ID for reload mapping and lets the checker report the exact `await` across which a raw pointer would be held (design A's rule that frames hold no raw pointers). `await` must be a prefix keyword, parsed at unary precedence and written after its operand in postorder; Rust's postfix `.await` doesn't fit, because keywords may be member names, so `x.await` is already a field access. `yield` is a statement that, like `return`, takes a value only from its own line. In the stackless case the frame of `patrol` embeds the frame of `wait`, because the callee is known; awaiting through a task-function value needs that callee's frame size at run time, or a frame allocated elsewhere ([Zig #23446](https://github.com/ziglang/zig/issues/23446), [#23367](https://github.com/ziglang/zig/issues/23367)). In the stackful case `await f()` is just a call.
  ```
  task fn wait(seconds: f32) {
      t: f32 = 0.0
      while t < seconds {
          yield
          t += frame_dt()
      }
  }

  task fn patrol(id: EnemyId, points: [4]Vec2) {
      while true {
          for p in points {
              while !step_toward(id, p, frame_dt()) {
                  yield
              }
              await wait(2.0)
          }
      }
  }

  t := patrol(id, points)
  done := resume(&t)
  cancel(&t)
  ```
  `resume` returns `true` when the task has finished, and `cancel` runs the task's pending `defer`s and marks it finished. Frames are plain values, so a game stores them wherever it stores other data, for example `scripts: [64]PatrolTask` resumed by `for _, i in scripts { resume(&scripts[i]) }`, where `PatrolTask` stands for the frame type of `patrol`, whose spelling is open. Copying a frame is safe only while no pointer points into it, which Rust handles with `Pin` and Goose by forbidding such pointers (memory-models.md section 7). The names `resume` and `cancel` are placeholders that could become keywords, built-ins or methods (sheet 5).
- **E. The return type marks it** (C# iterators, Unity). No marker keyword is needed if `Task` is a built-in type, and the checker accepts `yield` only in functions that return it. `yield` then means "suspend" with no operand and "run this nested task" with one, so its meaning depends on the operand's type, as Unity's scheduler decides at run time. A task that returns a value needs generic syntax such as `Task(int)` (sheet 6). One `Task` type also hides each frame's size, so the language must allocate frames on a heap or in a pool behind the program's back, which is the allocation Unity's manual warns about.
  ```
  fn wait(seconds: f32) -> Task {
      t: f32 = 0.0
      while t < seconds {
          yield
          t += frame_dt()
      }
  }

  fn patrol(id: EnemyId, points: [4]Vec2) -> Task {
      while true {
          for p in points {
              while !step_toward(id, p, frame_dt()) {
                  yield
              }
              yield wait(2.0)
          }
      }
  }

  t := patrol(id, points)
  ```

### Recommendation

**I recommend D, spelled `task fn`, bare `yield` and prefix `await`,** because:
- each form has exactly one meaning, so a call never changes behavior with its context, as it does in option C;
- the marker is local, so a function's type never depends on its callees, unlike B, which keeps checking parallel and reload per-function;
- every suspension point is a visible token, which gives stable IDs for mapping suspended frames onto new code and lets the checker name the exact line where a pointer would cross a suspension;
- frames are plain values that the program stores in its own pools, with no hidden allocation, unlike E;
- it works with stackless coroutines, the research's recommendation, and still works with stackful ones.

`task` reads better than `async` for game scripts resumed once per frame, and it should be contextual, a keyword only before `fn`, so `task` stays usable as a name.

### Questions for you

1. Mark tasks explicitly (C, D or E), rather than inferring them (B) or leaving them to a library (A)?
2. Spell the marker `task fn` or `async fn`?
3. Make `task` a reserved keyword, or contextual (a keyword only before `fn`)?
4. Make waiting on another task explicit with `await` (D), or implicit at the call (C)?
5. May a task produce values (`yield v`, a generator that `for x in` can consume), or only suspend?
6. Does `cancel` run the task's pending `defer`s?
7. How is a task's frame type named, so that it can be stored in a struct field: `typeof` (already reserved), a built-in, or something else?
8. Stackless or stackful? The research recommends stackless.

---

## 10. C interop spellings

chibi and C call each other in both directions. C calls chibi through `export fn`, C headers come in through a cached generator, and on 2026-11-10 the parser thread also settled how a C header import is spelled. This sheet covers what remains, which is how defines, include paths, targets and libraries reach an import, how the generated declarations are spelled, and the details of `export fn`.

### 10a. C imports: what is still open

Settled in the parser thread on 2026-11-10 ([decisions.md](../decisions.md) records the `.h` rule and the module forms): a path ending in `.h` is a C header, imported like a package. `import "SDL3/SDL.h"` binds `SDL`, and `const SDL := import "SDL3/SDL.h"` chooses the name, because `import` is also an expression, as in Zig; an ordinary package composes the same way, so `import "std/fmt"` binds `fmt`. C names keep their spelling under the bound name, as in `SDL.SDL_Init`, and each C declaration has one shared copy, so `SDL.SDL_Surface` and `IMG.SDL_Surface` are the same type. Names are private to their package unless marked `pub`. The mechanism was settled earlier: on a cache miss the build program runs clang and writes ordinary chibi declarations, the cache key covers every header clang read plus the target and the defines, and declarations that can't be translated become stubs that fail only when used.

Four things remain open:
1. Where defines and include paths come from. SDL3 is the standard case, because its callback entry points need `#define SDL_MAIN_USE_CALLBACKS` before `#include <SDL3/SDL_main.h>`, in one file ([README-main-functions.md L123-L131](https://github.com/libsdl-org/SDL/blob/3659b2f100e4434f515a5543c749beed3d3fcc2c/docs/README-main-functions.md#L123-L131)).
2. How a header is limited to some targets, such as `windows.h`.
3. Where the library to link is named.
4. How the generated file spells a C function that has no body, and C's `...` (see 4e).

#### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | The removed `@cImport({ @cDefine("NDEBUG", …); @cInclude("stdlib.h"); })` put defines in source. Since 0.16 the build program does it with `b.addTranslateC(.{…})`, and the code says `@import("c")`; the step has `defineCMacro`, `addIncludePath` and a `target`. Body-less C functions are `extern fn` | [langref L6677-L6705](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L6677-L6705); [0.16.0 notes](https://ziglang.org/download/0.16.0/release-notes.html), "cImport Moving to Build System"; [TranslateC.zig L22-L147](https://github.com/ziglang/zig/blob/0.15.2/lib/std/Build/Step/TranslateC.zig#L22-L147); [test_functions.zig L21-L27](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/test_functions.zig#L21-L27) |
| Odin | No header import; bindings are Odin source. The library is a `foreign import`, chosen per target: `when ODIN_OS == .Windows { @(export) foreign import lib { "SDL3.lib" } } else { … "system:SDL3" … }`. A declaration ends in `---` instead of a body | [sdl3__foreign.odin L5-L11](https://github.com/odin-lang/Odin/blob/85afcb386c389aafcec4f2e33d27ff8b42579822/vendor/sdl3/sdl3__foreign.odin#L5-L11); [overview.md L3871-L3962](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L3871-L3962) |
| Jai | A build metaprogram runs `Bindings_Generator` with `extra_clang_arguments` such as `-DWIN32_LEAN_AND_MEAN` and writes one file per OS under `#if OS == .WINDOWS`. Libraries are `lz4 :: #library "liblz4";` or `#system_library "libc"`, named by `#foreign` on each declaration (community source) | `~/Personal/jai-refs/JCL-wiki/Advanced.md` L557-L593; `The_Way_to_Jai/book/29A_Interacting_with_C.md` L10-L60 |
| C3 | No header import: `extern fn void puts(char*);` is written by hand. Libraries come from `-l foo` or `"linked-libraries"` in the project file | [cinterop.md L6-L19, L67-L74](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/language-common/cinterop.md#L6-L74) |
| Carbon | `import Cpp library "circle.h";` puts names under `Cpp.`; "Additional paths can be provided through compiler flags", and the driver passes `--clang-arg` to Clang. The toolchain also accepts `import Cpp inline '''#define …'''`, which the design still lists as TODO | [p6358 L94-L104](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p006358-c-interop-api-importing-and-semantics.md#L94-L104); [compile_options.cpp L33-L46](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/driver/compile_options.cpp#L33-L46); [socket.carbon L5-L21](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/examples/interop/cpp/socket.carbon#L5-L21) |
| ZenC | `import "raylib.h"` emits `#include` and leaves the rest to the C compiler. Build settings are comment directives, `//> include:`, `//> define:` and `//> link: -lraylib`, with `linux:`, `windows:` and `macos:` prefixes; `@cfg(USE_OPENGL)` keeps or drops an import | [12-advanced.md L145-L174, L281-L330](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/12-advanced.md#L281-L330); [stmt_import.c L745-L765](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/stmt/stmt_import.c#L745-L765) |

#### Options

Defines and include paths:

- **A. The build program sets them** (Zig since 0.16, Jai, Carbon's flags). Source keeps the settled form, and chibi, being its own build system, sets include paths and defines per target in the build program. It fits the decided cache key, which already holds the target and the defines, and it keeps the one-copy rule, because every import of a header in a build sees the same macros. A define that matters to one header sits far from the import that needs it. The build API is not designed, so the build lines below only illustrate.
  ```
  const SDL := import "SDL3/SDL.h"

  // build program, illustrative only
  game.c_include("vendor/SDL3/include")
  game.c_define("NDEBUG")
  ```
- **B. A wrapper header in the package** (C's own idiom). A two-line C header holds the define and the include, and the code imports it. No syntax is added, the define sits next to the code that needs it, in C, and the cache key already hashes every header clang read, so editing the wrapper regenerates the import.
  ```
  // src/sdl_main.h holds two lines of C:
  //   #define SDL_MAIN_USE_CALLBACKS
  //   #include <SDL3/SDL_main.h>
  const SDL_main := import "sdl_main.h"
  ```
- **C. Options on the import expression** (Zig's old `@cImport` block, Carbon's inline import). It needs a new clause after the path, because today `import "a".b` continues as a postfix chain and a trailing `.{…}` would read as a literal of the imported type. It also lets two imports of one header use different defines, which produces two versions of declarations that the one-copy rule says are shared, unless the generator proves they match.
  ```
  // hypothetical clause; no spelling is proposed
  ```
- **D. Comment directives** (ZenC). The block lexer discards comments with bit masks, so it would have to start reading them, and the settings would be invisible to the type system and to compile-time code.
  ```
  //> define: SDL_MAIN_USE_CALLBACKS
  ```

Targets and libraries:

- **E. One import, regenerated per target** (the decided cache key; Odin's `when`, Jai's `#if` and Zen C's `@cfg` for platform-only headers). Regeneration per target is already decided, because the target is in the cache key, so the build program only lists targets. A header that exists on only some targets sits inside a compile-time condition, which depends on sheet 7's compile-time `if`.
  ```
  const SDL := import "SDL3/SDL.h"
  // windows.h: imported inside a compile-time condition on the target (sheet 7)
  ```
- **F. The build program names libraries** (Zig's build, C3's project file). Source carries nothing, and prebuilt archives replace system libraries later, as decided long-term goal (4) plans.
  ```
  // build program, illustrative only
  game.link("SDL3")
  ```
- **G. Libraries named next to the import** (Odin's `foreign import`, Jai's `#system_library`, Zen C's `//> link:`). The library sits beside the code that needs it, but this needs a new declaration form in source.
  ```
  // hypothetical declaration form; no spelling is proposed
  ```

The generated declarations:

- **H. `extern fn`, the mirror of `export fn`** (Zig, C3, ZenC). A body-less C function is `extern fn SDL_Init(flags: u32) -> bool`, and a hand-written declaration for a function in a `.c` file uses the same form. `extern` is a new keyword, and the parser repeats `parse_export`'s shape, which is a keyword, then `fn`, then a name, allowing a missing body only there; today `fn f()` without a body is an error.
  ```
  extern fn SDL_Init(flags: u32) -> bool
  extern fn SDL_Log(fmt: *u8, ..)
  ```
- **I. Odin's `---` body** (Odin). It needs no keyword, but `---` would be a three-byte operator on the lexer's slow path.
  ```
  fn SDL_Init(flags: u32) -> bool ---
  ```

C names that are chibi keywords, such as a field or function named `type` or `in`, already work at use sites, because keywords may be member names, so `SDL.in` parses as a member access. Only the generated file must declare them. Parameters can simply be renamed, because C never sees their names, while a top-level C function named like a chibi keyword needs an escape, such as Zig's `@"in"` or Carbon's `r#in`; this is rare.

#### Recommendation

**I recommend A for build-wide defines and include paths, B for a define that one header needs, E for targets, F for libraries, and H for the generated declarations,** because together they leave the settled `import "x.h"` untouched, every define reaches the cache key through files or build settings the generator already hashes, the one-copy rule holds, and `extern fn` mirrors `export fn`.

#### Questions for you

1. Do defines and include paths live in the build program (A), with wrapper headers (B) for one-off defines?
2. Should source ever carry per-import options (C)?
3. Name libraries in the build program (F), or next to imports (G)?
4. Spell body-less C declarations `extern fn` (H)?
5. Is a header path searched relative to the package directory first, then the include paths, as C does for `#include "…"`?
6. Headers can contain function bodies: `static inline` helpers, and `SDL_main_impl.h`, which `SDL_main.h` pulls in under `SDL_MAIN_USE_CALLBACKS` and which defines `SDL_main` ([SDL_main.h L734-L736](https://github.com/libsdl-org/SDL/blob/b4c0634fefe59c5264e9c567e63eb9f3c55ad3a3/include/SDL3/SDL_main.h#L734-L736), [SDL_main_impl.h L62-L64](https://github.com/libsdl-org/SDL/blob/a19bac65218bf583e8e5cb8c52e1c9603fa8d94c/include/SDL3/SDL_main_impl.h#L62-L64)). Does the generator translate those bodies into chibi, or compile them with clang as C? It is not syntax, but it decides whether B works for SDL's main callbacks.

### 10b. `export fn` details

`export fn` is decided. It uses the C calling convention (the rules for which registers and stack slots carry arguments and results), keeps the name unmangled, and adds the function to a generated C header. Two details remain, which are overriding the C symbol name and typing the chibi functions that C calls back. SDL3's main callbacks are the test case, because under `SDL_MAIN_USE_CALLBACKS` SDL's own `main` calls `SDL_AppInit`, `SDL_AppIterate`, `SDL_AppEvent` and `SDL_AppQuit`, which the program must define with exactly those C names.

#### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | `export fn sub(a: i8, b: i8) i8` "makes it use the C ABI". Another name comes from `@export(&internalName, .{ .name = "foo", .linkage = .strong })` or `export fn @"any name"()`. Callbacks and function types say `callconv(.c)`, because only `export` and `callconv(.c)` promise the C ABI | [test_functions.zig L15-L27](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/test_functions.zig#L15-L27); [export_builtin.zig](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/export_builtin.zig); [langref L4844-L4866, L7339-L7344](https://github.com/ziglang/zig/blob/0.15.2/doc/langref.html.in#L4844-L4866) |
| Odin | `@(export, link_name="lib_foo") foo :: proc "c" () -> int`. The default `odin` convention passes an implicit `context` pointer, so procedures that C calls say `proc "c"` and set up a context themselves | [attributes.md L224-L232, L312-L327](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/attributes.md#L312-L327); [overview.md L2550-L2579](https://github.com/odin-lang/odin-lang.org/blob/1d272b93ccbd9a7e23f21cdbc2a6b5d5b8e5daa4/content/docs/overview.md#L2550-L2579) |
| Jai | `#program_export "main" __system_entry_point :: (argc: s32, argv: **u8) -> s32 #c_call`. Callbacks and their types say `#c_call` and must `push_context` before calling Jai code (community source) | `~/Personal/jai-refs/JCL-wiki/Advanced.md` L825-L860; `The_Way_to_Jai/book/29A_Interacting_with_C.md` L213-L250 |
| C3 | Functions already use the C ABI. `fn int square2(int x) @export("square")` renames; `@cname` and `@asmname` exist; `@callconv("…")` selects other conventions, default `"cdecl"` | [cinterop.md L21-L65, L93-L94](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/language-common/cinterop.md#L21-L65); [spec L2241-L2248, L2863](https://github.com/c3lang/c3-web/blob/d1088c190aefc8c05d4114fb722a84f5231155ff/docs/implementation-details/specification.md#L2241-L2248) |
| Carbon | A Carbon package exports a header that C++ includes, `#include "geometry.carbon.h"`; exporting functions to C++ is a future milestone, and I found no calling-convention syntax | [design README L3545-L3550](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/README.md#L3545-L3550); [milestones.md L159](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/project/milestones.md#L159) |
| ZenC | Every function is a C function, because Zen C compiles to C. `@export` sets default visibility, `@link_name("my_custom_name")` overrides the C identifier, and `fn*(int)` is a raw C function pointer for callbacks | [12-advanced.md L180-L205](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/12-advanced.md#L180-L205); [test_link_name.zc](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/tests/language/functions/test_link_name.zc); [04-functions-lambdas.md L87-L102](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/04-functions-lambdas.md#L87-L102) |

#### Options

The symbol name:

- **A. No override; the chibi name is the C name** (the decided default). The C name appears in chibi code, which states the contract honestly, and exports are few. Two packages that export the same name collide, and because the compiler sees the whole program it can report that before the linker does. How C's `void *` maps to a chibi type is the generator's choice and is undecided.
  ```
  const SDL := import "SDL3/SDL.h"

  export fn SDL_AppInit(state: **void, argc: i32, argv: **u8) -> SDL.SDL_AppResult {
      if !SDL.SDL_Init(SDL.SDL_INIT_VIDEO) {
          return SDL.SDL_APP_FAILURE
      }
      return SDL.SDL_APP_CONTINUE
  }
  ```
- **B. A string after `export`** (none of the six exactly; Jai's `#program_export "main"` is closest). One optional token in `parse_export`, which today checks for `export fn` followed by a name, lets chibi names follow chibi conventions and lets C names that are chibi keywords be exported.
  ```
  export "SDL_AppIterate" fn app_iterate(state: *void) -> SDL.SDL_AppResult {
      update_game()
      return SDL.SDL_APP_CONTINUE
  }
  ```
- **C. An attribute** (C3's `@export("x")`, Odin's `link_name`, ZenC's `@link_name`). It depends on whether chibi gets attributes at all; `@` is lexed and unused.
  ```
  // depends on attribute syntax, which is not designed
  ```

Callbacks that C calls:

- **D. Every chibi function uses the C convention** (C3, ZenC). A chibi function is then a valid C callback as written, and `fn(…) -> T` types are C function-pointer types. This requires that chibi's own convention never adds a hidden parameter, such as the implicit allocator context that memory-models.md leaves open (question 7), and that hot reload keeps the address C holds stable through a stub, a tiny fixed function that jumps to the current version (hypothesis I4). The checker rejects task functions and capturing `fn` literals where C expects a function pointer. SDL's `SDLCALL` is `__cdecl` only for Windows compilers that are not GCC-compatible, and empty elsewhere ([SDL_begin_code.h L405-L413](https://github.com/libsdl-org/SDL/blob/f0b89704e915bc0d678cec9efbf961cf979d1ba6/include/SDL3/SDL_begin_code.h#L405-L413)). On ARM and x64, Microsoft's docs say `__cdecl` "is accepted but typically ignored by the compiler" ([Microsoft](https://learn.microsoft.com/en-us/cpp/cpp/cdecl)), so modern targets have one C convention each.
  ```
  fn on_event(user: *void, event: *SDL.SDL_Event) -> bool {
      log_event(event)
      return true
  }

  SDL.SDL_AddEventWatch(on_event, nil)
  ```
- **E. A marker on C-callable functions and their types** (Zig's `callconv(.c)`, Odin's `proc "c"`, Jai's `#c_call`). It is needed only if chibi's own convention differs from C's, as Odin's does to pass its context. It costs a marker on functions and function types, and two function-pointer families that don't convert into each other.
  ```
  // hypothetical marker; no spelling is proposed
  ```
- **F. C by default, with an attribute for other platform conventions** (C3's `@callconv`). It is useful only for 32-bit Windows `stdcall` or special SIMD conventions, which none of chibi's targets need today.
  ```
  // depends on attribute syntax, which is not designed
  ```

#### Recommendation

**I recommend A and D,** because the decided default already gives every exported function its C name and exports are few, and because chibi functions should use the C convention everywhere, which hypotheses I1 and I2 already assume and which makes every non-task function a valid callback with no marker. Add B, at one token, when a real export needs a different chibi name, and revisit E only if chibi adopts an implicit context parameter.

#### Questions for you

1. Keep the chibi name as the C name (A), or allow `export "c_name" fn` (B) now?
2. Do all chibi functions use the C convention (D), which rules out a hidden context parameter?
3. Which types may appear in an `export fn` signature: only C-representable ones, or also slices, mapped to a generated `{ ptr, len }` struct in the header?
4. Should `export` also apply to global variables, as Zig's `export var` and Odin's `@(export)` do? Today the parser accepts only `export fn`.
