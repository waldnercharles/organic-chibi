# Syntax options: methods, generics, compile-time code and reflection

Written 2026-11-10. This file holds option sheets 5 to 8 for chibi-v2's syntax (methods, generics, compile-time code and reflection) and a closing section that shows the recommended answers working together in one program. Each sheet compares Zig, Odin, Jai, C3, Carbon and ZenC, lays out the options with their parsing, compile-speed and hot-reload costs, recommends one, and ends with questions that each ask for one decision. Every chibi example uses the forms settled in [decisions.md](../decisions.md) as of 2026-11-10, including the module forms (`package`, `import` as an expression, `pub`, `export fn`), plus the new forms that its option proposes or that another sheet here recommends; names such as `type_info`, `field` and `fmt.Builder` stand for library functions that don't exist yet. The four sheets share two choices, so each is made once. Sheet 7 holds the question of the compile-time marker, and the other sheets write `$` for it, while sheet 5 holds the receiver rules that sheets 6 and 8 reuse. Sheet 3 ([syntax-options-data.md](syntax-options-data.md)) recommends replacing the keywords `sizeof`, `alignof`, `offsetof` and `typeof` with the plain names `size_of`, `align_of`, `offset_of` and `type_of`, so this file writes those names, and sheet 8's recommendation is the same choice extended to the rest of reflection. The evidence comes from [comptime-generics-reflection.md](comptime-generics-reflection.md) (called "the research note" below, with its measurement IDs such as M18) and from section 6 of [metaprogramming-reload-coroutines.md](metaprogramming-reload-coroutines.md). Jai has no public documentation, so Jai facts come from the owner's copies of community books and wikis in `~/Personal/jai-refs` and are marked "(community source)"; anything else I couldn't confirm is marked "unverified".

## Contents

