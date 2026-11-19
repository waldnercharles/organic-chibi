# Shader languages: compiling chibi for the GPU

Research note, 2026-11-10. Question: what must chibi-v2 adopt so that it also compiles as a shader language, with GPU code first-class, and which decisions being made now does that change?

**Scope.** This note covers what shader languages have that CPU languages lack, how languages that target both CPU and GPU share code, what GPU targets forbid, how a GPU target affects [adopt-or-settle.md](adopt-or-settle.md) ranks 1–10 and the sheets on generics, compile-time code and tasks, and which format chibi should emit. Element-wise array math and swizzles are already in adopt-or-settle 2.14 and are not repeated.

**Sources.** Read on 2026-11-10 and pinned where possible:
- WGSL at gpuweb commit `6bf1c35f` (2026-11-06); the SPIR-V 1.6 revision 8 specification; the SPIR-V registry at `e5eba999`; the Vulkan specification sources at `fa30d4e3` (1.4.368); the GLSL specification sources at `0099bf83`;
- DXC's `DXIL.rst` at `823a3dad`, the D3D11.3 functional specification at DirectX-Specs `5122ac08`, and Microsoft Learn's HLSL pages;
- Apple's Metal API documentation. The MSL specification is a 14.5 MB PDF that the fetch tool refused, so every MSL language rule here is unverified;
- SDL `8b6ae5d7` (main, 2026-11-10), SDL_shadercross `1ff05bec`, and Cute Framework (CF) `6b00978a`;
- Slang, CUDA, Rust-GPU, Zig 0.17.1, Mojo, Circle, ISPC, Odin and Jai, cited where used.

I read the WGSL, SPIR-V, Vulkan, GLSL, DXIL, D3D11, Microsoft Learn and Apple sources myself. Two research agents read the rest through the pinned links given where they are cited: Slang, CUDA, Rust-GPU, Zig, Mojo, Circle, ISPC, Odin and Jai, SDL's backend sources, SDL_shadercross, CF's documentation and sources, and the converters. Of their findings, I re-read SDL's format flags, binding table and std140 rule, its Metal compile call, cute_spirv's header, and the Slang, Rust-GPU and Circle passages quoted in section 3. Claims that nobody read directly are marked "(unverified)" and listed in section 8. Jai facts are "(community source)".

**Updated 2026-11-18** to match [../decisions.md](../decisions.md) and [shader-backends.md](shader-backends.md). Each recommendation now says whether it is decided. Decided spellings replace the earlier ones: postfix `x!!`, `undefined`, the `.len` field, and no `uint`. Section 5.1 adds GPU fallbacks for the checks decided since: narrowing conversions, enum conversions and `switch`. Section 6 takes shader-backends.md section 6's corrections, and a DXIL writer moves from rejected to later. A new section 5.6 covers 8- and 16-bit integers, `bool`, enums, optionals and error unions, which decisions from 2026-11-11 to 2026-11-16 put into ordinary types. For this update I read WGSL, Vulkan's `features.adoc`, `spirvenv.adoc` and `VP_KHR_roadmap.json`, and SDL's `SDL_gpu.h` and `SDL_gpu_vulkan.c` at the commits above. SDL's default Vulkan device enables neither `robustBufferAccess` nor any 8-, 16- or 64-bit shader feature (5.1, 5.6).

## 1. Summary

### 1.1 Recommendations, ranked by the cost of deciding late

1. **Rank 2 changes for GPU code.** `int` stays 64-bit on every CPU target, but GPU code can't use it. In GPU code an untyped integer defaults to `i32`, `.len` is an `i32`, and `i64`, `u64` and `f64` need a target that has them. WGSL has no 64-bit integer type, Vulkan's `shaderInt64` is optional in every version and roadmap profile, and D3D12 reports 64-bit integer support as a capability bit (5.2). **Decided 2026-11-11.**
2. **Rank 3 narrows to the CPU backends.** The interpreter, the development backend and the C backend give one result. A GPU result is only within the target's documented error bounds, so lockstep and replay math stays on the CPU. WGSL may fuse, reassociate, round either way and flush subnormals; Vulkan assumes contraction unless an operation is marked `NoContraction`; HLSL is "fast" by default; Metal defaults to relaxed or fast math (5.3). Open.
3. **Ranks 1 and 9 get one GPU rule, because a GPU can't stop on a failed check.** Every GPU build takes a defined fallback. `+` wraps, `x / 0` is `x` and `x % 0` is 0 (WGSL's rule), a shift uses its count modulo the width, an out-of-bounds load gives zero, an out-of-bounds store is skipped, and `x!!` on `nil` gives zero. Debug GPU builds also record the first failure in a buffer, and the CPU reports it as a trap after the frame (5.1). Open: the rank 1 decision row says GPU results are not yet decided.
4. **Structs that reach a GPU buffer get a layout check.** [decisions.md](../decisions.md) says that with C layout a GPU buffer "needs no annotation". That holds only where C layout equals the GPU's layout. A `[3]f32` aligns to 4 bytes in C, a WGSL `vec3<f32>` to 16, and SDL requires std140 for uniform data. The compiler checks each such struct and names the padding to add (2.1). **Decided 2026-11-10.**
5. **A matrix's first index selects a column.** Then `[4, 4]f32` has the bytes of GLSL's `mat4` and WGSL's `mat4x4<f32>`, with no transpose. `==` on arrays returns one `bool`, as in GLSL, and ordering comparisons on arrays are functions (2.1). **Decided 2026-11-10,** except the ordering comparisons, which are still proposed.
6. **GPU code is ordinary chibi, chosen by reachability.** A function is compiled for the GPU when a GPU entry point (`@vertex`, `@fragment`, `@compute(64)`) calls it, as in Circle and Slang. There are no CUDA-style `__host__ __device__` marks. `$if target.gpu` picks target-specific code, as Mojo's `comptime if` does. Each target gets its own instance of a function, checked separately, as generics already are (3). Open.
7. **The typed IR keeps structured control flow, and GPU code has no cycles in its call graph.** SPIR-V needs merge blocks, and WGSL, MSL and HLSL text are structured. Recursion, and function values not known at compile time, are errors in GPU code (4). **The IR part is decided:** the GPU output row (2026-11-10) and the typed IR row (2026-11-12) keep `block`, `loop`, `if` and `end` inline, with join points marked. The GPU call-graph rules are open.
8. **An entry point's signature states its binding kinds.** Parameters say `@uniform` or `@read_write`, so a signature never depends on its body. The compiler assigns SDL's set and binding numbers (2.3). Open, with the rest of the attribute design (decision row of 2026-11-12).
9. **Emit SPIR-V first, from the typed IR, then add MSL and WGSL printers.** SDL's Vulkan backend takes SPIR-V, and SDL_shadercross can turn it into MSL, DXBC or DXIL meanwhile. CF's cute_spirv prints SPIR-V, GLSL ES, HLSL, MSL and WGSL from one checked syntax tree, in 13,667 lines of C with no dependencies (6). **Decided 2026-11-10,** after [shader-backends.md](shader-backends.md) found the same shape in Slang, Naga and Tint.
10. **Most shader features are a `gpu` package, not syntax.** The core language adds attributes for entry points, bindings, built-ins and interpolation, the GPU checks, and the vector rules. Textures, samplers, derivatives, `discard`, barriers, atomics and subgroup operations are intrinsic functions and opaque types (2.8). Open.
11. **Types narrower than 32 bits are computed in 32 bits on the GPU, and buffers check them.** `bool` is one byte, enums take the smallest type that fits, `?T` holds a `bool` and `E!T` a `u16` code, but WGSL has no 8- or 16-bit integers and can't put a `bool` in a buffer, and SDL's default Vulkan device enables no 8- or 16-bit feature. GPU writers wrap each narrow result to its width, read narrow buffer fields from their 32-bit word, and reject narrow writes to `@read_write` buffers and narrow stage inputs and outputs unless the target has the feature (5.6). Open.

### 1.2 Terms

| Term | Meaning |
|---|---|
| Shader | A program the GPU runs many times in parallel, once per vertex, pixel or work item. |
| Stage | The kind of shader: vertex (places each vertex), fragment (colors each pixel) or compute (general work). |
| Entry point | The function where a stage starts, like `main` for a program. |
| Invocation | One run of a shader: one vertex, one pixel or one work item. |
| Workgroup | A group of compute invocations that run together and share fast memory. |
| Subgroup | The invocations that one GPU core runs in lockstep, often 32 or 64. HLSL says wave, CUDA says warp, Metal says SIMD-group. |
| Uniform control flow | Every invocation in a group takes the same path through an `if` or a loop. |
| Derivative | How fast a value changes between neighboring pixels. The GPU compares a 2×2 quad of pixels, so all four must run the same code. |
| Helper invocation | A pixel invocation that runs only so its neighbors can compute derivatives. Its writes are thrown away. |
| `discard` | Throws away the current pixel's output. |
| Swizzle | Reading or reordering vector components by name: `v.yx`, `c.rgb`. |
| Component-wise | Applied to each component separately, so `a + b` adds lane by lane. |
| Address space (storage class) | Where memory lives: one invocation's locals, a workgroup's shared memory, or a buffer that every invocation sees. SPIR-V says storage class. |
| Uniform buffer, storage buffer | A small read-only buffer of constants for a draw; a large buffer that shaders may read and write. |
| Binding, set, location | Numbers that connect a shader's resources (set and binding) and its inputs and outputs between stages (location) to what the CPU provides. |
| Push constants, immediates | A few bytes passed with a draw, without a buffer. |
| Specialization constant | A constant whose value is set when the pipeline is created, without recompiling. WGSL calls it `override`. |
| Interpolation | How a vertex output is blended across a triangle before the fragment shader reads it. |
| Built-in | A value the hardware provides or consumes, such as the pixel position or the invocation's index. |
| Pipeline | A set of shaders plus fixed state, created once and bound before drawing. |
| SPIR-V | Khronos's binary intermediate format for shaders. Vulkan takes it. |
| Logical addressing | SPIR-V's mode in which pointers are abstract: no size, no numeric value, no arithmetic. |
| Buffer device address | A raw 64-bit GPU address of a buffer, usable as a pointer. |
| ULP | Unit in the last place: the gap between two neighboring floats. "2.5 ULP" means within 2.5 such gaps. |
| Subnormal, flush to zero | A tiny float below the normal range; replacing it with zero. |
| Robustness | A guarantee that an out-of-bounds access can't touch memory outside its buffer. |
| Device loss | The driver gives up on the GPU context, and the app must recreate everything. |
| GPU-reachable | This note's term for a function that a GPU entry point calls, directly or indirectly. |

## 2. What shader languages have (question 1)

### 2.1 Vectors, matrices, comparisons and buffer layout

