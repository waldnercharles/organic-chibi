# A. Dispatch prior art: guaranteed tail calls and computed goto

Retrieved 2026-11-12. Sources are primary: official documentation, release notes, specifications, RFCs, the projects' own source at a pinned tag or commit, papers, and posts by the authors. "UNVERIFIED" marks claims I could not confirm in a primary source. Verbatim quotes are kept under 15 words; everything else is paraphrase.

Pinned versions used throughout:

| Project | Pin |
|---|---|
| LLVM / Clang | tag `llvmorg-23.1.5` (2026-11-03); `main` at `1db8ce340a37` for "still true today" checks |
| GCC | manuals for 15.1.0 and 16.1.0; trunk manual |
| MSVC | Microsoft Learn pages (attributes page commit `5943f7d`, `__preserve_none` page updated 2026-02-24) |
| Zig | tag `0.17.1` on Codeberg (2026-11-02); release notes 0.14.0, 0.15.1, 0.16.0, 0.17.0 |
| Rust | `rust-lang/rust` main `29a5d066d98b`; RFC 3407 PR head `4dfb991`; RFC 3720 PR head `12af0ec` |
| CPython | tags `v3.14.0a5`, `v3.14.0a6`, `v3.14.0`, `v3.15.0` |
| Wasmtime / Cranelift | tag `v50.0.1` |
| Odin | releases `dev-2026-02`, `dev-2026-11` |
| C3 | `c3c` master release notes (0.8.6 released 2026-11-11; 0.8.7 in development) |

---

## 1. Clang `[[clang::musttail]]`, `preserve_none`, `preserve_most`

**What it is.** A statement attribute on a `return` whose operand is a call. Clang must emit a real tail call, or reject the program. It shipped in **Clang 13.0.0** (tag published 2021-10-01). The release note reads "Guaranteed tail calls are now supported with statement attributes." Josh Haberman wrote it (review D99517).

```c
[[clang::musttail]] return op_table[op](ARGS);   // C++11 / C23 spelling
__attribute__((musttail)) return next(ARGS);      // GNU spelling
```

**Rules** (`clang/include/clang/Basic/AttrDocs.td`. The text is the same at `llvmorg-23.1.5` and at `main` 1db8ce340a37.)
- It applies only to a `return` whose value is a function call. A `void` function still writes `return f();`.
- The callee must have the same number of arguments as the caller.
- Return and argument types must be "similar": they may differ only in cv-qualifiers or array size. The implicit `this` counts as an argument.
- All variables in scope, all arguments and the return value must be trivially destructible.
- The calling conventions must match.
- Variadic functions are not allowed. K&R declarations without a prototype are not allowed.
- The lifetimes of all locals and parameters end immediately before the call. Passing a pointer or reference to a local is undefined behaviour. Clang 20.1.0 added this sentence to the documentation and said behaviour did not change. Clang 20 also extended `-Wreturn-stack-address` to arguments of musttail calls.
- The guarantee holds "even when optimizations are disabled", so it applies at `-O0`.
- The rules are target-independent by design. The docs say they give "assurances that the tail call can be optimized on all targets".
- Caveat: a virtual call that goes through a thunk can still grow the stack.

**Have later versions relaxed the rules? No.** The rules are unchanged from 13.0.0 through `main` as of 2026-11-12.
- LLVM issue #54964, "Too strong validity checks for [[clang::musttail]]", has been open since 2022-04-18.
- Haberman's post of 2025-02-10 describes two proposed fixes: relax `musttail` so it fails only when the current target cannot do the call, or add a separate `nonportable_musttail`. He prefers the first. Neither was implemented.
- On 2026-10-29 Haberman posted the LLVM Discourse RFC "tail_chaincc". It proposes a new calling convention, spelled `__attribute__((tail_chain))` or `[[clang::tail_chain]]`, that combines:
  - callee-pop, taken from `tailcc`, so parameter lists may differ (return types must still match);
  - zero callee-saved registers and unlimited argument registers, taken from `preserve_none`;
  - reversed argument-register order, on x86-64 R12, R13, R14, R15, R11, R9, R8, RCX, RDX, RSI, RDI, RAX.

  Status: a proposal with two replies, no PR and no benchmark numbers. One reply (pinskia) says LLVM/Clang "will produce wrong code in many cases" where GCC rejects.

**Changes by version** (`clang/docs/ReleaseNotes.rst` at each tag)
- 18.1.0: fixed an assertion failure for musttail calls to `noreturn` functions.
- 19.1.0, PowerPC section: "Clang now emits errors for impossible" musttail.
- 20.1.0: the lifetime documentation note and the warning extension above.
- 21.1.0: a musttail call to a `not_tail_called` function is now an error.
- 22.1.0: `__has_attribute(musttail)` is no longer true on WebAssembly when the tail-call feature is off.

**Errors** (`DiagnosticSemaKinds.td` @23.1.5, paraphrased)
- Signature mismatch: "cannot perform a tail call to function … because its signature is incompatible". Notes give the cause: different number of parameters, a type mismatch at the Nth parameter, a different return type, or a member of a different class.
- Incompatible calling convention, with a note naming both conventions.
- The operand is not a call.
- The return value, a parameter or a temporary is not trivially destructible.
- The function is variadic, or the callee is `noreturn` or `not_tail_called`.
- The caller or callee is a constructor or destructor. The call is in a block or an Objective-C method. A declaration has no prototype.
- "cannot perform a tail call from this return statement" (a scope problem).
- Warnings when the address of a local or a local temporary is passed to a musttail function.

If a target cannot honour an IR `musttail` that reached the backend, LLVM aborts with "failed to perform tail call elimination on a call site marked musttail". This message is in the AArch64, ARM and PowerPC call lowering at 23.1.5. Zig #18189 hits it with composite return types.

**Known x86 miscompile, now fixed.** The x86 backend miscompiled musttail calls whose arguments are passed on the stack, for example a 24-byte struct passed by value. The PR says such a program segfaults "on any optimization level". PR #168956 fixed it, merged 2026-01-14. The fix is in 23.1.0. Its merge commit is not an ancestor of the 22.1.x tags; I did not check for a cherry-picked backport.

**`preserve_none`** is a Clang attribute that maps to LLVM `preserve_nonecc`. It first appears in **Clang/LLVM 19.1.0** (2024-09-17); neither docs file has it at 18.1.0 or 18.1.8. It was implemented for x86-64 in llvm PR #76868 and for AArch64 in PR #88333. Haberman notes that no release note announced it. Semantics, from AttrDocs @23.1.5:
- There are no callee-saved general-purpose registers. On x86-64 the callee preserves only RSP and RBP; on AArch64 only LR and FP.
- More registers carry arguments. On x86-64: R12, R13, R14, R15, RDI, RSI, RDX, RCX, R8, R9, R11, RAX. On AArch64: X20–X28, X0–X7, X9–X14, plus X15 except on Windows.
- Floating-point and SIMD registers follow the C convention.
- The docs say "`preserve_none`'s ABI is still unstable".

The Clang docs list x86-64 and AArch64. LangRef @23.1.5 lists `preserve_nonecc` for "x86_64, AArch64 and LoongArch". Odin issue #7667 (open) reports what happens elsewhere:
- LLVM 18 silently lowers it to the C convention on x64 and rejects it on arm64.
- On LLVM 19 and later, targets other than x64 and arm64 either fall back to the C convention silently or fail.

**`preserve_most` / `preserve_all`.**
- LLVM IR `preserve_mostcc` is already in LangRef at 3.5 (2014). The Clang attribute first appears in the 3.9 docs (2016).
- Arguments pass as in the C convention, but the callee preserves almost every general-purpose register: all except R11 on x86-64, and all except X0–X8 and X16–X18 on AArch64.
- `preserve_all` also preserves floating-point registers: XMM/YMM on x86-64, and the low 128 bits of V8–V31 on AArch64.
- Both are documented as experimental and intended for the Objective-C runtime. The intended use is a hot-path call to a function that has its own cold path.
- Haberman (2025) uses `preserve_most` on slow paths that handlers call without a tail call. He recommends `preserve_none` on the handlers instead, because `preserve_most` must be applied to every callee, and you cannot change it on `strlen`.
- Hazard: Odin #7669 (open) shows `preserve/all` plus musttail returning the wrong result on x64 when the arguments change. The callee is required to preserve the argument registers, so a tail call that changes them breaks the caller's assumption.