- [5. Methods](#5-methods)
- [6. Generics](#6-generics)
- [7. Compile-time code](#7-compile-time-code)
- [8. Reflection](#8-reflection)
- [How sheets 5–8 fit together](#how-sheets-58-fit-together)
- [Not verified](#not-verified)

---

## 5. Methods

A method is a function called with a dot on a value, as in `p.len()`. The value before the dot is the *receiver*, and it becomes the function's first argument. chibi has to decide whether methods exist, where they are declared, and how the receiver is written. The answer matters more than usual, because the owner wants compile-time code to create types that carry methods, because hot reload patches each function through a slot found by the function's stable name, and because `export fn` has to give every exported function a plain C name.

### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | Functions declared inside the struct. A function whose first parameter is the struct can be called with a dot, so `v1.dot(v2)` means `Vec3.dot(v1, v2)`: `const Vec3 = struct { x: f32, …, pub fn dot(self: Vec3, other: Vec3) f32 { … } };`. `list.append(x)` passes `&list`, because `append` takes `self: *Self`. | [langref 0.15.2, struct](https://ziglang.org/documentation/0.15.2/#struct), [test_structs.zig](https://github.com/ziglang/zig/blob/0.15.2/doc/langref/test_structs.zig), [array_list.zig](https://github.com/ziglang/zig/blob/0.15.2/lib/std/array_list.zig) |
| Odin | No methods and no uniform call syntax; only procedures, such as `vec_len :: proc(v: Vec3) -> f32`. `x->y(123)` means `x.y(x, 123)` and exists for procedures stored in vtables. | [FAQ](https://odin-lang.org/docs/faq/), "Why does Odin not have any methods?" and "Why does Odin not have Uniform Function Call Syntax (UFCS)?"; [overview](https://odin-lang.org/docs/overview/), "`->` operator (selector call expressions)" |
| Jai | No member functions. A procedure declared inside a struct only uses the struct as a namespace, so `Obj.set_x(*o, 100)` works and `o.set_x(100)` is an error (community source). | `The_Way_to_Jai/book/12A_Basics_of_structs.md` L749-L771 |
| C3 | Declared outside the struct and qualified by its type: `fn void Point.add(Point* p, int x)`. `fn int Foo.test(&self)` takes a non-null pointer and `fn int Bar.test(self)` a value. Called as `p.add(10)` or `Point.add(&p, 10)`. Built-in types can get methods (`fn int int.add(int i, int other)`), and a method can't share a member's name. | [functions.md, "Methods"](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/language-fundamentals/functions.md) |
| Carbon | Declared in the class with `self` inside the parentheses: `fn Diameter(self) -> f32`, `fn Expand(ref self, distance: f32);`, optionally defined later as `fn Circle.Expand(ref self, distance: f32) { … }`. Called as `c.Diameter()` or `Circle.Diameter(c)`. PR #7016, merged 2026-05-13, moved `self` out of `[self: Self]`. Interface methods come from `impl Song as Comparable { … }`. | [classes.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/classes.md), "Methods"; [PR #7016](https://github.com/carbon-language/carbon-lang/pull/7016); [generics/overview.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/generics/overview.md) |
| ZenC | `impl` blocks: `impl Point { fn dist(self) -> float { return sqrt(.x * .x + .y * .y); } }`, where `.x` means `self.x` and the generated C parameter is `Point* self`. Also `impl int { … }`, and traits as `trait Drawable { fn draw(self); }` with `impl Drawable for Circle { … }`. | [09-oop.md](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/09-oop.md); `patch_self_args` in [decl_function.c](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/decl/decl_function.c) |

### Options

**Where methods are declared**

- **A. Inside the struct** (Zig; Carbon declares them there and may define them later). The struct body lists fields and functions together, so everything about a type is in one place, and a function that returns a struct literal hands back its methods with it, which is how Zig gives generic types methods. The parser needs one more case in a struct body: `fn` followed by a name starts a method, while `fn` followed by `:` is still a field named `fn` (keywords may be member names), so one token of lookahead (peeking at the next token before deciding) is enough. The costs are that methods bury the field list in a language whose structs use C layout and may get a padding warning, and that a type built from data, such as an enum made from a table, has no body to hold methods. That is Zig's limit, because a type built with `@Type` may not have declarations (research note §3.1). For hot reload, each method still has to be its own declaration with its own stable ID, so that editing a method body doesn't look like a change to the layout of the struct around it. An `export fn` inside a struct needs a rule for its C name.
  ```
  struct Point {
      x: f32
      y: f32

      fn len(self: Point) -> f32 => sqrt(self.x * self.x + self.y * self.y)

      fn scale(self: *Point, k: f32) {
          self.x *= k
          self.y *= k
      }
  }
  ```
- **B. Outside the struct, named `Type.method`** (C3; Carbon's spelling for definitions). A method is a top-level function whose name is qualified by its type, and `p.len()` finds it by looking up `len` among `Point`'s members. The struct body stays a pure layout. Any type with a name can get methods, including one that compile-time code built and bound with `const Particles := Soa(Particle, 1024)`, which is the case option A can't serve. The declaration spells the full name, `Point.len`, which is also the name a debugger shows, the key of the function's hot-reload slot (development builds call every function through a table of slots, so a reload swaps one entry) and the base of its C name, and editing a method never touches the struct's declaration. The parser needs two tokens of lookahead after `fn` (a name, then `.`) and one node for the receiver's type, all decided before anything is written out. The declaration pass registers each method in its type's member table, so a lookup of `p.len` is one hash probe and never waits for code to run. For a generic type, the receiver binds the type's parameters with sheet 6's pattern, `self: *List($T)`, so one declaration serves every instance. The costs are that the type name repeats on every method and that methods can sit anywhere in their package. A rule also has to stop other packages from adding methods (question 4), because otherwise what `x.f` means would depend on what the calling file imports.
  ```
  struct Point {
      x: f32
      y: f32
  }

  pub fn Point.len(self: Point) -> f32 => sqrt(self.x * self.x + self.y * self.y)

  pub fn Point.scale(self: *Point, k: f32) {
      self.x *= k
      self.y *= k
  }

  fn demo() {
      p := Point.{ .x = 3, .y = 4 }
      p.scale(2)
      n := Point.len(p)
  }
  ```
- **C. `impl` blocks** (ZenC; Rust). Methods are grouped under `impl Point { … }`, and ZenC also writes `impl Drawable for Circle { … }` to implement a trait (a trait, or interface, is a named list of methods that a type promises to have). Grouping reads well, and a block for a generic type could bind its parameters once, `impl List($T) { … }`. It costs a keyword and a second level of braces, and without traits, which sheet 6 recommends against (its option H), the block only groups what option B already names. ZenC's version also makes the receiver an implicit pointer and lets `.x` stand for `self.x`; `.x` is already chibi's enum literal, so that shorthand can't come along. Compile-time-created types, hot reload and export work as in B.
  ```
  impl Point {
      fn len(self: Point) -> f32 => sqrt(self.x * self.x + self.y * self.y)

      fn scale(self: *Point, k: f32) {
          self.x *= k
          self.y *= k
      }
  }
  ```
- **D. No methods, only functions** (Odin, Jai). Odin's FAQ argues that data and code should stay separate concepts, and Jai treats procedures inside a struct only as a namespace (community source). Nothing new is parsed, no receiver rule or automatic `&` is needed, and an exported function's C name is simply its name. The cost is that call sites spell namespaces as prefixes, `point_scale(&p, 2)`, and an editor can't list a value's operations after a dot. Types created at compile time get "methods" only as functions that take them.
  ```
  pub fn point_len(p: Point) -> f32 => sqrt(p.x * p.x + p.y * p.y)

  pub fn point_scale(p: *Point, k: f32) {
      p.x *= k
      p.y *= k
  }

  fn demo() {
      p := Point.{ .x = 3, .y = 4 }
      point_scale(&p, 2)
  }
  ```
- **E. Uniform function call syntax, or UFCS** (D, [spec](https://dlang.org/spec/function.html#pseudo-member); Nim, [method call syntax](https://nim-lang.org/docs/manual.html#procedures-method-call-syntax)). Any function can be called with a dot: when `x` has no field `f`, `x.f(y)` means `f(x, y)`. It needs no new syntax and gives every type, generated ones included, methods for free. Odin's FAQ rejects it because `x.f` then collides with a field named `f`. The deeper cost for chibi is that the meaning of `x.f()` depends on which functions the calling file can see, so two files can resolve the same call to different functions, and a reader can't find the definition from the type. It changes nothing for parsing, hot reload or export.
  ```
  pub fn len(p: Point) -> f32 => sqrt(p.x * p.x + p.y * p.y)

  fn demo(p: Point) -> f32 => p.len()
  ```

**How the receiver is written**

- **F. The receiver is an ordinary first parameter** (Zig; C3's long form; every Odin and Jai procedure). `self: Point` takes a copy, `self: *Point` takes a pointer, and the name `self` is only a convention. A call `p.scale(2)` passes `&p` when the receiver is a pointer and `p` is a variable, field or element, and passes `p.*` when `p` is a pointer and the receiver is a value, as Zig does with `list.append(x)`. Nothing new is parsed, because the settled parameter forms already cover it, including an untyped receiver, which by the settled rule takes whatever value the call passes. A generic receiver uses sheet 6's pattern. The method's type is its written signature, `fn(*Point, f32)`, which is also its C signature.
  ```
  pub fn Point.scale(self: *Point, k: f32) {
      self.x *= k
      self.y *= k
  }

  pub fn List.push(self: *List($T), x: T) {
      self.items[self.len] = x
      self.len += 1
  }
  ```
- **G. `self` with an implied type, and a mark for pointers** (C3's `self` and `&self`; Carbon's `self` and `ref self`). `fn Point.len(self)` means `self: Point`, and `fn Point.scale(&self, k: f32)` means `self: *Point`. It's shorter, and whether a method mutates shows in its signature. But `&self` reads backwards in chibi, where `&x` makes a pointer rather than declaring one; an untyped `self` would mean "the receiver's type" while every other untyped parameter means "any type"; and the parser needs a special case for `&` at the start of a parameter, or a `ref` keyword.
  ```
  pub fn Point.len(self) -> f32 => sqrt(self.x * self.x + self.y * self.y)

  pub fn Point.scale(&self, k: f32) {
      self.x *= k
      self.y *= k
  }
  ```
- **H. An implicit receiver** (ZenC, whose generated C turns `self` into `Point* self`; C++'s `this`). The receiver is always a pointer and its type is never written, and C++ also lets a bare field name stand for `this->x`. It's the shortest form, but nothing shows whether a method mutates, a method's function type differs from its written parameters, and bare field names make every name lookup inside a method check the fields first. ZenC's `.x` shorthand collides with chibi's enum literals.
  ```
  impl Point {
      fn scale(k: f32) {
          self.x *= k
          self.y *= k
      }
  }
  ```

### Recommendation

**I recommend B with F: methods declared outside the struct as `pub fn Point.len(self: Point)`, in the same package as the type, with the receiver written as an ordinary first parameter and the call adding `&` or `.*` to match it,** because B is the only form that gives methods to every kind of type compile-time code can create, including types built from data; it needs no keyword, unlike C, and no receiver rules of its own, unlike G and H; its declaration states the full name that hot reload and export use; and the struct body stays a readable layout. Keeping a type's methods in its own package means `x.f` is resolved from the type alone, during the declaration pass, which is what makes a method lookup one hash probe and lets sheet 8's `has_method(T, "write")` give the same answer in every analysis order. For `export`, sheet 10b's option of a C name written after `export` ([syntax-options-effects.md](syntax-options-effects.md)) would let a method name its symbol directly, as in `export "point_len" fn Point.len(self: *Point) -> f32`, so question 5 only matters when no name is given. The examples in this file assume that `.` reaches a field through a pointer (`self.x` with `self: *Point`), as in Zig, Odin, Jai, C3 and ZenC; Carbon writes `->`, and chibi hasn't settled it.

### Questions for you

**Answered 2026-11-11** in the parser thread, against the recommendation above. The Methods row of [../decisions.md](../decisions.md) is the authority; in this sheet's terms:
- Question 1: no method declarations (D). Any function can be called with a dot instead (UFCS, option E), C functions from headers included: `device.create_buffer(10)` calls `create_buffer(&device, 10)`.
- Questions 2 and 3: yes. The receiver is the ordinary first parameter (F), and the call adds `&` or `.*` to match it.
- Question 4 doesn't arise in this form, because no function belongs to a type. Proposed: `x.f()` looks in the calling package first, then in the package that declares `x`'s type.
- Question 5, proposed: an overloaded exported function needs its own C name.
- Question 6: a dot call that could mean a field or a function is an error, and a function can't share a field's name.
- Beyond the questions: functions may be overloaded on every parameter, and only exact matches count. There is no widening and no ranking, and a literal fits any type that holds its value. Numeric families such as `abs(i32)` and `abs(f32)` wait for generics (sheet 6), because `abs(-5)` would match several of them. `.` reaches a field through a pointer (`p.x`), which settles the open point in the recommendation.

1. Are methods declared outside the struct as `fn Point.len(...)` (B), inside it (A), in `impl` blocks (C), or not at all (D)?
2. Is the receiver an ordinary first parameter with its type written out (F)?
3. Does `p.scale(2)` pass `&p` automatically when the receiver is a pointer, and `p.*` when `p` is a pointer and the receiver is a value?
4. Must a method be declared in the same package as its type, so that no package can add methods to `int` or to a type from a C header?
5. When no C name is given, what C name does `export fn Point.len` get: `Point_len`, with two exports of one C name reported as an error, or is `export` on a method an error?
6. Do a type's fields and methods share one set of names, so that a method can't be named like a field (C3's rule)?

---

## 6. Generics

A generic is a function or type written once with some parameters left open, and the compiler makes an *instance* (a filled-in copy) for each distinct set of arguments. chibi already has the simplest kind, because an untyped parameter accepts any type, so what remains is how to name a type, how to pass types and compile-time values explicitly, how to write a generic struct and its instances, and whether constraints and argument rewriting exist. The research note recommends checking each instance separately (its option G1) and keying instances by interned IDs, and the syntax shouldn't make either more expensive.

### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | `fn max(comptime T: type, a: T, b: T) T`; a generic type is a function returning a struct, `fn List(comptime T: type) type { return struct { items: []T, … }; }`; `anytype` with `@TypeOf` for deduced types; instance `List(i32)`. | [langref, Compile-Time Parameters](https://ziglang.org/documentation/0.15.2/#Compile-Time-Parameters), [Generic Data Structures](https://ziglang.org/documentation/0.15.2/#Generic-Data-Structures) (local copy `/tmp/zig-langref.html` L3861-L3923, L4071-L4099) |
| Odin | `my_new :: proc($T: typeid) -> ^T`; `Table_Slot :: struct($Key, $Value: typeid)`; specialization `make_slice :: proc($T: typeid/[]$E, len: int) -> T`; `foo :: proc(x: [$N]int) -> bool where N > 2 {`; instance `Table_Slot(string, int)`. | [overview, Parametric polymorphism](https://odin-lang.org/docs/overview/#parametric-polymorphism) and "`where` clauses" |
| Jai | `repeat :: (element: $T, count: s64) -> (output: [] T)`; value parameters `proc :: ($a: int) -> int`; `Vec :: struct($T: Type, $N: s64)`; `discuss :: (x: $T/interface Matchable)`; `#modify { T = s64; return true; }`; instance `Vec(float, 3)` (community source). | `22A_Polymorphic_procedures.md` L109-L121, L464-L520; `23A_Polymorphic_arrays_and_structs.md` L52-L226; `26B_Macros.md` L796-L880 |
| C3 | `struct MyStruct <Type> { Type a, b; }`, `fn Type square(Type t) <Type> { return t * t; }`, whole sections `module my_module <Type>;`; instances `Foo{int, 3} g;` and `alias FooFloat = Foo {float, 3};`; `@require` contracts checked per instance. Release 0.7.0 changed `Foo(<int>)` to `Foo {int}`, and 0.7.9 moved declarations to ad-hoc `<...>`. | [generics.md](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/generics.md); [releasenotes.md](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/releasenotes.md) |
| Carbon | Deduced parameters in brackets, `fn PrintIt[T: Printable](p: T*)`; class parameters `class Wrapper(T: type)`; constraints joined with `&` and `where .Element = i32`; `template T: type` checks per instance; instance `Vector(T)`. Proposal p7254 removed `:!`. | [generics/overview.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/generics/overview.md); [p7254](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/proposals/p007254-replace-and-with-keywords-and-contextual-defaults.md) |
| ZenC | `fn identity<T>(val: T) -> T`, `struct Box<T> { item: T; }`, `impl MyClone for GBox<T> { … }`, instance `GBox<int>{ val: 10 }`. To tell `<` apart, the parser scans ahead with `tokens_could_be_generic_args`, parses the types speculatively, and checks a table of known templates. | [10-generics.md](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/10-generics.md); [expr_helpers.c](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/expr/expr_helpers.c), [expr_primary.c](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/parser/expr/expr_primary.c); [test_generic_traits.zc](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/tests/language/generics/test_generic_traits.zc) |

### Options

**How parameters and instances are written**

- **A. `$` declares compile-time names, and instances are written like calls** (Jai, Odin; Carbon also writes instances as `Vector(T)`). `$T` in a parameter's type binds `T` to the type of that argument, so `fn max(a: $T, b: T) -> T` deduces `T` from the call (deducing means inferring it from the arguments). `$T: type` is a compile-time parameter passed explicitly, as in `make(int, 16)`, and `$N: int` is a compile-time value. A generic struct takes the same parameter list, `struct Ring($T: type, $N: int)`, and an instance is written like a call, `Ring(f32, 64)`, so `Ring(f32, 64).{}` already parses today as a call followed by a literal; named arguments work unchanged, `Ring(.T = u8, .N = 16)`. No token or keyword is added (`$` is TK_DOLLAR and `type` is TK_TYPE), and the parser decides each form at the `$`, before it writes any node. That matters because chibi's parser writes each node after its children (postorder) and never goes back, so a form decided late would force re-tagging, which means changing the kind of a node already written. Two parser changes are needed. `parse_type` must accept a call after a possibly qualified name, as in `x: geo.Ring(f32, 64)`. And a composite type such as `[]int` or `*Node` must become an ordinary operand, because type arguments are ordinary arguments, and today `make([]int, 16)` fails with "expected `.{` or `.[` after a type" while `List(*Node)` fails with "use `.*` to dereference". That hint would move from the parser to the checker. The instance key is the generic's stable ID plus the interned IDs of its `$` arguments and bound types, which is one hash probe (H10).
  ```
  fn max(a: $T, b: T) -> T {
      if a > b {
          return a
      }
      return b
  }

  struct Ring($T: type, $N: int) {
      items: [N]T
      head: int
      len: int
  }

  fn demo() {
      m := max(3, 4)
      r := Ring(f32, 64).{}
      q := Ring(.T = u8, .N = 16).{}
  }
  ```
- **B. `comptime` parameters and functions that return `type`** (Zig). Every compile-time parameter is written `comptime T: type`, types are passed explicitly, as in `max(i32, a, b)`, and a generic type is a function that returns a struct literal. It's one mechanism for everything, and a type function can compute its layout. It costs a keyword, longer signatures and explicit type arguments; Zig avoids the last with `anytype` and `@TypeOf`, which chibi's untyped parameters already cover. It also needs an anonymous `struct { … }` as an expression, and an identity for each returned struct that survives edits, which Zig gets by keying such a type on the struct literal's instruction plus the values it captures and by tracking those instructions across edits with a map per shard (research note §3.1). Parsing is as cheap as A, with the same change for composite types.
  ```
  fn max(comptime T: type, a: T, b: T) -> T {
      if a > b {
          return a
      }
      return b
  }

  fn Ring(comptime T: type, comptime N: int) -> type {
      return struct {
          items: [N]T
          head: int
          len: int
      }
  }

  fn demo() {
      m := max(int, 3, 4)
      r := Ring(f32, 64).{}
  }
  ```
- **C. Angle brackets** (ZenC; C3 declarations; C++, Rust and Java). `fn max<T>(a: T, b: T) -> T`, `struct Ring<T, N: int>` and instances `Ring<f32, 64>` look familiar, and in declarations and type positions (after `:` or `->`) they're unambiguous. In expressions they aren't, and banning chained comparisons removes only part of the problem. Four costs remain.
  1. With one argument, `f<int>(x)` can't be a comparison, because `f < int > (x)` would be a chained comparison, which is already an error. But the parser must still choose at the `<` whether a type or an expression follows, because `[]int`, `*Node` and `fn(int) -> int` don't parse as expressions, and `a < b` and `a<b>(c)` begin with the same tokens. So it has to scan ahead to the matching `>` before writing a node, and that scan has no fixed length, as in `f<fn([]Map<K, V>) -> int>(g)`. ZenC runs such a scan with no depth limit and then parses the types speculatively (parsing tentatively and starting over if it fails), so the same tokens are parsed twice, which is the work chibi's single-pass parser was built to avoid.
  2. With two or more arguments inside any comma list (call arguments, literal items, `m[i, j]` indices, switch labels, array dimensions), both readings are valid: `f(a < b, c > (d))` is either two comparisons or one call of the instance `a<b, c>`. Go's proposal rejected `F<T>` for this reason, using the statement `a, b = w < x, y > (z)`, which can't be parsed without knowing whether `w` is generic ([design doc](https://github.com/golang/proposal/blob/83ee2aaf9fdcc1a1cf4bb4d20216fc979635cf04/design/43651-type-parameters.md), "Why not use the syntax F\<T\> like C++ and Java?"). chibi adds a case of its own through enum literals: in `f(a < b, c > .d)`, `.d` is either an enum value or a member of the instance `a<b, c>`. By contrast, `f(a < b, c > d)` has only one reading in chibi, because a name can't directly follow an instance. The ambiguity needs a token after `>` that can continue an instance: `(`, `.`, `.{`, `.[`, `[` or a binary operator.
  3. A value argument that contains `>` must be parenthesized, `Vec<(N > 2)>`, as in C++.
  4. Closing nested lists makes the parser split tokens. `List<List<int>>` ends in `>>`, and `x: List<int>= .{}` contains `>=`. chibi's lexer emits `>>`, `>=` and `>>=` as single tokens from its two-byte table, so the parser would have to consume half a token, and a node could no longer point at a whole token.

  ZenC settles what is left by consulting the table of templates declared so far (`ctx->templates`), which makes a file's parse depend on declarations parsed before it, so files can no longer be parsed independently and in parallel. The research note's front-end profile names `<` as the classic case of syntax that adds branches or backtracking to every expression step.
  ```
  fn max<T>(a: T, b: T) -> T {
      if a > b {
          return a
      }
      return b
  }

  struct Ring<T, N: int> {
      items: [N]T
      head: int
      len: int
  }

  fn demo() {
      m := max<int>(3, 4)
      r := Ring<f32, 64>.{}
  }
  ```
- **D. Square brackets** (Go for all generics; Carbon for deduced parameters). `fn max[T: type](a: T, b: T) -> T`, `struct Ring[T: type, N: int]`, and instances `Ring[f32, 64]` and `max[int](3, 4)`. In expressions, `Ring[f32, 64].{}` parses today as an index followed by a literal, and the checker tells an instance from an index by what the name is, which is Go's approach. In types, `x: Ring[f32, 64]` is unambiguous, because chibi's array types are prefix (`[N]T`), so a `[` after a type name can't start one. Type parameters stay visibly apart from value parameters. The costs are a second parameter list in every generic declaration, instances that share their shape with indexing (as A's instances share theirs with calls), and the same change for composite types as A, for `Ring[[]u8, 4]`. The settled rule that a `[` at the start of a line starts a new statement still holds.
  ```
  fn max[T: type](a: T, b: T) -> T {
      if a > b {
          return a
      }
      return b
  }

  struct Ring[T: type, N: int] {
      items: [N]T
      head: int
      len: int
  }

  fn demo() {
      m := max(3, 4)
      r := Ring[f32, 64].{}
  }
  ```
- **E. Angle brackets to declare and braces to instantiate** (C3, which changed `Foo(<int>)` to `Foo {int}` in 0.7.0 and moved declarations to `<Type>` in 0.7.9). Braces after a name can't work in chibi, because `if`, `while` and `switch` take their condition without parentheses, so in `if ready { go() }` the parser couldn't tell an instance of `ready` from a condition followed by a block. C3 avoids the clash by requiring parentheses around conditions.
  ```
  fn max(a: Type, b: Type) -> Type <Type> {
      if a > b {
          return a
      }
      return b
  }

  fn demo(ready: bool) {
      m := max{int}(3, 4)
      if ready { go() }
  }
  ```

**Constraints** (a constraint is a condition a generic's arguments must meet, such as "is a number")

- **F. A `where` clause, checked per instance** (Odin). A compile-time condition after the signature is checked for each instance before its body, and Odin's error names the clause and the bound types (research note §3.3). The condition sits in the signature, where readers and editors see it. It costs a keyword, or a contextual keyword recognized only between the return type and the body, which moves a decision the lexer makes for free into the parser.
  ```
  fn sum(xs: []$T) -> T where is_number(T) {
      total: T = 0
      for x in xs {
          total += x
      }
      return total
  }
  ```
- **G. A compile-time assertion in the body** (Zig uses `@compileError` or a comptime `assert`; Rust's `const { assert!(…) }` fails only in particular instances; C3's `@require` contracts are also checked per instance). With per-instance checking this does the same work as F, and it reuses sheet 7's `$assert`, so nothing new is parsed. The error points into the generic, with a note naming the call that instantiated it, instead of pointing at the call.
  ```
  fn sum(xs: []$T) -> T {
      $assert(is_number(T), "sum needs a number type")
      total: T = 0
      for x in xs {
          total += x
      }
      return total
  }
  ```
- **H. Constraints checked once** (Carbon's `[T: Ordered]`; option G2 in the research note). The body is checked once against declared interfaces, so each instance stores only its parameter-dependent values. That needs interfaces, impls and coherence rules, and it can't check code whose layout a compile-time function chooses, so it conflicts with the per-instance checking the research note recommends for now. The example transcribes Carbon's form; `interface` and `Self` are not chibi forms.
  ```
  interface Ordered {
      fn less(a: Self, b: Self) -> bool
  }

  fn max[T: Ordered](a: T, b: T) -> T {
      if T.less(b, a) {
          return a
      }
      return b
  }
  ```

**Canonicalizing arguments** (rewriting arguments into one standard form, so that different spellings share one instance)

- **I. Interned argument IDs only** (Zig, Rust, Carbon). Arguments are evaluated first, then types and values are interned (stored once and named by a small integer), and the instance key is the tuple of IDs. So `Ring(f32, CAP)` and `Ring(f32, 2 * 32)` share one instance when `CAP` is 64. Every option needs this base.
  ```
  const CAP := 64

  fn demo() {
      a := Ring(f32, CAP).{}
      b := Ring(f32, 2 * 32).{}
  }
  ```
- **J. Forward to a canonical instance** (Zig's `ArrayList(T)` returns `Aligned(T, null)`, research note §3.1; the "generic shell, non-generic core" split, M18). A thin generic wrapper converts its arguments and names one shared instance. It needs no syntax and stays visible in the code, and M18 measured debug builds up to 5.7% faster after one such split in Rust's `RawVec`.
  ```
  struct AlignedArray($T: type, $ALIGN: int) {
      items: []T
      len: int
  }

  fn Array($T: type) -> type => AlignedArray(T, 0)
  ```
- **K. A rewrite hook before instantiation** (Jai's `#modify`, community source). A compile-time block between the signature and the body may change the bound `T` or reject the call, so `proc1(var_s8)` and `proc1(var_s64)` share the `s64` instance. It caps instance counts without a wrapper, but the hook runs before every lookup, the instance a call uses is no longer the one it spells, and chibi would need a block inside the signature. The spelling below is hypothetical.
  ```
  fn print_int(x: $T)
  $modify {
      T = i64
  }
  {
      write_i64(x)
  }
  ```

### Recommendation

**I recommend A for parameters and instances, G for constraints, and I with J for canonicalizing: `$T` patterns and `$N: int` parameters, `struct Ring($T: type, $N: int)` with instances `Ring(f32, 64)`, functions with `$` parameters that return `type` where code must choose the layout, `$assert` for constraints, and no rewrite hook,** because A adds no token and decides every form at one token, it's how Jai and Odin already write generics in C-speed game languages, `Ring(f32, 64).{}` already parses, and it composes with untyped parameters and with sheet 7's compile-time functions (a type function is just a function with `$` parameters that returns `type`, so B's power comes without B's syntax). Angle brackets would cost unbounded lookahead or speculative parsing in expressions plus token splitting, square brackets buy little over parentheses here, and constraints checked once would need interfaces that per-instance checking doesn't. A `where` clause can be added later, as a contextual keyword, if errors inside generics prove hard to read.

### Questions for you

1. Are generics written with `$` patterns and parameters, with instances written as calls, `Ring(f32, 64)` (A)?
2. Does a generic struct take its parameters in its header, `struct Ring($T: type, $N: int)`, with type functions (`fn Soa($T: type) -> type`) for layouts that code computes?
3. May composite types such as `[]int` and `*Node` appear as ordinary operands, so they can be passed as arguments, with the "use `.*` to dereference" hint moving to the checker?
4. Must a struct's parameters carry `$` even though they are always compile-time?
5. Are constraints written as `$assert` in the body (G) or as a `where` clause (F)?
6. Is canonicalizing left to forwarding (J), with no rewrite hook (K)?
7. Added 2026-11-11 after sheet 5's decision: how do generics interact with overloading? Overloads match exactly on every parameter, and numeric families were left for generics because `abs(-5)` would match `abs(i32)`, `abs(i64)` and `abs(f32)` alike. Does one generic `abs($T)` replace such a family? May a generic and a non-generic overload share a name, and which one does a call pick when both match exactly? What type does an untyped literal give `$T`?
8. Added 2026-11-11, when `-> LoadError!` was proposed for a written error set with no value: does generic code need a zero-size type such as Zig's `void`? Zig passes `void` as a map's value type "to make it into a Set", and then "the hash map entry type has no value field" (language reference 0.15.2). Go uses an empty struct instead: "A struct or array type has size zero if it contains no fields (or elements, respectively) that have a size greater than zero" ([spec](https://go.dev/ref/spec#Size_and_alignment_guarantees)). If an empty `struct {}` does the job, chibi may never need `void`. Answered in part on 2026-11-11: the owner chose to have `void`, and to require it after a written `->`, as in `-> !void` ([../decisions.md](../decisions.md), the `try` row). What remains for this sheet is whether a generic container drops a field whose type has size 0, so that `Map(string, void)` is a set with no value storage, as in Zig.

---

## 7. Compile-time code

Compile-time code is ordinary chibi run by the compiler's interpreter while it compiles, with its results (tables, types, checks) baked into the program. The owner has made it a defining feature: it creates types, those types have methods, it may call C, and chibi is its own build system. The syntax has to cover five jobs (running code, compile-time parameters, compile-time branches and loops, generating declarations, and the build program) with one marker and as few forms as possible, without breaking the order-free, parallel checker that the research recommends.

### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | `comptime` blocks, expressions and parameters; `inline for` and `inline while` unroll; an `if` or `switch` on a comptime-known value skips the untaken branch without a marker; expressions at container level are implicitly comptime. Types built with `@Type` (0.15), or `@Struct` and `@Enum` (0.16), can't carry declarations. The build is `build.zig`. | [langref, comptime](https://ziglang.org/documentation/0.15.2/#comptime), [inline for](https://ziglang.org/documentation/0.15.2/#inline-for), [Zig Build System](https://ziglang.org/documentation/0.15.2/#Zig-Build-System) (local L3854-L4070, L2767-L2790, L6611-L6646); research note §3.1 |
| Odin | No compile-time execution. `when ODIN_OS == .Linux { … }`; `FOO :: #config(FOO, false)`; `#load("logo.png")`; `#assert(…)`. Tables come from a generator program (`odin run tablegen`) plus `#load`. | [overview](https://odin-lang.org/docs/overview/), "Conditional compilation" and "Directives"; research note §3.3 |
| Jai | `#run proc1();` and `PIA :: #run compute_pi();`; `#if OS == .WINDOWS { … }`, also inside an enum body; `#insert "c := a + b;"`, `#insert #run gen_code(3);` and `#insert -> string { … }` inside a struct; `#code`; `#expand` macros; `#placeholder TRUTH;` filled by `add_build_string`; a `build.jai` metaprogram run by `#run build();` (community source). | `26A_Metaprogramming.md` L63-L160, L334-L460, L464-L700; `30A_Integrated_build_system.md` L1-L68, L200-L230; `JCL-wiki/Metaprogramming.md` L68-L124 |
| C3 | `$if $x > 3:` … `$endif`, `$for var $x = 0; $x < $a; $x++:` … `$endfor`, `$foreach $x : $SomeEnum::values:` … `$endforeach`, `$switch`; `$`-prefixed compile-time variables and parameters; `@const` macros fold to constants; `$embed`, `$exec` (needs `--trust=full`), and `$expand(string)` since 0.8.0. At top level only `@feat`, `@if` and `$assert` are allowed. Builds come from `project.json`, whose `exec` entries may run `.c3` scripts. | [compiletime.md](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/compiletime.md), [macros.md](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/macros.md), [project-config.md](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/build-your-project/project-config.md), [releasenotes.md](https://github.com/c3lang/c3c/blob/b2e1e99baaf2add3a40bd084823416a4437f3ec6/releasenotes.md) |
| Carbon | `eval` and `musteval` are reserved keywords, and `TryEvalCall` evaluates calls at compile time; the metaprogramming design is a placeholder that rules out textual preprocessing. | [token_kind.def L193](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/toolchain/lex/token_kind.def#L193); [metaprogramming.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/metaprogramming.md); research note §3.11 |
| ZenC | `comptime { yield("let x = 1;"); }` runs a tree-walking interpreter with a 1M-step limit and parses the text it emits; `@comptime fn double_ct(x: int) -> int` runs at both times; `compile_error`, `expect`; `@cfg(not(USE_VULKAN))` on top-level declarations; `embed "bios.bin" as u8[1024]`; `//> link: -lraylib -lm` build directives. | [13-comptime.md](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/13-comptime.md), [12-advanced.md](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/12-advanced.md); [packages.md](packages.md), "Zen C" |

### Options

**The marker** (one choice for sheets 6, 7 and 8)

- **A. `$`** (Jai and Odin for compile-time parameters; C3 for parameters, variables and `$if`, `$for` and `$switch`). It's already a token (TK_DOLLAR) and one character, and the parser decides every form from the `$` and the token after it: a name declares a compile-time name, `if`, `for` or `switch` makes that statement compile-time, and anything else is an expression evaluated at compile time. No keyword is added, and since `$` isn't a binary operator, a line that starts with `$` always starts a new statement under the line-break rule. Three of the six languages already read `$` as "compile time". The cost is a symbol-heavy look in code that is mostly metaprogramming.
  ```
  fn first(xs: []$T) -> T => xs[0]

  $assert(MAX_ENEMIES <= 4096)
  ```
- **B. `comptime`** (Zig, ZenC). A keyword reads as English and can prefix the same places: `comptime T: type`, `comptime if`, `comptime for`, `comptime` before an expression. It costs one keyword, which is free to recognize because keywords are interned before lexing starts, and eight characters at each use. It has no natural spelling for binding a type from an argument (A's `a: $T`); Zig doesn't offer that and uses `anytype` with `@TypeOf` instead. Zig itself uses two words for one idea (`comptime` and `inline for`), and ZenC uses `comptime` for blocks and `@comptime` for functions.
  ```
  fn first(comptime T: type, xs: []T) -> T => xs[0]

  comptime assert(MAX_ENEMIES <= 4096)
  ```
- **C. `#`** (Jai and Odin directives: `#run`, `#if`, `#insert`, `#assert`, `#load`, `#config`). Directives stand out, and they read like C's preprocessor, which is also the cost, because chibi has no textual preprocessor and `#if` suggests one. In today's lexer.h, `#` is not a token (the lexer reports "unexpected character" for it), while `$` and `@` are, so every `#` form needs a new token. And neither Jai nor Odin uses `#` for compile-time parameters (both use `$`), so `#` would mean two markers, or a parameter spelling that no language uses.
  ```
  fn first(xs: []$T) -> T => xs[0]

  #assert(MAX_ENEMIES <= 4096)
  ```

What each form costs the lexer, given today's lexer.h, where `$` (TK_DOLLAR), `@` (TK_AT) and `::` (TK_DCOLON) are tokens and `type`, `typeof` and `sizeof` are keywords:

| Form | Needs |
|---|---|
| `$T`, `$N: int`, `$if`, `$for`, `$switch`, `$assert(…)` | nothing new |
| `comptime T: type`, `comptime if`, `comptime for` | the keyword `comptime` (Zig also uses `inline`) |
| `#run`, `#if`, `#insert`, `#assert` | the token `#`, plus directive names read as identifiers after it |
| `where` clauses (sheet 6, F) | the keyword `where`, or a contextual one |
| `impl` blocks (sheet 5, C) | the keyword `impl` |
| `@type_info(T)` builtins (sheet 8, A) | nothing new |

Compile-time parameters are the same in every option below: sheet 6's `$T: type` and `$N: int`, spelled with whichever marker this group picks.

**Running code**

- **D. `const` runs its initializer at compile time, and `$` runs any other expression** (Zig, where expressions at container level are implicitly comptime and `comptime expr` forces the rest). `const SQUARES := squares(16)` runs `squares` in the interpreter with no marker, because a constant has to be known at compile time anyway. `$assert(N <= 256)` is `$` applied to an ordinary call, which makes a static assertion without a new form. The marker's naming rule also gives compile-time variables inside functions, `$offset := 0`, whose loads and stores all happen at compile time (Zig's `comptime var`). Any function may run at compile time, which is the only rule that fits the decision that compile-time code may call C, since a C function can't carry a chibi mark. Each evaluation runs on the interpreter over typed IR (option A2 in the research note), so a table of 65,536 entries should cost milliseconds rather than the seconds that copy-and-reanalyze designs pay (research note §7.1, an estimate that hypothesis C1 tests).
  ```
  fn squares($N: int) -> [N]int {
      t: [N]int
      for i in 0..N {
          t[i] = i * i
      }
      return t
  }

  const SQUARES := squares(16)

  $assert(SQUARES[15] == 225)
  ```
- **E. Mark every compile-time call** (Jai writes `PIA :: #run compute_pi();`, and a constant that calls a procedure without `#run` is an error; community source). It's explicit, at the cost of noise on the most common case, and `const` stops meaning "known at compile time" by itself.
  ```
  const SQUARES := $squares(16)
  ```
- **F. Mark the functions that may run at compile time** (ZenC's `@comptime fn`; C++'s `constexpr`; Rust's `const fn`). The mark splits the library into two kinds of function and must spread to every callee. Zig, Jai and D let any function run and report an error only when one does something impossible at compile time, which is the only form that fits calling C at compile time.
  ```
  comptime fn squares($N: int) -> [N]int {
      t: [N]int
      for i in 0..N {
          t[i] = i * i
      }
      return t
  }
  ```

**Branches and loops**

- **G. Explicit `$if`, `$for` and `$switch`** (C3's `$if`, `$for`, `$foreach` and `$switch`; Jai's `#if`; Odin's `when`; D's `static if` and `static foreach`). The interpreter evaluates the header, and the body is ordinary code: `$if` compiles only the chosen branch and doesn't check the others, `$switch` compiles one arm, and `$for` stamps out its body once per element with the loop variable as a constant (this is called unrolling). The same three forms work in three places, which is where composing pays off: in function bodies, in struct and enum bodies (conditional fields, like Jai's `#if` inside an enum), and at top level (conditional declarations). At top level, the declaration pass (the first pass, which registers every top-level name before any checking) still registers a name declared inside a `$if` branch, tagged with its condition, and a lookup that lands on a branch that wasn't taken reports that the name is declared only under that condition. Lookups then never wait for unknown names, only for the value of a condition. Each `$for` iteration is checked and lowered on its own, as C3's copies are, so a `$for` over 1,000 elements is 1,000 bodies; a loop that only computes a value belongs in ordinary code run by `const`.
  ```
  const target := import "std/target"
  const DEBUG := true

  $if target.os == .windows {
      const PATH_SEP := "\\"
  } else {
      const PATH_SEP := "/"
  }

  struct Enemy {
      hp: int
      speed: f32
      $if DEBUG {
          name: []u8
      }
  }

  fn dot(a: [$N]f32, b: [N]f32) -> f32 {
      sum: f32 = 0
      $for i in 0..N {
          sum += a[i] * b[i]
      }
      return sum
  }
  ```
- **H. Prune implicitly and unroll explicitly** (Zig: an `if` or `switch` on a comptime-known value skips analysis of the untaken branch, and loops need `inline for`). `if` needs no marker. But whether a branch is checked then depends on whether its condition happens to be constant, so turning a constant into a variable surfaces errors in code nobody touched, and the parser can't see which top-level statements are compile-time.
  ```
  fn dot(a: [$N]f32, b: [N]f32) -> f32 {
      sum: f32 = 0
      inline for i in 0..N {
          sum += a[i] * b[i]
      }
      return sum
  }
  ```
- **I. Loops that generate text** (Jai's `#insert #run for_each_member(T, …)`, community source; ZenC's `comptime { for … yield(…) }`). Compile-time code builds source text, which is then parsed and checked like hand-written code. It can produce anything, but generated code is paid for twice (M21–M24), errors point into text that nobody wrote, and ZenC re-parses the output of a tree-walking interpreter. `yield` is also a reserved keyword in chibi, kept for tasks.
  ```
  fn dot(a: [$N]f32, b: [N]f32) -> f32 {
      sum: f32 = 0
      $insert unrolled_sum("a", "b", N)
      return sum
  }
  ```

**Generating declarations**

- **J. Every top-level name is written in source, and compile-time code computes what it denotes** (Zig's `const T = @Type(…)`, or `@Struct` and `@Enum` in 0.16; C++26 can complete a class declared beforehand). `const Particles := Soa(Particle, 1024)` and `const Direction := make_enum(u8, .["north", "east", "south", "west"])` create types. Their members (fields, enum values) come from data, which is safe because member names never affect top-level lookup, so a lookup of `Direction.north` waits only for the one declaration `Direction`. With sheet 5's form, `fn Particles.step(…)` gives a created type methods. This is the research note's option B2 (values and types, no generated names), and it answers the open B3 question in its strictest form: every generated top-level name is an ordinary declaration, so the declaration pass registers all names before checking, checking stays order-free and parallel (C4), and a generated declaration's ID is just its path. A generator that makes several things returns them in one struct, and each gets its own `const`. What J can't do is create top-level names from data, such as one function per row of a table. In the example, the loop inside `Soa` is a plain `for`, because the whole function runs at compile time; `$` is only needed where compile-time and run-time code mix.
  ```
  const MAX_PARTICLES := 1024

  struct Particle {
      x: f32
      y: f32
      vx: f32
      vy: f32
  }

  fn Soa($T: type, $N: int) -> type {
      s := StructBuilder.{}
      for f in type_info(T).fields {
          s.add(f.name, array_of(f.type, N))
      }
      return s.build()
  }

  const Particles := Soa(Particle, MAX_PARTICLES)

  pub fn Particles.step(self: *Particles, dt: f32) {
      for i in 0..MAX_PARTICLES {
          self.x[i] += self.vx[i] * dt
          self.y[i] += self.vy[i] * dt
      }
  }

  const Direction := make_enum(u8, .["north", "east", "south", "west"])

  pub fn Direction.is_vertical(self: Direction) -> bool => self == .north or self == .south
  ```
- **K. Generators that declare their names** (Swift's SE-0389, where a macro lists the names it adds and lookup expands only the macros that could produce a wanted name, research note §3.9; Jai's `#placeholder TRUTH;` filled by `add_build_string`, community source). One generator fills several names that the source declares. Lookup stays order-free, but it needs new syntax, such as a destructuring `const` (not a settled form), and in Jai's version the generated text is also parsed late.
  ```
  const TokenKind, KIND2 := token_tables(SPECS)
  ```
- **L. Text or syntax insertion** (Jai's `#insert` with strings or `#code`; C3's `$expand(string)` since 0.8.0; ZenC's `comptime { yield(…) }`; D's `mixin`). It's the most powerful option and the one users ask for, which C3 shows by going from forbidding declarations that appear from nowhere to `$expand` in four years. But names then appear in the middle of checking (the research note's B4), so a failed lookup can't be reported until every generator has run, and the checker has to park lookups and retry them (Jai's `TYPECHECKED_ALL_WE_CAN` phase). C3's author calls `$expand` hostile to language servers and IDEs, and errors land in generated text.
  ```
  $insert token_source(SPECS)
  ```

**The build program**

- **M. A build file in chibi with an entry function** (Zig's `build.zig` with `pub fn build(b: *std.Build)`; Jai's `build.jai`, run by `#run build();`). The project root holds `build.chibi`, whose entry function receives a build graph and adds targets, options and libraries. The interpreter runs it, so building a project needs no backend first. It may call C and start processes, which makes it the place where clang runs when the C header cache misses, as decisions.md already describes. Because `import` is an expression, a build file can import another package's build code and call it, which is how Zig composes dependencies. No new syntax is needed; the API in the example is illustrative.
  ```
  package project

  const b := import "std/build"

  pub fn build(g: *b.Graph) {
      game := g.executable(.name = "game", .root = "src/game")
      game.link("SDL3")
      g.install(game)
  }
  ```
- **N. A metaprogram that intercepts the compiler** (Jai: `compiler_begin_intercept`, then a loop on `compiler_wait_for_message()` that receives each type-checked declaration; community source, `JCL-wiki/Metaprogramming.md` L353-L397). It can inspect every checked declaration and add code, which makes it the most powerful design the research note studied and the hardest to run in parallel or cache, because any unresolved name may be waiting on it.
  ```
  pub fn build(g: *b.Graph) {
      w := g.workspace("game")
      for m in w.messages() {
          switch m.kind {
              .checked => register_tests(w, m)
              _ => {}
          }
      }
  }
  ```
- **O. A manifest or directives** (C3's `project.json`, whose `exec` entries may compile and run `.c3` scripts under `--trust=full`; ZenC's `//>` comments; Odin's command-line flags). It's simple and needs no interpreter, but it contradicts "chibi is its own build system", and C3's `exec` scripts show code creeping back in.
  ```
  package game
  //> link: SDL3
  //> define: DEBUG=1
  ```

**The build program, looked at again (2026-11-26).** With N chosen, M splits into three questions.

*What the build code does*

- **M1. It describes a graph of steps** (Zig: `zig build` compiles `build.zig` with the build runner, and the result is a graph of steps that run concurrently; [packages.md](packages.md), Zig). `pub fn build(g: *Graph)` adds steps: compile, link, run, test, install, and a custom step that calls a chibi function. When `build` returns, the compiler runs the steps, those that don't depend on each other in parallel, and skips a step whose inputs haven't changed. `chibi build test` runs the step named `test`. A watcher (N) is a function attached to a compile step. A language server calls `build` and reads the graph without compiling anything.
- **M2. It drives the compiler itself** (Jai: the build program calls `compiler_create_workspace`, `set_build_options` and `add_build_file` ([The Way to Jai 30A L113-L155](https://github.com/Ivo-Balbaert/The_Way_to_Jai/blob/19cb4b7acb0de2798c769f9ad73313a4d15f4056/book/30A_Integrated_build_system.md#L113-L155)), and to watch, loops on `compiler_wait_for_message` (option N)). It's the simplest to explain and can do anything in any order. But the compiler sees calls, not a graph: steps run in the order written unless the build code starts threads, skipping unchanged work is the build code's job, and a language server learns the build only by running all of it.

M1 keeps M2's power through its custom step, whose function may call the compiler and decide what to build next. Corrected on 2026-11-26: such a step can still be skipped when nothing it read has changed, because the runner records each file and result it asks for through the build API while it runs, as Shake does. What it gives up, compared with Zig's fixed graph, is that the steps it adds can't be listed or shown in an editor until it has run. Jai gives up the same thing for the whole build, since its build program has to run before anyone knows what it builds. A read the runner can't see, such as a C `fopen` in a custom step, makes a skipped step stale; that cost belongs to any build system that skips work, Zig's run steps included, which must declare the files an external program reads.

*Where dependencies are listed*

- **P1. In the build code** (Jai). The build code fetches or imports what it likes. A package manager or language server has to run it to learn the dependencies, and so runs code from every package it reads. Swift's `Package.swift` is code of this kind, which SwiftPM runs in a sandbox.
- **P2. In a data file** (Zig's `build.zig.zon`, which names each dependency's URL and content hash; [packages.md](packages.md), Zig). A tool reads and writes it without running anything. It's a second format beside chibi.
- **P3. In `build.chibi`, as a constant that must be a literal**, such as `pub const DEPENDENCIES := .[.{ name = "sdl", url = "…", hash = "…" }]`. A tool parses the file and reads the literal without running it, so it has P2's property in chibi's own syntax. The cost is one rule that applies to one name, and a fetch command that edits chibi source to add a hash, as `cargo add` edits `Cargo.toml`. No language studied does this.

*Whether a program needs a build file*

- **R1. No.** `chibi run game.chibi` and `chibi build game.chibi` build one program with default options, as `zig run main.zig`, `jai main.jai` and `go run .` do, and the driver's file is the entry (top-level code, 2026-11-12). A build file is needed only for several programs, C libraries, options or dependencies.
- **R2. Yes, always.** Every program has a `build.chibi`. It's one rule, at the cost of a second file for every small program.

**Recommendation: M1, P3 and R1.** A graph lets the compiler run steps in parallel and skip unchanged ones, which a program that drives the compiler can't offer without its author writing both. The custom step and the watcher keep M2's power. P3 lets tools read dependencies without running anyone's code and needs no second format. R1 keeps a one-file program a one-file program.

### Recommendation

**I recommend `$` as the one marker (A), `const` plus `$` expressions for running code (D), explicit `$if`, `$for` and `$switch` (G), top-level names written in source with their contents computed (J), and a `build.chibi` program run by the interpreter (M),** because together they come to five rules built from one token the lexer already produces, every form is decided at its first token, every top-level name is visible to the declaration pass so checking stays order-free and parallel, nothing generates text that has to be parsed again, and each rule composes with the others: a `$for` can sit in a struct body, a `const` can hold a type, and a created type can get methods. The rules, stated once:

1. `$` before a name declares a compile-time name: `$T: type`, `$N: int`, `a: $T`, and `$offset := 0` inside a function.
2. `$` before `if`, `for` or `switch` evaluates the header at compile time and compiles only the chosen code.
3. `$` before any other expression evaluates it at compile time, as in `$assert(…)`.
4. `const` always evaluates its initializer at compile time, and that value may be a type.
5. A top-level name is always written in source, and compile-time code only computes what it denotes.

### Questions for you

1. Is the compile-time marker `$` (A), `comptime` (B) or `#` (C)? Answered 2026-11-25: `$`.
2. Does `const X := e` always evaluate `e` at compile time, with no extra marker (D)? Answered 2026-11-26: yes.
3. Are compile-time branches and loops explicit, `$if`, `$for` and `$switch` (G), rather than implied by a constant condition (H)? Answered 2026-11-25: yes.
4. May a top-level `$if` declare names, with the declaration pass registering the names from every branch? Answered 2026-11-26: yes, in the most powerful form (the row of that date in [../decisions.md](../decisions.md)).
5. Must every generated top-level name be written in source (J), leaving declared-name generators (K) and text insertion (L) out for now? Answered 2026-11-25: no. The owner chose B4, so insertion (L) is in, and J and K stay as the forms that never make a lookup wait (the compile-time declarations row of [../decisions.md](../decisions.md)).
6. Is the build program a `build.chibi` file with an entry function, run by the interpreter (M)? On 2026-11-26 the owner asked for the options, which are now the three questions under "The build program, looked at again": what the build code does (M1 or M2), where dependencies are listed (P1, P2 or P3), and whether a program needs a build file (R1 or R2). Answered 2026-11-26 for two of them: M1, with steps that may call the compiler and add steps so that it covers M2, and R1, with build code as compile-time code in any file and no file of a fixed name (the build program row of that date in [../decisions.md](../decisions.md)). Dependencies were decided later the same day: flat P1, with git repositories as a built-in source, a curated set of shipped libraries, and copying a directory as the path with no tooling (the dependencies row of that date in [../decisions.md](../decisions.md)). Before that, the owner leaned to P1 and wants a form that makes sense to the person writing the build without tribal knowledge. P3's rule that one constant must be a literal is that kind of knowledge, and it buys little in chibi, where compiling any package already runs its compile-time code, which may call C, so the recommendation is now P1: `b.fetch(url, .hash = …)` in the build code, where a missing hash makes the compiler fetch the archive and print the line to add, as Zig reports "dependency is missing hash field" and then `expected .hash = "…"` ([Fetch.zig L605-L611](https://github.com/ziglang/zig/blob/0.15.2/src/Package/Fetch.zig#L605-L611)). Deno imports by URL in code and recommends listing every dependency in one `deps.ts`, because updating a dependency imported in many files is "cumbersome and time consuming" ([manage_dependencies.md](https://github.com/denoland/manual/blob/main/examples/manage_dependencies.md)); in chibi `fetch` is a build call, so dependencies already sit in the build code. Odin, asked about the same day, has no package manager by design: its author argues that package managers automate "dependency hell" by fetching dependencies of dependencies, and recommends "copying and vendoring each package manually, and fixing the specific versions down" ([Package Managers are Evil](https://www.gingerbill.org/article/2025/09/08/package-managers-are-evil/), 2025-09-08). Odin ships its major libraries instead, in `core:` and the `vendor:` collection ([packages.md](packages.md), Odin). That gives P1 two variants: **transitive**, where a dependency's own `fetch` calls are followed, as Zig follows each `build.zig.zon`; and **flat**, where the build names every package at every depth with its hash, and a package it doesn't name is an error that prints the line to add, as Go's `go.mod` lists every module a build needs, indirect ones included, since Go 1.17. Copying a package's directory into a project needs no build code in either.
7. Added 2026-11-25, after the owner chose B4: is inserted code built from text, from syntax trees or both, and may a program watch the compiler's work (N)? Answered the same day: both, and yes.

---

## 8. Reflection

Reflection is code reading facts about types: names, sizes, fields, offsets and enum values. chibi needs it at compile time, to generate per-type code such as a serializer, and at run time, for `print`, debug views and hot reload's layout migration, which the architecture notes require in development builds. The syntax questions are how type queries are spelled, how code loops over a struct's fields, and what `any` and type IDs look like. Sheet 3 already recommends plain names for the four basic queries (`size_of`, `align_of`, `offset_of`, `type_of`), with the first three becoming library functions once reflection exists, so this sheet decides the family those names belong to.

### What the six languages do

| Language | Form | Source |
|---|---|---|
| Zig | `@typeInfo(T)` returns a `std.builtin.Type` whose fields are in source order; `inline for (@typeInfo(T).@"struct".fields) \|f\| … @field(v, f.name)`; `@TypeOf`, `@hasDecl`, `@FieldType`, `@sizeOf`. No run-time type table: only per-enum `@tagName` functions and one error-name table, emitted on first use. `anytype` exists only at compile time. | [langref, @typeInfo](https://ziglang.org/documentation/0.15.2/#typeInfo), [@field](https://ziglang.org/documentation/0.15.2/#field) (local L5785-L5801, L4879-L4886); research note §3.1 |
| Odin | `type_info_of(T)` returns `^runtime.Type_Info`; `typeid_of(T)`, `type_of(x)`, `size_of(T)`, `align_of(T)`, `offset_of(T, field)`; `any` holds a pointer and a `typeid`; `core:reflect` at run time and 91 `type_*` procedures in `base:intrinsics` at compile time; `-no-rtti` only on freestanding targets. | [overview](https://odin-lang.org/docs/overview/), "Built-in procedures", "`any` type" and "`typeid` type"; research note §3.3 |
| Jai | `type_info(T)` returns `*Type_Info_Struct`, whose `.members` carry name, type, `offset_in_bytes`, flags and notes such as `@NoSerialize`; `Any` is `{type: *Type_Info, value_pointer: *void}`; the binary holds type info for types reached from `type_info()` calls; `#type_info_none` opts a struct out; per-field code at compile time goes through `#insert #run for_each_member(T, …)` (community source). | `15A_Looping_with_while_and_for.md` L396-L480; `16A_Types_in_depth.md` L11-L40, L111-L135, L320-L340; `26C_Applications_of_Metaprogramming.md` L458-L540 |
| C3 | Properties with `::`: `Foo::size`, `Foo::alignment`, `Foo::members`, `Foo::methods`, `Enum::values`, `int::kind`, `Foo::typeid`; `$reflect(x)` and `$Typeof(x)`; at run time `obj.type` gives a `typeid` with fewer properties; `any` holds a pointer and a `typeid`. The docs warn that `::methods` can differ depending on when in resolution it is used. | [reflection.md](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/generic-programming/reflection.md); [types.md L304-L331](https://github.com/c3lang/c3-web/blob/ddff47422f823e7dfd99966c99f5914bceb32c15/docs/language-overview/types.md#L304-L331) |
| Carbon | No reflection design found; the metaprogramming page is a placeholder. | [metaprogramming.md](https://github.com/carbon-language/carbon-lang/blob/ca1898506be64134c3a25a5381622667a25cf67f/docs/design/metaprogramming.md) |
| ZenC | `sizeof` is a keyword; `@derive(Debug, Eq, Copy, Clone)` generates trait implementations for a struct. No `typeof` keyword in `token.h`, and no field iteration found. | [12-advanced.md](https://github.com/zenc-lang/docs/blob/60115e029d02e9cea4c9f721da5c82cd7b257de5/reference/12-advanced.md); [token.h](https://github.com/Zuhaitz-dev/zenc-lang/blob/b3f550e938be3fd395e9ebf657669f89dbb3453b/src/token.h) |

### Options

**How type queries are spelled**

- **A. `@` builtins** (Zig: `@typeInfo(T)`, `@field(v, name)`, `@hasDecl(T, name)`, `@TypeOf(x)`, `@sizeOf(T)`). The `@` keeps compiler built-ins apart from user names, so adding one never breaks a program, and `@` is already a token. But `@` would then mean "built in" rather than "compile time", since some of Zig's builtins run at run time, and `@` is the obvious spelling for annotations on fields (Jai's notes, C3's and ZenC's attributes), which reflection is often asked to read.
  ```
  const info := @type_info(Particle)
  const n := @size_of(Particle)
  ```
- **B. Ordinary functions** (Odin's `type_info_of(T)`, `typeid_of(T)`, `size_of(T)` and the `type_*` intrinsics; Jai's `type_info(T)` and `type_of(x)`). Queries look like calls, `type_info(T)` returns a record whose fields are read with `.`, and the same call works at compile time and at run time, so there is one API to learn. They can be predeclared like Go's `len`, so a user's own `field` shadows the built-in instead of clashing with it. Nothing new is parsed.
  ```
  const info := type_info(Particle)
  const n := size_of(Particle)
  ```
- **C. `::` properties** (C3 since 0.8.0: `Foo::size`, `Foo::members`, `Enum::values`, plus `$reflect(x).offset` and `$Typeof(x)`). It's short, and `::` is already a token (TK_DCOLON). But it adds a second member-access operator next to `.`, every property becomes a name the compiler reserves on every type, and C3's warning about `::methods` is exactly the dependence on analysis order that chibi's parallel checker has to avoid.
  ```
  const fields := Particle::fields
  const n := Particle::size
  ```
- **D. Keywords** (C's `sizeof`; chibi's lexer reserves `sizeof`, `alignof`, `offsetof` and `typeof` today). Keywords work for four queries, but reflection needs dozens (fields, enum values, names, kinds, methods), so they can't be the whole answer, and every other query would be a function anyway. Sheet 3 recommends removing the four keywords for the same reason.
  ```
  fn demo(p: Particle) {
      const n := sizeof(Particle)
      const T := typeof(p.x)
  }
  ```

**Looping over a struct's fields**

- **E. An unrolled compile-time loop** (Zig's `inline for` over `@typeInfo(T)` fields with `@field`; C3's `Foo::members` is a compile-time list, but its docs at this pin show no loop over it, so that is unverified). With sheet 7's `$for`, the loop variable `f` is a constant record describing one field, and `field(x, f)` names that field of `x`, so it can be read or assigned. Each type gets straight-line code, which is the fastest at run time; the cost is code per type, as with Zig's JSON parser, which emits a comparison per field for every type it parses (research note §3.1). An instance depends on its type's field list, so a layout change re-instantiates it, and hot reload patches in the new code.
  ```
  fn zero_floats(x: *$T) {
      $for f in type_info(T).fields {
          $if f.type == f32 {
              field(x.*, f) = 0
          }
      }
  }
  ```
- **F. Generated code text** (Jai's `#insert #run for_each_member(T, "if !serialize(s.%1, builder) return false;\n")`, community source). It produces the same code as E, but builds it as text that is parsed again, with the costs of sheet 7's option I.
  ```
  fn zero_floats(x: *$T) {
      $insert for_each_field(T, "x.%1 = 0\n")
  }
  ```
- **G. A run-time loop over the type table** (Jai's `for type_info(T).members`, with name, type, offset and notes per member; Odin's `reflect` and `fmt`, which print through `type_info_of`). One routine serves every type, so code size grows linearly with the number of types (the research note's §3.3 summary of gingerBill's argument), and a layout change only changes table data, which hot reload's migration needs at run time anyway. It's slower per call: Odin's `#soa` appends became up to 11× faster when table walks were replaced by stores the compiler lowered directly (M9). The same `type_info(T).fields` serves E and G, so the choice is made per use, not per language.
  ```
  fn print_fields(b: *fmt.Builder, p: *Particle) {
      for f in type_info(Particle).fields {
          b.text(f.name)
          b.text(" = ")
          b.any(field_any(p, f))
      }
  }
  ```

**`any` and type IDs** (`any` is a value of any type paired with a description of its type; a type ID is a number that names a type)

- **H. `any` holds a pointer and a type ID** (Odin and C3, each with a `typeid`). With the research note's design, a type ID is a hash of the type's canonical path, stable across builds, plus a separate layout hash (§7.7), and `type_info(t)` finds the record through the table. One type of types, `type`, can serve both times: at compile time it's the full type, and stored at run time it's the ID, which is what Jai does with `Type` (community source) and Odin with `typeid`.
  ```
  struct Component {
      kind: type
      data: *u8
  }

  fn log_all(values: []any) {
      for v in values {
          log_text(type_info(v.type).name)
      }
  }

  fn demo(hp: int, speed: f32) {
      log_all(.[hp, speed])
  }
  ```
- **I. `any` holds a pointer to the type's record** (Jai's `Any`, community source). Reading the record costs one load instead of a table lookup, but the pointer changes whenever the table is rebuilt, so such values can't live in state that survives a reload. The "persistable" rule in the research [README](README.md) already keeps raw pointers out of that state.
  ```
  struct Any {
      info: *TypeInfo
      data: *u8
  }
  ```
- **J. No `any`** (Zig's `anytype` exists only at compile time; Carbon and ZenC have none that I found). Everything goes through generics, which multiplies instances of things like `print` with every type they see; gingerBill's argument that per-type instantiation is the expensive default is aimed at exactly this (research note §3.3).
  ```
  fn log(x: $T) {
      log_text(type_info(T).name)
  }

  fn demo(hp: int, speed: f32) {
      log(hp)
      log(speed)
  }
  ```

### Recommendation

**I recommend B for queries, both E and G for fields, and H for `any`: `type_info(T)` returning one record that compile-time and run-time code share, `$for f in type_info(T).fields` with `field(x, f)` for per-type code, the same record read by a run-time loop for code that one routine serves, and `any` as a pointer plus a stable type ID,** because ordinary functions need no syntax and work at both times, which is what the research note's one-type-table design (R3) asks for; leaving `@` unused keeps it for annotations; and choosing between unrolled and table-driven code per use lets hot paths be fast and debug paths stay small. Sheets 5 and 7 make queries about declarations safe as well: every name is written in source and every method is registered in the declaration pass, so `has_method(T, "write")` gives the same answer in every analysis order, and because `import` is an expression, compile-time code can hand a whole package to a query such as `decls(import "game/tests")`. With annotations, which are a separate decision, that query can do the job of the Jai build program that collects procedures tagged `@TestProcedure` (research note §3.2). This is sheet 3's recommendation extended: `size_of`, `align_of` and `offset_of` become three more functions in the same family as `type_info`, and `type_of(x)`, the one query that must not evaluate its operand, returns a `type` that works wherever a type name works, including `$T` patterns and `type_info`. If the owner instead keeps the four keywords, nothing else in this sheet changes.

### Questions for you

1. Are type queries ordinary predeclared functions, `type_info(T)` and `field(x, f)`, leaving `@` free for annotations (B)?
2. Does `type_info(T)` return the same record at compile time and at run time, from one type table?
3. Is field iteration written `$for f in type_info(T).fields`, with `field(x, f)` usable for reading and assignment (E)?
4. Is `any` a built-in type that holds a pointer and a type ID (H)?
5. Is there one type of types, `type`, which is stored at run time as a stable path-hash ID?
6. May compile-time code list a type's methods (`has_method`) and a package's declarations (`decls`)?

---

## How sheets 5–8 fit together

The recommended answers share one marker, `$`, and one receiver rule, the ordinary first parameter. Together they reduce to a handful of rules: `$` before a name declares a compile-time name; `$` before `if`, `for` or `switch` makes that statement compile-time; `$` before any other expression evaluates it at compile time; `const` always evaluates at compile time; a type's methods are `fn Type.name(receiver, …)` in the type's package; and type queries are ordinary functions. The example below uses all of them: a generic, fixed-capacity `List` with methods, and a `to_string` that works for any struct by looping over its fields at compile time and defers to a type's own `write` method when it has one. The library calls `type_info`, `field`, `has_method` and `fmt.Builder` are illustrative, and the example assumes, as sheet 5 notes, that `.` reaches a field through a pointer.

**Combination 1, recommended: `$`, methods outside the type, parameters in the struct header.**

```
package game

import "std/fmt"

struct List($T: type, $N: int) {
    items: [N]T
    len: int
}

pub fn List.push(self: *List($T, $N), x: T) {
    assert(self.len < N)
    self.items[self.len] = x
    self.len += 1
}

pub fn List.write(self: List($T, $N), b: *fmt.Builder) {
    b.text(".[")
    for i in 0..self.len {
        if i > 0 {
            b.text(", ")
        }
        to_string(b, self.items[i])
    }
    b.text("]")
}

pub fn to_string(b: *fmt.Builder, x: $T) {
    const info := type_info(T)
    $if has_method(T, "write") {
        x.write(b)
    } else {
        $switch info.kind {
            .int => b.int(x)
            .float => b.float(x)
            .array => {
                b.text(".[")
                for e, i in x {
                    if i > 0 {
                        b.text(", ")
                    }
                    to_string(b, e)
                }
                b.text("]")
            }
            .struct => {
                b.text(info.name)
                b.text(".{ ")
                $for f, i in info.fields {
                    if i > 0 {
                        b.text(", ")
                    }
                    b.text(".")
                    b.text(f.name)
                    b.text(" = ")
                    to_string(b, field(x, f))
                }
                b.text(" }")
            }
            _ => $assert(false, "to_string: no case for this type")
        }
    }
}

struct Enemy {
    hp: int
    speed: f32
}

fn main() {
    enemies := List(Enemy, 16).{}
    enemies.push(.{ .hp = 10, .speed = 1.5 })
    enemies.push(.{ .hp = 25, .speed = 0.5 })

    b := fmt.Builder.{}
    to_string(&b, enemies)
}
```

After `main` runs, the builder holds `.[Enemy.{ .hp = 10, .speed = 1.5 }, Enemy.{ .hp = 25, .speed = 0.5 }]`. Here is where each sheet shows up and what the compiler does with it.

| Line | Sheet | What happens |
|---|---|---|
| `struct List($T: type, $N: int)` | 6, A | One generic struct; `List(Enemy, 16)` is interned once by (List's stable ID, the IDs of `Enemy` and `16`). |
| `pub fn List.push(self: *List($T, $N), x: T)` | 5, B and F; 6, A | The declaration pass registers `push` in `List`'s member table. `enemies.push(…)` passes `&enemies` and binds `T` and `N` from the receiver. |
| `x: $T` and `const info := type_info(T)` | 6, A; 7, D; 8, B | Each instance of `to_string` gets its own constant `info`, computed by the interpreter. |
| `$if has_method(T, "write")` | 7, G; 8 | Decided per instance. The answer can't depend on analysis order, because methods are written in source and registered before checking. The untaken branch isn't checked, so `x.write(b)` is never checked for `Enemy`. |
| `$switch info.kind` | 7, G | Exactly one arm is compiled per instance. |
| `$for f, i in info.fields` with `field(x, f)` | 7, G; 8, E | Unrolled twice for `Enemy`, into straight-line code for `hp` and `speed`. |
| `_ => $assert(false, …)` | 6, G; 7, D | A type with no case fails to compile, with a note naming the call that instantiated it. |

Checking `main` creates instances of `List.push` and `List.write` for `(Enemy, 16)`, and of `to_string` for `List(Enemy, 16)`, `Enemy`, `int` and `f32`. Each instance is keyed by stable IDs and records what it read, so a hot-reload edit to `Enemy`'s fields re-instantiates exactly the instances that depend on `Enemy`'s layout (`to_string` for `Enemy`, and `List.push` and `List.write` for `(Enemy, 16)`, whose element size changed) and patches their slots, while the `int` and `f32` instances stay as they are.

**Combination 2, the keyword alternative: `comptime`, methods inside the type, type functions (the Zig family).** If the owner prefers `comptime` (sheet 7, B) and methods inside the struct (sheet 5, A), the consistent set changes in four places. Generic types become functions that return an anonymous `struct { … }`; the methods move inside it and need a name for the type being defined (here `Self`, Zig's `@This()`); every `$` becomes `comptime`; and because `comptime` has no pattern form, `to_string` takes an untyped `x` and names its type with `type_of`. Everything else stays the same.

```
package game

import "std/fmt"

fn List(comptime T: type, comptime N: int) -> type {
    return struct {
        items: [N]T
        len: int

        pub fn push(self: *Self, x: T) {
            assert(self.len < N)
            self.items[self.len] = x
            self.len += 1
        }

        pub fn write(self: Self, b: *fmt.Builder) {
            b.text(".[")
            for i in 0..self.len {
                if i > 0 {
                    b.text(", ")
                }
                to_string(b, self.items[i])
            }
            b.text("]")
        }
    }
}

pub fn to_string(b: *fmt.Builder, x) {
    const T := type_of(x)
    const info := type_info(T)
    comptime if has_method(T, "write") {
        x.write(b)
    } else {
        comptime switch info.kind {
            .int => b.int(x)
            .float => b.float(x)
            .array => {
                b.text(".[")
                for e, i in x {
                    if i > 0 {
                        b.text(", ")
                    }
                    to_string(b, e)
                }
                b.text("]")
            }
            .struct => {
                b.text(info.name)
                b.text(".{ ")
                comptime for f, i in info.fields {
                    if i > 0 {
                        b.text(", ")
                    }
                    b.text(".")
                    b.text(f.name)
                    b.text(" = ")
                    to_string(b, field(x, f))
                }
                b.text(" }")
            }
            _ => comptime assert(false, "to_string: no case for this type")
        }
    }
}
```

Compared with combination 1, this costs a keyword, anonymous struct expressions and a `Self` name, and it can't give methods to a type built from data, such as sheet 7's `Soa(Particle, 1024)`, because such a type has no body to put them in. It gains one mechanism for all generic types and Zig's familiarity.

A ZenC-style combination (`<T>` generics, `impl` blocks, `comptime { yield(…) }`) fits chibi less well, because angle brackets need the unbounded lookahead described in sheet 6, `impl` adds a keyword without traits to justify it, `yield` is reserved for tasks, and a `to_string` built from emitted text is parsed twice.

---

## Not verified

- **Jai:** every Jai fact rests on community sources. The Way to Jai contradicts itself on foreign calls, saying that `#run` code loads DLLs dynamically (`26A_Metaprogramming.md` L168) and that FFI can't run during `#run` (L180). It also writes polymorphic struct parameters both as `struct($T: Type, $N: s64)` and as `struct (T: Type)`.
- **C3:** whether `$foreach` can iterate `Foo::members`, and how code reads a member through a reflected member, since the reflection and compile-time pages at `ddff474` show neither; whether methods may be declared in a different module from their type, which `functions.md` doesn't say.
- **ZenC:** whether a method can take `self` by value, since the docs never state the receiver's type and only the generated C (`Point* self`) shows it; how methods are named in the generated C; the absence of `typeof` and of field reflection, which I checked only in `token.h` and the reference chapters. The docs site documents v0.4.4, while the source pin `b3f550e9` is v0.5.0.
- **Carbon:** the syntax for out-of-line methods of a generic class; I found no compile-time loops and no reflection design.
- **Odin:** exact anchors in the FAQ and the overview, so the links name sections instead; that `where` clauses are evaluated per instance comes from the research note's reading of `check_expr.cpp`, not from the overview.
- **Zig:** the 0.16 and 0.17 changes (`@Struct`, `@Enum`, `std.lang`) come from the research note, not from the local 0.15.2 langref; the langref anchors are derived from section titles in the local copy, which is the unrendered template, and weren't checked against the rendered page.
- **chibi itself:** the cost claims for parser changes are my reading of `src/parser.c` as of 2026-11-10; none of the proposed forms has been prototyped, and no chibi example in this file has been parsed.