**What it is.** Shading languages build in small vectors and matrices, component-wise operators and swizzles. Adopt-or-settle 2.14 already gives chibi arithmetic on fixed arrays, swizzles, and an element-wise `*` for every shape. A GPU target leaves three open questions. Which matrix index is the column? What do `==` and `<` return on vectors? And when does a struct's C layout match a GPU buffer?

**Who has it.**

| | WGSL | GLSL | HLSL | chibi today |
|---|---|---|---|---|
| Matrix memory | "C column vectors" ([L3103](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L3102-L3107)) | `m[1] = vec4(2.0); // sets the second column` ([operators.adoc](https://github.com/KhronosGroup/GLSL/blob/0099bf83b0288469917fc2e2972cb0a2b74d6711/chapters/operators.adoc#L587-L599)) | `column_major` unless told otherwise ([Variable syntax](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-variable-syntax)) | `[4, 4]f32` in C layout, indexed `m[i, j]` |
| `a == b` on vectors | "Component-wise when T is a vector" ([L7267](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L7260-L7267)) | "a scalar Boolean"; `<` works only on scalars, and vectors use `lessThan` ([operators.adoc](https://github.com/KhronosGroup/GLSL/blob/0099bf83b0288469917fc2e2972cb0a2b74d6711/chapters/operators.adoc#L870-L893)) | (unverified) | struct `==` compares fields (adopt-or-settle 2.3) |
| 3-vector | `vec3<f32>`: align 16, size 12 ([L11840](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L11840-L11842)) | std140: 16-byte aligned (SDL, below) | (unverified) | `[3]f32`: align 4, size 12 |
| 3×3 matrix | `mat3x3<f32>`: 48 bytes ([L11880](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L11880-L11882)) | — | — | `[3, 3]f32`: 36 bytes |

SDL adds a rule for uniform data: "The data being pushed must respect std140 layout conventions. In practical terms this means you must ensure that vec3 and vec4 fields are 16-byte aligned" ([SDL_gpu.h](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/include/SDL3/SDL_gpu.h#L3533-L3535)). Without its `uniform_buffer_standard_layout` extension, WGSL also requires array elements in a uniform buffer to sit on 16-byte boundaries ([L12265](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L12265-L12268)).

**Why it matters for chibi.**
- A struct with `[3]f32` or `[3, 3]f32` fields has different offsets in C and on the GPU. The shader then reads the wrong bytes, with no error. This is the silent kind of mistake that adopt-or-settle ranks highest.
- If chibi reads `m[i, j]` as row `i`, column `j`, then every matrix uploaded byte for byte arrives transposed. WGSL has no row-major option: none of its attributes changes matrix layout ([L9812–L10217](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L9812-L10217)). So the convention has to come from chibi.
- `==` already compares whole structs (adopt-or-settle 2.3). If `==` on arrays returned an array of `bool`, `if a == b` would not compile, and code that sorts or hashes vectors would need a second operator.

**Recommendation.**
- **Matrices.** In `m[i, j]`, `i` selects the column and `j` the row, as GLSL's `m[i][j]` does. A `[4, 4]f32` then uploads to `mat4x4<f32>` unchanged. Matrix products stay named functions, as adopt-or-settle 2.14 recommends.
- **Comparisons.** `==` and `!=` on arrays return one `bool`, as in GLSL. `<`, `<=`, `>` and `>=` on arrays are errors. Component-wise comparisons are functions that return `[N]bool`, used with `any`, `all` and `select`.
- **Layout check.** Keep C layout. When a struct reaches a GPU binding or vertex input, the compiler computes its layout under the target's rules, such as std140 for SDL uniforms and WGSL's table for storage buffers. Where an offset differs from C, it names the field and the padding to insert. Reflection already has every number this needs.
- **Swizzle writes** (`v.xy = p`) can wait. WGSL only gained them as a language extension ([L7935](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L7933-L7937)).

**Decided 2026-11-10:** the matrix convention, `==` and `!=` on arrays returning one `bool`, and the layout check, as recommended ([../decisions.md](../decisions.md)). The ordering-comparison rule is still proposed. Fields narrower than 32 bits add a second part to the layout check (5.6).

### 2.2 Small floats: `f16` and minimum precision

**What it is.** GPUs can compute in 16-bit floats at up to twice the rate (unverified) and with half the memory traffic. Some languages give an exact 16-bit type; others give a hint that the hardware may ignore.

**Who has it.**
- **WGSL:** using `f16` is "a shader-creation error" unless the program says `enable f16;` ([L2953](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L2953-L2956)).
- **Vulkan:** `shaderFloat16` is optional. The Roadmap 2024 profile requires it ([Roadmap-2024.adoc](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/appendices/roadmap/Roadmap-2024.adoc#L44-L52)).
- **HLSL:** `min16float` is a minimum: "hardware can ignore the minimum precision indicators and run at full 32-bit precision" ([Learn](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/using-hlsl-minimum-precision)). Exact 16-bit types came with Shader Model 6.2 (unverified).
- **GLSL ES:** `mediump` and `lowp` precision qualifiers ([variables.adoc](https://github.com/KhronosGroup/GLSL/blob/0099bf83b0288469917fc2e2972cb0a2b74d6711/chapters/variables.adoc#L5861-L5862)).
- **Metal:** `half` (unverified).

**Why it matters for chibi.** A minimum-precision type gives a different result on each GPU, which is the opposite of what rank 3 wants. An exact `f16` is portable wherever it exists, and it is useful on the CPU as a storage format for textures and meshes.

**Recommendation.** Add `f16` later as an exact type. Adding a predeclared type name breaks nothing. Reject minimum-precision types.

### 2.3 Resources and bindings: textures, samplers, buffers, push and specialization constants

**What it is.** A shader reads textures through samplers and reads or writes buffers. Each resource has numbers that tell the CPU where to bind it.

**Who has it.**

| | WGSL | Vulkan GLSL | HLSL | MSL |
|---|---|---|---|---|
| Binding | `@group(0) @binding(1)` | `layout(set = 1, binding = 0)` ([GL_KHR_vulkan_glsl](https://github.com/KhronosGroup/GLSL/blob/0099bf83b0288469917fc2e2972cb0a2b74d6711/extensions/khr/GL_KHR_vulkan_glsl.txt#L120-L131)) | `register(t0, space0)` | `[[texture(0)]]`, `[[buffer(0)]]` |
| Push constants | `immediate` address space; one per entry point ([L5695](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L5692-L5695)) | `layout(push_constant)` ([L201](https://github.com/KhronosGroup/GLSL/blob/0099bf83b0288469917fc2e2972cb0a2b74d6711/extensions/khr/GL_KHR_vulkan_glsl.txt#L201-L219)); Vulkan guarantees 128 bytes ([limits.adoc](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/chapters/limits.adoc#L6882)) | root constants (unverified) | `setBytes`, under 4 KB ([Apple](https://developer.apple.com/documentation/metal/mtlcomputecommandencoder/setbytes(_:length:index:))) |
| Specialization constants | `override`, "fixed at pipeline-creation time" ([L5545](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L5545-L5575)) | `layout(constant_id = 17) const int arraySize = 12;` ([L134](https://github.com/KhronosGroup/GLSL/blob/0099bf83b0288469917fc2e2972cb0a2b74d6711/extensions/khr/GL_KHR_vulkan_glsl.txt#L134-L149)) | none (unverified) | `[[function_constant(index)]]` ([Apple](https://developer.apple.com/documentation/metal/mtlfunctionconstantvalues)) |

SDL fixes the numbers. For SPIR-V vertex shaders it uses "Set 0 for samplers, storage textures, and storage buffers" and "Set 1 for uniform data". Fragment shaders use sets 2 and 3, and bindings start at 0 "leaving no gaps" ([SDL_gpu.h](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/include/SDL3/SDL_gpu.h#L2921-L2932)). There are "4 available uniform slots per shader stage" ([L297](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/include/SDL3/SDL_gpu.h#L297)). SDL implements those slots as uniform buffers, not push constants; its Vulkan backend sets `pushConstantRangeCount = 0` ([SDL_gpu_vulkan.c](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/src/gpu/vulkan/SDL_gpu_vulkan.c#L5320-L5329)). `SDL_gpu.h` mentions no specialization constants and no push constants.

**Why it matters for chibi.**
- Binding numbers are an agreement between shader and CPU code. Written twice by hand, they drift apart. Shader compilers therefore ship reflection: SDL_shadercross writes JSON, and CF fills a `CF_ShaderInfo` struct.
- chibi has compile-time reflection, so CPU code can read the numbers from the entry point itself.
- Without `const` qualifiers (decided 2026-11-10), nothing says whether a buffer is read-only. SDL's compute layout puts read-only and read-write resources in different sets, so the compiler must know. Inferring it from writes in the body would make a signature depend on its body, which parallel checking and hot reload avoid (adopt-or-settle 2.4).

**Recommendation.**
- Textures and samplers are opaque types in a `gpu` package: they have no CPU layout and can't be stored in buffers.
- Uniform buffers are structs, and storage buffers are slices `[]T`.
- A parameter's attribute states its binding kind: `@uniform`, or `@read_write` for a storage buffer the shader writes. Storage buffers are read-only by default, and the checker rejects writes to them.
- The compiler numbers bindings by SDL's table: by resource kind, then in parameter order. Reflection exposes the result to CPU code.
- Push constants and specialization constants wait until a target needs them. `$` parameters on entry points cover shader variants (5.4).

### 2.4 Address spaces

**What it is.** GPU memory comes in kinds with different sharing rules. WGSL's table lists `function`, `private`, `workgroup`, `uniform`, `storage`, `immediate` and `handle` ([L11654](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L11654-L11700)). In WGSL and SPIR-V, a pointer's type includes its address space.

**Who has it.**
- **Written in the pointer type:** WGSL `ptr<function, f32>`; SPIR-V storage classes; Zig's `addrspace(.storage_buffer)`, where the 0.17.1 list has a block of "GPU address spaces" ([lang.zig](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/lang.zig#L575-L622)); Mojo's `AddressSpace` parameter ([address_space.mojo](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/stdlib/std/memory/address_space.mojo#L44-L66)); Slang's `Ptr<T, Access, AddressSpace>` ([03-convenience-features.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/user-guide/03-convenience-features.md#L612-L666)).
- **Inferred:** Rust-GPU says "The SPIR-V storage class of types is inferred for function signatures", guided by attributes on entry points ([attributes.md](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/docs/src/attributes.md#L99-L112)).
- **Generic pointers:** in CUDA, "CUDA pointers (T*) can access objects regardless of where the objects are stored" ([guide](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#address-space-conversion-functions)).

**Why it matters for chibi.** chibi has one `*T` and no qualifiers. If address spaces were part of pointer types, every helper that takes a pointer would need a version per space, the way `const` would have doubled signatures.

**Recommendation.** Leave address spaces out of chibi's types. Entry-point parameters fix each binding's space, and the GPU backend gives a function that takes `*T` one instance per space its callers pass, as Rust-GPU does. The number of instances is bounded by call sites. The only space written in source is `@workgroup` on a declaration (2.7).

### 2.5 Stages, entry points, built-ins, interpolation, `invariant` and `precise`

**What it is.** An entry point names its stage. Its inputs and outputs are built-ins such as the vertex position, or user values linked by location. Interpolation qualifiers say how outputs blend across a triangle: perspective-correct (the default), linear, or flat.

**Who has it.**

| | Entry point | Built-in | Interpolation | Invariance |
|---|---|---|---|---|
| WGSL | `@fragment`, `@compute @workgroup_size(64)` | `@builtin(position)` | `@interpolate(flat)` | `@invariant`, only on `position` ([L10065](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L10065-L10091)) |
| GLSL | `main` per stage, `layout(local_size_x = 64) in;` | `gl_Position` | `smooth`, `flat`, `noperspective` ([L5490](https://github.com/KhronosGroup/GLSL/blob/0099bf83b0288469917fc2e2972cb0a2b74d6711/chapters/variables.adoc#L5490-L5505)) | `invariant`, `precise` ([L6269](https://github.com/KhronosGroup/GLSL/blob/0099bf83b0288469917fc2e2972cb0a2b74d6711/chapters/variables.adoc#L6269-L6300)) |
| HLSL | `[numthreads(64, 1, 1)]` | `SV_Position` | `nointerpolation`; `linear` and `noperspective` (unverified) | `precise` |
| Slang | `[shader("compute")]` ([L729](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/user-guide/02-conventional-features.md#L729)) | HLSL semantics | HLSL's | HLSL's |
| Rust-GPU | `#[spirv(fragment)]`, `#[spirv(compute(threads(32)))]` ([L13](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/docs/src/attributes.md#L13-L45)) | `#[spirv(global_invocation_id)]` | (unverified) | — |
| Zig 0.17 | `callconv(.{ .spirv_fragment = .{} })` ([lang.zig](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/lang.zig#L380-L386)) | extern variables in `std.spirv`, named `std.gpu` before 0.17 ([0.15.2](https://github.com/ziglang/zig/blob/e4cbd752c8c05f131051f8c873cff7823177d7d3/lib/std/gpu.zig#L1-L127)) | a `flat` decoration on `@extern` ([L1211](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/lib/std/lang.zig#L1211-L1231)) | — |

HLSL's `precise` keeps "Separate operations ... separate" (no fused multiply-add), keeps the written order, and respects NaN and infinity rules ([Variable syntax](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-variable-syntax)). SPIR-V's `NoContraction` decoration means "if applied to an OpFMul, that multiply can't be combined with an addition to yield a fused multiply-add" ([SPIR-V, Decoration](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#Decoration)). WGSL has no `precise`. Its `@invariant` "maps to the `precise` qualifier in HLSL, and the `invariant` qualifier in GLSL".

**Why it matters for chibi.** Entry points and built-ins must be visible to the declaration pass, because they decide which functions become GPU code. Attributes already exist for this purpose (adopt-or-settle 3.8), so nothing new is needed in the parser. Only `@tail` is decided so far; the rest of the attribute design is open (decision row of 2026-11-12), and the GPU attributes below will be its largest user.

**Recommendation.** Mark entry points with `@vertex`, `@fragment` and `@compute(x, y, z)` on ordinary functions. A built-in is a parameter or struct field marked `@builtin(.position)`, with names from an enum in `gpu`. Locations follow declaration order unless `@location(n)` overrides them. Add `@flat`, `@linear` and `@invariant` later. `precise` belongs to rank 3 (5.3).

### 2.6 Uniform control flow, derivatives and `discard`

**What it is.** WGSL: "A collective operation (e.g. barrier, derivative, or a texture operation relying on an implicitly computed derivative) requires coordination among different invocations." Such an operation is only portable "when all invocations execute it concurrently, i.e. in uniform control flow." WGSL therefore runs a static uniformity analysis. A failure on a derivative triggers a `derivative_uniformity` diagnostic, which is an error by default; a failure on a barrier is always an error ([L12504](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L12504-L12560)). Derivatives exist only in fragment shaders, and they return "an indeterminate value if called in non-uniform control flow" ([L18037](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L18037-L18057)). `discard` "converts the invocation into a helper invocation and throws away the fragment output" ([L8910](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L8910-L8925)). SPIR-V deprecates `OpKill` in favor of `OpTerminateInvocation` and `OpDemoteToHelperInvocation` ([OpKill](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpKill)).

```wgsl
@fragment fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
    if (uv.x > 0.5) {
        return textureSample(tex, smp, uv); // error: derivative in non-uniform control flow
    }
    return vec4f(0.0);
}
```

**Why it matters for chibi.** If chibi prints WGSL, Tint and Naga run this analysis on chibi's output, and their errors point at generated code. To report the error at the chibi line, the checker needs its own analysis, at least as strong as WGSL's. That is a pass over GPU-reachable functions with no syntax. It needs structured control flow, which section 4 requires anyway.

**Recommendation.** Derivatives, sampling with an implicit level of detail, and `discard` are `gpu` intrinsics allowed only in fragment-reachable code. The uniformity analysis comes later, before the WGSL printer.

### 2.7 Workgroup memory, barriers, atomics and subgroups

**What it is.** Compute invocations in one workgroup can share fast on-chip memory. A barrier makes every invocation wait until all have reached it, so writes before it are visible after it. Atomics update memory without data races. Subgroup operations combine values across the invocations that run in lockstep, such as a sum over 32 lanes, without going through memory.

**Who has it.**
- **WGSL:** `var<workgroup>`, `workgroupBarrier()` and `atomic<u32>`. Subgroup functions need `enable subgroups;`, and "All subgroup sizes are powers of two within the range [4, 128]" ([L14116](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L14116)). Workgroup variables start at zero ([L5763](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L5763-L5775)).
- **HLSL:** `groupshared`, limited to 32 KB in D3D11, and `GroupMemoryBarrierWithGroupSync()` ([Variable syntax](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-variable-syntax)). Wave operations need the `WaveOps` capability ([D3D12 options](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options1)).
- **Rust-GPU:** an entry-point parameter `#[spirv(workgroup)] var: &mut [Vec4; 4]`.
- **Mojo:** `stack_allocation[..., address_space=.SHARED]` and `barrier()` ([block-and-warp.mdx](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/max/docs/site/gpu/block-and-warp.mdx#L94-L100)).

**Why it matters for chibi.** These are the tools of compute work such as particles, culling and prefix sums. Only workgroup memory needs a declaration form; the rest are calls.

**Recommendation.** `@workgroup` marks a package-level declaration that only compute code may use: `@workgroup tile: [256]f32 = undefined`. Here the decided `undefined` skips the zeroing on SPIR-V, MSL and HLSL; WGSL zeroes anyway. Barriers and subgroup operations are `gpu` intrinsics. Atomics are one `atomic` package that lowers to C11 atomics on the CPU and to atomic instructions on the GPU.

### 2.8 Core language or library

| Feature | Where | Why |
|---|---|---|
| Vectors, matrices, swizzles, component-wise arithmetic | core | adopt-or-settle 2.14; plus the matrix and comparison rules of 2.1 |
| Component-wise comparisons | library | functions returning `[N]bool`; `==` stays one `bool` |
| Buffer layout check | core | it guards C layout, a core rule |
| `f16` | core, later | a type; additive |
| Minimum-precision types | rejected | results differ per device |
| Textures, samplers | library | opaque types and intrinsics in `gpu` |
| Uniform and storage buffers | core | structs and slices; binding kinds are attributes |
| Address spaces | core, inferred | no syntax except `@workgroup` |
| Entry points and their attributes | core | attributes the declaration pass reads |
| Bindings and locations | core | assigned by the compiler; `@location(n)` overrides |
| Built-in inputs and outputs | core attribute, library names | `@builtin(.position)`, enum in `gpu` |
| Interpolation qualifiers | core attributes, later | `@flat`, `@linear` |
| `invariant` and `precise` | core attributes, later | see 5.3 |
| Derivatives, uniform control flow | library intrinsics; core analysis later | the analysis is a checker pass |
| `discard` | library | an intrinsic, fragment code only |
| Workgroup memory | core attribute | `@workgroup` |
| Barriers, atomics, subgroup operations | library | intrinsics |
| Push constants | later | SDL doesn't use them |
| Specialization constants | later | `$` parameters cover variants; SDL exposes none |

## 3. How CPU-and-GPU languages share code (question 2)

**What it is.** A single-source language lets one function run on both the CPU and the GPU. It has to answer four questions. Which code goes to the GPU? How does shared code pick target-specific parts? What does the GPU path forbid? And what does it emit?

**Who has it.**

| Language | GPU code chosen by | Shared code | Target-specific code | Emits |
|---|---|---|---|---|
| Slang | `[shader("stage")]` entry points | any reachable function; required capabilities are inferred ([05-capabilities.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/user-guide/05-capabilities.md#L279-L387)) | `__target_switch`, "considered internal compiler features" and "subject to breaking changes" ([a1-04-interop.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/user-guide/a1-04-interop.md#L8-L17)) | SPIR-V, DXIL, DXBC, MSL, metallib, WGSL, CUDA, PTX, C++, LLVM ([slangc reference](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/command-line-slangc-reference.md#L1336-L1375)) |
| CUDA C++ | `__global__` kernels | "Functions declared with multiple execution spaces (for example, `__host__ __device__`) are compiled for each context" ([guide](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#execution-space-specifiers)) | `#if defined(__CUDA_ARCH__)` | PTX, then cubin |
| Rust-GPU | `#[spirv(...)]` entry points in a `no_std` crate | crates such as `glam` on both sides ([structure.md](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/docs/src/getting-started/structure.md#L36-L51)) | `#[cfg(target_arch = "spirv")]` | SPIR-V only; "not yet production-ready" ([README](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/README.md#L20-L27)) |
| Zig 0.17.1 | SPIR-V calling conventions | the same source compiled per target | `comptime` switches on `builtin.target` | SPIR-V; its tests are disabled "due to no active maintainer" ([tests.zig](https://codeberg.org/ziglang/zig/src/commit/be248cc1fdc3cc654f9ec8ba68514bcb93b204fe/test/tests.zig#L1587-L1599)) |
| Mojo | any function, compiled with `ctx.compile_function[f]()` ([fundamentals.mdx](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/max/docs/site/gpu/fundamentals.mdx#L186-L204)) | v1.2.0: "CPU and GPU now run one shared Mojo implementation" of `std.math` ([notes](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/docs/site/releases/v1.2.0.md#L17-L21)) | `comptime if is_gpu()` ([info.mojo](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/stdlib/std/sys/info.mojo#L1110-L1118)) | PTX (NVIDIA), AMD, metallib (Apple) |
| Circle | `[[spirv::vert]]` and similar ([README](https://github.com/seanbaxter/shaders/blob/185c6e81bfaabd54fde80a0a2318636ae01bfab5/README.md#L286-L349)) | "If ODR-used from a SPIR-V entry point ... the AST is lowered to SPIR-V IR" | `if codegen(__is_spirv_target)` | SPIR-V and DXIL; the repository's last commit is from 2022 |
| ISPC | SPMD "gangs" on CPU SIMD; `task` functions on Intel Xe GPUs ([ispc_for_xe.rst](https://github.com/ispc/ispc/blob/752011f5c4f59bbe9c5a60f2322d652830c6fb5f/docs/ispc_for_xe.rst#L226-L350)) | the same functions | — | CPU code; SPIR-V for Xe |
| Odin | nothing of its own | — | — | bindings only |
| Jai | nothing in the language | — | — | — |

Circle's README states the design choice outright: "There's no annotation for discriminating code that's intended to run in a shader versus code that is meant for host execution" ([README](https://github.com/seanbaxter/shaders/blob/185c6e81bfaabd54fde80a0a2318636ae01bfab5/README.md#L1966-L1996)). CUDA marks every shared function instead:

```cpp
__host__ __device__ void func() {
#if defined(__CUDA_ARCH__)
    // device code path
#else
    // host code path
#endif
}
```

Slang picks implementations per target inside one function ([a1-04-interop.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/user-guide/a1-04-interop.md#L113-L142), trimmed):

```hlsl
uint2 getRealtimeClock() {
    __target_switch {
    case glsl: __intrinsic_asm "clockRealtime2x32EXT()";
    default:   return uint2(0, 0);
    }
}
```

Rust-GPU declares bindings on entry-point parameters ([compute.md](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/docs/src/getting-started/compute.md#L15-L24), trimmed):

```rust
#[spirv(compute(threads(32)))]
pub fn main(
    #[spirv(descriptor_set = 0, binding = 0, storage_buffer)] a: &[u32],
    #[spirv(global_invocation_id)] global_index: UVec3,
) { /* … */ }
```

Other details:
- **Slang's CPU target** runs compute only. "Barriers are not supported", "groupshared is not yet supported", and a `Texture2D` becomes an interface "that has to be implemented in client side code" ([cpu-target.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/cpu-target.md#L13-L21)). Its compiler rejects recursion on every target but the CPU ([slang-ir-check-recursion.cpp](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/source/slang/slang-ir-check-recursion.cpp#L96-L134)). Slang moved to Khronos governance on 2024-11-21 ([Khronos](https://www.khronos.org/news/archives/khronos-group-launches-slang-initiative-hosting-open-source-compiler-contributed-by-nvidia)).
- **ISPC's** `uniform` qualifier marks "a single value that is shared across the entire gang", and `varying` is the default ([ispc.rst](https://github.com/ispc/ispc/blob/752011f5c4f59bbe9c5a60f2322d652830c6fb5f/docs/ispc.rst#L2573-L2589)). On Xe, floats are "not guaranteed to be bit-reproducible between CPU and GPU" ([ispc_for_xe.rst](https://github.com/ispc/ispc/blob/752011f5c4f59bbe9c5a60f2322d652830c6fb5f/docs/ispc_for_xe.rst#L537-L558)).
- **Odin's** author on its forum, 2024-11-04: "I would highly recommend against using a general purpose language like Odin on the GPU", because "You'd have to restrict Odin by a lot to the point it wouldn't be Odin any more" ([forum](https://forum.odin-lang.org/t/spirv-compilation-from-odin/196/4)). Odin ships graphics bindings and, since 2026-06-26, a SPIR-V encoding library, `core:rexcode/ir/spirv`, but no shader target ([spirv.odin](https://github.com/odin-lang/Odin/blob/2347ac067f9e4cdd26019c48f2be1b99a9fce749/core/rexcode/ir/spirv/spirv.odin#L1-L22)).
- **Jai** has a community "Jai-Shader-Transpiler", a "Jai metaprogram for converting Jai functions to GLSL shaders" (community source, `~/Personal/jai-refs/JCL-wiki/References.md`, line 15). No statement by Jonathan Blow on compiling Jai to shaders was found.

**Why it matters for chibi.** Odin's warning is half right. Section 4 shows that GPU code must give up recursion, function values, 64-bit integers, strings, allocation and coroutines. But chibi already plans per-instance checking for generics, and a per-target instance is the same mechanism. Circle, Slang and Mojo show that a language can keep one syntax and restrict only the GPU instances. CUDA's marks are the cost to avoid: every shared helper needs one, and a missing mark is a compile error far from its cause.

**Recommendation.**
- **Reachability decides.** An entry point attribute makes a function a GPU root. Every function it calls gets a GPU instance, and the GPU rules of section 4 apply to that instance only. An error names the call chain, as Slang's capability errors do.
- **`$if target.gpu`** selects target-specific code, using sheet 7's compile-time `if`. A predeclared compile-time value `target` describes the instance being compiled. Only the branch taken is checked, so CPU-only calls in the other branch are fine.
- **One instance per target.** It is keyed like a generic instance, so caching and reload reuse that machinery.
- **No `__host__ __device__` marks,** and no separate shader files or packages.

```
package sprites

import "gpu"

struct Frame {
    view_proj: [4, 4]f32
    tint: [4]f32
}

struct Varyings {
    @builtin(.position) clip: [4]f32
    uv: [2]f32
}

fn tinted(c: [4]f32, t: [4]f32) -> [4]f32 => c * t

@vertex
fn sprite_vs(pos: [2]f32, uv: [2]f32, @uniform frame: Frame) -> Varyings {
    clip := gpu.mul(frame.view_proj, .[pos.x, pos.y, 0.0, 1.0])
    return Varyings.{ clip, uv }
}

@fragment
fn sprite_fs(v: Varyings, @uniform frame: Frame, tex: gpu.Texture2D, smp: gpu.Sampler) -> [4]f32 {
    return tinted(gpu.sample(tex, smp, v.uv), frame.tint)
}

@compute(64)
fn scale(@builtin(.global_id) id: [3]i32, @read_write data: []f32, @uniform k: f32) {
    i := id.x
    if i < data.len {
        data[i] = data[i] * k
    }
}
```

`tinted` runs on both sides, and CPU code calls it as usual. On the CPU side, a compile-time call such as `gpu.shader(sprite_fs)` could yield a constant that holds the SPIR-V bytes and the reflected bindings, ready for `SDL_CreateGPUShader` (a sketch, not a proposal for the exact API).

## 4. What a GPU target forbids (question 3)

**What it is.** Shader targets give up the CPU features that need a call stack, a heap, or pointers with numeric values, and they leave many edge cases undefined. The table shows each rule per target.

**Why it matters for chibi.** Each "forbidden" below becomes a check on GPU instances. Each "undefined" becomes a fallback that chibi must emit itself, because ranks 1 and 9 promise no undefined behavior.

| Rule | WGSL | SPIR-V for Vulkan | HLSL and DXIL | Metal | CUDA |
|---|---|---|---|---|---|
| Recursion | "functions must not be recursive, either directly or indirectly" ([L2192](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L2179-L2192)) | "static recursion is not allowed" ([spirvenv.adoc](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/appendices/spirvenv.adoc#L182-L183)) | "Recursion is not allowed; DXIL validator enforces this" ([DXIL.rst](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/DXIL.rst#L260)) | only through indirect calls, with a declared stack depth ([Apple](https://developer.apple.com/documentation/metal/mtlcomputepipelinedescriptor/maxcallstackdepth)) | allowed in `__device__` functions ([guide](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-support.html#recursion)) |
| Function pointers | none (unverified) | none without an extension (unverified); Rust-GPU reports "function pointer types are not allowed" ([spirv_type.rs](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/crates/rustc_codegen_spirv/src/spirv_type.rs#L180)) | none (unverified) | visible function tables, macOS 11 and iOS 14 ([Apple](https://developer.apple.com/documentation/metal/mtlvisiblefunctiontable)) | allowed, but host code can't take a `__device__` function's address ([guide](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-support.html#pointers-and-memory-addresses)) |
| Pointers | "not storable", can't be returned, no integer conversion, no heap ([L4637](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L4628-L4655)) | Logical: "pointers are abstract", "must not be stored into an object" ([SPIR-V](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#MemoryModelSection)); buffer addresses need `PhysicalStorageBuffer64` | none (unverified) | (unverified) | generic pointers |
| 64-bit integers | "WGSL does not have a concrete 64-bit integer type" ([L12130](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L12130)) | `shaderInt64` is optional (below) | `Int64ShaderOps` capability ([Learn](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options1)) | (unverified) | yes |
| 64-bit floats | none: `f32`, and `f16` with an extension ([L2947](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L2947-L2956)) | `shaderFloat64` optional ([features.adoc](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/chapters/features.adoc#L681-L688)) | optional (unverified) | (unverified) | yes |
| Integer overflow | "modulo 2^bitwidth" ([L2932](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L2932)) | `OpIAdd` keeps "the low-order N bits" ([SPIR-V](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpIAdd)) | (unverified) | (unverified) | (unverified) |
| Divide by zero | `x / 0` is `x`, `x % 0` is 0; `MIN / -1` is `MIN` ([L7059](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L7059-L7116)) | "Behavior is undefined" ([OpSDiv](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpSDiv)) | D3D11 `udiv`: "Divide by zero returns 0xffffffff for both quotient and remainder" ([D3D11.3](https://github.com/microsoft/DirectX-Specs/blob/5122ac08b27588a5bbbe0a7390474bcab44aaf2e/d3d/archive/D3D11_3_FunctionalSpec.htm#L25814)) | (unverified) | (unverified) |
| Shift ≥ width | count "modulo the bit width" ([L7363](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L7363-L7364)) | result "is poison" ([SPIR-V](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpShiftLeftLogical)) | (unverified) | (unverified) | (unverified) |
| Out of bounds | a "dynamic error"; outcomes include ending the invocation, loading zero or any value from the same buffer, and storing anywhere in that buffer or not at all ([L4151](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L4151-L4214)) | undefined for logical access chains; `robustBufferAccess` must be supported but is optional to enable ([vk.xml](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/xml/vk.xml#L20387-L20389)) | "Each memory access instruction defines its behavior for out of bounds accesses" ([D3D11.3](https://github.com/microsoft/DirectX-Specs/blob/5122ac08b27588a5bbbe0a7390474bcab44aaf2e/d3d/archive/D3D11_3_FunctionalSpec.htm#L7539)) | (unverified) | — |
| Trap | none the program can request; an out-of-bounds access may end the invocation | `VK_KHR_shader_abort`, ratified, "triggering device loss" ([proposal](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/proposals/VK_KHR_shader_abort.adoc#L14-L61)) | none (unverified) | none (unverified) | `__trap()`; `assert` leaves "a corrupted CUDA context" ([guide](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#trap)) |
| Strings, allocation, coroutines | none; "no way to allocate new memory from a 'heap'" | `OpString` only names debug info and "has no semantic impact" ([OpString](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpString)) | none (unverified) | none (unverified) | `printf` and an 8 MB device heap ([guide](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-support.html#malloc-and-free)); coroutines (unverified) |

**Floats.** No GPU target promises CPU-identical results:
- **WGSL.** "No rounding mode is specified. An implementation may round an intermediate result up or down." Implementations "may assume that overflow, infinities, and NaNs are not present", and subnormal inputs and outputs "may be flushed to zero" ([L14354](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L14351-L14400)). "An implementation may reassociate operations", and "may fuse operations if the transformed expression is at least as accurate" ([L14733](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L14721-L14738)). Even the `fma` built-in "may expand to an ordinary multiply ... and an add".
- **Accuracy bounds.** WGSL and Vulkan give the same bounds for f32. Division is within 2.5 ULP; `sin` and `cos` have an absolute error of at most 2^-11 on [-π, π]; `exp` is within 3 + 2·|x| ULP ([WGSL L14507–L14602](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L14496-L14602); [spirvenv.adoc](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/appendices/spirvenv.adoc#L4055-L4123)).
- **Vulkan.** "If the operation is not decorated NoContraction then the flags AllowContract, AllowReassoc, AllowRecip, and AllowTransform are assumed". Subnormals "may be flushed to zero" unless the shader asks for `DenormPreserve`, and that support is optional ([spirvenv.adoc](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/appendices/spirvenv.adoc#L3646-L3740)). The GLSL `fma` is "Inherited from OpFMul followed by OpFAdd" ([L4103](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/appendices/spirvenv.adoc#L4103-L4104)). A correctly rounded fma needs the 2025 `VK_KHR_shader_fma` extension ([appendix](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/appendices/VK_KHR_shader_fma.adoc)).
- **HLSL.** "By default, all floating-point HLSL operations are considered 'fast' or non-precise" ([DXIL.rst](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/DXIL.rst#L414-L416)). D3D11 requires f32 subnormals to be "flushed to sign-preserved zero" ([D3D11.3](https://github.com/microsoft/DirectX-Specs/blob/5122ac08b27588a5bbbe0a7390474bcab44aaf2e/d3d/archive/D3D11_3_FunctionalSpec.htm#L448)). DXIL can set the mode to "'any', 'preserve', or 'ftz'" ([L268](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/DXIL.rst#L268)).
- **Metal.** The relaxed math mode "is the default for Apple silicon devices", and the fast mode "is the default for Intel and AMD devices" ([relaxed](https://developer.apple.com/documentation/metal/mtlmathmode/relaxed), [fast](https://developer.apple.com/documentation/metal/mtlmathmode/fast)). The fast FP32 math functions are "the default behavior" ([Apple](https://developer.apple.com/documentation/metal/mtlmathfloatingpointfunctions/fast)). SDL compiles MSL with `options:nil`, so these defaults apply ([SDL_gpu_metal.m](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/src/gpu/metal/SDL_gpu_metal.m#L865-L873)).

**64-bit integers in Vulkan, verified.** The feature text says: "If this feature is not enabled, 64-bit integer types must not be used in shader code" ([features.adoc](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/chapters/features.adoc#L689-L696)). No core version requires `shaderInt64`. `vk.xml` requires it only for devices that support 64-bit atomics ([vk.xml](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/xml/vk.xml#L21000-L21002)). None of the 2022, 2024 and 2026 roadmap profiles in `VP_KHR_roadmap.json` lists it ([profiles](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/xml/profiles/VP_KHR_roadmap.json)). Buffer device addresses, by contrast, are required from Vulkan 1.3 ([vk.xml](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/xml/vk.xml#L21327-L21360)). But SDL starts Vulkan 1.0 unless an app passes Vulkan options.

**Recommendation: the GPU check list.** The checker applies these rules to GPU instances only:
1. The call graph is acyclic: one strongly-connected-components pass over GPU-reachable functions.
2. Function values must be known at compile time, so each call becomes direct. Dispatch goes through a `switch` on a tagged union.
3. Pointers: `*T` may point at a local, a binding element or workgroup memory. It may be passed to functions, but not stored, returned, compared or converted. `?*T`, `[*]T` and pointer arithmetic are errors.
4. Types: no `int`, `i64`, `u64` or `f64` unless the target has them (5.2). Types narrower than 32 bits follow 5.6. No `string` values at run time.
5. No allocation, no context reads (rank 7), no tasks or coroutine suspension (their keywords wait for the coroutines sheet), and no `export fn`.
6. Control flow is structured. `goto` is no longer a keyword (token audit, 2026-11-11). The decided labels are `break name`, `continue name`, `break name value` out of a labeled block, and `continue name value` into another arm of a labeled `switch`. WGSL's `break` takes no label ([syntax](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/syntax/break_statement.syntax.bs.include)), so the backend lowers `break outer` with a flag variable, and a `break` with a value stores the value in a local first. `continue name value` becomes a loop around the `switch`, with the next value in a local. The checker rejects `continue name value` for now (decision row of 2026-11-16). All of this is my analysis.
7. Textures and samplers can't be struct fields and can't be copied into locals; they reach a function only as parameters. DXC and glslang need SPIRV-Tools' legalization passes mainly for HLSL that does both, so this rule lets the SPIR-V writer run with no legalization ([shader-backends.md](shader-backends.md) 1.1, item 3).
8. `@tail return` is an error. This was proposed with the tail-call decision of 2026-11-12 ([checker-and-ir.md](checker-and-ir.md) 6.2, rule 4).

## 5. Effect on adopt-or-settle ranks 1–10 and the other sheets (question 4)

| Item | What a GPU target needs | Change? |
|---|---|---|
| 1. Overflow (decided: option C) | a result without a trap; WGSL wraps and defines `x / 0` | **Add a GPU rule** (5.1) |
| 2. `int` is 64-bit; literal types (decided 2026-11-11) | no 64-bit integers on WGSL; optional elsewhere | **Yes** (5.2) |
| 3. One contract for every backend | GPUs can't give bit-identical floats | **Yes: narrowed to CPU backends** (5.3) |
| 4. Errors (no exceptions; `try` and `catch` decided) | values only; no unwinding | No. An error union is a plain struct and works on the GPU; its `u16` code follows 5.6. Mojo's typed errors "work on GPUs ... as long as you avoid heap-allocated types like `String`" ([errors.mdx](https://github.com/modular/modular/blob/f364f26cd29207a9501625f566527354bb5dac4f/Mojo/docs/site/manual/errors.mdx#L242-L246)). Entry points can't return errors. |
| 5. Pointers (decided 2026-11-15) | no stored pointers, no arithmetic, no null | No. The split between `*T` and `[*]T` is what lets GPU code keep `*T` and reject `[*]T`. |
| 6. Initialization (`undefined` decided) | WGSL always zeroes; SPIR-V needn't | No. `undefined` on workgroup arrays saves the zeroing on SPIR-V, MSL and HLSL. |
| 7. C ABI, thread-local context | no ABI and no thread-local storage on the GPU | No. Option B is the GPU-friendly one: GPU code simply can't read the context, while a hidden parameter (option A) would have to be stripped from every GPU function. |
| 8. Parameters | GPU functions are mostly inlined | No. A printer must not lower a `*T` parameter to HLSL's copy-in, copy-out `inout` when arguments may alias (unverified). |
| 9. Safety modes (bounds checks decided 2026-11-15) | no trap; robustness varies | **Add the same GPU rule** (5.1) |
| 10. Strings (no const correctness decided) | no strings | No. Strings stay CPU-only; failure reports carry source IDs, not text. |
| Generics | monomorphic code only | No. Per-instance checking fits, and a target is one more key. |
| Compile-time code | variants and bindings | No. It gains two uses (5.4). |
| Tasks | no coroutines | No. Tasks stay CPU-only; a task can `await` a GPU fence. |
| Context | none on the GPU | No (rank 7). |
| `bool`, enums, `?T`, `E!T` (decided 2026-11-11 to 2026-11-16) | no 8- or 16-bit integers on WGSL, optional on Vulkan; no `bool` in buffers or between stages | **Yes** (5.6) |

### 5.1 Ranks 1 and 9 on the GPU: checks that can't stop the program

**What it is.** The rank 1 decision says `+`, `-` and `*` trap in debug and release builds, and that a zero divisor traps in every build. Its decision row now says: "Shaders can't trap; shader-languages.md 5.1 proposes fixed GPU results instead, not yet decided". Rank 9's bounds checks were decided on 2026-11-15: indexing and slicing check bounds in every build, and a block can opt out with an attribute. Narrowing conversions, enum conversions and `switch` add checks of their own (decision rows of 2026-11-10 and 2026-11-16). On a GPU:
- A WGSL program has no way to request a trap, and SDL exposes none.
- Vulkan's new `VK_KHR_shader_abort` aborts by "triggering device loss in the API". It is optional, and SDL doesn't expose it.
- WGSL warns that ending one invocation early breaks group operations: "a call to workgroupBarrier may hang the shader" ([L4211](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L4208-L4212)).

**Options.**
- **A. Defined fallbacks, plus reports in debug builds.** Release GPU builds use one fallback per check. Debug GPU builds also record the first failure, as its kind, a source-location ID and the invocation ID, with an atomic write into a hidden storage buffer. Execution then continues with the fallback, so barriers still meet. After the frame, the CPU maps the ID back to `file:line` and reports a trap. Rust-GPU meets the same problem with panics. Its default `SilentExit` means "Return from shader entry-point with no side-effects", and `DebugPrintfThenExit` reports first ([spirv-builder](https://github.com/Rust-GPU/rust-gpu/blob/60d1e2541ea55c82e1024b8d6969573736f062a5/crates/spirv-builder/src/lib.rs#L158-L262)). chibi continues instead of returning, because an early return can hang a barrier.
- **B. Abort through `VK_KHR_shader_abort`.** This is a real trap, but only on Vulkan drivers that support it, and every abort loses the device.
- **C. Forbid what can fail,** such as division by a value that isn't proven nonzero. This is too strict for ordinary shader code.

**Recommendation.** Option A, with these fallbacks:

| Check | CPU (decided) | GPU fallback, every build |
|---|---|---|
| `+`, `-`, `*`, negation | trap; wrap in release-fast | wrap |
| `x / 0`, `MIN / -1` | trap in every build | `x`, as in WGSL |
| `x % 0`, `MIN % -1` | trap in every build | `0`, as in WGSL |
| shift ≥ width | trap; count modulo width in release-fast | count modulo width, as in WGSL |
| index out of bounds | trap | the load gives zero; the store is skipped |
| `x!!` on `nil` | trap in every build | the zero value of the type |
| narrowing `T(x)` out of range | trap in debug and release builds | the low bits, as `T(x & mask)` gives |
| `Color(n)` when no member has the value `n` | trap in checked builds | `n`, unchanged |
| `switch` with no arm for its value, which only an invalid enum value reaches | trap | no arm runs; a `switch` used as a value gives the zero value |

The fallbacks cost little. WGSL notes that its division rule "may require an implementation to perform more operations", and its source sketches the fix as one `select` on the divisor ([L7077](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L7077-L7084)). chibi emits that `select` itself for SPIR-V, MSL and HLSL. A bounds check is one unsigned compare and a `select` per dynamic index. Constant indices into fixed arrays need no check. This is my estimate, not a measurement. The CPU and GPU differ only for programs that already have a bug, which is how rank 1 already separated options B and C. The opt-out attribute for bounds checks (decided 2026-11-15; its spelling waits for the attribute syntax) keeps its meaning on the GPU: no check, and the platform's behavior. On SDL's Vulkan backend that behavior is undefined by default: SDL's default device enables only a short list of features, and `robustBufferAccess` only when the app requests it through `SDL_GPUVulkanOptions` ([defaults](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/src/gpu/vulkan/SDL_gpu_vulkan.c#L12785-L12799), [opt-in](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/src/gpu/vulkan/SDL_gpu_vulkan.c#L11872-L11898)).

### 5.2 Rank 2 on the GPU: there is no 64-bit `int`

**What it is.** Rank 2 makes `int` 64-bit everywhere and makes it the type of lengths, so results match on wasm and desktop. No portable GPU target has 64-bit integers (section 4). WGSL's `arrayLength` returns `u32` ([L16112](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L16112)), and its built-in indices are `u32` or `vec3<u32>` ([L10578](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L10578-L10617)).

**Options.**
- **A. GPU code has no `int`.** Naming `int` or `uint` in GPU code is an error. An untyped integer with no other type becomes `i32` there. WGSL does the same: when nothing forces a type, "an integer literal is interpreted as an i32 value" ([L2818](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L2818-L2823)), and GLSL and HLSL do too (unverified). `.len` is an `i32` there. Built-in indices may be declared `i32`, and the compiler converts them; a dispatch never comes near 2^31 invocations (my analysis).
- **B. `int` means `i32` in GPU code.** No error, but a struct with an `int` field would have different layouts on the two sides. That is the pointer-sized-`int` problem that rank 2 rejected.
- **C. Emulate 64-bit integers on the GPU.** Every add and compare would become several 32-bit operations (unverified cost), in the hottest loops of shader code.
- **D. Make GPU code `u32`-first,** with `.len` a `u32`. That matches WGSL, but it brings back the unsigned-length bugs that made C3 reverse its sizes.

**Decided 2026-11-11:** option A, together with rank 2 ([../decisions.md](../decisions.md)). chibi has no `uint`, so the list below no longer names it, and lengths became the `.len` field on 2026-11-12.

**Recommendation.** Option A. On the CPU, rank 2 is unchanged. In GPU code:
- `int`, `i64`, `u64` and `f64` are errors unless the target declares them (`$if target.has_int64`). On SDL's Vulkan backend the app can request `shaderInt64` and `shaderFloat64` through `SDL_GPUVulkanOptions`, and SDL's default device enables neither ([SDL_gpu_vulkan.c](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/src/gpu/vulkan/SDL_gpu_vulkan.c#L12785-L12799));
- untyped integers default to `i32`, and `.len` is an `i32`;
- shared code that writes no integer type compiles on both sides with the same results below 2^31;
- a struct shared with the GPU uses `i32`, `u32` and `f32`, which the layout check (2.1) enforces anyway.

```
fn sum4(a: [4]f32) -> f32 {
    s := 0.0              // f32 on both sides
    for i in 0..4 {      // i is int on the CPU and i32 on the GPU
        s += a[i]
    }
    return s
}

@fragment
fn shade(v: Varyings) -> [4]f32 {
    n: int = 3            // error: GPU code has no 64-bit int; write i32
    …
}
```

### 5.3 Rank 3 on the GPU: one contract for the CPU, error bounds for the GPU

**What it is.** Rank 3 promises one result on every backend: no contraction, no fast math, IEEE rounding. Section 4 shows that no GPU API promises this. WGSL can't even express it, because it has no `precise`, its rounding direction is unspecified, and implementations may flush subnormals and assume there is no NaN.

**Options.**
- **A. One result on every CPU backend, error bounds on the GPU.** The interpreter, the development backend and the C backend keep the strict contract. GPU code has the target's documented bounds, and determinism-critical code (lockstep simulation, replays) runs on the CPU.
- **B. Make the GPU as strict as possible.** That means `NoContraction` on every SPIR-V float operation, `precise` everywhere in HLSL, Metal's safe math mode, and `DenormPreserve` where supported. It is slower, it still differs in transcendental functions and division, and WGSL can't do it.
- **C. Loosen the CPU contract to match GPUs.** This throws away lockstep determinism, which GPUs can't give anyway.

**Recommendation.** Option A. Rank 3's sentence becomes: "one result on every CPU backend; GPU results are within the target's documented error bounds." Two consequences follow:
- A table that compile-time code computes from a shared function matches the CPU, not the GPU.
- The explicit `fma` builtin is fused on the CPU but only "fused or not" on the GPU, unless `VK_KHR_shader_fma` is present.

A `@precise` block attribute that emits `NoContraction` and `precise` can come later, for watertight seams.

### 5.4 Generics, compile-time code, tasks and the context

- **Generics.** GPU backends only see monomorphic code, and per-instance checking (sheet 6) already produces it. Slang checks constrained generics once at the definition ([06-interfaces-generics.md](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/user-guide/06-interfaces-generics.md#L97)), but chibi doesn't need that for the GPU. Slang's dynamic dispatch through interface values needs concrete types "free of any opaque-typed fields" ([L683](https://github.com/shader-slang/slang/blob/4f66408b3dee5a1d7e6200326b48eb4603b306e7/docs/user-guide/06-interfaces-generics.md#L683-L755)), so chibi's tagged unions and `switch` are simpler.
- **Compile-time code.** `$` parameters on an entry point replace preprocessor permutations, and each instance is its own pipeline: `@fragment fn shade($normal_map: bool, …)`. Reflection over the entry point gives CPU code the bindings, vertex attributes and uniform structs. That replaces SDL_shadercross's JSON and CF's `CF_ShaderInfo`. The interpreter rejects GPU-only intrinsics such as sampling, as Slang's CPU target leaves textures to client code.
- **Tasks.** No GPU target has coroutines, and ISPC says "`launch` and `sync` keywords are not supported for GPU" ([ispc_for_xe.rst](https://github.com/ispc/ispc/blob/752011f5c4f59bbe9c5a60f2322d652830c6fb5f/docs/ispc_for_xe.rst#L537-L558)). Sheet 9's design is unchanged. A CPU task can `await` a submitted GPU command buffer's fence, which suits a loading screen or a compute job.
- **Context.** GPU code has no thread-local storage and no allocator. Reading the context from GPU code is an error.
- **Hot reload.** Editing a GPU-reachable function re-emits the entry points that reach it and recreates their pipelines at the frame boundary. There are no GPU frames to migrate. A buffer whose struct layout changed must be rebuilt by the CPU. This is my analysis.

### 5.5 Running GPU code on the CPU to debug it (2026-11-11)

**What it is.** The owner wants to run shaders on the CPU, so that they can be stepped through in a debugger, with the GPU's semantics. Written 2026-11-11; not decided.

**What a CPU run can match.**
- **chibi's GPU rules, exactly.** The fallback table in 5.1 and the types in 5.2 are chibi's own rules, compiled from the same IR on both sides. A CPU run can also report each fallback with its source line and then continue with the fallback value, so its results stay those of the GPU.
- **Floats, as one permitted result.** A strict CPU result lies within every bound in section 4, but a given GPU may round differently, fuse, flush subnormals or assume there is no NaN. A CPU run can report where a NaN, an infinity or a subnormal first appears, which is where GPUs start to differ.
- **Invocations that run together, with work.** Barriers, workgroup memory, subgroup operations and derivatives need several invocations at the same point. Slang's CPU target runs "all of the threads within the group" in one call, but "Barriers are not supported (making these work would require an ABI change)" and "groupshared is not yet supported" ([cpu-target.md](https://github.com/shader-slang/slang/blob/master/docs/cpu-target.md)). pocl, which runs OpenCL on CPUs, must "execute each region between the barriers for each work-item before proceedings to the next region", and keeps values that cross a barrier in "context arrays" ([kernel compiler](https://portablecl.org/docs/html/kernel_compiler.html)). chibi's stackless coroutines (research README, point 5) do the same job: each invocation is a frame, a barrier is a suspension point, and a debugger steps one invocation through ordinary code. Subgroup operations and derivatives work the same way, within a subgroup or a 2×2 quad. This is my analysis.
- **Races.** A CPU run in one fixed order hides a race that a GPU shows now and then. CUDA's racecheck "can report shared memory data access hazards that can cause data races" ([Compute Sanitizer](https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html)). A CPU run can record the last writer of each workgroup-memory location since the last barrier.
- **Textures and the draw pipeline** (rasterization, sampling, blending) are the largest part. SwiftShader is "a high-performance CPU-based implementation of the Vulkan 1.3 graphics API" and is conformant ([README](https://github.com/google/swiftshader)). SHADERed steps through "vertex, geometry, tessellation control, pixel and compute shaders" line by line, and lists SPIRV-VM, "a virtual machine for executing SPIR-V shaders", among its dependencies ([SHADERed](https://github.com/dfranx/SHADERed), [SPIRV-VM](https://github.com/dfranx/SPIRV-VM)).

**Recommendation.** GPU code also compiles for the CPU through the development backend, with 5.1's fallbacks and reports instead of traps. Compute shaders and pure functions come first, with barriers lowered like coroutines. Draw shaders that sample textures run on SwiftShader until chibi needs its own sampler. RenderDoc and Xcode remain the tools for a pixel on the real GPU.

### 5.6 Types narrower than 32 bits: `bool`, small integers, enums, optionals and error unions (2026-11-18)

**What it is.** Decisions made after this note put types narrower than 32 bits into ordinary code. `bool` is one byte (rank 2, 2026-11-11). An enum without `: T` takes the smallest of `u8`, `u16`, `u32` and `u64` that holds its values (2026-11-16), so most enums are `u8`. `?T` is `{ T value; bool present }`, and `E!T` is `{ T value; u16 code }` (2026-11-15). GPU targets support these types less well than `i32`:
- **WGSL** has no 8- or 16-bit integers: "The integer scalar types are AbstractInt, i32, and u32" ([L2981](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L2981-L2982)). Its `bool` "is not host-shareable" ([L12104](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L12104-L12106)), and uniform and storage buffers must hold host-shareable types ([L3709](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L3709-L3711)). Each user-defined stage input and output must "be of numeric scalar type or numeric vector type" ([L11221](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L11221-L11224)), which leaves out `bool`, and integer ones must use `flat` interpolation ([L11393](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/wgsl/index.bs#L11393-L11394)).
- **Vulkan** makes each use a separate optional feature. `shaderInt8` and `shaderInt16` allow the types in shader code ([features.adoc](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/chapters/features.adoc#L697-L700), [L1608](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/chapters/features.adoc#L1608-L1612)). `storageBuffer8BitAccess` and `storageBuffer16BitAccess` allow them as members of storage buffers, and two more features cover uniform buffers ([L1403](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/chapters/features.adoc#L1403-L1430), [L1491](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/chapters/features.adoc#L1491-L1520)). The 2024 and 2026 roadmap profiles require `shaderInt8`, `shaderInt16` and both storage-buffer features; the 2022 profile requires none of them ([VP_KHR_roadmap.json](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/xml/profiles/VP_KHR_roadmap.json#L340-L372)). A `bool` stage input or output must be a built-in ([VUID-StandaloneSpirv-Input-07290](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/appendices/spirvenv.adoc#L832-L833)).
- **SDL:** "When no such structure is provided, SDL will use Vulkan API version 1.0 and a minimal set of features" ([SDL_gpu.h](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/include/SDL3/SDL_gpu.h#L2436-L2440)). The structure is `SDL_GPUVulkanOptions`, through which an app can request each feature above ([SDL_gpu_vulkan.c](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/src/gpu/vulkan/SDL_gpu_vulkan.c#L11872-L11898)). It applies to the Vulkan backend only.
- **HLSL and Metal:** HLSL's 16-bit integers need Shader Model 6.2 (unverified). MSL's 8- and 16-bit types, and whether a `bool` may sit in a buffer, were not checked (unverified).

**Why it matters for chibi.** Without a rule, a `u8` counter, a three-member enum or an optional `f32` makes a GPU function fail to compile for WGSL and for SDL's default Vulkan device, and a struct with such a field can't be shared with the GPU at all. Dense component tables use exactly these types, so shared structs will hold them.

**Options.**
- **A. Compute in 32 bits, and check buffers and stage interfaces.** Values of 8- and 16-bit types are 32-bit values in GPU code. After each arithmetic operation the writer wraps the result to the declared width: an `and` for unsigned types, a sign extension for signed ones. So `250 +% 10` on `u8` values is 4 on both sides, and a plain `+` gives the same 4 as 5.1's GPU fallback. A shift takes its count modulo the declared width. A buffer field narrower than 32 bits is read by loading the 32-bit word that holds it and extracting its bits, and the writer declares the buffer as those words. Writing such a field through a `@read_write` binding is an error unless the target has 8- or 16-bit storage, because a read, modify and write of the whole word would race with invocations that write its other bytes. A `bool` field is one byte in C layout and is read as a `u8`, nonzero for `true`. Stage inputs and outputs can't be `bool` or narrower than 32 bits; the check suggests `u32` or `i32` with `@flat`.
- **B. Forbid types narrower than 32 bits in GPU code** unless the target has them, as rank 2 does for 64-bit types. This is simple, but it rejects enums, `?T` and `E!T` in every portable shader, because their tags are narrow.
- **C. Give GPU instances 32-bit members.** A `bool` or an enum would take 4 bytes in GPU code. A struct shared between the CPU and the GPU would then have two layouts, which C layout and the layout check exist to prevent (2.1).

**Recommendation.** Option A. On a target with `shaderInt8` or `shaderInt16` and the matching storage features, the SPIR-V writer uses the native types and drops the wrapping and the write restriction. The costs are my analysis, not measured: one `and` or sign extension per arithmetic result of a narrow type, and one load plus a bit extraction per narrow field read. Comparisons and widening conversions cost nothing extra, because every narrow value stays wrapped. The `present` flag of `?T` and the code of `E!T` are mostly locals, so they cost only the wrapping. Where a shader writes an enum field of a buffer, the layout check suggests `: u32` on the enum. That also fixes the enum's size, which the enum decision row says the 257th member would otherwise change.

## 6. Output format (question 5)

**What each consumer takes.**
- **SDL3** has six format flags: `PRIVATE` ("Shaders for NDA'd platforms"), `SPIRV` ("SPIR-V shaders for Vulkan"), `DXBC` ("DXBC SM5_1 shaders for D3D12"), `DXIL` ("DXIL SM6_0 shaders for D3D12"), `MSL` and `METALLIB` ([SDL_gpu.h](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/include/SDL3/SDL_gpu.h#L1043-L1051)). "Each backend (Vulkan, Metal, D3D12) requires a different shader format" ([L119](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/include/SDL3/SDL_gpu.h#L119-L125)).
  - The Vulkan backend accepts only SPIR-V. D3D12 accepts DXBC, plus DXIL on Shader Model 6.0 devices. Metal compiles MSL source at run time or loads a metallib ([SDL_gpu_d3d12.c](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/src/gpu/d3d12/SDL_gpu_d3d12.c#L10059-L10071), [SDL_gpu_metal.m](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/src/gpu/metal/SDL_gpu_metal.m#L865-L885)).
  - SDL's Vulkan backend requests Vulkan 1.0 unless the app passes Vulkan options ([SDL_gpu_vulkan.c](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/src/gpu/vulkan/SDL_gpu_vulkan.c#L11953-L11955)), and Vulkan 1.0 accepts only SPIR-V 1.0 ([spirvenv.adoc](https://github.com/KhronosGroup/Vulkan-Docs/blob/fa30d4e312e843193601be7cb926c7e25d9ec885/appendices/spirvenv.adoc#L20-L48)).
  - `main` has no WebGPU backend. Pull request [#16020](https://github.com/libsdl-org/SDL/pull/16020), open since 2026-07-18, adds one with a WGSL format.
  - SDL's header now calls SDL_shadercross "a convenient command-line wrapper" and says "offline ahead-of-time compilation is preferred" ([L131](https://github.com/libsdl-org/SDL/blob/8b6ae5d72496c04723fd3016ea7a5c138a156422/include/SDL3/SDL_gpu.h#L131-L138)).
- **SDL_shadercross** "takes SPIRV or HLSL as the source and outputs DXBC, DXIL, SPIRV, MSL, or HLSL" ([README](https://github.com/libsdl-org/SDL_shadercross/blob/1ff05bec573988a98ef9e0260b4da44f512b8367/README.txt#L3-L16)). It uses SPIRV-Cross for every SPIR-V translation and DXC for DXIL. For DXBC it needs d3dcompiler_47 on Windows and vkd3d-utils elsewhere. It has no tagged release yet.
- **Cute Framework** replaced glslang, SDL_shadercross and DXC with its own compiler in July 2026. `cute_spirv.h` compiles a GLSL 450 subset "directly to SPIR-V 1.3 for Vulkan 1.1+, with zero external dependencies" ([cute_spirv.h](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_spirv.h#L24-L27)).
  - Between July and October 2026 it gained GLSL ES 3.00, HLSL SM 5.1, MSL and WGSL printers that work "straight from the checked AST -- no SPIR-V decompilation" ([history](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_spirv.h#L69-L136)).
  - On D3D12, CF compiles the HLSL to DXBC at run time with the system's d3dcompiler_47 ([cute_graphics_sdlgpu.cpp](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/src/cute_graphics_sdlgpu.cpp#L752-L797)).
  - `CF_ShaderBytecode` carries SPIR-V plus GLSL ES, HLSL, MSL and WGSL text ([cute_shader_bytecode.h](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/include/cute_shader_bytecode.h#L650-L674)).
  - CF's WebGPU backend takes WGSL. The owner's fork changes no shader code.
- **Converters.**
  - SPIRV-Cross turns SPIR-V into GLSL, MSL and HLSL, with no WGSL output ([README](https://github.com/KhronosGroup/SPIRV-Cross/blob/57e4efcc84112abcd426dc5ec248b5339bee04f2/README.md#L8-L22)).
  - Naga reads SPIR-V, WGSL and GLSL and writes SPIR-V, WGSL, MSL, HLSL and GLSL ([README](https://github.com/gfx-rs/wgpu/blob/6838055e0a1912f2cd1a78466faf0c830c909817/naga/README.md#L14-L30)).
  - Tint reads SPIR-V and WGSL and writes SPIR-V, WGSL, HLSL, GLSL and MSL. It may reject SPIR-V that "uses features unavailable in WGSL" ([spirv-reader-overview.md](https://github.com/google/dawn/blob/96120a39412d9cc55a0ab4b9a823ed3c0a3a8a48/docs/tint/spirv-reader-overview.md#L10-L21)).
  - DXC compiles HLSL to DXIL and SPIR-V, at Shader Model 6.0 or later, so it produces no DXBC ([README](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/README.md#L3-L25)).
- **The future of DirectX.** Microsoft wrote in 2024: "Once Shader Model 7 is released, DirectX 12 will accept shaders compiled to SPIR-V", and "This transition will take several years" ([DirectX blog](https://devblogs.microsoft.com/directx/directx-adopting-spir-v/)). No later date was found.
- **The browser.** WebGPU's shader module takes "The WGSL source code for the shader module" ([WebGPU spec](https://github.com/gpuweb/gpuweb/blob/6bf1c35fffaae37cf9db16c88e7d5d03382b5c52/spec/index.bs#L7194-L7205)).

**Options.**

| Option | Runs today | Dependencies | Errors point at | Work for chibi |
|---|---|---|---|---|
| A. SPIR-V only, converted for other APIs | Vulkan directly; Metal and D3D12 through SDL_shadercross; WebGPU through Tint or Naga | SPIRV-Cross, DXC or d3dcompiler, Tint or Naga, all C++ or Rust | chibi, or converter output | one binary backend |
| B. Text per target: MSL, HLSL, WGSL | Metal; WebGPU; D3D12 through FXC or DXC | the platform compilers | generated text | three printers; no Vulkan path |
| C. SPIR-V plus text printers (CF's path) | everywhere, with no converters | d3dcompiler_47 on Windows for DXBC | chibi | one binary backend plus printers |

**Why it matters for chibi.**
- SPIR-V is binary, with types declared up front. That is the shape of chibi's own development backend: one pass per function from the typed IR, no text, no parser on the other side (my analysis). SPIR-V's values are SSA, but chibi's IR keeps locals in numbered slots (decided 2026-11-12). The writer gives each slot a `Function` variable and leaves promotion to the driver, as CF does ([shader-backends.md](shader-backends.md) section 6, item 4).
- SPIR-V is also the hub format. SDL's Vulkan path, every converter above, and future DirectX all read it.
- Text formats are easier to debug and let each platform's compiler optimize. MSL is needed on macOS, where SDL's GPU API uses Metal (whether its Vulkan backend runs over MoltenVK is unverified).
- DXIL is LLVM bitcode, and "The LLVM bitcode version is currently fixed at LLVM 3.7 for all DXIL versions" ([DXIL.rst](https://github.com/microsoft/DirectXShaderCompiler/blob/823a3dadf981b70cec3285faa979dcbd4be94aa4/docs/DXIL.rst#L81)). Emitting it directly would tie chibi to that format for years. [shader-backends.md](shader-backends.md) section 6 found that Mesa and gogpu/naga write DXIL without LLVM, so a DXIL writer is now listed as later, not rejected (7.2).
- The no-C-compiler goal favors option C. Converters are large C++ or Rust tools, while CF fits its front end plus five outputs in 13,667 lines of C.

**Recommendation.** Option C, in stages:
1. **SPIR-V 1.0** with Logical addressing, from the typed IR after generics and compile-time code have run for the target, with no optimizer and no legalization pass ([shader-backends.md](shader-backends.md) 1.1, items 1 and 3). For now, SDL_shadercross makes MSL, DXBC and DXIL from it at build time. In SPIR-V 1.0, storage buffers use the `Uniform` storage class with the `BufferBlock` decoration, because the `StorageBuffer` class does not exist yet. Use SPIR-V 1.3 only when subgroup operations are used and the app requests Vulkan 1.1. Names, and in debug builds `NonSemantic.Shader.DebugInfo.100`, come with the first module, and tests run SPIRV-Tools' validator on every module ([shader-backends.md](shader-backends.md) 1.1, items 4 and 5).
2. **An MSL printer,** because the owner develops on macOS, where Metal is SDL's native backend.
3. **A WGSL printer** when chibi targets the web, through CF's WebGPU backend or SDL's once it merges. The uniformity analysis (2.6) must come first.
4. **HLSL 5.1 for DXBC** only if Windows builds need D3D12 rather than Vulkan. When Shader Model 7 ships, SPIR-V may cover D3D12 too.
5. **For CF's draw-shader hook,** which wraps a user function `vec4 shader(vec4 color, ShaderParams params)` in CF's own stub ([drawing.md](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/docs/topics/drawing.md#L312-L336)), chibi could later print CF's GLSL subset and let cute_spirv finish the job.

Shipped games get compiled shader bytes, never a run-time cross-compiler.

**Decided 2026-11-10:** chibi's own writers, all reading one typed IR that keeps structured control flow, with join points marked, typed values and field indices; SPIR-V first, then MSL and WGSL ([../decisions.md](../decisions.md)). The owner decided after [shader-backends.md](shader-backends.md) surveyed 16 compilers and pipelines and found that section 6 holds, with the corrections folded in above.

## 7. Lists

### 7.1 Adopt now, because it affects decisions being made now

| Item | Decision it affects | Section | Status |
|---|---|---|---|
| GPU fallbacks for failed checks, with first-failure reports in debug GPU builds | rank 1 (its row says GPU results are not yet decided), rank 9, postfix `!!`, narrowing and enum conversions, `switch` | 5.1 | open |
| No `int` in GPU code; untyped integers and `.len` are `i32` there; 64-bit types need the target | rank 2 | 5.2 | decided 2026-11-11 |
| The float contract covers CPU backends; GPUs give error bounds | rank 3 | 5.3 | open |
| A layout check for structs that reach GPU bindings | C layout (the decision row said GPU buffers need no annotation) | 2.1 | decided 2026-11-10 |
| A matrix's first index selects the column; `==` on arrays returns one `bool`; ordering comparisons are functions | rank 14, before vector code exists | 2.1 | decided 2026-11-10, except ordering comparisons |
| GPU code chosen by reachability from `@vertex`, `@fragment` and `@compute`; `$if target.gpu`; one instance per target | sheets 6 and 7, the declaration pass | 3 | open |
| Structured control flow in the typed IR; an acyclic GPU call graph; function values known at compile time | the IR design, rank 13 | 4 | IR decided 2026-11-10 and 2026-11-12; GPU rules open |
| Binding kinds in entry-point signatures (`@uniform`, `@read_write`) | no const correctness (decided); signatures independent of bodies; the attribute design | 2.3 | open |
| `*T` only, never stored, in GPU code | rank 5 (pointer kinds decided 2026-11-15) | 4 | open |
| No textures or samplers in structs or locals, so the SPIR-V writer needs no legalization | the GPU writers | 4 | open |
| No context reads in GPU code | rank 7, option B (confirmed) | 5.4 | open |
| Types narrower than 32 bits computed in 32 bits; narrow buffer fields read from their word; narrow writes and stage interfaces checked | `bool`, enums, `?T` and `E!T` layouts (decided 2026-11-11 to 2026-11-16), the layout check | 5.6 | open |

### 7.2 Adopt later: additive

| Feature | Who has it | What would bring it in |
|---|---|---|
| `gpu` package: textures, samplers, sampling, derivatives, `discard`, barriers, subgroup operations | every shading language | the first shader |
| One `atomic` package for CPU and GPU | WGSL, HLSL, C11 | the first compute shader or job system |
| `f16` | WGSL with `enable f16`, Vulkan `shaderFloat16`, Metal `half` | mesh and texture storage |
| `@flat`, `@linear`, `@invariant`, `@precise` | WGSL, GLSL, HLSL | integer varyings; watertight seams |
| Uniformity analysis | WGSL | the WGSL printer |
| MSL, WGSL and HLSL printers | cute_spirv, Slang, Naga, Tint | macOS without shadercross; the web; D3D12 |
| Swizzle writes | GLSL, HLSL; WGSL's `swizzle_assignment` extension | code that updates `v.xy` |
| Push constants or immediates | Vulkan, WGSL `immediate`, Metal `setBytes` | a backend other than SDL |
| Specialization constants | SPIR-V `SpecId`, WGSL `override`, Metal function constants | values chosen at pipeline creation |
| Buffer device addresses | Vulkan 1.3, Slang `Ptr`, Zig `physical_storage_buffer` | GPU-side data structures; needs SDL support |
| `VK_KHR_shader_abort` in Vulkan debug builds | Vulkan 2026 | drivers that ship it |
| Running compute functions on the CPU for tests | Slang's CPU target, ISPC | testing kernels as pure transforms; the owner wants it for debugging (5.5) |
| CF draw shaders from chibi | CF's `cf_make_draw_shader` | games that use CF's draw API |
| Native 8- and 16-bit integers in GPU code | Vulkan `shaderInt8` and `shaderInt16`, required by the 2024 roadmap profile | a target that enables them through `SDL_GPUVulkanOptions` (5.6) |
| A DXIL writer | Mesa, gogpu/naga | D3D12 without DXC ([shader-backends.md](shader-backends.md) section 6, item 8) |

### 7.3 Rejected

| Feature | Who has it | Why not for chibi |
|---|---|---|
| `__host__ __device__` marks on shared functions | CUDA | Reachability decides, as in Circle and Slang; marks put an error far from its cause (3) |
| A separate shading language | GLSL, HLSL or WGSL beside C, Odin or Jai | Two languages with no shared types or functions; the layout check and reflection need one compiler that sees both sides |
| A compile-time library that prints chibi functions as shader text | Jai's community transpiler (community source) | It needs code as data, which sheet 7 left out, and its errors would point at generated text |
| Minimum-precision types | HLSL `min16float`, GLSL ES `mediump` | Results differ per device (2.2) |
| `int` silently 32-bit on the GPU | — | Layouts and overflow points would differ between the CPU and the GPU (5.2) |
| Emulated 64-bit integers by default | — | Costs every integer operation in shader code (5.2) |
| Bit-identical GPU floats through `NoContraction` everywhere | — | Slower, impossible in WGSL, and transcendental functions still differ (5.3) |
| Address spaces in pointer types | Zig, Mojo, Slang, WGSL | A version of every helper per space; inference does the job (2.4) |
| Dynamic dispatch through interface values on the GPU | Slang | Tagged unions and `switch` do the job (5.4) |
| Recursion or coroutines on the GPU through emulated stacks | — | No target supports them portably (4) |
| Shipping a run-time cross-compiler | SDL_shadercross as a library | SDL prefers offline compilation, and DXC is large (6) |
| Preprocessor permutations | GLSL, HLSL | `$` parameters and `$if` do the job (5.4) |
| Silently padding shared structs | — | It would break C layout; the check names the padding instead (2.1) |
| 32-bit `bool` and enum members in GPU instances | — | One struct would have two layouts (5.6) |

## 8. Not verified

**Metal.** The MSL specification is a 14.5 MB PDF that the fetch tool refused, and no PDF reader was available. Only Apple's API documentation was read: math modes, function constants, `setBytes`, visible function tables and call-stack depth. Every MSL language rule in this note is unverified: address space names, `half`, 64-bit integers, 8- and 16-bit integers, `bool` in buffers, division by zero, out-of-bounds behavior, subnormals, the size of `float3`, direct recursion and pointers.

**HLSL and D3D12.** Not read for this note:
- HLSL's lack of pointers, `==` on vectors, integer wrapping, shifts, and the copy-in, copy-out semantics of `inout`;
- Shader Model 6.2's 16-bit types, root constants, `double` support, and HLSL's lack of specialization constants;
- adopt-or-settle's claim about `mul`.

**Other targets.**
- CUDA's default float contraction, its overflow, division and shift rules, and whether device code supports coroutines.
- HLSL's and Metal's lack of strings, heaps and traps.
- Whether SDL's Vulkan backend runs on macOS over MoltenVK. (Whether SDL enables `robustBufferAccess` was checked on 2026-11-18: not by default, 5.1.)
- The SPIR-V specification's own rule on storing `OpTypeBool`: its page refused the fetch tool on 2026-11-18, so 5.6 rests on WGSL's rule and Vulkan's interface rule.

**Read by the research agents, not by me.** Facts about the other languages, SDL's backend sources, SDL_shadercross, CF and the converters rest on their reading of the pinned links, except the items I re-read (listed under Sources). They could not verify these:
- whether Slang supports derivatives on its CPU target, and its rules on function pointers;
- a SPIR-V pass rate for Zig's backend;
- whether Rust-GPU still reserves `descriptor_set = 0`;
- Mojo's AMD output format and its full list of kernel restrictions;
- whether Circle's current build still emits DXIL and PTX;
- a macro that detects ISPC's Xe targets;
- Odin discussions outside the forum and GitHub;
- a primary source by Jonathan Blow.

Also unverified:
- whether SDL's pull request #16020 will merge;
- any Microsoft date for Shader Model 7;
- whether drivers accept SPIR-V 1.3 on SDL's default Vulkan 1.0 instance.

**My analysis, not measured.**
- The cost of a divisor `select` and of a bounds check per dynamic index (5.1).
- That address-space instances are bounded by call sites (2.4).
- That SPIR-V suits one-pass emission from the typed IR (6).
- That GPU reload needs only pipeline recreation (5.4).
- That built-in indices fit in `i32` (5.2).
- That labeled `break`, `break name value` and `continue name value` lower to flags and locals for WGSL (4).
- The cost of emulated 64-bit integers (5.2).
- That 16-bit floats run faster (2.2).
- The cost of wrapping narrow values and of extracting narrow buffer fields, and that comparisons on wrapped values need nothing extra (5.6).
