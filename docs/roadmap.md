# Roadmap

Updated 2026-11-27, and with every step that changes it. This page gives the order of the work and where it stands. The owner's decisions behind the order are in [decisions.md](decisions.md), each built step's details and measurements are in [research/checker-and-ir.md](research/checker-and-ir.md) section 8, and open questions are in [research/README.md](research/README.md).

## Where things stand

- **Built:** the lexer and parser; the checker and its typed IR, through step 3k; the one-pass arm64 development backend (JIT); the C backend with `chibi build` and `chibi c`; the C-to-chibi translator and the bootstrap check; the x86-64 development backend's integers, control flow and `switch` (3x.0–3x.2); and debug info for both development backends' functions, lines and frames (3x.3.1).
- **The compiler's source is still C.** `make translated` translates it to chibi on every run, and the translated compiler rebuilds its own C byte for byte. On the Ryzen 9 9900X the translation runs within 1% of the C build's cycles.

## Now: before the switch

The owner chose on 2026-11-25 to build the x86-64 development backend and debug info in C, translated, before chibi becomes the compiler's source.

1. **3x.3 Debug info for the development backends.**
   - 3x.3.1 Function names, line tables, unwinding through gdb's JIT interface, and a perf map. Built 2026-11-27. Debug info costs 19% more code generation time than none (365.9 against 306.9 cycles per line on the Ryzen 9 9900X), and nothing when off.
   - 3x.3.2 A failed check stops at its own line, not the function's last. Next; it changes the generated code, so it comes with measurements and a choice for the owner.
   - 3x.3.3 Locals, parameters and their types in the debugger.
   - lldb: checked 2026-11-27 on x86-64 and arm64 Linux and on GitHub's macOS runner, and CI runs gdb and lldb on every push ([experiments/debuggers](../experiments/debuggers/README.md)).
2. **3x.4** Division, shifts and bit counts on x86-64, then floats.
3. **3x.5** Pointers, arrays, structs, slices, bounds checks and stack probes on x86-64.
4. **3x.6** Calls into C on x86-64: System V's classes for structs, variadic calls, callbacks, exports and C globals.
5. **3x.7** Optionals, errors and tuples, then 16- and 64-byte vectors and 128-bit integers. x86-64 hosts then use the x86-64 backend by default.
6. **3x.8** Time x86-64 code generation against arm64's on the corpus.
7. **The switch.** chibi becomes the compiler's source (the Compiler rows of 2026-11-17 and 2026-11-23). The merged source must run on GitHub's macOS runner.

## After the switch

- **Compile-time code:** `$` marks compile-time names and expressions, `const` always evaluates at compile time, and a top-level `$if` may declare (decided 2026-11-25 and 2026-11-26).
- **The build program** as compile-time code in any file, adding steps to a graph that may grow (decided 2026-11-26).
- **Dependencies:** a flat list pinned by git commit or hash, fetched through the `git` command at first. The curated libraries: SDL2, SDL3, Box2D, Box3D, stb_truetype, kb_text_shape, stb_image, stb_vorbis, cgltf, Dear ImGui, ENet and Vulkan-Headers ([libraries.md](libraries.md)).
- **Generics and packages.**
- **chibi's own optimizing backend,** written in chibi once compile-time code, generics and packages exist (decided 2026-11-27). Debug builds start on the one-pass backend's code and swap in its code for hot functions while the program runs, and it replaces the C backend for release builds.
- **The C tests in chibi,** translated once chibi has what they need, such as `@test` and calls through function values (the Compiler row of 2026-11-25).
- **Still to decide,** in [research/README.md](research/README.md) under "After self-hosting": reflection, tasks and coroutines, the memory model, hot reload, targets such as WebAssembly, and the GPU recommendations.

## Long term

The 2026-11-09 goal is a chibi that needs no C compiler:

- a C header parser written in chibi, in place of clang for imports;
- chibi's own linker;
- prebuilt archives of the curated C libraries;
- a git client of chibi's own, using the operating system's TLS (decided 2026-11-27).

GPU output follows its own order: SPIR-V first, then MSL and WGSL, all from one typed IR (decided 2026-11-10).