Sources:
- https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/include/clang/Basic/AttrDocs.td
- https://github.com/llvm/llvm-project/blob/llvmorg-13.0.0/clang/docs/ReleaseNotes.rst
- release notes at llvmorg-18.1.0, 19.1.0, 20.1.0, 21.1.0 and 22.1.0
- https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/clang/include/clang/Basic/DiagnosticSemaKinds.td
- https://github.com/llvm/llvm-project/issues/54964
- https://github.com/llvm/llvm-project/pull/168956
- https://discourse.llvm.org/t/rfc-tail-chaincc-a-maximum-efficiency-tail-call-convention/92661
- https://blog.reverberate.org/2025/02/10/tail-call-updates.html
- https://github.com/odin-lang/Odin/issues/7667
- https://github.com/odin-lang/Odin/issues/7669

## 2. GCC `musttail` and MSVC `[[msvc::musttail]]`

**GCC**
- Since **GCC 15.1** (2025-04-25). The C-family entry in GCC 15 changes reads: "A musttail statement attribute was added to enforce tail calls." Haberman's 2025 post links the C and C++ commits from July 2024.
- Spellings: `[[gnu::musttail]]`, `[[clang::musttail]]` and `__attribute__((musttail))`, each placed on `return f(...)`.
- Rules, from the GCC 15.1 manual under Statement Attributes:
  - GCC reports an error when it cannot generate the tail call. The manual adds "On some targets, tail calls may not be supported at all."
  - The lifetimes of automatic variables, parameters and temporaries end before the call. Accessing them from the callee is undefined behaviour.
  - A variable that needs non-trivial destruction makes the call invalid.
  - Warnings: `-Wmusttail-local-addr` (on by default) and `-Wmaybe-musttail-local-addr` (enabled by `-Wextra`).
  - "Enabling -O1 or -O2 can improve the success of tail calls", so GCC documents no `-O0` guarantee.
  - The manual has no same-signature rule. Haberman's 2026 RFC says GCC accepts a call with a mismatched signature but then fails in the backend on some targets, such as 32-bit x86. I did not verify this independently. His 2025 post says GCC is stricter than Clang about pointers to locals.
- **`preserve_none` in GCC:**
  - It is absent from the GCC 15.1 manual. x86 had only `no_callee_saved_registers` there.
  - The **GCC 16.1** manual (2026-04-30) documents an x86 `preserve_none`. It is like `no_callee_saved_registers`, but r12, r13, r14, r15, rdi and rsi carry integer arguments on x86-64. It is "subject to change".
  - GCC 16.1 also has an AArch64 `preserve_none`. It has no callee-saved registers, including SIMD and floating-point registers. Arguments go in r20–r28, r0–r7, r10–r14, r9 and r15; Windows excludes r15. It has "not been stabilized".
  - GCC 16's changes page announces the AArch64 version.
  - Clang's AArch64 variant leaves SIMD/FP registers under AAPCS rules and documents a different argument order. Whether the two compilers' AArch64 `preserve_none` are ABI-compatible is UNVERIFIED.

**MSVC**
- **`[[msvc::musttail]]`** arrived in **MSVC Build Tools 14.50**: Visual Studio 2026 version 18.0, released November 2025, toolset v145. Microsoft Learn calls it an "experimental x64-only Microsoft-specific attribute". If the compiler cannot emit the tail call, compilation fails. Requirements:
  - matching return types;
  - compatible calling conventions;
  - the tail call is the final action;
  - the callee cannot use more stack space than the calling function;
  - with more than four integer parameters, the caller must already have allocated the extra stack space;
  - compile with `/O2` or `/O2 /GL`.

  So there is no guarantee in `/Od` debug builds, and no ARM64 support.
- **`__preserve_none`** (MSVC):
  - experimental, C only, x64 only;
  - up to 10 arguments, all in registers: r13, r14, r15, rbx, rsi, rdi, r9, r8, rdx, rcx. The hidden struct-return pointer takes r13;
  - no stack-passed parameters, no floating-point parameters, no varargs;
  - only rsp, rbp and r12 are nonvolatile;
  - the callee cleans its own stack;
  - Microsoft documents it for use together with `msvc::musttail`.

  The page does not say which version introduced it (UNVERIFIED). CPython 3.15 rejects MSVC older than `_MSC_VER` 1950, with the message "You need at least VS 2026 / PlatformToolset v145".
- A third-party blog (Aba Search & Replace) reports MSVC silently ignoring `[[msvc::musttail]]` in some functions. That is not a primary source: UNVERIFIED.

Sources:
- https://gcc.gnu.org/gcc-15/changes.html
- https://gcc.gnu.org/onlinedocs/gcc-15.1.0/gcc/Statement-Attributes.html
- https://gcc.gnu.org/onlinedocs/gcc-16.1.0/gcc/x86-Attributes.html
- https://gcc.gnu.org/onlinedocs/gcc-16.1.0/gcc/AArch64-Attributes.html
- https://gcc.gnu.org/gcc-16/changes.html
- https://learn.microsoft.com/en-us/cpp/cpp/attributes?view=msvc-170 (section `[[msvc::musttail]]`)
- https://learn.microsoft.com/en-us/cpp/cpp/preserve-none
- https://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-msvc
- https://github.com/python/cpython/blob/v3.15.0/Python/ceval_macros.h

## 3. LLVM IR `tail` vs `musttail` (LangRef @23.1.5, now `LangRef.md`)

```llvm
%r = musttail call ptr @next(ptr %pc, ptr %sp)   ; must be followed by:
ret ptr %r
```

- **`tail`** is a hint that LLVM may ignore. LLVM guarantees the tail call only when all of these hold:
  - both functions use `fastcc` or `tailcc`;
  - the call is in tail position;
  - `-tailcallopt` is set or the convention is `tailcc`;
  - the target's own constraints are met.
- **`musttail`** means the call "must be tail call optimized" for the program to be correct, even under `"disable-tail-calls"`. It guarantees:
  - no unbounded stack growth inside a recursive cycle;
  - `inalloca` and `preallocated` arguments are forwarded in place;
  - in a `"thunk"` function where both sides are varargs, the unprototyped arguments are forwarded.
- **Rules for `musttail`:**
  - the call immediately precedes `ret`, optionally with a pointer bitcast between;
  - `ret` returns the call's value, `undef` or void;
  - caller and callee use the same calling convention;
  - the callee is varargs if and only if the caller is;
  - the return type is not rewritten into an `sret` pointer;
  - for any convention other than `tailcc` or `swifttailcc`: all ABI-impacting attributes (`sret`, `byval`, `inreg`, `returned`, `inalloca`) match, and **the caller and callee prototypes must match**;
  - with `tailcc` or `swifttailcc`: only `sret`, `byval`, `swiftself` and `swiftasync` are allowed, and "Prototypes are not required to match".
- Both markers promise that the callee does not access the caller's allocas, `va_arg`s or `byval` arguments. The one exception is passing them on as `byval`, which copies.

**Why prototypes must match: stack-passed arguments.**
- The C convention is caller-pops. A callee can reuse only the argument area its own caller allocated.
- The LLVM code generator manual's sibling-call rule says so directly: stack-passed callee arguments must already sit in the caller's incoming argument area at the same offsets.
- The guaranteed path (`-tailcallopt` / `tailcc`) works when the callee has more arguments because it switches to a callee-pops convention. The manual warns this costs a stack readjustment after every non-tail `fastcc` call.
- Swift's Joe Groff gives the same reasoning (2024-02-08): a tail call is possible only when the new callee uses the same amount of stack space or less. To guarantee more, you need "a Pascal-style callee-pop convention".
- Indirectly passed arguments must also live in the argument area rather than in the caller's frame. See Rust in item 5.
- Related conventions:
  - **`tailcc`** guarantees that tail-position calls are optimized. It allows no varargs and requires the prototype of every callee to match the function definition exactly.
  - **`swifttailcc`** is `swiftcc` with callee-pop.
  - **`ghccc`** passes everything in registers and has no callee-saved registers. It exists on X86, AArch64 and RISC-V, with limits: x86-64 up to 10 integer and 6 floating-point parameters; AArch64 up to 10 integer, 4 f32 and 4 f64; RISCV64 up to 11 integer, 4 f32 and 4 f64.
- The code generator manual says tail call optimization is supported on x86/x86-64, PowerPC, AArch64 and WebAssembly. Constraints by target:
  - x86, PowerPC, AArch64: no varargs.
  - PowerPC: no `byval`.
  - x86-64 PIC: only module-local callees.
  - WebAssembly: the `tail-call` target feature must be on, and return types must match.

