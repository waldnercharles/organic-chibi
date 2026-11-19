# Shader backends: how real compilers produce GPU code

Research note, 2026-11-10. Question: how do real compilers turn source code into GPU code, and which architecture fits chibi? [shader-languages.md](shader-languages.md) section 6 recommends emitting SPIR-V straight from chibi's typed IR, then adding MSL and WGSL printers, and it leans on Cute Framework's `cute_spirv.h`. The owner asked for a decision that does not rest on one library. This note compares sixteen compilers and pipelines.

**Scope.** For each compiler: the input language and the internal representation, how each output is produced, dependencies and size, published compile speed, and how it handles structured control flow, validation and debug info. Section 5 names the recurring architectures and weighs them for chibi. Section 6 says what changes in shader-languages.md section 6. SDL3's formats, SDL_shadercross, CF and the converters are described in shader-languages.md sections 4 and 6, and are not repeated here.

**Sources.** I read each project's source at the commits below, from sparse clones, and counted lines myself. "Code lines" come from `scc` and leave out blank lines and comments; "lines" come from `wc -l`. Speeds come from published measurements, mostly one benchmark of eight compilers on one corpus (section 3). Source text is paraphrased, not quoted, and each claim links to where I read it. Claims I could not read directly are marked "(unverified)" and listed in section 7. "My analysis" marks reasoning, and "my arithmetic" marks numbers I derived.

| Project | Commit, date | Project | Commit, date |
|---|---|---|---|
| Slang | `4f66408b`, 2026-11-10 | Zig 0.17.1 (Codeberg) | `be248cc1`, 2026-11-02 |
| glslang | `b411a8fb`, 2026-11-09 | Mojo (modular repository) | `f364f26c`, 2026-11-10 |
| DXC | `823a3dad`, 2026-11-09 | Circle's shader documentation | `185c6e81`, 2022-03-09 |
| LLVM and Clang | `23df5cc0`, 2026-11-10 | Godot | `75ef9877`, 2026-11-10 |
| Naga (in wgpu) | `4fe54b02`, 2026-11-10 | bgfx | `4608478e`, 2026-11-09 |
| Tint (in Dawn) | `b73cc737`, 2026-11-10 | sokol-tools | `f1dcd178`, 2026-11-10 |
| rust-gpu | `60d1e254`, 2026-10-27 | Cute Framework | `6b00978a`, as in shader-languages.md |

Also read: SPIRV-Cross `57e4efcc`, Mesa `a379b735`, gogpu/naga `69b15c79`, Unity's HLSLcc `3ea1fcd6`, SPIR-T `e8757adb`, chibi-v2's [H9 interpreter](../../experiments/h9-interpreter/README.md), and the official pages of Microsoft, Apple, Unity and RenderDoc linked where used.

## 1. Summary

### 1.1 Recommendations, ranked by the cost of deciding late

