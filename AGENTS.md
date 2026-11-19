Be candid and thoughtful. Tell Charles what they need to hear, while staying curious and caring.
Write plainly in active voice. Minimize hedging, padding, and grand claims. Favor specific and concise responses.

1. Never write any comments in the code.
   The human will review the code and write any comments themselves.
   If you change any code that invalidates an existing comment, then
   you may update comments.

2. Write imperative, procedural code.

3. Never commit or push unless explicitly told to do so.

## Code Conventions

- No defensive NULL checks. Let invalid lifetime crash. Use `assert` for invalid state.
- Only do the work the problem requires. No allocation, copy, hash, string building, re-parse, repeated traversal, dynamic dispatch or layer that the problem doesn't need.
- Bound growth first. Assume any size a user controls reaches at least a thousand times the expected size.
- When optimizing, consider the following in this order:
  1. Work that doesn't need to happen
  2. Serial dependency chains
  3. Unpredictable branches
  4. SIMD Width
  5. Bytes moved through the caches
  6. Multithreading
- Keep per-element loops free of allocation, hashing, string comparison, map insertion, and parsing.
- Gather loop-invariant data once before the loop, build lookup tables once per process, reserve capacity before appending, and reuse large buffers across frames and files.
- Specialize to what the caller knows (static data, a fixed direction, bounded counts, a closed set of kinds).
- Precompute a representation shaped for the query when data is read more often than it changes.
- Return only what the caller needs, and encode "absent" in a sentinel when it's free.
- Handle the common case on the main path. Detect rare cases and send them down the slow path.
- Keep a path to fast code open. Instead of a per-object heap graph, use a flat array. Instead of a per-item call, use a batch. Avoid dispatch that blocks inlining in a hot loop.
- Give each closed set of kinds one definition. An `enum` or named constants declared once.
- Names state the capability. Skip filler words such as Manager, Data, Info, Util, Helper and Misc, and don't repeat the package name. Wider scopes get longer names; short names stay local.
- Comments should state contracts and reasons. The code says what it does.

## Comments

- By default, never write comments.
- A comment is warranted when the "why" isn't obvious, and the code's behavior would surprise a competent reader of this code.
- Keep comments one line whenever possible.
- Use ASD-STE100 when writing comments is necessary.

## Whitespace

A blank line is a paragraph break. Use it to show which lines belong together.

- Separate functions, and separate `#include`s from code.

  ```c
  // bad
  #include <string.h>
  static inline bool
  byte_is_ws(u8 c) { ... }
  static inline u64
  restart_at(u64 starts, u32 r, u8 byte) { ... }

  // good
  #include <string.h>

  static inline bool
  byte_is_ws(u8 c) { ... }

  static inline u64
  restart_at(u64 starts, u32 r, u8 byte) { ... }
  ```

- Separate the phases of a block: setup, work, result.

  ```c
  // bad
  u32 rest = (u32)__builtin_ctzg(~next_ident, 64);
  u64 lo, hi;
  memcpy(&lo, last, 8);
  memcpy(&hi, &last->length, 8);
  last->length = length + (continues ? rest : 0);

  // good
  u32 rest = (u32)__builtin_ctzg(~next_ident, 64);

  u64 lo, hi;
  memcpy(&lo, last, 8);
  memcpy(&hi, &last->length, 8);

  last->length = length + (continues ? rest : 0);
  ```

- Separate values that come from different sources.

  ```c
  // bad
  Token *t = &out[name_tokens[i]];
  const char *s = src + t->start;
  const NameSlot *slot = &slots[hash & mask];

  // good
  Token *t = &out[name_tokens[i]];
  const char *s = src + t->start;

  const NameSlot *slot = &slots[hash & mask];
  ```

- Keep a value together with the check on it.

  ```c
  // bad
  u32 id = names_intern(&lx->names, keywords[i], len);

  assert(id == i + 1);

  // good
  u32 id = names_intern(&lx->names, keywords[i], len);
  assert(id == i + 1);
  ```

- Keep a run of related statements together. Split it only where the topic changes.

  ```c
  // bad
  u8x16 v0 = simd_load(p);

  u8x16 v1 = simd_load(p + 16);

  u8x16 n0 = simd_load(p + 1);
  u8x16 n1 = simd_load(p + 17);

  // good
  u8x16 v0 = simd_load(p);
  u8x16 v1 = simd_load(p + 16);

  u8x16 n0 = simd_load(p + 1);
  u8x16 n1 = simd_load(p + 17);
  ```

## Parameter Order

- Put an argument that many functions share in the same position, first.

  ```c
  // bad
  fail(msg, src, pos);
  escape_len(q, src);
  closing_quote(open, src);

  // good
  fail(src, pos, msg);
  escape_len(src, q);
  closing_quote(src, open);
  ```

- Put the target before the values. A new optional argument can then be added without touching existing calls.

  ```c
  // bad
  names_intern(text, len, &lx->names);

  // good
  names_intern(&lx->names, text, len);
  ```

- Put an argument that is often omitted or defaulted last. The less likely it is to be set, the later it goes.

  ```c
  // bad
  lexer_tokenize(lx, keep_comments, src, out);

  // good
  lexer_tokenize(lx, src, out, keep_comments);
  ```

## Function Order

- Define a function before the functions that call it, so every name is defined before it is used. This matches C and the order the lexer already uses.

  ```c
  // bad
  int lexer_tokenize(...);                     // calls block_masks
  static inline BlockMasks block_masks(...);

  // good
  static inline BlockMasks block_masks(...);
  int lexer_tokenize(...);                     // calls block_masks
  ```

- Sort functions at the same depth in the order their caller calls them. `block_masks` calls `is_ident`, `is_ws` and `pair_firsts`, in that order.

  ```c
  // bad
  pair_firsts(...);
  is_ws(...);
  is_ident(...);
  block_masks(...);

  // good
  is_ident(...);
  is_ws(...);
  pair_firsts(...);
  block_masks(...);
  ```

- A function with several callers stands alone. Put it before its first caller.

  ```c
  // bad
  escape_len(...);     // calls fail
  fail(...);           // also called by closing_quote and slow_token
  closing_quote(...);

  // good
  fail(...);
  escape_len(...);
  closing_quote(...);
  ```
