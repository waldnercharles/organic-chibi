# Top-level code: the details left open

Written 2026-11-12 by the research thread, after the owner chose option B for top-level code ("I prefer B now", in the parser thread; recorded in [decisions.md](../decisions.md)). Code at the top level of a file is allowed only in the entry file, and it runs top to bottom as the body of `main`. Every other file holds declarations only. The checker already reports code in any other file: "only the file you run may contain code".

Five details were open, and a sixth, how code reads its command line, came with the owner's choice on 2026-11-23 to start the compiler from top-level code (the Compiler row of that day). Each section lists the options, what other languages do, and a recommendation. Step 3k.27 built the recommendations of sections 1 to 4 and 6 on 2026-11-23, while the owner reviews them ([checker-and-ir.md](checker-and-ir.md) section 8).

Sources:
- Swift: [The Swift Programming Language, Declarations](https://github.com/swiftlang/swift-book/blob/main/TSPL.docc/ReferenceManual/Declarations.md), "Top-Level Code".
- C#: [Top-level statements](https://learn.microsoft.com/en-us/dotnet/csharp/fundamentals/program-structure/top-level-statements) and the [C# 9 feature specification](https://github.com/dotnet/csharplang/blob/main/proposals/csharp-9.0/top-level-statements.md).
- Go: the [specification](https://go.dev/ref/spec#Package_initialization), "Package initialization", and the [`os` package](https://pkg.go.dev/os#pkg-variables), `os.Args`.

## 1. Which file is the entry

- **A. The file the driver is given**: `chibi run game.chibi`, or the entry named in the build description. Python, Jai, C#'s file-based apps (`dotnet file.cs`) and Zig's root source file work this way.
  - Pros: explicit; one package can hold several programs, such as a game, a level editor and a test runner, each with its own entry file; matches the error message the owner saw.
  - Cons: the driver or the build description must name the file.
- **B. A fixed name, such as `main.chibi`**: Swift's `main.swift` and Rust's `src/main.rs`, by convention.
  - Pros: nothing to configure.
  - Cons: one program per package; the name carries meaning that no other file name does.
- **C. Whichever single file has code**: C#, where "only one compilation_unit is allowed to have statements" (feature specification).
  - Pros: automatic.
  - Cons: the error becomes "two files have code" instead of naming the file in the wrong; the checker must see every file before it knows which one is the entry; a library file that gains a statement silently turns its package into a program.

**Recommendation: A.** chibi is its own build system (the compile-time row of decisions.md), so the build description can name the entry where the command line doesn't.

Built: the driver's first file is the entry, as it was for the error on code in other files.

## 2. Top-level code and a `fn main` in the same program

- **Swift:** an error. A program uses "at most one" of its ways to mark an entry point: a file with top-level code, `main.swift`, the `main` attribute, and two platform attributes.
- **C#:** allowed, with a warning. An explicit `Main` "can't function as an entry point" when top-level statements exist.

**Recommendation: an error,** as in Swift. A `fn main` that never runs, with only a warning, is a trap. Allowing both later breaks no code.

Built: any declaration named `main`, in any file, gives "the program starts at the code in the file you run, so it can't declare `main`" when the entry file has code. A program whose entry file has no code still starts from `export fn main(argc: i32, argv: [*][*]u8) -> i32`, as the translated compiler does until the switch.

## 3. `name := value` among the entry file's code

The same text means a global in a library file, so this needs a rule.

- **A. A local of `main`**, as in C#: top-level locals are wrapped into the generated entry method, and using them anywhere else is an error (feature specification).
  - Pros: it follows from the decision that the code is `main`'s body, since `name := value` in any body declares a local. The shadowing rule for locals applies. Other files can't read it before `main` has assigned it.
  - Cons: `name := value` means a global in a library file and a local in the entry file. A program that needs a global puts it in another file.
- **B. A global**, as in Swift, where top-level declarations are "accessible to code in every source file that's part of the same module".
  - Pros: the text means the same in every file.
  - Cons: the code runs top to bottom, so another file's function could read the global before `main`'s code has reached its assignment. Avoiding that needs either initialization before `main`, which reorders the code, or a check on every read.

**Recommendation: A.** Declarations at the entry file's top level, such as `fn`, `struct`, `enum` and `const`, stay package declarations, as C# allows type declarations after its statements.

Built: `name := value`, `name: T` and `name: T = value` among the entry file's code declare locals of `main`, and a function that names one, even in the entry file, gives "unknown name". `extern name: T` stays a C global.

## 4. `return`, `defer` and `try` in the entry file's code

Since the code is `main`'s body, the recommendation is that each means what it means in `main`:
- **`return`** leaves `main` and ends the program normally. C# also allows it, and a `return` with a value becomes the exit code.
- **`defer`** runs when `main`'s body ends, by falling off the end, by `return`, or by an error.
- **`try`** passes an error out of `main`. The program then prints the error's name, prints the error return trace in debug builds, and exits with status 1. Zig's start code does the same when `main` returns an error.

Exit codes and `main`'s full signature belong with question 2: if `fn main` may return an integer, top-level code would use the same rule.

Built: `return n` takes an `i32`, as `export fn main` returns, and becomes the exit status; a bare `return` and the end of the code exit with 0. Step 3c built `defer`, which runs at the code's end and at each `return`, after the exit status is read. `try` isn't built anywhere yet.

## 5. Whether a library's global may run code at startup

For example, `score := load_score()` in a library file.

- **A. No: a global's initial value must be known at compile time.** Zig makes "the initialization value of container level variables" implicitly compile-time (language reference, Container Level Variables). Rust's statics need constant initializers, and so do C's static objects.
  - Pros:
    - no hidden work before `main`;
    - no order between packages to define;
    - hot reload never has to decide whether to run an initializer again;
    - chibi's compile-time execution still lets a global take any value computable while compiling.
  - Cons: run-time setup, such as loading a file, is an explicit call from `main`.
- **B. Yes, in dependency order before `main`.** Go runs package-level initializers "stepwise", always picking the variable "earliest in declaration order" with no uninitialized dependencies, then each package's `init` functions.
  - Pros: convenient.
  - Cons:
    - work runs before `main`, in an order that depends on the dependency graph;
    - a hot reload must decide what to run again;
    - C++'s version of this, without Go's ordering, produced the static initialization order fiasco.
- **C. On first use.** Swift computes global variables "lazily, in a similar manner to Lazy Stored Properties" (The Swift Programming Language, Properties).
  - Pros: no order problem.
  - Cons: every read of such a global first checks whether it has been initialized, which is a hidden branch on a hot path.

**Recommendation: A.** Moving to B later breaks no code. C conflicts with the goal of code that does no hidden work.

**Decided 2026-11-19: A** (the Language row of that date in [decisions.md](../decisions.md)).

## 6. How code reads its command line

C's `main` takes `argc` and `argv`. Top-level code has no parameters, so it needs another way to reach them.

Where the arguments live:
- **A. A global in a built-in package: `import "std/os"`, then `os.args`.** Go's `os.Args`, which "hold the command-line arguments, starting with the program name" (the `os` package), Odin's `os.args` and Zig's `std.os.argv` are package variables that start-up code fills before `main` runs.
  - Pros: any function reads it, so code that parses flags needs no parameter for them; it follows the 2026-11-23 rule that builtins live in built-in packages, so the plain name `args` stays free.
  - Cons: the start of `main` writes a global, and a function can depend on the command line without saying so.
- **B. A name that only top-level code sees,** as C#: "Top-level statements can reference the `args` variable to access any command-line arguments passed to the app when it starts" (C#'s top-level statements page).
  - Pros: nothing global; the code passes the arguments to what needs them.
  - Cons: `args` becomes a name that the entry file's code can't declare, and a function, even one in the entry file, can't see it.
- **C. C's `argc` and `argv`, seen by top-level code.** As B, with two names, a count and a pointer that the code pairs itself.

What each argument is:
- **`cstring`**, as Zig's `std.os.argv` holds `[*:0]u8`: the slice points at C's `argv` and its length is `argc`, so filling it costs two stores, and an argument passes to C as it is.
- **`string`**, as Go and Odin: `==` and slicing work on each argument at once, but every program's start scans each argument for its end and allocates the slice, whether the program reads its arguments or not.

**Recommendation: A, with `os.args: []cstring`.** The first element is the program's path, as C's `argv[0]` and Go's `os.Args[0]`. A program that wants a `string` measures the argument, as with C's `strlen`, and slices it. `os.args` can be assigned, as Go's `os.Args` can.

Built: a program reads `os.args` only when it starts from top-level code; elsewhere `os.args` gives "`os.args` is set only when the program starts at the code in the file you run", because nothing fills it. The code has no names for `main`'s `argc` and `argv`, so `os.args` is its only way to them.