1. **GPU writers read a typed, structured IR, not interpreter bytecode.** That IR keeps four things: structured control flow with explicit merge points, a type for every value, field and element indices instead of byte offsets, and a separate type for each buffer layout. Compilers that lost the structure had to rebuild it: LLVM's SPIR-V backend runs five passes for it, and rust-gpu built a new IR. Before Slang wrote its direct SPIR-V emitter, its lead named type layouts and address spaces among the hard parts. H9's interpreter IR uses frame byte offsets and plain jumps, so GPU writers need the level above it (section 4). This ranks first because the IR is being designed now.
2. **Use one IR with one writer per output format.** Slang, Naga and Tint all work this way, and `cute_spirv.h` does the same from its syntax tree. SPIR-V is written as binary; MSL, WGSL and HLSL are printed as text. This fits the speed budget, needs no C++ or Rust library, keeps hot reload inside the compiler, and keeps chibi's names in every output (section 5).
3. **Run no optimizer in the GPU path.** Drivers and platform compilers optimize anyway, and Slang, CF, Naga and Tint rely on that. In the Khronos stack, legalizing and optimizing take about 80% of the compile time (section 3). DXC and glslang need SPIRV-Tools mainly to legalize HLSL that puts textures in structs or copies them into locals ([SPIR-V.rst](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/SPIR-V.rst#L490-L511)). chibi's GPU rules can forbid those patterns instead, as an addition to shader-languages.md section 4's check list (my analysis). spirv-opt can become an optional release step once it is measured.
4. **Write debug info from the first SPIR-V module.** Always emit names. In debug builds, emit `NonSemantic.Shader.DebugInfo.100` with chibi's source, so RenderDoc steps through chibi lines, as Slang, DXC and glslang allow. Printed MSL and WGSL keep chibi's names.
5. **Check every writer's output in CI, and compare GPU results with the CPU.** Use spirv-val for SPIR-V, Naga or Tint for WGSL, `xcrun metal` for MSL and DXC for HLSL, as Naga's development workflow does. Run compute functions on the interpreter and on each GPU API, and compare within the GPU error bounds, as Slang's and rust-gpu's tests do. Zig shows the risk of skipping this: its SPIR-V tests were switched off, and the backend broke as its IR changed.
6. **Keep section 6's order: SPIR-V, then MSL, then WGSL, then HLSL text only if D3D12 is needed.** SPIR-V has a thorough validator, spirv-val, and RenderDoc support. MSL should come soon after, because the owner's Mac runs Metal.
7. **Use converters only as bridges and test oracles, never in the shipped or hot-reload path.** SDL_shadercross can turn SPIR-V into MSL until the MSL printer exists. Naga and Tint read only the part of SPIR-V that maps to WGSL, so chibi prints WGSL itself.
8. **Leave DXIL, DXBC and metallib to platform tools for now, but stop ruling out a DXIL writer.** Mesa and a Go reimplementation of Naga write DXIL without LLVM, and the validator hash is now public. But that Go writer is 38,000 lines and still experimental, and D3D12 does not accept SPIR-V yet. On Windows, use SDL's Vulkan backend, or print HLSL and run DXC offline.

### 1.2 Terms

| Term | Meaning |
|---|---|
| IR (intermediate representation) | The compiler's internal form of a program, between the source text and the output. |
| Front end, back end | The part that reads and checks source code; the part that produces output. |
| Writer | A back end for one output format. A printer writes text; an emitter writes binary. |
| Basic block | A run of instructions with one way in, at the top, and one way out, a branch at the bottom. |
| Control-flow graph (CFG) | A function's basic blocks, linked by the branches between them. |
| Structured control flow | Control flow built only from nested `if`, `switch` and loop regions, each with one entry and a known exit. |
| Merge block, join point | The block where the paths of an `if` or `switch` meet again, or where a loop exits. SPIR-V requires each one to be declared. |
| Reducible CFG | A CFG in which every loop has a single entry. DXIL requires this; structured control flow is stricter. |
| Structurizer | A pass that turns an arbitrary CFG back into structured regions. |
| SSA | Static single assignment: each value is assigned once. Where two paths merge, a phi node or a block parameter picks the value. |
| Legalization | Rewriting code that the target rejects into code it accepts, such as removing local copies of texture handles. |
| Optimizer, validator | Passes that make code faster or smaller; a tool that checks output against the format's rules. spirv-opt and spirv-val are SPIRV-Tools' versions. |
| Hub format | One format that every path passes through, so N inputs and M outputs need N + M tools instead of N × M. |
| Cross-compiler, translator | A tool that converts one shader format into another, such as SPIRV-Cross from SPIR-V to MSL. |
| Downstream compiler | A compiler that another compiler runs on its output, as Slang runs DXC. |
| LLVM IR, bitcode | The IR of the LLVM compiler framework, and its binary file format. DXIL and Apple's AIR are bitcode. |
| MLIR | An LLVM framework for IRs with many levels, called dialects. Mojo is built on it. |
| DXIL, DXBC | Direct3D's shader formats: DXIL is LLVM 3.7 bitcode for Shader Model 6; DXBC is the older bytecode that FXC produces. |
| AIR, metallib | Apple's Metal IR, which is bitcode, and the library file that holds it. |
| NIR | Mesa's shader IR, used inside every Mesa GPU driver. |
| NSDI | `NonSemantic.Shader.DebugInfo.100`, a SPIR-V instruction set that describes source files, lines, variables and types for debuggers. `OpLine` is the older line-only form. |
| Snapshot test, differential test | A test that compares output with a stored file; a test that runs one program two ways and compares the results. |

## 2. How each compiler produces GPU code

| Compiler | Input → IR | Written directly | Handed to another tool | Own size |
|---|---|---|---|---|
| Slang | Slang, HLSL → SSA IR with join points | SPIR-V; HLSL, GLSL, MSL, WGSL, C++, CUDA text | DXIL (DXC), DXBC (FXC), metallib (Apple), PTX (NVRTC) | 362,000 code lines |
| glslang | GLSL → AST | SPIR-V | — | 108,000 code lines |
| DXC | HLSL → Clang 3.7 AST | SPIR-V from the AST; DXIL through LLVM 3.7 | SPIR-V to SPIRV-Tools | 102,700 code lines plus an LLVM fork |
| Clang and LLVM | HLSL → modern LLVM IR | DXIL and SPIR-V through LLVM targets | — | 51,700 code lines in the two targets, plus Clang and LLVM |
| Naga | WGSL, SPIR-V, GLSL → expression arena plus statement tree | SPIR-V; WGSL, MSL, HLSL, GLSL text | — | 99,400 Rust code lines |
| Tint | WGSL, SPIR-V → AST → IR | SPIR-V; WGSL, MSL, HLSL, GLSL text | — | 284,000 lines |
| rust-gpu | Rust → rustc's unstructured IR | SPIR-V | spirv-opt, spirv-val; WGSL through Naga | 26,900 code lines plus rustc |
| Zig | Zig → AIR, its typed IR | SPIR-V | — | 15,900 lines |
| Mojo | Mojo → MLIR → LLVM IR | PTX, AMD GPU code, AIR, metallib | `xcrun metal`, optional | not measured |
| Circle | C++ → closed source | SPIR-V, DXIL | a DXIL signing tool | closed |
| cute_spirv.h | CF-GLSL → AST | SPIR-V 1.3; GLSL ES, HLSL, MSL, WGSL text | — | 13,667 lines |

### 2.1 Slang

- **Input and IR.** Slang and HLSL. The IR is SSA in which blocks take parameters instead of phi nodes ([ir.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/design/ir.md#L110-L122)). A structured branch carries its join point as one of its own operands, not as a separate instruction as in SPIR-V ([L180-L182](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/design/ir.md#L180-L182)). Loops record their exit but not their continue block, so printed code may repeat a continue clause ([L184-L186](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/design/ir.md#L184-L186)). Generics are specialized away before emission ([overview.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/design/overview.md#L200-L206)).
- **Outputs.** SPIR-V is emitted directly from the IR by default since [PR #4002](https://github.com/shader-slang/slang/pull/4002), merged 2024-04-23. Before that, Slang printed GLSL and ran glslang. The 2019 issue that proposed the change said the extra step cost compile time and compiler size, and limited Slang to what GLSL can say ([#1124](https://github.com/shader-slang/slang/issues/1124)). The old path survives as `-emit-spirv-via-glsl` ([a2-01](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/user-guide/a2-01-spirv-target-specific.md#L418-L426)). HLSL, GLSL, Metal, WGSL, C++ and CUDA are printed from the IR. DXIL goes through DXC, DXBC through FXC, PTX through NVRTC, and metallib through Apple's compiler ([overview.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/design/overview.md#L250-L258)).
- **Size and dependencies.** `source/slang` has 362,000 code lines, plus a core module of 44,600 code lines written in Slang. The SPIR-V emitter is 12,703 lines. The MSL printer is 1,858 lines, WGSL 2,057, HLSL 2,649 and GLSL 4,123, on a shared 5,668-line base. glslang and SPIRV-Tools ship as `slang-glslang`. On the direct path it is loaded only to optimize, link or validate SPIR-V ([slang-emit.cpp](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/source/slang/slang-emit.cpp#L3638-L3656)).
- **Control flow, validation, debug info.** The SPIR-V emitter turns an if-else's join point straight into `OpSelectionMerge` ([slang-emit-spirv.cpp](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/source/slang/slang-emit-spirv.cpp#L5509-L5526)). The printers rebuild `if` and loop statements from the same join points with an algorithm the design notes call ad hoc ([overview.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/design/overview.md#L247)), and every IR pass must preserve that information ([ir.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/design/ir.md#L189)). The direct path supports only SPIR-V 1.3 and later ([slang-diagnostics.lua](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/source/slang/slang-diagnostics.lua#L5856-L5861)). spirv-val runs only when the environment variable `SLANG_RUN_SPIRV_VALIDATION` is `1` ([slang-emit.cpp](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/source/slang/slang-emit.cpp#L3577-L3600)), while the default level `-O1` runs SPIRV-Tools' optimizer ([reference](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/command-line-slangc-reference.md#L1287-L1292)). `-g` emits NSDI, which the documentation says was tested with RenderDoc ([08-compiling.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/user-guide/08-compiling.md#L1270-L1274)).
- **Testing.** `COMPARE_COMPUTE` tests run a compute shader on chosen graphics APIs and compare its output with an expected file ([slang-test](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/tools/slang-test/README.md#L117)).

### 2.2 glslang

- **Input and IR.** GLSL and ESSL, parsed into an AST. Its HLSL front end was deprecated in April 2026 ([README](https://github.com/KhronosGroup/glslang/blob/b411a8fb8aa0f6ee727df17f3f8eafb3eb85ba9e/README.md#L7)).
- **Output.** SPIR-V only, built while walking the AST ([README](https://github.com/KhronosGroup/glslang/blob/b411a8fb8aa0f6ee727df17f3f8eafb3eb85ba9e/README.md#L35-L37)). Visiting a loop node creates the header block and `OpLoopMerge` on the spot ([GlslangToSpv.cpp](https://github.com/KhronosGroup/glslang/blob/b411a8fb8aa0f6ee727df17f3f8eafb3eb85ba9e/SPIRV/GlslangToSpv.cpp#L5565-L5582)).
- **Size and dependencies.** 108,000 code lines in `glslang/` and `SPIRV/`; the AST-to-SPIR-V traverser is 13,276 lines. SPIRV-Tools is optional ([README](https://github.com/KhronosGroup/glslang/blob/b411a8fb8aa0f6ee727df17f3f8eafb3eb85ba9e/README.md#L250)).
- **Validation and debug info.** `--spirv-val` runs the SPIRV-Tools validator. `-g` emits line information, `-gV` emits NSDI, and `-gVS` adds the source text ([StandAlone.cpp](https://github.com/KhronosGroup/glslang/blob/b411a8fb8aa0f6ee727df17f3f8eafb3eb85ba9e/StandAlone/StandAlone.cpp#L2079-L2083), [L2227](https://github.com/KhronosGroup/glslang/blob/b411a8fb8aa0f6ee727df17f3f8eafb3eb85ba9e/StandAlone/StandAlone.cpp#L2227)).

### 2.3 DXC

- **Input.** HLSL, parsed by a fork of Clang 3.7. In May 2024 Microsoft wrote that it had no reasonable path to a newer LLVM ([DirectX blog](https://devblogs.microsoft.com/directx/dxc-1-8-2405-available/)).
- **DXIL.** Clang produces a high-level LLVM IR, and the optimizer lowers it to DXIL ([DXIL.rst](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/DXIL.rst#L12-L14)), which uses a subset of LLVM IR ([L67](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/DXIL.rst#L67)). The CFG must be reducible, and DXIL deliberately drops structured control flow so that third-party LLVM tools can produce it ([L248-L251](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/DXIL.rst#L248-L251)). The validator writes a hash that the runtime checks. In 2024 Microsoft announced that it would publish the hash algorithm and accept a bypass value from other DXIL generators ([DirectX blog](https://devblogs.microsoft.com/directx/open-sourcing-dxil-validator-hash/)). Godot now computes the hash itself (section 2.11).
- **SPIR-V.** A separate code generator walks the Clang AST and adds the header and merge blocks that SPIR-V's structured control flow needs ([SpirvEmitter.cpp](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/tools/clang/lib/SPIRV/SpirvEmitter.cpp#L2741-L2752)). Legalization, optimization and validation are delegated to SPIRV-Tools ([SPIR-V.rst](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/SPIR-V.rst#L478-L489)), and validation runs by default ([L585-L589](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/SPIR-V.rst#L585-L589)).
- **Size.** The SPIR-V generator is 38,400 code lines and the DXIL libraries 64,300, on top of the LLVM and Clang fork. GitHub counts 84 MB of C++ in the repository.
- **Debug info.** `-fspv-debug=vulkan-with-source` emits NSDI, which supports source-level debugging in RenderDoc even for optimized SPIR-V ([SPIR-V.rst](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/SPIR-V.rst#L628-L634)).

### 2.4 Clang and LLVM

- **Goal.** Clang is meant to replace DXC in every use ([HLSLSupport.md](https://github.com/llvm/llvm-project/blob/23df5cc002e3e704c135a49f595a03a4caca3064/clang/docs/HLSL/HLSLSupport.md#L13-L15)). Microsoft plans Clang support for both DXIL and SPIR-V, and announced in 2024 that D3D12 will accept SPIR-V from Shader Model 7, a transition of several years ([DirectX blog](https://devblogs.microsoft.com/directx/directx-adopting-spir-v/)). The newest Agility SDK preview is 1.722.1 of 2026-11-09. The newest shader model on that page is 6.10, and the page does not mention SPIR-V ([downloads](https://devblogs.microsoft.com/directx/directx12agility/)).
- **IR.** Clang emits modern LLVM IR. For HLSL it adds convergence-control tokens, which tell later passes where invocations must run together ([CGHLSLRuntime.cpp](https://github.com/llvm/llvm-project/blob/23df5cc002e3e704c135a49f595a03a4caca3064/clang/lib/CodeGen/CGHLSLRuntime.cpp#L1395-L1397)).
- **DirectX target.** IR passes rewrite LLVM constructs into DXIL's forms. Then a second bitcode writer downgrades the module to LLVM 3.7 bitcode ([DXILArchitecture.md](https://github.com/llvm/llvm-project/blob/23df5cc002e3e704c135a49f595a03a4caca3064/llvm/docs/DirectX/DXILArchitecture.md#L86-L92)). The format is tested by round trips through DXC's tools ([L146-L152](https://github.com/llvm/llvm-project/blob/23df5cc002e3e704c135a49f595a03a4caca3064/llvm/docs/DirectX/DXILArchitecture.md#L146-L152)). The target is 15,300 code lines.
- **SPIR-V target.** It uses LLVM's GlobalISel instruction selector ([SPIRVTargetMachine.cpp](https://github.com/llvm/llvm-project/blob/23df5cc002e3e704c135a49f595a03a4caca3064/llvm/lib/Target/SPIRV/SPIRVTargetMachine.cpp#L98)). For shaders it rebuilds structure in five passes: simplify loops, move values to memory, merge each region's exits, structurize, and move values back to registers ([L205-L236](https://github.com/llvm/llvm-project/blob/23df5cc002e3e704c135a49f595a03a4caca3064/llvm/lib/Target/SPIRV/SPIRVTargetMachine.cpp#L205-L236)). The target is 36,400 code lines. 52 of the 53 [structurizer tests](https://github.com/llvm/llvm-project/tree/23df5cc002e3e704c135a49f595a03a4caca3064/llvm/test/CodeGen/SPIRV/structurizer) pipe their output into spirv-val. `-g` emits NSDI ([SPIRVUsage.md](https://github.com/llvm/llvm-project/blob/23df5cc002e3e704c135a49f595a03a4caca3064/llvm/docs/SPIRVUsage.md#L30-L32)).

### 2.5 Naga

- **Input and IR.** WGSL, SPIR-V and GLSL. Expressions live in an arena and have no side effects. Statements form a tree that points into the expressions, and the tree carries the structured control flow ([ir/mod.rs](https://github.com/gfx-rs/wgpu/blob/4fe54b0207a2ec7e0d65af900b1218b3c1529699/naga/src/ir/mod.rs#L15-L27)). The module is plain public data that a program can build by hand ([survey](https://bkaradzic.github.io/posts/ideal-shader-compiler/)).
- **Outputs.** SPIR-V binary; WGSL, MSL, HLSL (Shader Model 5.0 and later) and GLSL text; no DXIL or DXBC ([README](https://github.com/gfx-rs/wgpu/blob/4fe54b0207a2ec7e0d65af900b1218b3c1529699/naga/README.md#L12-L31)). Each writer takes the validator's results as input ([back/spv/mod.rs](https://github.com/gfx-rs/wgpu/blob/4fe54b0207a2ec7e0d65af900b1218b3c1529699/naga/src/back/spv/mod.rs#L1175-L1180)).
- **Size and dependencies.** 99,400 Rust code lines, with no C or C++. Writers: SPIR-V 14,800, HLSL 9,700, MSL 9,600, GLSL 5,000, WGSL 2,000. The WGSL front end is 13,300 and the validator 9,400. All of Naga builds into a 3.7 MB program ([survey](https://bkaradzic.github.io/posts/ideal-shader-compiler/)).
- **Validation and debug info.** Development compares snapshot outputs, then checks them with SPIRV-Tools, Xcode's Metal compiler, glslang, DXC and FXC ([README](https://github.com/gfx-rs/wgpu/blob/4fe54b0207a2ec7e0d65af900b1218b3c1529699/naga/README.md#L65-L88)). Given the source, the SPIR-V writer embeds it and emits `OpLine` ([mod.rs](https://github.com/gfx-rs/wgpu/blob/4fe54b0207a2ec7e0d65af900b1218b3c1529699/naga/src/back/spv/mod.rs#L194-L199), [instructions.rs](https://github.com/gfx-rs/wgpu/blob/4fe54b0207a2ec7e0d65af900b1218b3c1529699/naga/src/back/spv/instructions.rs#L100)). It writes no NSDI.

### 2.6 Tint

- **Input and IR.** WGSL and SPIR-V. WGSL is parsed into an AST, resolved, then converted into Tint's IR. The IR was added because transforms that rebuilt the AST were slow ([ir.md](https://github.com/google/dawn/blob/b73cc737e4e714b5ce17dc5d7316a30e35e23990/docs/tint/ir.md#L3-L8)). Its `if`, `switch` and `loop` instructions own their blocks ([L46-L48](https://github.com/google/dawn/blob/b73cc737e4e714b5ce17dc5d7316a30e35e23990/docs/tint/ir.md#L46-L48)). The SPIR-V reader is built on SPIRV-Tools' optimizer IR ([parser.cc](https://github.com/google/dawn/blob/b73cc737e4e714b5ce17dc5d7316a30e35e23990/src/tint/lang/spirv/reader/parser/parser.cc#L54-L56)) and goes through a WGSL AST on its way to the writers ([survey](https://bkaradzic.github.io/posts/ideal-shader-compiler/)).
- **Outputs.** SPIR-V binary; WGSL, HLSL, MSL and GLSL text.
- **Size and dependencies.** 284,000 lines of non-test C++ under `src/tint`: SPIR-V writer 11,300, MSL 20,900, HLSL 23,300, GLSL 14,100, the WGSL reader, resolver and writer 72,700, and the core with the IR 85,400. Dawn pins SPIRV-Tools and abseil ([DEPS](https://github.com/google/dawn/blob/b73cc737e4e714b5ce17dc5d7316a30e35e23990/DEPS#L382-L423)), and protobuf for fuzzing ([L679-L680](https://github.com/google/dawn/blob/b73cc737e4e714b5ce17dc5d7316a30e35e23990/DEPS#L679-L680)).
- **Validation and debug info.** The IR validator runs only in debug builds, because a failure means a compiler bug ([ir.md](https://github.com/google/dawn/blob/b73cc737e4e714b5ce17dc5d7316a30e35e23990/docs/tint/ir.md#L101-L106)). The SPIR-V writer's tests run the SPIRV-Tools validator on every module ([helper_test.h](https://github.com/google/dawn/blob/b73cc737e4e714b5ce17dc5d7316a30e35e23990/src/tint/lang/spirv/writer/common/helper_test.h#L124-L136)). End-to-end tests compare every backend's output with an expectation file ([end-to-end-tests.md](https://github.com/google/dawn/blob/b73cc737e4e714b5ce17dc5d7316a30e35e23990/docs/tint/end-to-end-tests.md#L11-L13)). I found only names, no line information, in the SPIR-V writer (my reading).

### 2.7 rust-gpu

- **Input and IR.** `no_std` Rust on a pinned nightly compiler ([rust-toolchain.toml](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/rust-toolchain.toml#L1-L3)). rust-gpu implements rustc's code-generation interface and builds SPIR-V with the rspirv crate. rustc's IR is a plain CFG, so the result is not structured until the linker runs.
- **Pipeline.** Its own linker inlines and removes dead code, then structurizes the CFG with SPIR-T ([linker/mod.rs](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/crates/rustc_codegen_spirv/src/linker/mod.rs#L566-L568)), an IR with structured regions built for this job ([SPIR-T](https://github.com/Rust-GPU/spirt/blob/e8757adba8d14068a7bf1b3bc9f24cac982f4bd3/README.md#L65)). Then spirv-opt runs when optimizing, and spirv-val runs by default ([link.rs](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/crates/rustc_codegen_spirv/src/link.rs#L269-L313), [codegen-args.md](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/docs/src/codegen-args.md#L107-L109)). Turning off the structurizer probably produces invalid modules ([L136-L138](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/docs/src/codegen-args.md#L136-L138)). WGSL comes from Naga reading the SPIR-V ([Cargo.toml](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/crates/rustc_codegen_spirv/Cargo.toml#L58-L65)).
- **Size and dependencies.** `rustc_codegen_spirv` is 26,900 code lines, plus rustc's internals, SPIRV-Tools (C++), SPIR-T, rspirv and Naga.
- **Debug info and tests.** It emits `OpLine` and `OpSource`; inside the compiler it uses its own instructions modelled on NSDI ([custom_insts.rs](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/crates/rustc_codegen_spirv/src/custom_insts.rs#L161-L171)). Its difftests run Rust shaders and WGSL shaders and check that they give the same output ([testing.md](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/docs/src/testing.md#L1-L10)).

### 2.8 Zig 0.17.1

- **Input and IR.** AIR, Zig's typed IR per function, the same input its CPU backends read.
- **Output.** SPIR-V words, emitted directly by `CodeGen.zig` (8,826 lines), with a 917-line linker, a 1,138-line assembler for inline assembly and 4,544 lines of generated opcode tables. No external dependency.
- **Control flow.** AIR's blocks and loops become SPIR-V constructs. Leaving several nested selections at once needs a ladder of nested `OpSelectionMerge` instructions ([CodeGen.zig](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/src/codegen/spirv/CodeGen.zig#L114-L139)).
- **Validation, debug info, status.** The compiler runs no validator. It emits names, and an `OpLine` for each statement ([L8475-L8492](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/src/codegen/spirv/CodeGen.zig#L8475-L8492)). Its Vulkan tests are disabled because nobody maintains the backend, and they fail because AIR changed and the backend is incomplete ([tests.zig](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/test/tests.zig#L1587-L1597)).

### 2.9 Mojo

- **Input and IR.** Mojo, lowered through MLIR dialects. One of them, `hlcf`, keeps high-level control flow in the IR so that later passes need not rediscover it ([PassesAndIR.md](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/docs/compiler/manual/PassesAndIR.md#L130-L134)). All dialects are lowered to MLIR's `llvm` dialect, which hands the program to LLVM ([L150-L160](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/docs/compiler/manual/PassesAndIR.md#L150-L160)). The compiler's sources are in the public repository at this commit.
- **Outputs.** GPU kernels go through LLVM's NVIDIA, AMD and Apple AIR targets ([adding-gpu-targets.md](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/docs/contributing/stdlib/adding-gpu-targets.md#L286-L289)). For Metal, Mojo writes AIR bitcode and either links a metallib with `xcrun metal` or packages the metallib itself, without Apple's toolchain ([MetalBackend.cpp](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/lib/Compiler/ObjectCompiler/Target/max_license/Metal/MetalBackend.cpp#L14-L16), [MetallibWriter.h](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/lib/Compiler/ObjectCompiler/Target/max_license/Metal/MetallibWriter.h#L14-L15)).
- **Control flow and debug info.** These targets accept any LLVM CFG, so Mojo needs no structurizer (my analysis). Debug info is LLVM's, adjusted for the NVIDIA and AMD targets ([walkthrough](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/docs/compiler/MojoCompilerWalkthrough.md#L980-L986)).

### 2.10 Circle

Circle compiles C++ with shader attributes, and lowers shader declarations to SPIR-V binary ([README](https://github.com/seanbaxter/shaders/blob/185c6e81bfaabd54fde80a0a2318636ae01bfab5/README.md#L475)). Extra CFG passes split pointers into bases and offsets so that most C++ pointer code fits SPIR-V's logical addressing ([L495](https://github.com/seanbaxter/shaders/blob/185c6e81bfaabd54fde80a0a2318636ae01bfab5/README.md#L495)). It also writes DXIL, which must then be signed with a separate tool ([L3887-L3920](https://github.com/seanbaxter/shaders/blob/185c6e81bfaabd54fde80a0a2318636ae01bfab5/README.md#L3887-L3920)). The compiler is closed source, so its IR, size and checks are unknown (unverified), and the documentation repository's last commit is from 2022.

### 2.11 Engine pipelines

| Pipeline | You write | Path to the GPU | Own code |
|---|---|---|---|
| Godot | Godot shading language | own parser → GLSL text → glslang → SPIR-V. D3D12: Mesa converts SPIR-V to NIR, then NIR to DXIL. Metal: SPIRV-Cross → MSL, optionally a metallib through `xcrun` | parser 12,329 lines, GLSL generator 1,714 |
| Unity 6.6 | HLSL | FXC → DXBC. For OpenGL and Metal, HLSLcc translates DXBC into GLSL or MSL; for Vulkan into GLSL, then glslang → SPIR-V. D3D12: DXC or FXC | closed |
| Unreal | HLSL | DXC → DXIL or SPIR-V; SPIRV-Cross → MSL or GLSL; Apple's Metal shader converter for DXIL on Mac (unverified) | closed |
| bgfx shaderc | GLSL dialect plus macros | own preprocessor and text patching → FXC, DXC or glslang → SPIRV-Tools → SPIRV-Cross (MSL, GLSL) or a stripped Tint (WGSL) | 11,533 lines |
| sokol-shdc | Vulkan GLSL 450 plus tags | glslang → SPIRV-Tools → SPIRV-Cross (GLSL, HLSL, MSL) or Tint (WGSL); bytecode only from FXC on Windows or `metal` on macOS | 12,100 code lines |

- **Godot.** The GLSL comes from [shader_compiler.cpp](https://github.com/godotengine/godot/blob/75ef98777c3becdc6ca1de365d156fe5ada9d469/servers/rendering/shader_compiler.cpp#L462). glslang adds NSDI with source when Godot asks for SPIR-V debug info ([register_types.cpp](https://github.com/godotengine/godot/blob/75ef98777c3becdc6ca1de365d156fe5ada9d469/modules/glslang/register_types.cpp#L127-L133)). The D3D12 driver runs Mesa's `spirv_to_nir` and `nir_to_dxil` and computes the DXIL hash itself ([rendering_shader_container_d3d12.cpp](https://github.com/godotengine/godot/blob/75ef98777c3becdc6ca1de365d156fe5ada9d469/drivers/d3d12/rendering_shader_container_d3d12.cpp#L334-L469), [L245](https://github.com/godotengine/godot/blob/75ef98777c3becdc6ca1de365d156fe5ada9d469/drivers/d3d12/rendering_shader_container_d3d12.cpp#L245)), using a prebuilt Mesa ([SCsub](https://github.com/godotengine/godot/blob/75ef98777c3becdc6ca1de365d156fe5ada9d469/drivers/d3d12/SCsub#L12-L13)). Metal uses SPIRV-Cross ([rendering_shader_container_metal.cpp](https://github.com/godotengine/godot/blob/75ef98777c3becdc6ca1de365d156fe5ada9d469/drivers/metal/rendering_shader_container_metal.cpp#L324-L360)).
- **Unity.** The manual lists the paths in the table. Player builds run about one compiler process per CPU core, and compiled variants are cached by source in `Library/ShaderCache` ([Unity 6.6 manual](https://docs.unity.com/en-us/engine/current/manual/materials-and-shaders/shaders/shader-troubleshooting/shader-reducing/shader-compilation)). DXC is supported only on D3D12 and is experimental on Vulkan and Metal ([Unity 6.7 manual](https://docs.unity.com/en-us/engine/6000.7/manual/materials-and-shaders/shaders/shader-troubleshooting/shader-reducing/shader-dxc-compiler)). HLSLcc must infer types for DXBC's typeless registers and detect for-loops again from jumps ([HLSLcc README](https://github.com/Unity-Technologies/HLSLcc/blob/3ea1fcd6bd0ac445bc078de0bf32f0950188577b/README.md#L12-L19)).
- **Unreal.** The engine's source needs an Epic account, and Epic's pages refused the fetch tool. Apple documents its Metal shader converter as a tool and library that turns a DXIL file into a metallib ([Apple](https://developer.apple.com/metal/shader-converter/)).
- **bgfx.** Its author describes shaderc as a preprocessor and a hand-off to other compilers, with no parser, AST or IR of its own, and writes that a new start today would use Slang ([post, 2026-09-27](https://bkaradzic.github.io/posts/shaderc-overview/)). The dependencies are in [shaderc.lua](https://github.com/bkaradzic/bgfx/blob/4608478e119667eab7635226f721ede32b0935c3/scripts/shaderc.lua#L8-L39).
- **sokol-shdc.** The pipeline is in [sokol-shdc.md](https://github.com/floooh/sokol-tools/blob/f1dcd178743c62265a4e00a594f7ad13b25dea7f/docs/sokol-shdc.md#L16-L38). Its `sokol_shader_debuggable` build mode ships source instead of bytecode, so that graphics debuggers can show it ([L199-L201](https://github.com/floooh/sokol-tools/blob/f1dcd178743c62265a4e00a594f7ad13b25dea7f/docs/sokol-shdc.md#L199-L201)).

### 2.12 cute_spirv.h, for comparison

`cute_spirv.h` compiles a GLSL subset to SPIR-V 1.3, and prints GLSL ES, HLSL, MSL and WGSL from the same checked AST (shader-languages.md section 6). It is 13,667 lines of C with no dependencies. It optimizes nothing by design: locals are memory variables, nothing is converted to SSA, and every consumer's own compiler optimizes ([cute_spirv.h](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_spirv.h#L24-L34)). Its tests run spirv-val on every module and check the WGSL output with Naga ([cute_spirv_test.c](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/tools/cute_spirv_test.c#L9-L12), [L91-L94](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/tools/cute_spirv_test.c#L91-L94)).

### 2.13 Parts the pipelines share

- **SPIRV-Cross** has 64,200 code lines of C++. Its MSL writer is 21,443 lines, GLSL 21,326 and HLSL 7,451 ([source](https://github.com/KhronosGroup/SPIRV-Cross/tree/57e4efcc84112abcd426dc5ec248b5339bee04f2)).
- **Mesa's DXIL writer**, written by Microsoft in C, writes LLVM bitcode by hand without LLVM ([dxil_module.c](https://gitlab.freedesktop.org/mesa/mesa/-/blob/a379b735aa859a628b6a88783d4eec07b22e061a/src/microsoft/compiler/dxil_module.c#L77-85)). `nir_to_dxil.c` is 6,823 lines and `dxil_module.c` 3,964.
- **gogpu/naga**, a Go reimplementation of Naga, adds an experimental DXIL writer: 238 of 267 entry points in its corpus pass Microsoft's validator ([README](https://github.com/gogpu/naga/blob/69b15c79a4280356ea978d0a9c54ef36ebaa527b/README.md#L65)). Its `dxil/` directory is 38,424 lines of non-test Go.

## 3. Published compile speeds

One benchmark covers most compilers. Branimir Karadžić, the author of bgfx, compiled the 368 shaders of the bgfx examples with each compiler, on one thread of a Ryzen 9 7950X, keeping the best of ten runs ([survey, 2026-10-24](https://bkaradzic.github.io/posts/ideal-shader-compiler/)). The corpus exists as HLSL (316K lines, including a 1,000-line helper file in every shader), as WGSL (41K lines) and as MSL (37K lines). Lines per second compare badly across languages, so the median per shader matters more.

| Path | Compiler | Total | Median per shader | Lines per second (my arithmetic) |
|---|---|---|---|---|
| WGSL → SPIR-V | Naga | 0.22 s | 85 µs | 186,000 |
| WGSL → SPIR-V | Tint | 0.64 s | 408 µs | 64,000 |
| HLSL → SPIR-V, no optimizer | glslang | 0.93 s | 2.1 ms, given per shader | 340,000 |
| HLSL → SPIR-V | glslang + SPIRV-Tools | 4.96 s | 3.4 ms | 64,000 |
| HLSL → DXIL | DXC | 5.99 s | 11.3 ms | 53,000 |
| HLSL → SPIR-V | DXC | 8.56 s | 10.6 ms | 37,000 |
| HLSL → SPIR-V | Slang | 14.70 s | 33.4 ms | 21,000 |
| HLSL → DXBC | FXC | 15.43 s | 4.8 ms | 20,000 |
| MSL → AIR | Apple's compiler, one process per shader | 22.37 s | 56.5 ms | 1,700 |

What the survey shows:
- **Writing output is cheap; front ends and optimizers are not.** Naga needs 201 ms to parse and validate the WGSL, then about 20 ms to write any target. For Tint, each target costs 200 to 350 ms. That is about 2 million WGSL lines per second for one of Naga's writers (my arithmetic).
- **Legalizing and optimizing dominate the Khronos stack.** glslang's SPIR-V fails spirv-val for 173 of the 368 HLSL shaders until SPIRV-Tools legalizes it, and legalizing plus optimizing takes 81% of the 4.96 s.
- **Slang's time is in its front end.** Printing HLSL costs the same 33 ms per shader as emitting SPIR-V, and a one-line shader takes 0.5 ms.
- **Converters.** From SPIR-V to MSL for 358 shaders: Naga 38 ms, SPIRV-Cross 152 ms, Tint 1,005 ms. Naga and Tint accept only the part of SPIR-V that maps to WGSL.
- **Fixed costs.** DXC never goes below 11 ms per shader. Apple's compiler costs 10 ms to start and 47 ms per shader.

Other measurements: one user timed glslang at 70 ms to parse and link a shader, then 2 ms to turn the AST into SPIR-V; Slang took 750 ms for the same shader ([discussion #9354](https://github.com/shader-slang/slang/discussions/9354)). I found no published numbers for Clang's HLSL support, LLVM's SPIR-V and DirectX targets, rust-gpu, Zig, Mojo, Circle, Godot, Unity or Unreal.

**For chibi (my arithmetic and analysis).** At 1M lines per second, one core has about 1 µs per line for the whole build ([README](README.md)). A Naga-style writer uses about 0.5 µs per line of shader code, for each target, and only GPU-reachable functions pass through it. SPIRV-Tools adds about 13 µs per HLSL line, and DXC costs about 19 µs. So writers fit the budget, assuming shader code is a small share of a game; optimizers and LLVM-based compilers do not.

## 4. What the IR must keep for GPU writers

SPIR-V for Vulkan and WGSL need structured control flow; DXIL needs only a reducible CFG, and PTX, AMD GPU code and AIR take any CFG. The compilers get structure in four ways:

| Approach | Who | Cost |
|---|---|---|
| Emit while walking the checked AST | glslang, DXC's SPIR-V path, cute_spirv.h, Circle | Simple, but it writes source-level functions, before generics and per-target instances exist (my analysis) |
| Keep structure in the IR | Slang (join point as a branch operand), Naga (statement tree), Tint (control instructions own blocks), Zig (AIR blocks), Mojo (`hlcf`) | Every pass must preserve it; Slang's passes must not create multi-level breaks |
| Rebuild it from a CFG | LLVM's SPIR-V target, rust-gpu with SPIR-T, Unity's HLSLcc; the SPIR-V readers of Naga and Tint, helped by SPIR-V's merge instructions | Several passes, new IR features (convergence tokens) or a whole new IR |
| Not needed | DXIL, PTX, AMD GPU code, AIR | Only for targets that accept any CFG |

H9's interpreter IR uses byte offsets into a frame, and plain `JUMP`, `JUMP_UNLESS` and `LOOP_IF` instructions ([H9](../../experiments/h9-interpreter/README.md#L30-L65)). That suits the interpreter, but a GPU writer can't use it directly (my analysis):
- **Merge points.** Each conditional branch needs its join point, and each loop its continue and exit blocks, as operands. Slang puts them on the branch instruction itself, which suits a flat array of instructions ([ir.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/design/ir.md#L180-L182)). CPU backends ignore them.
- **Typed values.** SPIR-V's logical addressing forbids reading frame bytes as another type (shader-languages.md section 4).
- **Field and element indices.** SPIR-V reaches into structs and arrays by index (`OpAccessChain`), not by byte offset.
- **One type per layout, and address spaces.** In SPIR-V, the same struct in two buffer layouts must be two types, and pointers carry address spaces. Slang's lead named both, plus type deduplication, as the hard parts of the direct emitter ([#1124](https://github.com/shader-slang/slang/issues/1124)). chibi already plans the layout check and inferred address spaces (shader-languages.md sections 2.1 and 2.4).

So GPU writers should read the typed IR after generics and compile-time code are resolved for the target, and before it is lowered to frame offsets. Slang and Zig both emit SPIR-V from that level.

## 5. The recurring architectures

- **A. One IR, many writers.** One front end, one typed and structured IR, and a writer per format: Slang, Naga and Tint; `cute_spirv.h` from its AST. Platform compilers are used only for the formats they own: DXIL, DXBC and metallib.
- **B. SPIR-V hub with converters.** One tool produces SPIR-V, often glslang or DXC; SPIRV-Tools optimizes it; SPIRV-Cross, Tint, Naga or Mesa convert it. Users: sokol-shdc, Godot, bgfx, SDL_shadercross, rust-gpu for WGSL, and Unreal (unverified). Projects choose it when they have no compiler of their own. The bgfx author notes that each part follows its own owner's plans, and the seams between parts are left to the user ([post](https://bkaradzic.github.io/posts/shaderc-overview/)).
- **C. LLVM IR hub.** The front end produces LLVM IR and an LLVM target finishes: DXC for DXIL, Clang with LLVM's DirectX and SPIR-V targets, Mojo, and Apple's MSL compiler.
- **D. Bytecode converters.** One low-level binary is converted into another: Unity's HLSLcc (DXBC to GLSL and MSL), Mesa (SPIR-V to DXIL through NIR), Apple's Metal shader converter (DXIL to metallib), and Microsoft's announced SPIR-V and DXIL translators. Going up from bytecode means recovering types and loops, as HLSLcc must.

| Criterion | A. One IR, many writers | B. SPIR-V hub | C. LLVM hub | D. Bytecode converters |
|---|---|---|---|---|
| 1M lines per second | Fits: writers cost about 0.5 µs per shader line | Adds a converter per target, and usually legalizing and optimizing at about 80% of the time | Over: about 11 ms per shader at least | Extra step per target |
| No C compiler | All in chibi | Needs C++ (SPIRV-Cross, SPIRV-Tools, glslang) or Rust (Naga) libraries | Needs LLVM | Closed tools, or Mesa in C |
| SDL formats and WGSL | SPIR-V, MSL and WGSL direct; DXIL and DXBC through HLSL and Microsoft's compilers | All formats, but WGSL converters accept only part of SPIR-V | DXIL, SPIR-V and AIR; no MSL or WGSL text | One format to another |
| Hot reload | Inside the compiler, per changed entry point | A converter call per target per reload | Slow per shader | Extra step |
| GPU debuggers | chibi names and lines in SPIR-V, readable MSL | MSL comes from SPIRV-Cross, a step further from chibi | DWARF or PDB, and NSDI | Debug info depends on the converter |
| Testing | A validator per writer, plus differential runs | Fewer writers, but bugs in code chibi doesn't own | LLVM's own test tools | Hard to test end to end |

**Speed.** Section 3's numbers decide this. Architecture A spends time only where chibi's IR becomes output. B adds SPIRV-Cross's 0.4 ms per shader per target (my arithmetic from 152 ms for 358 shaders), plus the optimizer when one runs. C pays an LLVM floor per shader.

**No C compiler.** Under A, everything is chibi code. Microsoft's and Apple's compilers appear only as optional offline steps for DXIL, DXBC and metallib. B ties chibi to two or three large C++ projects; glslang's deprecated HLSL front end shows how such a part can disappear under its users.

**SDL3's formats and the web.** A writes SPIR-V for Vulkan, MSL source for Metal and WGSL for WebGPU, which SDL and CF accept as they are (shader-languages.md section 6). Only D3D12 needs a Microsoft compiler, unless chibi writes DXIL. B reaches every format, but its WGSL step works only while chibi's SPIR-V stays within what Naga and Tint read.

**Hot reload.** Under A, editing a function re-emits only the entry points that reach it, in microseconds to milliseconds (my analysis, from section 3). Pipelines are then recreated at a frame boundary (shader-languages.md section 5.4). Engines cache by source: Unity keeps `Library/ShaderCache`, and Godot names its cache files by a hash. chibi keys GPU instances like generic instances, so it gets the same cache. The driver's own compile at pipeline creation remains in every architecture; for MSL source on Metal its cost is unknown (unverified).

**GPU debuggers.** RenderDoc debugs shaders only on D3D11, D3D12 and Vulkan; on Vulkan it needs NSDI for source-level stepping ([RenderDoc](https://renderdoc.org/docs/how/how_debug_shader.html)). Xcode's Metal debugger needs the shader source included in the build ([Apple](https://developer.apple.com/documentation/xcode/building-your-project-with-embedded-shader-sources)); with SDL's MSL path, the source Xcode shows is the MSL chibi printed (my analysis). So under A, RenderDoc can step through chibi lines on Vulkan, as it does for Slang (unverified for a language RenderDoc does not know), and Xcode shows MSL with chibi's names. Mapping MSL back to chibi lines through `#line` is unverified.

**Testing.** Under A, each writer is a pure transform, tested with tables and checked by an outside validator. A differential test runs compute functions on chibi's interpreter and on each GPU API, and compares within the GPU's error bounds (shader-languages.md section 5.3). Slang compares compute results across graphics APIs, rust-gpu compares Rust shaders with WGSL ones, and CF checks every module with two validators. Zig did not keep its tests running, and its backend fell behind.

**Verdict.** A fits every criterion. B suits projects without a compiler of their own; chibi has one, so a hub only adds a conversion (my analysis). C and D solve formats chibi does not need yet.

## 6. Does section 6 of shader-languages.md hold?

**It holds.** Writing SPIR-V directly, with no GLSL detour and no converter, is what Slang (since 2024), Naga, Tint, Zig, glslang and DXC's SPIR-V path do. Slang moved off its GLSL-plus-glslang path to save compile time and compiler size. MSL and WGSL printers match Slang, Naga, Tint and CF. Shipped games still get compiled shader bytes and no run-time cross-compiler.

**What changes:**
1. **The evidence is broader.** CF is one instance of architecture A; Naga, Tint and Slang are larger instances of the same shape.
2. **Print from the instantiated IR, not the syntax tree.** CF prints from its AST because CF-GLSL has no generics. chibi's GPU functions exist per target after generics and compile-time code run (shader-languages.md section 3), so writers read the IR, as Slang does after specialization (my analysis).
3. **Add section 4's requirements to the IR design.** Section 6 assumed SPIR-V suits one-pass emission from the typed IR. That holds only if the IR keeps merge points, typed values, access by index and one type per layout.
4. **SPIR-V needs no SSA.** CF keeps each local in a memory variable and leaves promotion to the driver. A first writer can do the same.
5. **Say "no optimizer, no legalization" explicitly,** and add the rules that make legalization unnecessary: in GPU code, no texture or sampler inside a struct, and no local copy of one (section 1.1, item 3).
6. **Debug info and CI validators come with the first writer,** not later.
7. **SPIR-V version.** Section 6 chose SPIR-V 1.0, because SDL starts Vulkan 1.0. Slang's direct emitter supports only 1.3 and later and warns below that, and CF emits 1.3. In 1.0, the `StorageBuffer` storage class does not exist yet, so storage buffers use the `Uniform` class with the deprecated `BufferBlock` decoration ([SPIR-V spec](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#Storage_Class)). That is a small cost in one writer (my analysis). Keep 1.0 unless the app asks SDL for Vulkan 1.1.
8. **DXIL moves from rejected to later.** Section 7.3 rejected emitting DXIL because it is LLVM 3.7 bitcode. Mesa and gogpu/naga show that bitcode can be written without LLVM, and the hash no longer needs Microsoft's DLL. The cost is still large, so the trigger is a need for D3D12 without DXC.
9. **The order stays, with MSL close behind SPIR-V.** Until the MSL printer exists, the owner's Mac runs chibi's SPIR-V through SDL_shadercross.

## 7. Not verified

**Not read directly.**
- Unreal Engine's pipeline: its use of DXC, SPIRV-Cross and ShaderConductor, and of Apple's Metal shader converter on Mac. The source needs an Epic account, and Epic's pages refused the fetch tool; I saw only search-result excerpts.
- Circle's IR, size, validation and debug info: the compiler is closed source.
- Unity's pipeline beyond its manual and HLSLcc's README.
- Compile speeds: I did not rerun the survey. Its numbers come from one corpus on one Windows machine. The glslang and Slang timings in discussion #9354 are one user's report.

**Not found.**
- Published compile speeds for Clang's HLSL, LLVM's SPIR-V and DirectX targets, rust-gpu, Zig, Mojo, Circle, Godot, Unity and Unreal.
- How long Metal takes to compile MSL source at pipeline creation, which is SDL's Metal path.
- Whether Xcode and PIX map printed MSL or HLSL back to chibi lines through `#line`; PIX's debug-info needs in general.
- Whether RenderDoc shows and steps through NSDI source in a language it does not know. Slang's documentation reports that it works for Slang.
- Whether drivers, mobile ones especially, run unoptimized SPIR-V as fast as optimized SPIR-V. bgfx adopted an optimizer in 2012 because mobile GLSL compilers of the time optimized poorly ([post](https://bkaradzic.github.io/posts/shaderc-overview/)).
- Any public date or preview for SPIR-V in D3D12.

**My reading, not a full audit.**
- That Tint's SPIR-V writer emits no line information: I searched its sources and found none.
- That Mojo needs no structurizer: its targets accept any CFG, but I did not trace its GPU lowering.

**My analysis, not measured.**
- The per-line costs in section 3, and the assumption that shader code is a small share of a game.
- That GPU writers should read the IR above H9's frame offsets, and the four requirements in section 4.
- That a hub adds work without removing any for a language that has its own compiler.
- That SPIR-V 1.0 costs only the storage-buffer difference in one writer.
- That Xcode shows the printed MSL when SDL compiles MSL source.