Sources:
- https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/docs/LangRef.md (call instruction; calling conventions)
- https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/docs/CodeGenerator.md (sections on tail call and sibling call optimization)
- https://forums.swift.org/t/pitch-syntactic-tail-calls-stc/69921

## 4. Zig `@call(.always_tail, f, args)`

```zig
fn opAdd(vm: *Vm, pc: [*]const Inst) callconv(.{ .x86_64_preserve_none = .{} }) u64 {
    ...
    return @call(.always_tail, handlers[pc[1].op], .{ vm, pc + 1 });
}
```

**Documented rule** (CallModifier doc, language reference 0.17.0): it "Guarantees that the call will be generated with tail call optimization". If that is not possible, compilation fails.

**Rules enforced** (`Sema.handleTailCall`, the same logic at 0.11.0 and 0.17.1):
1. The backend must support tail calls on the target. Otherwise: "unable to perform tail call: compiler backend '…' does not support tail calls…".
2. The callee's function type must be *identical* to the caller's: parameters, return type and calling convention. Otherwise "type of function being called … does not match type of calling function". This is stricter than Clang's "similar types".
3. Sema emits `ret` right after the call.

**Open bugs show what this design misses** (all open on GitHub):
- #18223: `return n * @call(.always_tail, f, …)` is accepted and silently returns the wrong value. The call is an expression, and Sema inserts the `ret` regardless.
- #9703: an argument slice that points into the caller's stack frame is corrupted after the tail call.
- #18189: a composite return type makes LLVM abort with "failed to perform tail call elimination".
- The original proposal #694 (2018, still open) wanted `defer`s still pending after the call to be a compile error. Whether 0.17.1 rejects this, runs the defers, or skips them is UNVERIFIED. Sema emits `ret` immediately after the call, so defers placed after it look unreachable, but I did not test this.
- Sema carries a TODO saying the error-return trace is not fixed up correctly for `always_tail`.

**Backend support** (`src/target.zig` `supportsTailCall` @0.17.1)

| Backend | `always_tail` |
|---|---|
| LLVM | Yes, except MIPS and PowerPC ("LLVM does not support tail calls on them"). On wasm32/wasm64 the `tail_call` CPU feature must be enabled. |
| C backend | Yes. It emits `zig_always_tail return f(...)`. `zig.h` defines that as `__attribute__((musttail))` when the C compiler has it; otherwise it expands to an undefined identifier, so the C compile fails. `zig.h` does not use MSVC's `[[msvc::musttail]]`. |
| Self-hosted x86_64, aarch64, wasm, riscv64, spirv, others | No: compile error. The x86_64 and wasm code generators also carry a "TODO implement tail calls" error. |

Consequence: since **0.15.x** the self-hosted x86_64 backend is the default for Debug builds on x86_64, except on NetBSD, OpenBSD and Windows. The 0.15.1 notes give the reason: "around a 5x decrease" in compile time. So `.always_tail` fails to compile in default x86_64 Debug builds unless you pass `-fllvm`. The self-hosted aarch64 backend is still work in progress (0.17.0 notes), so Debug on Apple Silicon uses LLVM.

**`preserve_none`-like conventions:** yes, since **0.17.0** (2026-10-01).
- `std.lang.CallingConvention` gained `x86_64_preserve_none` and `aarch64_preserve_none`. Neither exists at 0.16.0, and the 0.17.0 release notes do not mention them.
- The LLVM backend maps both to LLVM `preserve_nonecc`.
- The C backend and `zig.h` have no mapping for them, so their behaviour on the C backend is UNVERIFIED.
- Zig 0.14 also made calling conventions a tagged union of target-specific conventions, for example `x86_64_sysv` and `aarch64_aapcs_darwin`.

Sources:
- https://ziglang.org/documentation/0.17.0/#call
- https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/Sema.zig
- https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/target.zig
- https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/llvm.zig
- https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/zig.h
- https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/lang.zig
- https://ziglang.org/download/0.15.1/release-notes.html#x86-Backend
- https://github.com/ziglang/zig/issues/18223, /9703, /18189, /694

## 5. Rust `become` (RFC 3407, explicit tail calls)

```rust
#![feature(explicit_tail_calls)]
fn op_add(vm: &mut Vm, pc: usize) -> u64 { /* ... */ become HANDLERS[vm.code[pc]](vm, pc + 1) }
```

**Status as of 2026-11-12: not accepted, not stabilized.**
- RFC PR rust-lang/rfcs#3407 has been open since 2023-04-06; it was last updated 2026-10-29.
- The tracking issue, #112788, is labelled `B-experimental`. It states the RFC "is not accepted yet" and that the feature is "implemented as part of an experiment".
- The feature gate `explicit_tail_calls` is marked `incomplete`, on nightly since 1.72.0 (`rustc_feature/src/unstable.rs` @29a5d066).
- Unresolved questions:
  - the semantics of indirectly passed and moved arguments;
  - waiting "for good LLVM support of all supported targets".
- PR #156007, "Do proper signature equality checking for tail calls", is still open.

**Rules** (RFC text @4dfb991)
- The operand must be a call that "exactly matches the function signature and calling convention". Lifetimes may differ. Calls through tables, such as `become function_table[idx](arg)`, are allowed.
- **Drop order:** `become` first evaluates the arguments, moving any locals they use. It then drops every other local, removes the caller's frame and jumps. A `return` would drop the locals after the call. The borrow checker rejects passing references to locals.
- `become` has type `!`. A never-to-any return coercion is not supported.
- Tail calls *to* or *from* closures are not allowed, because closures use the `rust-call` convention and captures are dropped too early.
- `c_variadic` functions are not allowed. Tail calls *from* `async` functions or blocks are not allowed. Operators cannot be tail-called.
- `#[track_caller]`: codegen emits a lint that tail calling such a function "has no special effect".
- If the backend cannot guarantee the tail call, that is an internal compiler error.

