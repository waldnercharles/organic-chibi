# Top-level code: the details left open

Written 2026-11-12 by the research thread, after the owner chose option B for top-level code ("I prefer B now", in the parser thread; recorded in [decisions.md](../decisions.md)). Code at the top level of a file is allowed only in the entry file, and it runs top to bottom as the body of `main`. Every other file holds declarations only. The checker already reports code in any other file: "only the file you run may contain code".

Five details are still open. Each section lists the options, what other languages do, and a recommendation.

Sources:
- Swift: [The Swift Programming Language, Declarations](https://github.com/swiftlang/swift-book/blob/main/TSPL.docc/ReferenceManual/Declarations.md), "Top-Level Code".
- C#: [Top-level statements](https://learn.microsoft.com/en-us/dotnet/csharp/fundamentals/program-structure/top-level-statements) and the [C# 9 feature specification](https://github.com/dotnet/csharplang/blob/main/proposals/csharp-9.0/top-level-statements.md).
- Go: the [specification](https://go.dev/ref/spec#Package_initialization), "Package initialization".

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

## 2. Top-level code and a `fn main` in the same program

- **Swift:** an error. A program uses "at most one" of its ways to mark an entry point: a file with top-level code, `main.swift`, the `main` attribute, and two platform attributes.
- **C#:** allowed, with a warning. An explicit `Main` "can't function as an entry point" when top-level statements exist.

**Recommendation: an error,** as in Swift. A `fn main` that never runs, with only a warning, is a trap. Allowing both later breaks no code.

## 3. `name := value` among the entry file's code

The same text means a global in a library file, so this needs a rule.

- **A. A local of `main`**, as in C#: top-level locals are wrapped into the generated entry method, and using them anywhere else is an error (feature specification).
  - Pros: it follows from the decision that the code is `main`'s body, since `name := value` in any body declares a local. The shadowing rule for locals applies. Other files can't read it before `main` has assigned it.
  - Cons: `name := value` means a global in a library file and a local in the entry file. A program that needs a global puts it in another file.
- **B. A global**, as in Swift, where top-level declarations are "accessible to code in every source file that's part of the same module".
  - Pros: the text means the same in every file.
  - Cons: the code runs top to bottom, so another file's function could read the global before `main`'s code has reached its assignment. Avoiding that needs either initialization before `main`, which reorders the code, or a check on every read.

**Recommendation: A.** Declarations at the entry file's top level, such as `fn`, `struct`, `enum` and `const`, stay package declarations, as C# allows type declarations after its statements.

## 4. `return`, `defer` and `try` in the entry file's code

Since the code is `main`'s body, the recommendation is that each means what it means in `main`:
- **`return`** leaves `main` and ends the program normally. C# also allows it, and a `return` with a value becomes the exit code.
- **`defer`** runs when `main`'s body ends, by falling off the end, by `return`, or by an error.
- **`try`** passes an error out of `main`. The program then prints the error's name, prints the error return trace in debug builds, and exits with status 1. Zig's start code does the same when `main` returns an error.

Exit codes and `main`'s full signature belong with question 2: if `fn main` may return an integer, top-level code would use the same rule.

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