**Backend support** (main @29a5d066)
- LLVM: implemented with `musttail`, merged 2025-07-31 (#144232).
- `rustc_codegen_gcc`: implemented with libgccjit's `set_require_tail_call(true)`.
- Cranelift: not supported. `span_bug!` reports "tail calls are not yet supported in `rustc_codegen_cranelift` backend".

**Indirect arguments.** A by-value aggregate that the ABI passes by pointer cannot point into the caller's frame. Codegen copies such arguments into temporaries, then into the caller's own incoming argument slots. A source comment says this is valid "Because guaranteed tail calls demand that the caller's signature matches the callee's".

**Calling convention.** `extern "tail"` is gated by `rust_tail_cc`, nightly since 1.98.0. PR #157016 merged it on 2026-06-07, and #157427 tracks it. It maps to LLVM `tailcc`. It "compiles on" x86_64 and aarch64; s390x and loongarch64 work only incidentally, according to the PR.

**Debugging:** the RFC lists lost stack frames as a drawback. It suggests a flag to disable tail calls in debug builds (which reintroduces stack overflow), or debugger support for tracking recent `become` calls.

Sources:
- https://github.com/rust-lang/rfcs/pull/3407
- RFC text at https://github.com/phi-go/rfcs/blob/4dfb99128b8572df8292ca7af757125fb5199016/text/0000-explicit-tail-calls.md
- https://github.com/rust-lang/rust/issues/112788
- https://github.com/rust-lang/rust/issues/157427
- https://github.com/rust-lang/rust/pull/157016
- `compiler/rustc_codegen_ssa/src/mir/block.rs`, `compiler/rustc_codegen_cranelift/src/base.rs` and `compiler/rustc_codegen_gcc/src/builder.rs` at commit 29a5d066d98b58d948d285ba22390a5f849f5938

## 6. WebAssembly tail calls; Cranelift `return_call` and the `tail` convention

**Instructions**
- `return_call $f` and `return_call_indirect $table (type $t)`. The typed function-references proposal adds `return_call_ref`.
- Validation combines the rules for `return` and for calls. **The callee's result type must equal the caller's result type.** "Caller's and callee's parameter types do not need to match."
- Callees may be static or dynamic. The Wasm 3.0 announcement calls Wasm tail calls "fully general".

```wat
(func $op_add (param $pc i32) (param $sp i32) (result i32)
  ;; ...
  (return_call_indirect $handlers (type $op) (local.get $pc) (local.get $sp) (local.get $opcode)))
```

**Standardization.** Tail calls are part of **Wasm 3.0**, which the W3C groups declared complete on 2025-09-17 (announcement by Andreas Rossberg). The feature table lists the proposal at phase 5.

**Engine support** (webassembly.org `features.json`, fetched 2026-11-12)

| Engine | First version with tail calls |
|---|---|
| Chrome | 112 |
| Firefox | 121 |
| Safari | 18.2 |
| Node.js | 20.0 |
| Deno | 1.32 |
| Wasmtime | 22 (on by default with Cranelift; s390x since 24) |
| Wasmer | 7.1 |
| Wasmi | 0.28 |
| Wasm3 | 0.9.0 |
| WasmEdge | 0.10.0 |
| wasm2c | 1.0.42 |
| wazero | behind a flag |
| w2c2 | not supported |

**LLVM / Clang as a producer.**
- `musttail` on wasm requires the `tail-call` target feature.
- At 23.1.5, LLVM's default `generic` wasm CPU does **not** include tail calls; only `bleeding-edge` does. So `-mtail-call` is needed.
- LLVM coroutine lowering marks symmetric-transfer resumes `musttail` "on targets that support that". The Wasmtime RFC notes that C++20 coroutines therefore fail to compile to wasm without the proposal.

**Cranelift**
- CLIF has `return_call` and `return_call_indirect`.
- Wasmtime release notes:
  - 11.0.0 (2023-07-20): tail call implementation "has begun".
  - 12.0.0 (2023-08-21): the proposal can be enabled, on x86_64, AArch64 and RISC-V.
  - 22.0.0 (2024-06-20): on by default. 21.0 meant to do this, but a mistake blocked it.
- Verifier @v50.0.1:
  - the callee's convention must support tail calls, and only `tail` does;
  - "callee's calling convention must match caller";
  - "results of callee must match caller";
  - parameters may differ.
  - A PR merged 2026-10-10 (#14649) rejects struct arguments in the `tail` convention. Moving on-stack structs during a tail call was more complexity than the project wanted to accept.
- The `tail` convention @v50.0.1: in the source's words, "basically sys-v except that callees pop stack arguments".
- **Why** (Bytecode Alliance RFC "tail-calls"): callees must clean up their own stack arguments. Otherwise a chain of tail calls that each allocate stack arguments grows the stack O(n). That ends interoperability with the native convention, which needs host trampolines. The RFC called the new convention `wasm`; it shipped as `tail`.
- History:
  - #6608 (2023-06-21) first gave `tail` no callee-saved registers and used almost every register for arguments, a `preserve_none`-like design.
  - #8246 (x64, 2024-03-27), #8389 (aarch64, 2024-04-18) and #8405 (riscv64) brought callee-saved registers back, and now follow the platform ABI for callee-saves.
  - The PR bodies describe *how* but not *why* they switched back. The reason is UNVERIFIED.

Sources:
- https://github.com/WebAssembly/tail-call/blob/main/proposals/tail-call/Overview.md
- https://webassembly.org/news/2025-09-17-wasm-3.0/
- https://webassembly.org/features.json
- https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/lib/Target/WebAssembly/WebAssembly.td
- https://github.com/llvm/llvm-project/blob/llvmorg-23.1.5/llvm/docs/Coroutines.md
- https://github.com/bytecodealliance/rfcs/blob/main/accepted/tail-calls.md
- https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/cranelift/codegen/src/isa/call_conv.rs
- https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/cranelift/codegen/src/verifier/mod.rs
- https://github.com/bytecodealliance/wasmtime/blob/v50.0.1/RELEASES.md and the release notes at v11.0.0, v12.0.0, v21.0.0 and v22.0.0

## 7. Implicit proper tail calls, self-recursion-only forms, and languages that rejected or added them

- **Scheme R7RS §3.5.** Implementations "are required to be properly tail-recursive". Any call in a defined tail context is a tail call, and the implementation must support an unbounded number of active tail calls. ([R7RS, corrected HTML](https://standards.scheme.org/corrected-r7rs/r7rs-Z-H-5.html))
- **Lua 5.4 §3.4.10.** A call of the form `return f(x)` that is *not in the scope of a to-be-closed variable* is a tail call. The callee reuses the caller's stack entry, so the nesting of tail calls has no limit. A tail call "erases any debug information about the calling function". ([Lua 5.4 manual](https://www.lua.org/manual/5.4/manual.html#3.4.10))
- **Kotlin `tailrec`.** Self-recursion only, and the call must be the function's final operation. It is not allowed inside `try`/`catch`/`finally` or in `open` functions. The compiler rewrites the function into a loop. ([kotlinlang.org](https://kotlinlang.org/docs/functions.html#tail-recursive-functions))
- **Scala `@tailrec`.** It verifies that the method compiles with tail call optimization, meaning self-recursion becomes a loop. Otherwise it is a compile error. ([scala.annotation.tailrec](https://www.scala-lang.org/api/current/scala/annotation/tailrec.html))
- **Go: rejected.** Proposal golang/go#22624, "add become statement to support tail calls", was filed by Ian Lance Taylor on 2017-11-07 and closed 2019-02-19. The closing comment: "So we are not going to do it". The reasons given were that it does not address a significant problem, and that tail calls interfere with `runtime.Callers`. Robert Griesemer objected that banning `become` because of a `defer` elsewhere in the function is a "non-orthogonal interaction". ([issue](https://github.com/golang/go/issues/22624))
- **Swift: no guarantee and no accepted proposal.** The pitch "[Pitch] Syntactic tail calls (STC)" opened 2024-02-08. Joe Groff's points:
  - reliable tail calls need explicit syntax;
  - non-sibling calls also need a callee-side marker (`@tailCallable`), so the callee can use a callee-pop convention;
  - indirectly passed arguments would have to move into the argument area;
  - a marker on the `return` is still needed, because `defer` runs code after a return.

  I found no Swift Evolution proposal. The 2015 mailing-list proposal and draft PR swift-evolution#103 did not proceed. ([forum thread](https://forums.swift.org/t/pitch-syntactic-tail-calls-stc/69921))
- **Odin: added in 2026.** `dev-2026-02` (2026-02-04) added the `#must_tail` directive and the conventions `"preserve/none"`, `"preserve/most"` and `"preserve/all"`. Syntax: `return #must_tail exec(vm, instrs[1:])`.
  - Rules from the directives docs: the callee must have the same type as the caller, and must not use the `"odin"` convention, because of its implicit context pointer (PR #7692, merged 2026-10-02). On wasm the `tail-call` feature is required (PR #7658).
  - The newsletter warns of "issues with address sanitization" and calls it "a very advanced feature".
  - Open bugs: `defer` before `#must_tail` reaches an LLVM error (#6529); `#must_tail` outside top-level procedure scope (#6640; PR #7129 would forbid it); multiple return values (#7507); the `preserve/most`/`all` x64 miscompile (#7669); `preserve/none` needs LLVM 19 and works only on x64/arm64 (#7667).
  - Fixed: `dev-2026-11` emits "return cleanup" (ASan unpoisoning, lifetime markers) *before* the musttail call (#8078 for #8072).
  - ([directives](https://odin-lang.org/docs/directives/#must_tail), [newsletter](https://odin-lang.org/news/newsletter-2026-q1/))
- **C3:** has no tail-call attribute in its built-in attribute list (checked 2026-11-12). The feature list says `goto` "has been removed and replaced by" labelled `break`/`continue` and `nextcase`. ([attributes](https://c3-lang.org/language-common/attributes/), [all features](https://c3-lang.org/faq/allfeatures/))
- **Jai:** UNVERIFIED. The language is in closed beta and I found no public primary documentation.

## 8. Cleanup, debuggability, varargs and coroutines at a tail call

| System | Cleanup / destructors at the tail call | Pointers to locals | Varargs | Debuggability | Coroutines / async |
|---|---|---|---|---|---|
| Clang `musttail` | Every variable in scope must be trivially destructible; lifetimes end before the call | Undefined behaviour; warning | Error | Frames are gone (not documented) | LLVM coroutine lowering uses `musttail` for symmetric transfer |
| GCC `musttail` | Variables with non-trivial destruction make the call invalid; lifetimes end before the call | Undefined behaviour; two warnings | Not documented (UNVERIFIED) | — | — |
| MSVC `[[msvc::musttail]]` | Not documented | Not documented | `__preserve_none` forbids varargs | Requires `/O2` | — |
| LLVM IR | No access to the caller's allocas or `byval` arguments | Not allowed | Only when both sides are varargs (thunks) | — | `swifttailcc` exists for Swift async |
| Zig `.always_tail` | `defer` handling UNVERIFIED (proposal #694 wanted an error); error-return-trace TODO | Miscompiles (#9703) | — | — | Issue #4060 "Async tail recursive calls are not allowed" (open) |
| Rust `become` | Drops run **before** the call, after arguments are evaluated | Borrow-check error | Forbidden | RFC: debugging context is lost | Forbidden from `async` |
| Odin `#must_tail` | `defer` + `#must_tail` gives an LLVM error (#6529, open; no decided semantics); return cleanup moved before the call (#8078) | — | — | ASan caveat | — |
| Go (rejected) | The `defer` interaction was one reason to reject | — | — | `runtime.Callers` accuracy | — |
| Swift (pitch) | `defer` is why a `return` marker is needed | Indirect arguments must move | — | — | Swift async lowers through `swifttailcc` |
| Lua | A to-be-closed variable in scope means the call is not a tail call | — | — | Erases the caller's debug info | — |
| GNU C++ computed goto | "will not call destructors" for objects going out of scope | — | — | — | — |

The structured replacements order things the same way:
- **Zig** labeled switch evaluates the `continue :sw x` operand first, then runs the `defer`s of the exited scopes, then dispatches (`AstGen.continueExpr` @0.17.1).
- **C3** 0.8.7 (in development) fixes a bug where `nextcase x` in a `@jump` switch read `x` after the case's `defer`s had run.

Deegen uses tail calls so that the C stack is empty at every bytecode boundary. A coroutine switch is then just a tail call to the other continuation, which is how it implements stackful Lua coroutines (2022 post).

Sources: as cited in items 1–7, plus
- https://gcc.gnu.org/onlinedocs/gcc/Labels-as-Values.html
- https://codeberg.org/ziglang/zig/src/tag/0.17.1/lib/std/zig/AstGen.zig
- https://github.com/c3lang/c3c/blob/master/releasenotes.md
- https://github.com/ziglang/zig/issues/4060

---

## 9. CPython's tail-calling interpreter (3.14) and the LLVM 19 correction

**Design** (`Python/ceval_macros.h` @v3.15.0)
- Each opcode is its own function, declared `Py_PRESERVE_NONE_CC`. All handlers share one six-parameter signature: frame, stack_pointer, tstate, next_instr, dispatch table, oparg.
- Dispatch is `Py_MUSTTAIL return table[opcode](args)`.
- With Clang or GCC, the macros expand to `__attribute__((musttail))` and `__attribute__((preserve_none))`. The build fails if either attribute is missing.
- With MSVC, they expand to `[[msvc::musttail]]` and `__preserve_none`.
- Build with `./configure --with-tail-call-interp`. PGO is "highly recommended". The configure documentation requires a compiler with "proper tail call support" and `preserve_none`.

**Claims across releases**
- `v3.14.0a5` What's New: "anywhere from -3% to 30% faster" and "a geometric mean of 9-15%" on pyperformance. The baseline was 3.14 built with Clang 19, *without* the new interpreter.
- `v3.14.0a6` onward: revised to "up to 30%" and a geometric mean of 3–5%. A note says the 9–15% figure was "cautiously revised down" because of a Clang/LLVM 19 bug (llvm/llvm-project#106846) that slows the normal interpreter.
- Final 3.14.0 text: a 3–5% geometric mean; Clang 19 or newer only, on x86-64 and AArch64; opt-in. Contributed by Ken Jin (gh-128563).

**Nelson Elhage, "Performance of the Python 3.14 tail-call interpreter" (2025-03-09)**
- Setup: all builds used LTO and PGO. Compilers: Clang 18.1.8, GCC 14.2.1 and Clang 19.1.7. Results are pyperformance averages relative to clang18 with computed gotos.

  | Machine | clang19 (CG) | clang19 + taildup flags | clang19 tail-call | gcc 14 (CG) |
  |---|---|---|---|---|
  | Intel Raptor Lake i5-13500 | 1.09x slower | 1.01x faster | **1.03x faster** | 1.02x faster |
  | Apple M1 MacBook Air | 1.12x slower | 1.02x slower | **1.00x** | n/a |

- Cause: LLVM PR #78582 (merged 2024-04-17) capped the tail-duplication pass. On CPython the cap left every dispatch jump merged. Counting indirect jumps in `_PyEval_EvalFrameDefault`:

  | Build | Indirect jumps |
  |---|---|
  | clang18, computed goto | **332** |
  | clang19, computed goto | **3** |
  | clang18, plain switch | **306** |
  | clang19, plain switch | 3 |

  Clang 18 tail-duplicates even the plain switch, so computed goto was nearly redundant on Clang and "don't even suffice to guarantee it". GCC 14.2.1 does not duplicate a switch, but it does honour computed goto.
- Switch versus computed goto on Clang 18: the plain switch was 1.01x *faster*. clang19 with a plain switch was 1.02x slower than clang18, while the clang19 computed-goto build was 1.09x slower. Elhage reads about 2% as the fair value of dispatch duplication alone.
- Conclusion: against a good baseline, the tail-call interpreter's gain "drops to 1-5% or so".
- The fix, LLVM PR #114990 ("[TailDuplicator] Do not restrict the computed gotos"), merged 2025-03-10. It is in **LLVM 20.1.2** (2025-04-02, but not 20.1.0 or 20.1.1) and in 21.1.0 and later. The LLVM 21 source comment explains that computed gotos are factored early in compilation and must be "unfactored" again later.
- The regression's reporter, Mikulas Patocka of the Ajla language, posted a computed-goto microbenchmark. Times in seconds; clang-19 is the merged-dispatch build:

  | CPU | gcc-14 | clang-18 | clang-19 |
  |---|---|---|---|
  | Sandy Bridge | 2.1 | 2.3 | 4.2 |
  | Zen 4 | 1.0 | 1.1 | 1.1 |
  | AMD K10 | 5.2 | 5.3 | 5.8 |
  | Cortex-A72 | 6.5 | 6.4 | 9.5 |
  | RISC-V JH7110 | 39.2 | 46.9 | 47.7 |

  He notes that Zen 4 shows no slowdown.

**3.15 (docs say released 2026-10-09)**
- 64-bit builds with Visual Studio 2026 (MSVC 18) may use the tail-calling interpreter. The official python.org Windows x64 binaries now do.
- Results with VS 18.1.1: a 15–20% geometric-mean speedup on pyperformance on an AMD Ryzen 7 5800X, over the *switch-case* interpreter. MSVC has no computed goto, so the switch is the baseline. Observed speedups range from 14% for large pure-Python libraries to 40% for long-running small scripts. (gh-143068)

Sources:
- https://github.com/python/cpython/blob/v3.14.0a5/Doc/whatsnew/3.14.rst
- https://github.com/python/cpython/blob/v3.14.0a6/Doc/whatsnew/3.14.rst
- https://docs.python.org/3.14/whatsnew/3.14.html#whatsnew314-tail-call-interpreter
- https://docs.python.org/3.15/whatsnew/3.15.html
- https://github.com/python/cpython/blob/v3.15.0/Python/ceval_macros.h
- https://github.com/python/cpython/blob/v3.15.0/Doc/using/configure.rst
- https://blog.nelhage.com/post/cpython-tail-call/
- https://github.com/llvm/llvm-project/issues/106846
- https://github.com/llvm/llvm-project/pull/114990
- https://github.com/llvm/llvm-project/blob/llvmorg-21.1.0/llvm/lib/CodeGen/TailDuplicator.cpp
- http://www.jikos.cz/~mikulas/testcases/clang/computed-goto.txt

## 10. Josh Haberman, "Parsing Protobuf at 2+GB/s: How I Learned To Love Tail Calls in C" (2021-04-21)

- **Claim:** protobuf parsing at over 2 GB/s, "more than double the previous state of the art". He warns that "tail calls == 2x speedup" is the wrong lesson, because several techniques contributed. The design is shared with Gerben Stavenga and modelled on Wasm3's "meta machine".
- **Numbers** (upb PR #310, 2020; the hardware is not stated):

  | Benchmark | Before | After | Change | Throughput |
  |---|---|---|---|---|
  | `Parse_Upb_FileDesc_WithArena` | 11.7 µs | 3.4 µs | −71% | 2.03 GB/s |
  | `Parse_Upb_FileDesc_WithInitialBlock` | 11.4 µs | 3.1 µs | −73% | 2.28 GB/s |

  The C++ proto2 parser reached about 225–402 MB/s on the same benchmark.
- **Mechanism.** One small function per operation, each ending in a `MUSTTAIL` dispatch through a table. Tail calls make each function boundary a point where register allocation is fixed: the six arguments live in registers. Slow paths are separate functions, so they no longer degrade the fast path.
  - This builds on Mike Pall's 2011 analysis of why C compilers handle big interpreter loops badly: register allocation fails on large, highly connected CFGs, and slow paths contaminate fast paths.
- **Limitations he lists:**
  - Any non-tail call forces a stack frame and spills.
  - `musttail` was Clang-only.
  - The portable fallback is a trampoline loop that returns once per iteration.
- **2025 update** ("A Tail Calling Interpreter For Python (And Other Updates)"):
  - Use `preserve_none` on the handlers and, optionally, `preserve_most` on slow paths called without a tail call.
  - GCC added `musttail` in July 2024.
  - WG14 paper N3266 proposes `return goto f(x);` for C, with looser signature rules than Clang. Its C2y status is UNVERIFIED.
  - Clang's "portable" rules buy no real predictability, because some targets (wasm without the tail-call extension) cannot do tail calls at all.

Sources:
- https://blog.reverberate.org/2021/04/21/musttail-efficient-interpreters.html
- https://github.com/protocolbuffers/upb/pull/310
- https://blog.reverberate.org/2025/02/10/tail-call-updates.html

## 11. Wasm3, LuaJIT's interpreter, Deegen / LuaJIT Remake

**Wasm3 ("M3", Massey Meta Machine).**
- Wasm bytecode is compiled into "operations": C functions with **one fixed signature**, `void * Op(pc_t pc, u64 * sp, u8 * mem, reg_t r0, f64 fp0)`. Each operation calls the next, so the VM registers live in CPU registers (pc in rdi, sp in rsi, and so on).
- Wasm3 relies on the C compiler's ordinary tail-call optimization. A later commit added `M3_MUSTTAIL`, which uses `__attribute__((musttail))` when available (present by commit `1a6ca56`, 2022-09-12).
- Loops, calls and traps unwind by *returning*, so stack depth stays bounded even without guaranteed tail calls.
- The Windows x64 convention passes only four arguments in registers; Wasm3 uses `vectorcall` there.
- Numbers (`docs/Interpreter.md`; 2014 4 GHz Core i7 iMac; M3 built with `clang -Os`, the C references with `gcc -O3`):

  | Benchmark | M3 | Native GCC | M3 / GCC | Others |
  |---|---|---|---|---|
  | Mandelbrot | 17.9 s | 4.1 s | 4.4x (3.7x on a 2016 MacBook Pro) | Lua port 122 s; "Life" 547 s |
  | CRC32 | 5.1 s | 0.6 s | 8.5x | — |

  Summary claim: "around 4-15X slower than compiled code", and about 3x faster than Lua on basic benchmarks.

**LuaJIT.**
- The interpreter is hand-written assembly, one DynASM `vm_*.dasc` file per architecture.
- Mike Pall explained why on lua-l (2011-02-15), in reply to Josh Haberman:
  - In a switch interpreter, diamond-shaped control flow defeats optimization and register allocation.
  - Computed goto replicates dispatch, but the compiler cannot keep the same register assignment before each `goto`.
  - Tail-merging and CSE "will happily join" the dispatch points again.
  - In assembly you can keep a fixed register assignment, move slow paths out of line, and pre-decode the next instruction.

**Deegen / LuaJIT Remake (Haoran Xu).**
- Deegen generates the interpreter from C++ bytecode semantics, emitting LLVM IR rather than C.
- That lets it use the **GHC calling convention** (no callee-saved registers) and unify all handler prototypes. The two C-level `musttail` problems he names are the lost callee-saved registers ("6 (or 8...) of the 15 registers" on x86-64) and Clang's identical-prototype rule.
- 2022-11-22 post: LJR's interpreter is 28% faster than LuaJIT's interpreter and 171% faster than PUC Lua, and wins 31 of 34 benchmarks (Intel i7-12700H, 32 GB DDR4). Much of the gain comes from inline caching rather than from dispatch.
- Paper, arXiv 2411.11469 (Xu & Kjolstad, 2024), over 44 benchmarks:
  - interpreter: 179% faster than PUC Lua and 31% faster than LuaJIT's interpreter;
  - baseline JIT: 360% faster than PUC Lua and 33% slower than LuaJIT's optimizing JIT (faster on 13 of 44).
- Haberman (2025) notes that LJR has no garbage collector, so the comparisons disable GC for LuaJIT too. I did not confirm this in the paper.

Sources:
- https://github.com/wasm3/wasm3/blob/v0.5.0/docs/Interpreter.md
- https://github.com/wasm3/wasm3/blob/1a6ca56ee1250d95363424cc3a60f8fd14f24fa7/source/m3_config_platforms.h
- http://lua-users.org/lists/lua-l/2011-02/msg00742.html
- https://sillycross.github.io/2022/11/22/2022-11-22/
- https://arxiv.org/abs/2411.11469

## 12. Computed goto (labels as values)

```c
static void *table[] = { &&op_add, &&op_sub, &&op_halt };
#define DISPATCH() goto *table[*pc++]
op_add: /* ... */ DISPATCH();
```

**GCC "Labels as Values"**
- `&&label` yields a `void *`; `goto *exp;` jumps to it.
- There is no bounds check, and you must not jump into another function.
- Label addresses may differ when the function is inlined or cloned.
- Storing label differences (`&&foo - &&foo`) keeps the table read-only under PIC.
- In GNU C++, a computed goto "will not call destructors".
- The manual recommends `switch` unless the problem does not fit one.

**Compiler support**
- **Clang** has supported it efficiently since LLVM 2.7 (Chris Lattner, LLVM blog, 2010) through `blockaddress` and `indirectbr`.
  - Front ends emit at most one `indirectbr` per function, factoring N×M edges. Code generation tail-duplicates it back into per-site jumps.
  - Functions that contain `indirectbr` were not inlined, by LLVM or by GCC.
  - The blog cites "around 15% is typical" for interpreter loops.
- **MSVC** does not support it. Microsoft documents no labels-as-values extension.
  - Lua 5.4.7 enables its jump table only `#if defined(__GNUC__)`.
  - CPython's Windows build compares against the "switch-case interpreter".
  - I found no primary Microsoft statement that explicitly rules it out.

**Per-site dispatch is not guaranteed even with computed goto.** LLVM 19 merged CPython's 332 dispatch jumps down to 3 (item 9). The fix keeps duplicating computed-goto blocks after register allocation.

**Ertl & Gregg**
- "The Structure and Performance of Efficient Interpreters" (JILP vol. 5, 2003; simulation):
  - Indirect branches are 3.2–13% of executed instructions. In some simulated configurations they take more than half of run time.
  - With a BTB, threaded code mispredicts 57–63% of dispatches; a two-bit BTB, 50–61%; profile-guided prediction, 71–88%.
  - Switch dispatch mispredicts 98% for OCaml and Scheme48 and 81–86% for Yap with BTB or two-bit BTB; profile-guided, 84–100%.
  - Two-level predictors stay below about 10% for all interpreters.
  - The reason: each threaded dispatch site gives the predictor a kind of one-deep history.
- PLDI 2003, "Optimizing Indirect Branch Prediction Accuracy in Virtual Machine Interpreters":
  - BTB accuracy for existing interpreters is "only 2%–50%".
  - Replicating VM instructions plus superinstructions removes nearly all dispatch mispredictions. Speedups reach 3.17x over efficient threaded-code interpreters, and up to 1.3x over superinstructions alone.
- Their 2001 Euro-Par paper (quoted by Rohou et al.) found threaded code "almost twice as fast as switch based interpreters".

**Rohou, Swamy & Seznec, "Branch Prediction and the Performance of Interpreters — Don't Trust Folklore" (CGO 2015)**
- Hardware: Nehalem W3550 3.07 GHz, Sandy Bridge i7-2620M 2.70 GHz, Haswell i7-4770 3.40 GHz, plus an ITTAGE simulation.
- Python 3.3.2 on the Unladen Swallow benchmarks:

  | Measure | Nehalem | Sandy Bridge | Haswell |
  |---|---|---|---|
  | Average speedup from threaded code | 10.1% (some outliers at 30–40%) | 4.2% | 2.8% |
  | Python 2 opcode prediction | 2.8% | 3.2% | 1.8% |

- Threaded code also executes about 3.3% fewer instructions.
- Branch mispredictions fall from 12–16 MPKI (Nehalem) to 4–8 (Sandy Bridge) to 0.5–2 (Haswell). ITTAGE lands in the Haswell range.
- Conclusion: indirect branch prediction accuracy "is no longer critical for interpreters".

**Security.** John McCall (Swift forums, 2024-02-08) argues that a serious engine needs control-flow integrity on goto labels, such as pointer authentication. That protection "can eat up a lot of the benefit" of computed goto. Long runs of small cases can also exhaust the processor's capacity to schedule authentications.

Sources:
- https://gcc.gnu.org/onlinedocs/gcc/Labels-as-Values.html
- https://blog.llvm.org/2010/01/address-of-label-and-indirect-branches.html
- https://github.com/lua/lua/blob/v5.4.7/lvm.c
- https://www.jilp.org/vol5/v5paper12.pdf
- https://www.complang.tuwien.ac.at/papers/ertl%26gregg03.ps.gz
- https://inria.hal.science/hal-01100647
- https://forums.swift.org/t/pitch-syntactic-tail-calls-stc/69921

---

## 13. Zig labeled switch: `continue :sw value` (0.14.0, 2025-03-05)

```zig
vm: switch (code[pc].op) {
    .add => { stack[sp - 2] +%= stack[sp - 1]; sp -= 1; pc += 1; continue :vm code[pc].op; },
    .jmp => { pc = code[pc].arg; continue :vm code[pc].op; },
    .halt => break :vm stack[sp - 1],
}
```

**Semantics** (0.14.0 release notes)
- A `continue` targeting a labeled switch takes one operand, which becomes the switch's replacement operand. This is equivalent to a switch inside a loop with a state variable.
- `break :sw v` makes the switch expression produce `v`.
- An unlabeled `break` never targets a switch.
- A switch with `continue`s is not implicitly evaluated at compile time, though it can be forced in a `comptime` context.
- `defer`s: the operand is evaluated first, then the `defer`s of the exited scopes run, then dispatch happens (`AstGen.continueExpr` @0.17.1).

**Promised code generation**
- A compile-time-known operand becomes an unconditional branch.
- A runtime operand means "each continue can become a separate conditional branch (ideally via a shared jump table)". This gives the branch predictor distinct branch sites.
- Code size may grow.
- Any implementation is "free to lower this syntax however it wishes". The official compiler "will attempt to match" this lowering, particularly in ReleaseFast. **So this is a best-effort promise, not a guarantee.**
- Measured: rewriting Zig's tokenizer with it gave "a 13% performance boost" (PR #21367, 2024-09-11; hardware not stated).

**Lowering by backend** (source @0.17.1)

| Backend | Comptime-known operand | Runtime operand |
|---|---|---|
| LLVM | direct `br` to the case | **One shared jump table plus one `indirectbr` per `continue` site**, after a bounds check that branches to `else`. The table is used when values are dense and unsigned, up to 1024 entries ("about 8K of pointers"). A source comment explains that LLVM's own lowering duplicated the table per case with an extra conditional branch, "really, really, not what we want". On wasm there is no table, because `blockaddress` is unsupported there. Without a table, each site emits its own LLVM `switch`. |
| Self-hosted x86_64 | direct jump | Per-site `jmp [table + index*8]` |
| C backend | `goto zig_switch_N_dispatch_K` | Assigns the state variable and jumps back with `goto zig_switch_N_loop`: one shared `switch`, so per-site dispatch depends on the C compiler |
| Self-hosted wasm | — | `br_table` (wasm has no indirect jumps) |
| Self-hosted aarch64 | — | Supports the construct; lowering details UNVERIFIED |

The Zig compiler dogfoods the construct: its aarch64 instruction selector dispatches with `continue :air_tag`.

**Origin.** Proposal ziglang/zig#8220, "introduce labeled continue syntax inside a switch expression", by Andrew Kelley (2021-03-12). It was implemented by mlugg in PR #21257, merged 2024-09-05.
- Motivation: Zig removed `goto` in #630 and so cannot express computed goto. LLVM "un-inlined" a hand-duplicated dispatch back into a single jump. The proposal cites the "context problem" from Zaleski's dissertation: a single dispatch branch shares one BTB entry.
- Alternatives the proposal considered: guaranteed tail calls also work, but:
  - tail calls "don't work on some architectures";
  - debugging tail-call dispatch is harder;
  - tail calls force the logic into separate functions.

Sources:
- https://ziglang.org/download/0.14.0/release-notes.html#Labeled-Switch
- https://github.com/ziglang/zig/issues/8220
- https://github.com/ziglang/zig/pull/21257
- https://github.com/ziglang/zig/pull/21367
- https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/llvm/FuncGen.zig
- https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/x86_64/CodeGen.zig
- https://codeberg.org/ziglang/zig/src/tag/0.17.1/src/codegen/c.zig

## 14. Rust `#[loop_match]` and `#[const_continue]` (nightly, 2025)

```rust
#![feature(loop_match)]
#[loop_match]
loop {
    state = 'blk: {
        match state {
            State::A => { #[const_continue] break 'blk State::B }
            State::B => { /* ... */ }
        }
    }
}
```

**Status**
- Feature `loop_match` is marked `incomplete`, on nightly since 1.90.0. The tracking issue, #132306, calls it a lang experiment, approved in April 2025.
- PR #138780, by Folkert de Vries, merged 2025-06-25.
- RFC 3720, "loop match", has been open since 2024-10-28 and is not accepted. The implementation "does not exactly implement" the RFC; the RFC will follow the experiment.

**Semantics.** Observable behaviour is unchanged by the attributes. `#[const_continue]` is allowed **only when the next state is known at compile time**; the arm then jumps directly to the next arm. The loop body must have exactly the shape shown above.

**Motivation**
- Large C state machines in zlib and bzip2 decompression, ported to Rust in zlib-rs and libbzip2-rs, rely on `switch` fallthrough and direct jumps.
- In Rust, `loop` + `match` re-dispatches through an unpredictable branch, and LLVM sometimes builds a jump table even when the targets are known.
- RFC 3720 also proposes `loop match x { … continue v … }`. For a non-constant `continue`, its prototype duplicated the match per site, but "it is not currently clear" whether that lowering will stay.

**Numbers** (posted by Folkert de Vries on the project goal, 2025-03-18; hardware not stated)

| Benchmark | Baseline | LLVM DFA jump-threading flag | `loop_match` |
|---|---|---|---|
| comrak `autolink_email` | 1.126 s | 583.9 ms | **411.4 ms** (2.74x faster than baseline; 1.42x faster than the flag) |
| zlib-rs decompression, 16-byte chunks | — | −4.8% wall time | **−7.8% wall time, −8.5% cycles, −6.6% instructions** |

RFC 3720's zlib-rs report: a proof-of-concept `loop match` is about 15% faster than a *tail-call* version in some benchmarks (−14.0% wall time, −15.4% cycles, −24.9% instructions). The tail-call version keeps reloading state from memory, while the single-frame loop keeps values on the stack or in registers.

Sources:
- https://github.com/rust-lang/rust/issues/132306
- https://github.com/rust-lang/rust/pull/138780
- https://github.com/rust-lang/rust-project-goals/issues/258#issuecomment-2732965199
- https://github.com/rust-lang/rfcs/pull/3720 (text at head 12af0ec)
- https://doc.rust-lang.org/nightly/unstable-book/language-features/loop-match.html

## 15. C3 `nextcase`

```c3
switch MAIN: (state)
{
    case LOAD:  /* ... */ nextcase EXEC;      // constant target
    case EXEC:  /* ... */ nextcase code[pc];  // runtime target is allowed
    case SUB:   nextcase MAIN: LOAD;          // jump to an enclosing labelled switch
    default:    nextcase default;
}
```

- Plain `nextcase;` falls through to the next case.
- `nextcase expr;` jumps to the case matching `expr`. **`expr` may be a runtime value**; the docs' own example is `nextcase rand();`.
- `nextcase default;` and `nextcase LABEL: value;` target the default case or an enclosing labelled switch.
- The docs present this as structured goto for state machines.
- In "runtime evaluation" switches (`switch (true) { case x < 0: … }`), `nextcase expr` re-tests the cases from the top.
- **No computed goto.** `goto` was removed. Instead, C3 **0.6.0** (2024-06-12) added the `@jump` attribute on switches:
  - It "Turns switches to guaranteed jump tables", for integer and enum cases only.
  - The docs present it as the replacement for C's computed goto, and say it may be faster or slower than a regular switch.
  - "nextcase statements will also use jumptable dispatch when @jump is used." Whether each `nextcase` site gets its own indirect jump in machine code is not stated: UNVERIFIED.
- `defer` interaction: the 0.8.7 changelog (in development) fixes `nextcase x` in a `@jump` switch reading `x` after the case's `defer`s had run.
- No tail-call attribute exists (item 7).

Sources:
- https://c3-lang.org/language-fundamentals/statements/ (sections on `nextcase` and `@jump`)
- https://c3-lang.org/language-common/attributes/
- https://c3-lang.org/faq/allfeatures/
- https://github.com/c3lang/c3c/blob/master/releasenotes.md (sections 0.6.0 and 0.8.7)

---

## What a language must decide

1. **Syntax style.**
   - *Tail call as a statement form*:
     - attribute on `return` (Clang, GCC, MSVC);
     - `return #must_tail f()` (Odin);
     - `become f()` (Rust);
     - the C proposal `return goto f()` (N3266).
   - *Tail call as an expression-level builtin*: Zig's `@call(.always_tail, …)`. Zig therefore accepts `n * @call(.always_tail, …)` and miscompiles it (#18223).
   - *Structured dispatch*:
     - Zig `continue :sw value`;
     - C3 `nextcase value`;
     - Rust `#[const_continue] break 'blk value`, compile-time values only, with RFC 3720 proposing `continue value`.
   - A statement form that *is* the function exit, checked in the front end for tail position, avoids the class of bugs Odin hit (#6640, #6529) and Zig hit (#18223). In both, invalid uses reached LLVM and surfaced as backend errors.

2. **Signature rule.** Options seen:
   - exact type identity, including the calling convention: Zig, Odin, Rust;
   - C-"similar" types with the same argument count: Clang;
   - stack-fit rules: MSVC ("can't use more stack space"), LLVM sibling calls;
   - free parameter lists under a **callee-pop** convention, with return types still matching: LLVM `tailcc`/`swifttailcc`, Cranelift `tail`, Wasm `return_call`, Rust `extern "tail"`, the proposed `tail_chaincc`.

   With chibi's rule that every function uses the C convention (caller-pops), the options are:
   - an exact-signature rule. Rust's codegen shows it also makes by-pointer aggregate arguments sound, because they are copied into the caller's own argument slots;
   - forbidding stack-passed and indirect arguments in tail calls. MSVC's `__preserve_none` simply rejects stack parameters, and Cranelift rejected struct arguments in `tail` in October 2026;
   - a second, opt-in callee-pop convention for tail-callable functions. Swift's Groff argued a callee-side marker is needed for exactly this reason.

   Every source requires matching *return* types.

3. **Cleanup rule** (`defer`, `defer catch`, locals' addresses).
   - Precedents:
     - Rust: evaluate arguments, then drop locals, then jump.
     - Zig labeled switch, and C3 after its fix: evaluate the operand, then run `defer`s, then jump.
     - Odin: "return cleanup" is emitted before the call; user `defer` semantics are still undecided (#6529).
     - Clang and GCC: reject non-trivial destructors in scope.
     - Go: rejected the feature partly over its `defer` interaction.
     - Lua: a to-be-closed variable demotes the call to a non-tail call, which is acceptable only because Lua's tail calls are implicit.
   - Chibi-specific: a tail call ends the caller's frame. If the callee later returns an error, the caller's `defer catch` can no longer run. Zig has the analogous unsolved problem with its error-return trace for `always_tail`. The language must either forbid a tail call while a `defer catch` is live, or define the tail call as a successful exit of the caller (run plain `defer`s, skip `defer catch`) and document it.
   - Addresses of locals passed as arguments must be rejected (as Rust does) or declared undefined behaviour (Clang, GCC). Zig's silent corruption (#9703) is the failure case.
   - Varargs must be rejected (Clang, Rust, MSVC `__preserve_none`, LLVM outside thunks).

4. **Calling convention.**
   - Under the C convention every handler saves and restores callee-saved registers. Deegen counts this as 6–8 of x86-64's 15 registers lost.
   - Haberman shows any non-tail call inside a handler forces a frame and spills. That fits chibi's measured 8% loss on the call-heavy program; it is not verified as the cause.
   - Production answer: `preserve_none` on the handlers only. CPython, Odin and Zig 0.17 do this. Availability:
     - Clang 19+ on x86-64 and AArch64, with LoongArch in LLVM IR;
     - GCC 16+ on x86 and AArch64;
     - MSVC 14.50+, C only, x64 only, at most 10 register-only parameters.
   - Hazards:
     - every vendor marks the ABI unstable;
     - GCC and Clang document AArch64 differently;
     - `preserve_most`/`all` combined with tail calls that change arguments is unsound on x64 (Odin #7669);
     - making a no-callee-saves convention the default for *all* functions is a different trade-off. Cranelift tried that for `tail` in 2023 and restored callee-saves in 2024 (reason UNVERIFIED).
   - Chibi's "all functions use the C convention" rule would need one opt-in exception, or chibi gives up most of the register benefit.

5. **Backend obligations.**
   - **Own debug backend:** it must implement tail calls itself, including at its lowest optimization level. Zig's self-hosted backends never did. Since 0.15, `always_tail` fails in default x86_64 Debug builds there, while labeled switch works, because a jump table plus a per-site `jmp` is easy to emit.
   - **C backend:**
     - three spellings: Clang 13+ `musttail`; GCC 15+ `musttail`, where `-O0` success is not promised; MSVC 14.50+ `[[msvc::musttail]]`, x64 only and `/O2` only;
     - `preserve_none` needs Clang 19+, GCC 16+ or MSVC 14.50+;
     - computed goto works on GCC and Clang, not MSVC;
     - a plain `switch` relies on the C compiler's tail duplication, which Clang does, GCC does not, and LLVM 19.1.x–20.1.1 broke for computed goto.
   - **WebAssembly:** LLVM needs `-mtail-call`, which is not in the `generic` CPU at 23.1.5. Engines need Chrome 112, Firefox 121, Safari 18.2, Wasmtime 22 or later. There are no indirect jumps, so labeled switch becomes `br_table`, and a function-table tail call becomes `return_call_indirect`.
   - **GPU:**
     - WGSL forbids recursion: "functions must not be recursive, either directly or indirectly" ([WGSL](https://www.w3.org/TR/WGSL/)).
     - Core SPIR-V has function pointers only through the `SPV_INTEL_function_pointers` extension.
     - Metal shading language support: UNVERIFIED.
     - Tail calls and computed goto therefore need a defined fallback, or a compile error, on GPU targets. Labeled switch lowers to `loop` + `switch` everywhere.
   - In every backend, the front end must catch what the backend would reject. Clang, Zig and Odin all leak LLVM's fatal "failed to perform tail call elimination".

6. **Per-site dispatch guarantee.**
   - None of the source-level constructs truly *guarantees* one indirect branch per dispatch site:
     - GNU computed goto: LLVM 19 merged 332 jumps to 3;
     - Zig labeled switch: "will attempt";
     - C3 `@jump`: guarantees a table, not per-site replication;
     - Rust `#[const_continue]`: guarantees direct jumps only for constant targets.
   - Tail-call dispatch gives per-site branches *structurally*: each handler ends in its own jump. That robustness against compiler regressions is Elhage's main argument for `musttail`.
   - chibi's own native backend can make labeled-switch `continue` a hard guarantee, as a per-site `jmp [table + i*8]`, because chibi controls code generation. In the C backend it can only request it: computed goto on GCC and Clang, nothing on MSVC.

7. **Expected payoff depends on hardware.** Published dispatch-duplication gains:

   | Source | Gain |
   |---|---|
   | Ertl & Gregg, 2001-era CPUs | ~2x |
   | LLVM 2010 blog | ~15% |
   | Rohou et al., Python 3.3 on Nehalem / Sandy Bridge / Haswell | 10.1% / 4.2% / 2.8% |
   | Elhage, CPython | ~2% |
   | Patocka microbenchmark | 1.8x on Sandy Bridge, 1.5x on Cortex-A72, 0% on Zen 4 |
   | CPython tail calls vs a computed-goto baseline | 1–5% |
   | CPython tail calls vs MSVC's plain switch | 15–20% |

   Chibi's M4 Pro result (16–25% fewer cycles per op on 3 of 4 programs) sits at the high end of this range. That is plausible for a small register VM, but no published Apple-M-series measurement was found to compare against. The same lowering should be re-measured whenever the toolchain changes.

8. **Debuggability and security.**
   - Tail calls erase caller frames (Lua, Rust RFC). Go cites `runtime.Callers`. Both Zig's proposal and RFC 3720 cite harder debugging as a reason to prefer a single-frame `loop`/`switch` construct.
   - Function-pointer and label tables are control-flow-integrity targets. McCall notes pointer authentication can erase much of computed goto's benefit, which matters on arm64e.
