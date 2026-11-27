# Vendor libraries: the curated set chibi ships

Research note, 2026-11-27. Question: which C libraries should chibi ship as its curated set, now that the owner has dropped the plan to ship Cute Framework (CF) whole? The set follows Odin's `vendor:` collection: libraries prebuilt for Windows, linked from the system elsewhere, with bindings written by chibi's C header import (the dependencies row of 2026-11-26 in [decisions.md](../decisions.md)). This note compares Odin's `vendor:` with CF's dependencies, and asks which libraries Odin's core library and language make unnecessary, and which chibi's own features do.

**Scope.** Section 2 lists every package in Odin's `vendor/` at one commit: what it provides, its upstream version and license, how Odin ships it, and the size of the C library. Section 3 lists the Odin `core:` packages and language features that replace a C library a game would otherwise vendor. Section 4 gives the Odin equivalent of each CF dependency and of CF's own headers. Section 5 judges every candidate for chibi. Section 6 recommends a first set, the standard-library work that replaces the rest, and the license and maintenance flags. Box2D, Box3D, SDL2 and SDL3 are already in the set; this note records their versions, licenses and sizes and does not argue for them. CF's dependency list at `6b00978a` comes from [libraries.md](../libraries.md); I refetched CF only to count lines and read headers.

**Sources.** I read each project at the commits below, from shallow or sparse clones, and read the sizes of Odin's Git LFS binaries from their pointer files, without downloading them. "Code lines" are lines that hold code, without blank lines or comments, as scc counts them. scc isn't installed on this machine, so a counter of mine produced them, with C, C++, Objective-C, Rust and Odin comment rules. A count covers a library's own source and public headers, not its tests, examples or bundled dependencies, unless the row says otherwise. Single-header libraries that Odin or CF copy into their trees (miniaudio, kb_text_shape, Box3D's snapshot, the stb headers, Dear ImGui, minicoro, debugbreak, the cute headers) I counted from those copies. Maintenance dates are each repository's last commit and last release on 2026-11-27, read with `gh api`; the last-commit links point at those commits. Claims I could not read directly are in section 7. "My estimate" marks a size I guessed from a comparable library.

| Project | Commit, date | Project | Commit, date |
|---|---|---|---|
| Odin | `19039b3e`, 2026-11-27 | odin-lang.org docs | `f2751d70`, 2026-11-26 |
| Cute Framework | `6b00978a`, 2026-11-04 | SDL 3.4.20 | `f00dc919`, 2026-11-03 |
| SDL 3.4.0 (CF's) | `a962f40b`, 2025-12-31 | SDL 2.32.10 | `5d249570`, 2025-09-01 |
| SDL_image 3.4.8 | `0891fc30`, 2026-10-02 | SDL_mixer 3.2.4 | `72a81869`, 2026-06-03 |
| SDL_net 3.2.0 | `1a84a2a6`, 2026-05-31 | SDL_ttf 3.2.2 | `a1ce3670`, 2025-03-31 |
| SDL_image 2.8.12 | `12cb2e40`, 2026-05-01 | SDL_mixer 2.8.2 | `b208916a`, 2026-05-11 |
| SDL_net 2.4.0 | `904600c6`, 2026-05-15 | SDL_ttf 2.24.0 | `2a891473`, 2025-01-02 |
| FreeType, SDL_ttf's fork | `9973564c`, 2025-03-20 | HarfBuzz, SDL_ttf's fork | `564bf981`, 2025-03-20 |
| Box2D 3.2.0 | `178495ba`, 2026-10-18 | Box2D 3.1.1 (CF's) | `8c661469`, 2025-06-03 |
| Box3D 0.1.0 | `8441b4a0`, 2026-06-30 | stb | `6b5799ba`, 2026-11-19 |
| cgltf 1.15 | `360db1a9`, 2025-02-09 | cmark 0.31.2 | `eec0eeba`, 2026-02-14 |
| lz4 1.11.1 | `aa4647fa`, 2026-11-09 | curl 8.23.0 | `3163ec06`, 2026-10-14 |
| ENet 1.3.18 | `2662c0de`, 2024-04-14 | GLFW 3.5.1 | `d9d6f0f1`, 2026-07-31 |
| Lua 5.3.6 | `75ea9ccb`, 2020-08-18 | Lua 5.4.9, 5.5.1 | `312b9efa`, `7579fc9d`, 2026 |
| microui | `0850aba8`, 2024-08-13 | nanovg | `ce3bf745`, 2026-02-19 |
| fontstash | `b5ddc974`, 2019-04-22 | GGPO | `7ddadef8`, 2019-11-07 |
| OpenEXR 3.5.3 | `39502905`, 2026-10-30 | PortMidi 2.0.8 | `101dac94`, 2025-12-02 |
| raylib 6.2 | `02566453`, 2026-10-15 | raygui 5.0 | `020a61be`, 2026-07-21 |
| zlib 1.4 | `99623ef7`, 2026-11-04 | Vulkan-Headers 1.4.354 | `01393c3d`, 2026-06-12 |
| wgpu-native 29.0.1.1 | `6aed5095`, 2026-06-22 | wgpu 29.0.3 | `4cbe6232`, 2026-05-01 |
| PhysicsFS (CF's pin) | `9d18d36b`, 2025-07-31 | s2n-tls 1.6.3 | `1c984472`, 2025-12-08 |

DXC's size comes from [shader-backends.md](shader-backends.md) section 2.3, which read DXC at `823a3dad`.

## 1. Summary

### 1.1 Recommendations

1. **Ship seven libraries beside the four decided ones** (section 6.1): stb_truetype and kb_text_shape for fonts, stb_image for images, stb_vorbis for audio decoding, cgltf for 3D models, Dear ImGui with its dear_bindings C API for tool UI, and ENet for networking. Add Vulkan-Headers, as headers only, for code that goes below SDL3's GPU API. All are MIT, zlib, Apache-2.0 or public domain, and every one had a commit in 2026. Odin's Windows builds of the set, without Dear ImGui, come to about 11.4 MB (my arithmetic).
2. **Let SDL3 cover more than windows, input and the GPU.** SDL 3.4 loads PNG and BMP and saves PNG ([SDL_surface.h](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_surface.h#L32-L33)), loads WAV, mixes every audio stream bound to a device with per-stream gain and pitch ([SDL_audio.h](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_audio.h#L37)), generates GPU mipmaps, and lists directories and title storage. That takes SDL3_image, SDL3_mixer, miniaudio, stb_image_resize2, stb_image_write and PhysicsFS out of the first set.
3. **Put formats and protocols in chibi's standard library, not in the set.** Odin shows the sizes: inflate and deflate in 3,933 lines, PNG in 2,008, JSON in 4,408, LZ4 in 2,310, sockets in 6,270, and a TLS 1.3 client in 2,959 plus 2,093 for X.509. These replace zlib, lz4, cute_png, cute_jpg, PhysicsFS's archive layer, cute_tls and s2n-tls (section 6.2). The TLS client also answers the "TLS for chibi's own git client" item in libraries.md.
4. **Write no binding packages.** Odin keeps 370,246 lines of Odin bindings in `vendor/`. chibi's C import writes what C headers declare, which covers 100,542 of those lines. The other 269,704 are Apple's Objective-C frameworks (112,574) and Android's Java API through JNI (157,130), which no C import reaches. SDL3 hides both from a game.
5. **Generate OpenGL and Vulkan loaders with compile-time code** from the imported headers, instead of shipping glad's output, as CF does, or committing generated bindings, as Odin does with 7,438 lines for OpenGL and 23,966 for Vulkan.
6. **Leave out every library that duplicates SDL3 or a chibi feature**: GLFW, raylib, miniaudio, wgpu-native, DXC, FXC, cute_spirv, glad, minicoro, cute_sync, ckit.h, debugbreak and stb_sprintf (section 5).
7. **Build SDL's satellite libraries without their non-permissive parts if one is ever added.** Odin's prebuilt SDL2_mixer and SDL3_mixer load `libgme.dll`, which is LGPL-2.1. SDL_mixer's optional mpg123 is LGPL-2.1 too, and its MIDI player, timidity, is under the Artistic License. SDL_mixer builds without all three.
8. **Pin Box3D to a commit.** Box3D's only release is 0.1.0 (2026-06-30), Odin ships a master snapshot of 2026-08-04, and master had commits on 2026-11-25. CF pins Box2D 3.1.1, one minor version behind the 3.2.0 that Odin ships.

### 1.2 Terms

| Term | Meaning |
|---|---|
| Vendor collection | A set of third-party libraries that ships with a compiler, as Odin's `vendor:` does. |
| Prebuilt | Compiled ahead of time: a `.lib` or `.dll` on Windows, an `.a`, `.so` or `.dylib` elsewhere. |
| Static library, import library | A `.lib` that holds the code; a `.lib` that only connects a program to a `.dll`. |
| System library | A library that the operating system or its package manager installs, linked by name, as Odin's `system:SDL3`. |
| Single-header library | A C library in one header. One C file defines a macro before including it, and gets the implementation. |
| Git LFS | Git Large File Storage. The repository holds a pointer file of about 130 bytes, and the file itself lives on a separate server. |
| Binding | Declarations that let one language call functions written in another. |
| Satellite library | One of SDL's companion libraries: SDL_image, SDL_mixer, SDL_net, SDL_ttf. |
| Shaping | Turning characters into positioned glyphs by applying a font's rules for ligatures, marks and joining. |

## 2. Odin's `vendor:` collection (task 1)

### 2.1 How Odin ships a vendor package

- The packages "and their associated binary files are curated and maintained by the Odin team" ([vendor/README.md](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/README.md#L7)).
- **Windows.** Prebuilt `.lib` and `.dll` files sit in the repository next to the bindings. [.gitattributes](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/.gitattributes#L7-L56) puts the large ones in Git LFS. Windows binaries total 149.1 MB: 131.0 MB in LFS and 18.1 MB in plain git. Another 40.0 MB of macOS, Linux and WASM archives sit beside them (my arithmetic from the pointer files). wgpu's static library alone is 52.3 MB, and DXC's two DLLs are 23.1 MB.
- **Elsewhere, Odin uses three methods.**
  1. A system library: SDL2 and SDL3 and their satellites, curl, zlib, lz4, cmark, ENet, GLFW on Linux, and DXC ([sdl3__foreign.odin](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/sdl3/sdl3__foreign.odin#L5-L11)).
  2. Prebuilt archives in the repository for macOS, and for some libraries Linux and WASM too: raylib, Box3D, Box2D (macOS), miniaudio, kb_text_shape, GLFW (macOS) and stb (macOS) ([box3d.odin](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/box3d/box3d.odin#L13-L32)).
  3. A build script that the user runs on source in the repository: stb, cgltf, miniaudio, kb_text_shape and Box3D. Box2D's script downloads the 3.2.0 tarball ([build_box2d.sh](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/box2d/build_box2d.sh#L4-L10)). When the archive is missing, the binding stops compilation with `#panic` and names the script ([stb_image.odin](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/stb/image/stb_image.odin#L6-L24)).
- **Platform bindings** ship no binary: `darwin`, `directx` (apart from the shader compilers), `egl`, `windows`, `x11`, `android` and `wasm`.
- **Ports.** `fontstash`, `microui`, `nanovg` and `stb/easy_font` are C libraries rewritten in Odin.

### 2.2 Packages

Versions are those in the files at `19039b3e`. Where the README says otherwise, the README's number follows in brackets (section 2.3). "LFS" marks binaries stored in Git LFS. Sizes are code lines of the upstream C library.

| Package | Provides | Version | License | Windows binary in the repository | Elsewhere | C library size |
|---|---|---|---|---|---|---|
| [`android/*`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/android), 12 packages | Android NDK (AAudio, camera, media, ICU, OpenSL ES, binder) and the Java API through JNI | NDK | Odin's | none | system | OS library; 165,029 lines of Odin, 157,130 of them in `android/java`. Not in the README. |
| [`box2d`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/box2d) | 2D physics | [3.2.0](https://github.com/erincatto/box2d/tree/178495ba35695aefe9c321e569d2277f06db3b90) | MIT | static `.lib`, 1.8 MB, LFS | macOS archive; script elsewhere | 32,921 |
| [`box3d`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/box3d) | 3D physics | master snapshot of [2026-08-04](https://github.com/odin-lang/Odin/commit/11f481b4c38394d52c250472dc8ff5631c6dd795) | MIT | static `.lib` 3.1 MB, `.dll` 1.1 MB, LFS | Linux and macOS archives, LFS; source in tree | 47,297 in Odin's copy; 46,067 at [0.1.0](https://github.com/erincatto/box3d/tree/8441b4a06d6d09dcfb0b0f704df4d847d1437b92) |
| [`cgltf`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/cgltf) | glTF 2.0 reading and writing | [1.15](https://github.com/jkuhlmann/cgltf/tree/360db1a95480fe102ae9c69b27c5d101167ff5ba) | MIT | static `.lib`, 0.4 MB | script on bundled source | 7,313 |
| [`commonmark`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/commonmark) | Markdown parsing and rendering | cmark [0.31.2](https://github.com/commonmark/cmark/tree/eec0eeba6d31189fd828314576494566d539b1e3) | BSD-2-Clause | static `.lib`, 0.4 MB | system | 15,910 |
| [`compress/lz4`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/compress/lz4) | LZ4 compression | [1.11.1](https://github.com/lz4/lz4/tree/aa4647fad575d01a874aaaadcdbe8e6a40e072f5) [1.10.0] | BSD-2-Clause | static `.lib`, 0.4 MB | system | 8,379 |
| [`curl`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/curl) | HTTP(S) and other transfers | [8.23.0](https://github.com/curl/curl/tree/3163ec065643ba20709f190111f42c3e1fe95273) | curl, MIT-style | `libcurl.lib` 3.2 MB, plus zstd, brotli, nghttp2 and zlib: 5.5 MB in all | system | 131,983 for libcurl alone |
| [`darwin/*`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/darwin), 19 frameworks | AppKit, Metal, MetalKit, QuartzCore, AVFoundation, GameController and others | macOS and iOS 26.2 SDKs | Odin's | none | system | OS library; 112,574 lines of Odin for Objective-C APIs |
| [`directx/d3d11`, `d3d12`, `dxgi`, `d3d_common`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/directx) | Direct3D 11 and 12, DXGI | Windows API | Odin's | none | Windows only | OS library |
| [`directx/d3d_compiler`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/directx/d3d_compiler) | FXC, the old HLSL compiler | `d3dcompiler_47` | Microsoft's, redistributable | `d3dcompiler_47.dll`, 4.2 MB | Windows only | closed source |
| [`directx/dxc`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/directx/dxc) | DXC: HLSL to DXIL and SPIR-V | 1.9.2609 | NCSA | `dxcompiler.dll` 21.2 MB, `dxil.dll` 1.9 MB, LFS | system | 102,700 plus an LLVM and Clang fork (shader-backends.md) |
| [`egl`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/egl) | EGL 1.5 | — | Odin's | none | system | OS library |
| [`ENet`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/ENet) | Reliable UDP | [1.3.18](https://github.com/lsalzman/enet/tree/2662c0de09e36f2a2030ccc2c528a3e4c9e8138a) | MIT | `enet64.lib`, 0.1 MB | system | 4,862 |
| [`fontstash`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/fontstash) | Font atlas | Odin port | zlib | none | — | port of [1,367 lines](https://github.com/memononen/fontstash/tree/b5ddc9741061343740d85d636d782ed3e07cf7be); 914 lines of Odin |
| [`ggpo`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/ggpo) | Rollback netcode | [`7ddadef`](https://github.com/pond3r/ggpo/tree/7ddadef8546a7d99ff0b3530c6056bc8ee4b9c0a), 2019 | MIT | `GGPO.lib`, 3.3 MB | none: [Windows only](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/ggpo/ggpo.odin#L17) | 3,557 C++ |
| [`glfw`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/glfw) | Windows, input, OpenGL and Vulkan contexts | [3.5.1](https://github.com/glfw/glfw/tree/d9d6f0f1f967807ffade6598ea9a631ebaf37a56) | zlib | static `.lib` 0.7 MB, `.dll` 0.2 MB | macOS archive; Linux system | 28,837 |
| [`kb_text_shape`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/kb_text_shape) | Unicode segmentation and OpenType shaping | 2.32 [2.31] | zlib | static `.lib`, 0.7 MB | macOS archive, LFS; Linux script | 26,771 |
| [`libc`](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/libc/libc.odin#L3) | Removed: its only line is a `#panic` that points to `libc-shim` | — | — | — | — | — |
| [`libc-shim`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/libc-shim) | Minimal libc for compiling C libraries to WASM | — | Odin's | none | — | 265 lines of headers, 1,075 of Odin |
| [`lua/5.1` to `lua/5.5`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/lua) | Lua | 5.1.5, 5.2.4, 5.3.6, 5.4.9, 5.5.1 | MIT | one DLL per version, 0.16 to 0.24 MB, LFS | Linux `.a` and `.so`, LFS; else system | [17,593](https://github.com/lua/lua/tree/75ea9ccbea7c4886f30da147fb67b693b2624c26) (5.3.6), [21,231](https://github.com/lua/lua/tree/312b9efaa1061c2c4cad08554dbc1351c3270eef) (5.4.9), [22,731](https://github.com/lua/lua/tree/7579fc9d7ed90240487251dfb69168f8e64e9294) (5.5.1) |
| [`microui`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/microui) | Small immediate-mode UI | Odin port | MIT | none | — | port of [1,121 lines](https://github.com/rxi/microui/tree/0850aba860959c3e75fb3e97120ca92957f9d057); 2,749 lines of Odin |
| [`miniaudio`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/miniaudio) | Audio devices, decoding, mixing, effects | 0.11.26 | public domain or MIT-0 | static `.lib`, 2.9 MB, LFS | macOS archive, LFS; Linux script | 72,954 |
| [`nanovg`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/nanovg) | Antialiased 2D vector drawing on OpenGL | Odin port | zlib | none | — | port of [4,167 lines](https://github.com/memononen/nanovg/tree/ce3bf745eb2d2dbc14a50bf2446783f691ac4353); 3,712 lines of Odin |
| [`OpenEXRCore`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/OpenEXRCore) | EXR images | [3.5.3](https://github.com/AcademySoftwareFoundation/openexr/tree/3950290530c4fd4776667d0dc4d65a9d003eb936) | BSD-3-Clause | static `.lib` with OpenJPH, zstd and libdeflate, 4.5 MB, LFS | macOS archive; else system | 30,195 for OpenEXRCore |
| [`OpenGL`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/OpenGL) | OpenGL bindings and loader, generated with glad | — | MIT | none | system GL | 7,438 lines of Odin |
| [`portmidi`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/portmidi) | MIDI input and output | [2.0.8](https://github.com/PortMidi/portmidi/tree/101dac9455e2718512c94e24cbcae6a6f34b908b) | MIT-style | static `.lib`, 0.1 MB, LFS | macOS archive; else system | 4,267 |
| [`raylib`, `raylib/rlgl`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/raylib), raygui | Game library, its GL layer, and an immediate-mode UI | [6.2](https://github.com/raysan5/raylib/tree/02566453880e06bec97f5b8160fa6fe1e98397ca), raygui [5.0](https://github.com/raysan5/raygui/tree/020a61bebcbe288b4414de3416e219ef40af847a) | zlib | static `.lib` 4.9 MB, `.dll` 1.7 MB, raygui 0.3 MB, LFS | Linux, macOS and WASM archives | 40,356 of its own plus 200,308 bundled in `src/external`; raygui 4,170 |
| [`sdl2`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/sdl2) | Windows, input, audio, 2D rendering | [2.32.10](https://github.com/libsdl-org/SDL/tree/5d249570393f7a37e037abf22cd6012a4cc56a71) | zlib | `SDL2.dll` 1.5 MB, LFS | system | 318,157 |
| `sdl2/image` | Image loading | [2.8.12](https://github.com/libsdl-org/SDL_image/tree/12cb2e40330d256d9b1329647be8f366546d715c) | zlib, plus the codec DLLs' licenses | 0.3 MB, plus 3.4 MB of codec DLLs | system | 22,689 |
| `sdl2/mixer` | Mixing and music | [2.8.2](https://github.com/libsdl-org/SDL_mixer/tree/b208916aed9250fe434360e6c6a95f0697bb7b01) | zlib; loads LGPL-2.1 `libgme.dll` | 0.4 MB, plus 1.4 MB of codec DLLs | system | 34,309 |
| `sdl2/net` | Sockets | [2.4.0](https://github.com/libsdl-org/SDL_net/tree/904600c6133e0435d627ec1878bfdfeac414a899) | zlib | 0.1 MB | system | 1,152 |
| `sdl2/ttf` | TrueType text | [2.24.0](https://github.com/libsdl-org/SDL_ttf/tree/2a891473eaf05ba1707a4b7913e6c4db7de7458a) | zlib, plus FreeType | 1.7 MB | system | 4,260 without FreeType |
| [`sdl3`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/sdl3) | Windows, input, audio, GPU, storage and more | [3.4.20](https://github.com/libsdl-org/SDL/tree/f00dc919ca35b9756316e86c4777392de442f9b2) [3.4.18] | zlib | `SDL3.dll` 2.7 MB, LFS | system | 493,069 |
| `sdl3/image` | Image loading | [3.4.8](https://github.com/libsdl-org/SDL_image/tree/0891fc30428b518e7d018677bd6fa95c1a50f4c6) | zlib, plus the codec DLLs' licenses | 0.4 MB, plus 8.5 MB of codec DLLs (`libavif-16.dll` alone is 7.3 MB) | system | 26,280 |
| `sdl3/mixer` | Mixing, music, spatial audio | [3.2.4](https://github.com/libsdl-org/SDL_mixer/tree/72a81869b45e249e8e67102db4e98dd2441f05a1) | zlib; loads LGPL-2.1 `libgme.dll` ([LICENSE.gme.txt](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/sdl3/mixer/LICENSE.gme.txt#L1)) | 0.4 MB, plus 1.4 MB of codec DLLs | system | 31,629, of which dr_libs 12,785, stb_vorbis 4,269 and timidity 3,814 |
| `sdl3/net` | Sockets | [3.2.0](https://github.com/libsdl-org/SDL_net/tree/1a84a2a6b9663572f77e2eb5348d42845bac0053) | zlib | 0.1 MB | system | 2,320 |
| `sdl3/ttf` | TrueType text | [3.2.2](https://github.com/libsdl-org/SDL_ttf/tree/a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b) | zlib, plus FreeType (FTL or GPL-2.0), HarfBuzz (MIT), PlutoSVG and PlutoVG (MIT) | 1.9 MB | system | 7,972, plus [FreeType](https://github.com/libsdl-org/freetype/tree/9973564cfa63763a3e4ac67c09147899539b1e07) 134,780 and [HarfBuzz](https://github.com/libsdl-org/harfbuzz/tree/564bf9818a18709776856533829c0c04950773d6) 110,435 C++ |
| [`stb/easy_font`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/stb/easy_font) | Bitmap debug text | Odin port | public domain or MIT | none | — | port of 159 lines |
| [`stb/image`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/stb/image) | stb_image, stb_image_write, stb_image_resize2 | 2.34, 1.19, 2.51 [2.30, 1.16, 2.50] | public domain or MIT | 0.3, 0.1 and 0.5 MB | macOS archives; script elsewhere | 5,662; 1,359; 10,517 |
| [`stb/rect_pack`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/stb/rect_pack) | Rectangle packing | 1.02 [1.01] | public domain or MIT | 0.01 MB | as above | 329 |
| [`stb/sprintf`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/stb/sprintf) | `snprintf` replacement | 1.10 | public domain or MIT | 0.02 MB | as above | 1,473 |
| [`stb/truetype`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/stb/truetype) | TrueType parsing and rasterizing | 1.28 [1.26] | public domain or MIT | 0.2 MB | as above | 3,670 |
| [`stb/vorbis`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/stb/vorbis) | Ogg Vorbis decoding | 1.25 [1.22] | public domain or MIT | 0.2 MB | as above | 4,131 |
| [`vulkan`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/vulkan) | Vulkan, loaded through function pointers | headers [1.4.354](https://github.com/KhronosGroup/Vulkan-Headers/tree/01393c3df0e5285b54ee6527466513f9e614be94) | Apache-2.0 | none | the driver's loader | `vulkan_core.h` 22,697; 23,966 lines of Odin |
| [`wasm/WebGL`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/wasm) | WebGL 1 and 2 for `js_wasm32` | — | Odin's | none | browser | — |
| [`wgpu`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/wgpu) | WebGPU through wgpu-native | [29.0.1.1](https://github.com/gfx-rs/wgpu-native/tree/6aed50955d934ac36049ba8d002034841633ae02) | MIT or Apache-2.0 | static `.lib` 52.3 MB, `.dll` 8.7 MB, LFS | the user downloads a release ([wgpu.odin](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/wgpu/wgpu.odin#L10-L16)) | Rust: 7,029 in wgpu-native, plus 179,330 in [wgpu-core, wgpu-hal, wgpu-types and naga 29.0.3](https://github.com/gfx-rs/wgpu/tree/4cbe6232b2d7c289b6e1a38416a6ae1461a22e81) |
| [`windows/GameInput`, `XAudio2`, `wasapi`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/windows) | Game input and audio on Windows | Windows SDK 10.0.26100.0 | Odin's | none | Windows only | OS library |
| [`x11/xlib`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/x11) | Xlib | — | Odin's | none | system | OS library |
| [`zlib`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/zlib) | Deflate | [1.4](https://github.com/madler/zlib/tree/99623ef7097352da7993ba614161fd6a311e046e) | zlib | static `.lib`, 0.2 MB | system | 17,837, of which 6,771 in `.c` files; most of the rest is CRC tables in headers |

### 2.3 Where Odin's README and files disagree

The README's version column claims to name "the upstream version that the bindings and the binaries we ship are for" ([README](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/README.md#L9)). It last changed on [2026-11-06](https://github.com/odin-lang/Odin/commit/7ae2b179e679811e70bcac6c4e159f473ad4effd). The files moved past it:

- SDL3 is 3.4.20 since [2026-11-04](https://github.com/odin-lang/Odin/commit/75e949bd993edd5f15589e78541ec89929f8408f) ([SDL_version.h](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/vendor/sdl3/include/SDL_version.h#L65)); the README says 3.4.18.
- lz4 is 1.11.1 since [2026-11-12](https://github.com/odin-lang/Odin/commit/aea5818161f3d4d8355a020c712c9c83c704b449), kb_text_shape 2.32 since [2026-11-15](https://github.com/odin-lang/Odin/commit/ab4aae65ecf811604cc24c539ad2ee8543088bc7), and stb_image 2.34, stb_image_write 1.19, stb_truetype 1.28 and stb_vorbis 1.25 since [2026-11-23](https://github.com/odin-lang/Odin/commit/843160168a8ed7c34f3b27ce97d77a31d1230c11). Each commit updated the binaries too.
- `android` has been in `vendor/` since [2026-10-20](https://github.com/odin-lang/Odin/commit/a49f8d4238ae2f5d2aebf09dee9fdae88f388353) and is missing from the README. `libc` is in the README and is only a `#panic`.

stb itself moved stb_image_resize2 and stb_sprintf into their own repositories ([stb README](https://github.com/nothings/stb/blob/6b5799bae8a1fe3792691ef4b72ae26434c3e799/README.md#L8-L10)); Odin keeps copies.

## 3. What Odin's core library and language replace (task 2)

Odin has no package manager by design and credits a strong standard library with keeping dependency counts low ([packages.md](packages.md), Odin, "Dependencies"). These features and packages do the work of a C library that a C game would vendor. "Language" means the compiler implements it. Sizes are code lines of Odin at `19039b3e`. `ODOC` is [overview.md at f2751d70](https://github.com/odin-lang/odin-lang.org/blob/f2751d7082388bf6a9c84ba7086eef415cd20778/content/docs/overview.md).

| Odin feature or package | Kind | Replaces | Size |
|---|---|---|---|
| `matrix[R, C]T` with `*` as the matrix product ([ODOC](https://github.com/odin-lang/odin-lang.org/blob/f2751d7082388bf6a9c84ba7086eef415cd20778/content/docs/overview.md#L2822)) | language | A C vector-math library: cglm, HandmadeMath, CF's `cute_math.h` | compiler |
| `quaternion64/128/256`, `complex32/64/128` ([ODOC](https://github.com/odin-lang/odin-lang.org/blob/f2751d7082388bf6a9c84ba7086eef415cd20778/content/docs/overview.md#L3029)) | language | The quaternion part of a math library | compiler |
| Array programming and swizzles on `[N]T` ([ODOC](https://github.com/odin-lang/odin-lang.org/blob/f2751d7082388bf6a9c84ba7086eef415cd20778/content/docs/overview.md#L1690)) | language | Vector structs and operator helpers | compiler |
| `#simd[N]T` ([ODOC](https://github.com/odin-lang/odin-lang.org/blob/f2751d7082388bf6a9c84ba7086eef415cd20778/content/docs/overview.md#L3077)) and [`core:simd`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/simd) | language and library | SIMD wrapper headers such as SIMDe and sse2neon | 15,201: x86 intrinsics 5,933, ARM 9,046 |
| [`core:math/linalg`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/math/linalg), with `glsl` and `hlsl` flavors | library | The rest of a math library | 13,106 |
| `core:math/noise` (OpenSimplex2), `rand`, `ease` | library | stb_perlin, FastNoise, PCG | 697; 1,189; 711 |
| `map[K]V`, `[dynamic]T`, `#soa` ([ODOC](https://github.com/odin-lang/odin-lang.org/blob/f2751d7082388bf6a9c84ba7086eef415cd20778/content/docs/overview.md#L2418)) | language | stb_ds.h, CF's ckit.h | runtime: map 827, SOA 541, dynamic arrays 107 |
| `context` with allocators ([ODOC](https://github.com/odin-lang/odin-lang.org/blob/f2751d7082388bf6a9c84ba7086eef415cd20778/content/docs/overview.md#L3706)) and `core:mem` | language and library | Allocator libraries, stb_leakcheck | `core:mem` 4,875 |
| `#load`, `#load_hash` ([directives.md](https://github.com/odin-lang/odin-lang.org/blob/f2751d7082388bf6a9c84ba7086eef415cd20778/content/docs/directives.md#L447)) | language | bin2c, incbin | compiler |
| `intrinsics.debug_trap` ([intrinsics.odin](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/base/intrinsics/intrinsics.odin#L28)) | language | debugbreak.h | compiler |
| [`core:image`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/image): PNG, JPEG, QOI, TGA, BMP, netpbm, BNG, each loading and saving | library | stb_image, stb_image_write, cute_png, cute_jpg | 11,557; PNG 2,008; JPEG 5,395, which reads baseline, progressive, lossless and arithmetic-coded files ([jpeg.odin](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/image/jpeg/jpeg.odin#L1-L6)) |
| [`core:compress/deflate`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/compress/deflate), compressing and decompressing raw, zlib and gzip streams; `compress/zlib` is [deprecated](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/compress/zlib/doc.odin#L4) | library | zlib, miniz | 3,933 |
| `core:compress/lz4`, plus `lzma`, `xz`, `bzip2`, `ppmd` | library | lz4, xz | lz4 2,310; all of `core:compress` 23,223 |
| [`core:encoding/json`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/encoding/json) (marshal and unmarshal by reflection), `xml`, `csv`, `ini`, `cbor`, `base64` | library | cJSON, yxml, inih, CF's `cute_json.h` | JSON 4,408; XML 1,339 |
| `core:fmt`, `core:strconv` | library | stb_sprintf | 2,883; 3,957 |
| `core:strings`, `core:unicode` | library | UTF-8 helpers | 2,228; 9,774 |
| `core:text/regex`, `text/edit`, `text/i18n` | library | A regex library, stb_textedit, gettext | 2,751; 867; 327 |
| `core:thread` (with a pool), `core:sync` (with channels) | library | pthread wrappers, cute_sync | 940; 2,360 |
| [`core:crypto`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/crypto) | library | libsodium, Monocypher, libhydrogen | 48,898 |
| [`core:crypto/tls`](https://github.com/odin-lang/Odin/tree/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/crypto/tls) and `x509`: a TLS 1.3 client; the caller passes the trusted roots ([tls_client.odin](https://github.com/odin-lang/Odin/blob/19039b3eacaac836ae398eafe8f371e2c58c38ce/core/crypto/tls/tls_client.odin#L27-L48)); imported [2026-11-12](https://github.com/odin-lang/Odin/commit/c04e834fb5238cd981d5eabe4a356e76bd6830be) | library | s2n-tls, mbedTLS, cute_tls's Linux path | 2,959 plus 2,093 |
| `core:net`, `core:nbio` | library | Socket wrappers, SDL_net, libuv | 6,270; 5,880 |
| `core:os` | library | The file I/O part of PhysicsFS; not archives | 11,342 |
| `core:dynlib` | library | `dlopen` wrappers, for hot reload | 231 |
| `core:hash` (xxHash, CRC, FNV) | library | xxHash | 4,023 |
| `core:container` (handle map, small array, queue, priority queue, LRU, AVL, red-black tree) | library | C container headers | 2,897 |
| `core:debug/trace` | library | The stack-trace part of cute_crash | 1,005 |
| `core:prof/spall` | library | Profiler instrumentation | 325 |
| `core:testing` with `odin test` | library and compiler | pico_unit | 2,030 |

Odin's core has nothing for fonts, audio, coroutines, UI, physics, reliable UDP or glTF. Those are where its `vendor:` does the work. It ships three duplicates as well: `vendor:zlib` beside `core:compress/deflate`, `vendor:compress/lz4` beside `core:compress/lz4`, and `vendor:stb/image` beside `core:image`.

## 4. CF's dependencies and their Odin equivalents (task 3)

Versions come from [libraries.md](../libraries.md) or from the file's own header. Sizes are code lines. The cute headers are dual-licensed zlib or Unlicense ([cute_png.h](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_png.h#L1950-L1953)).

| CF dependency | Version in CF | Size | CF uses it for | Odin equivalent |
|---|---|---|---|---|
| SDL3 ([CMakeLists.txt](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/CMakeLists.txt#L638)) | [3.4.0](https://github.com/libsdl-org/SDL/tree/a962f40bbba175e9716557a25d5d7965f134a3d3) | 487,097 | The platform layer | vendor `sdl3`, at 3.4.20 |
| Box2D ([L368-L369](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/CMakeLists.txt#L368-L369)) | [3.1.1](https://github.com/erincatto/box2d/tree/8c661469c9507d3ad6fbd2fea3f1aa71669c2fe3) | 23,604 | 2D physics; CF's math uses its collision code | vendor `box2d`, at 3.2.0 |
| Box3D ([L403-L404](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/CMakeLists.txt#L403-L404)) | 0.1.0 | 46,067 | 3D physics and collision | vendor `box3d`, a later snapshot |
| PhysicsFS ([L490-L491](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/CMakeLists.txt#L490-L491)) | [`9d18d36b`](https://github.com/icculus/physfs/tree/9d18d36b5a5207b72f473f05e1b2834e347d8144) | 17,898 | A file system over directories and archives | none; `core:os` reads files but mounts no archives |
| s2n-tls ([L535](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/CMakeLists.txt#L535)) | [1.6.3](https://github.com/aws/s2n-tls/tree/1c9844722d9cc3beca5551c0f9bfed8aabbc22ed) | 38,633, plus a system libcrypto | TLS on Linux for cute_tls | `core:crypto/tls` |
| wgpu-native | 29.0.1.1 | Rust, section 2.2 | Only with `CF_WEBGPU`, off by default | vendor `wgpu` |
| Dear ImGui and its dcimgui C bindings | [1.92.7](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/imgui/imgui.h#L32) | 44,798 C++, plus 12,863 for dcimgui and 1,625 for the SDL3 and SDL_GPU backends; the demo adds 8,887 | Debug UI | none; vendor `microui` is the nearest |
| glad 0.1.36 output and `khrplatform.h` | [0.1.36](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/glad/glad.h#L3) | 5,783 and 117 | The OpenGL 3.3 and GLES 3.0 loader | vendor `OpenGL`, glad output in Odin |
| stb_truetype | 1.26 | 3,482 | Glyph rasterizing | vendor `stb/truetype` |
| stb_vorbis | header says 1.09 | 3,967 | Ogg Vorbis for cute_sound | vendor `stb/vorbis` |
| minicoro | [0.2.0](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/edubart/minicoro.h#L1-L12) | 1,522 | Stackful coroutines behind `cute_coroutine.h` | none: Odin has no coroutines |
| debugbreak.h | — | 96 | Breakpoints | `intrinsics.debug_trap` |
| pico_unit | — | 206 | CF's tests | `core:testing` |
| pico_qt | — | 458 | A quadtree | none |
| `ckit.h` | — | 1,379 | Arrays, maps, strings, interning | built-in `map` and `[dynamic]`, `core:strings` |
| `cute_png.h` | 1.06 | 1,471 | PNG | `core:image/png` |
| [`cute_jpg.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_jpg.h#L15) | 1.00 | 979 | JPEG decoding | `core:image/jpeg` |
| [`cute_dds.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_dds.h#L15) | 1.00 | 281 | The DDS container for compressed textures | none |
| [`cute_aseprite.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_aseprite.h#L16) | 1.04 | 1,633 | Aseprite files | none |
| [`cute_atlas_cache.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_atlas_cache.h#L14) | 1.15 | 1,526 | A texture atlas built at run time | none; vendor `stb/rect_pack` does the packing |
| [`cute_sound.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_sound.h#L16) | 3.00 | 1,919 | Playing, looping, panning and fading sounds on SDL3 audio | vendor `miniaudio` or `sdl3/mixer` |
| [`cute_net.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_net.h#L15) | 1.03 | 10,224, with libhydrogen embedded ([L722](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_net.h#L722); [ISC](https://github.com/jedisct1/libhydrogen/blob/05d569efd91bde52f722f29b5937df99c75d22bc/LICENSE)) | Encrypted reliable and unreliable UDP with connect tokens | vendor `ENet`, without encryption |
| [`cute_arith.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_arith.h#L9) | — | 387 | A range coder for packet compression | none |
| [`cute_tls.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_tls.h#L15-L26) | 1.02 | 1,165 | HTTPS through Secure Channel, Network.framework, or s2n on Linux | `core:crypto/tls` |
| [`cute_sync.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_sync.h#L15) | 1.02 | 902 | Threads, atomics, locks, a thread pool | `core:thread`, `core:sync` |
| `cute_spirv.h` | 1.11 | 11,603 | CF's shader compiler ([shader-languages.md](shader-languages.md) section 6) | none; vendor `directx/dxc` is the nearest |
| [`cute_h264.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_h264.h#L15) | 0.03 | 5,335 | Encoding and decoding H.264 video | none |
| [`cute_crash.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_crash.h#L18), `cute_sym.h` | 0.01 each | 3,432 and 4,063 | Crash reports and their symbols (PDB, DWARF, Mach-O) | `core:debug/trace`, stack traces only |
| [`cute_model.h`](https://github.com/RandyGaul/cute_framework/blob/6b00978ac4a453a9a62b7e93e7d8508a2320f317/libraries/cute/cute_model.h#L24) | 1.00 | 1,567, plus ckit.h | glTF and GLB loading, skinning, animation | vendor `cgltf` |

## 5. Verdicts for chibi (task 4)

Four verdicts: **Ship** puts the C library in the set. **Std** replaces it with chibi code in the standard library. **Feature** means a chibi feature makes it unnecessary. **Out** leaves it out. Std sizes are my estimates unless a number names its source. The chibi features are those in the Language section of [decisions.md](../decisions.md).

### 5.1 Platform

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| SDL3 | both | Ship, decided | zlib, 493,069 lines, `SDL3.dll` 2.7 MB in Odin's build. Last commit [2026-11-25](https://github.com/libsdl-org/SDL/commit/3ebae2a704f945edd51e204d96a059d3946cb58e), release 3.4.20 on 2026-11-03. |
| SDL2 | Odin | Ship, decided | zlib, 318,157 lines, `SDL2.dll` 1.5 MB. No release since 2.32.10 on 2025-09-01; its branch had a commit on [2026-11-21](https://github.com/libsdl-org/SDL/commit/b53be5bb76b903081fb930e64e343cddabc1211d). |
| GLFW | Odin | Out | A second window and input library beside SDL3, without audio or a GPU API. |
| raylib, rlgl, raygui | Odin | Out | A framework over its own copies of GLFW, miniaudio, stb and others: 200,308 of its 240,664 lines are in `src/external`. It overlaps SDL3. |
| Windows, Direct3D, DXGI, X11, EGL and Android NDK bindings | Odin | Feature: the C import | They are C headers, with COM in C form for Direct3D. The C import writes their declarations. Odin keeps 29,710 lines of Odin for these five: Direct3D 12,668, NDK 7,899, X11 6,332, Windows 2,570 and EGL 241. |
| `darwin` frameworks, `android/java` | Odin | Out | Objective-C and Java APIs, which no C import reads. SDL3 covers what a game needs from them. An Objective-C import is a separate decision. |
| `libc-shim`, `wasm/WebGL` | Odin | Out | They serve WASM, which chibi doesn't target yet. |

### 5.2 Audio

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| SDL3 audio | both | Ship, in SDL3 | A device mixes all streams bound to it ([SDL_audio.h](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_audio.h#L37)), each with its own gain ([L1259](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_audio.h#L1259)) and pitch ([L1213](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_audio.h#L1213)), and `SDL_LoadWAV` reads WAV ([L2278](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_audio.h#L2278)). |
| stb_vorbis | both | Ship | Ogg Vorbis is the common format for game music. 4,131 lines, public domain or MIT, 0.2 MB in Odin's build. |
| miniaudio | Odin | Out | It duplicates SDL3's device layer. 72,954 lines. |
| SDL3_mixer | Odin | Out of the first set | Add it when a game needs MP3, FLAC, panning or 3D audio. Build it with `SDLMIXER_GME` ([L137](https://github.com/libsdl-org/SDL_mixer/blob/72a81869b45e249e8e67102db4e98dd2441f05a1/CMakeLists.txt#L137)), `SDLMIXER_MP3_MPG123` ([L149](https://github.com/libsdl-org/SDL_mixer/blob/72a81869b45e249e8e67102db4e98dd2441f05a1/CMakeLists.txt#L149)) and `SDLMIXER_MIDI_TIMIDITY` ([L157](https://github.com/libsdl-org/SDL_mixer/blob/72a81869b45e249e8e67102db4e98dd2441f05a1/CMakeLists.txt#L157)) off. With its in-tree codecs only, it is zlib plus dr_libs and stb_vorbis. |
| `cute_sound.h` | CF | Std, about 2,000 lines | Playing, looping, panning and fading over SDL3 audio streams. cute_sound does it in 1,919. |
| PortMidi | Odin | Out | Games rarely talk to MIDI devices. |

### 5.3 GPU and shaders

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| SDL3 GPU | both | Ship, in SDL3 | chibi's GPU output targets it ([shader-languages.md](shader-languages.md) section 6). It also generates mipmaps ([SDL_gpu.h](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_gpu.h#L4066)). |
| Vulkan-Headers | Odin | Ship, headers only | The C import needs `vulkan_core.h` (22,697 lines) to write declarations, and Windows has no system copy outside the Vulkan SDK. The loader, `vulkan-1.dll`, comes with the driver. Apache-2.0. Only code below SDL3 GPU needs it. |
| Odin's `vulkan` and `OpenGL` bindings | Odin | Feature: the C import plus compile-time code | Compile-time code builds the function-pointer table from the imported declarations. Odin commits 23,966 and 7,438 generated lines. |
| glad output, `khrplatform.h` | CF | Feature: the C import plus compile-time code | glad is a generator. chibi's compile-time code does its job from the imported Khronos headers. |
| wgpu-native | both | Out | SDL3 GPU covers native targets. It is 186,359 lines of Rust, and Odin's Windows static library is 52.3 MB. On the web, chibi prints WGSL itself. |
| DXC, FXC | Odin | Feature: chibi's SPIR-V, MSL and WGSL writers | The GPU output row of 2026-11-10. D3D12 gets printed HLSL and offline DXC only if needed ([shader-backends.md](shader-backends.md) 1.1, item 8). |
| `cute_spirv.h` | CF | Feature: chibi's SPIR-V writer | It compiles CF's shader dialect; chibi compiles its own shader code. |

### 5.4 Physics

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| Box2D | both | Ship, decided | MIT. 32,921 lines at 3.2.0 (released 2026-10-19), Odin's static library 1.8 MB. CF pins 3.1.1, 23,604 lines. |
| Box3D | both | Ship, decided | MIT. 46,067 lines at 0.1.0, 47,297 in Odin's snapshot, Odin's static library 3.1 MB. Last commit [2026-11-25](https://github.com/erincatto/box3d/commit/042be585b58d7eebcafecb287053d2448c116b06). Pin a commit. |

### 5.5 Fonts and text

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| stb_truetype | both | Ship | Glyph rasterizing in 3,670 lines, public domain or MIT. Its header warns against untrusted fonts ([L6-L8](https://github.com/nothings/stb/blob/6b5799bae8a1fe3792691ef4b72ae26434c3e799/stb_truetype.h#L6-L8)); a game ships its own. |
| kb_text_shape | Odin | Ship | OpenType shaping and Unicode segmentation in one zlib header, 26,771 lines, last commit [2026-11-14](https://github.com/JimmyLefevre/kb/commit/4eec43b4c753a42b64aa8d7fd55dbce16a70343c). A game needs it when it is translated into Arabic, Hindi or another script that joins or reorders glyphs. |
| SDL3_ttf | Odin | Out | It wraps FreeType (134,780 lines; the FTL with a credit clause, or GPL-2.0, [LICENSE.TXT](https://github.com/libsdl-org/freetype/blob/9973564cfa63763a3e4ac67c09147899539b1e07/LICENSE.TXT#L9-L26)) and HarfBuzz (110,435 lines of C++). stb_truetype and kb_text_shape cover the same need in 30,441 lines. |
| fontstash | Odin | Out | Unmaintained: last commit [2019-04-22](https://github.com/memononen/fontstash/commit/b5ddc9741061343740d85d636d782ed3e07cf7be). A glyph cache belongs with the atlas code in the std. |
| stb_easy_font | Odin | Out | SDL3's 2D renderer draws 8×8 debug text ([SDL_render.h](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_render.h#L2856)). |
| nanovg | Odin | Out | Vector drawing on OpenGL only; chibi draws through SDL3 GPU. |
| stb_sprintf | Odin | Feature: std formatting | chibi's own formatting replaces `snprintf`, as `core:fmt` does in Odin. |

### 5.6 Images

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| SDL3 surfaces | both | Ship, in SDL3 | `SDL_LoadPNG` ([L692](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_surface.h#L692)), `SDL_SavePNG` ([L730](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_surface.h#L730)) and BMP loading, since 3.4.0. SDL builds them on its own copy of stb_image, limited to PNG and JPEG, and miniz ([SDL_stb.c](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/src/video/SDL_stb.c#L57-L80)). |
| stb_image | Odin | Ship | JPEG, TGA, GIF, HDR and PSD in 5,662 lines, public domain or MIT, 0.3 MB in Odin's build. SDL3 exposes PNG and BMP only. The std replaces it later (section 6.2). |
| stb_image_write | Odin | Out | SDL3 saves PNG and BMP. |
| stb_image_resize2 | Odin | Out | 10,517 lines for work done offline or by `SDL_GenerateMipmapsForGPUTexture`. |
| stb_rect_pack | Odin | Std, about 330 lines | It is 329 lines. |
| SDL3_image | Odin | Out | Odin's Windows build adds 8.5 MB of codec DLLs. Built from its in-tree code only, it is stb_image with wrappers ([CMakeLists.txt](https://github.com/libsdl-org/SDL_image/blob/0891fc30428b518e7d018677bd6fa95c1a50f4c6/CMakeLists.txt#L109)). |
| `cute_png.h`, `cute_jpg.h` | CF | Std, 3,000 to 4,000 lines with inflate | cute_png is 1,471 lines and cute_jpg 979; inflate adds about 2,000 (my estimate from Odin's 3,933 for both directions). |
| `cute_dds.h` | CF | Std, about 300 lines | It is 281 lines and decodes nothing; it hands the GPU its blocks. |
| `cute_aseprite.h` | CF | Std, about 1,600 lines | It is 1,633 lines and needs inflate. |
| `cute_atlas_cache.h` | CF | Std, about 1,500 lines | It is 1,526 lines. |
| OpenEXR | Odin | Out | An HDR film format for tools: 30,195 lines plus OpenJPH, zstd and libdeflate, and a 4.5 MB library. |
| `cute_h264.h` | CF | Out | Video capture and playback; 5,335 lines; no early game needs it. |

### 5.7 Models and UI

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| cgltf | Odin | Ship | glTF is the interchange format for 3D assets. MIT, 7,313 lines, 0.4 MB in Odin's build. Last commit [2026-02-02](https://github.com/jkuhlmann/cgltf/commit/85cd62382dfea638278962690cf515023f33ed00); no release since 2025-02-09, so pin the commit. |
| `cute_model.h` | CF | Std later, 1,500 to 2,500 lines | A glTF loader on the std's JSON reader, which reflection drives. cute_model is 1,567 lines over ckit.h. |
| Dear ImGui with dear_bindings | CF | Ship | Tool and debug UI. MIT, release 1.93.1 on [2026-11-26](https://github.com/ocornut/imgui/commit/0baafdcd3870bcdf5714a81ee2e4b1cc90ed134c). It is C++, so the set ships a prebuilt library that exports the dcimgui C API, and the C import reads `dcimgui.h`. Odin's `vendor:` has no ImGui. |
| microui | Odin | Out | Unmaintained: last commit [2024-08-13](https://github.com/rxi/microui/commit/0850aba860959c3e75fb3e97120ca92957f9d057). Dear ImGui covers tool UI. |
| raygui | Odin | Out | It draws through raylib. |

### 5.8 Networking

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| ENet | Odin | Ship | Reliable and unreliable UDP channels. MIT, 4,862 lines, 0.1 MB in Odin's build. Last commit [2026-06-23](https://github.com/lsalzman/enet/commit/5a9c537fd464b3c6d3c55e1d3bd47588faf71b42); no release since 2024-04-14, so pin the commit. |
| `cute_net.h` | CF | chibi code later | It adds encryption and connect tokens that ENet lacks. 10,224 lines, libhydrogen included. A std package or a chibi package, once sockets and the coroutines exist. |
| `cute_arith.h` | CF | Std, with the netcode | 387 lines. |
| SDL3_net, SDL2_net | Odin | Out | Socket wrappers of 2,320 and 1,152 lines. The std has sockets, as Odin's `core:net` does in 6,270. |
| GGPO | Odin | Out | Unmaintained: last commit [2019-11-07](https://github.com/pond3r/ggpo/commit/7ddadef8546a7d99ff0b3530c6056bc8ee4b9c0a). C++, and Windows-only in Odin. |
| curl | Odin | Out | 131,983 lines, plus zstd, brotli, nghttp2 and zlib. CF's HTTPS client is 404 lines over cute_tls; chibi's sits on the std's TLS. |
| `cute_tls.h` | CF | Std | Secure Channel on Windows and Network.framework on Apple through the C import; chibi's own TLS 1.3 client on Linux. Odin's client is 2,959 lines plus 2,093 for X.509, over its crypto primitives. |
| s2n-tls | CF | Out | Linux only, 38,633 lines, and it needs a system libcrypto. Odin answered the same need with `core:crypto/tls`. |

### 5.9 Files, compression, scripting

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| PhysicsFS | CF | Std, 1,000 to 2,000 lines | SDL3 lists directories ([SDL_filesystem.h](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_filesystem.h#L369)), globs them ([L506](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_filesystem.h#L506)) and opens title storage ([SDL_storage.h](https://github.com/libsdl-org/SDL/blob/f00dc919ca35b9756316e86c4777392de442f9b2/include/SDL3/SDL_storage.h#L353)). Mounting zip archives is what remains: a zip reader over the std's inflate. PhysicsFS is 17,898 lines and last released 3.2.0 on [2022-09-30](https://github.com/icculus/physfs/releases/tag/release-3.2.0), though commits continue. |
| zlib | Odin | Std, about 3,900 lines | Odin's inflate and deflate are 3,933 lines; zlib is 17,837. |
| lz4 | Odin | Std, about 2,300 lines | Odin's is 2,310; lz4 is 8,379. |
| cmark | Odin | Out | Games don't render Markdown. |
| Lua | Odin | Out | Hot reload serves the edit loop that a scripting language serves in a C game. A game that wants Lua names it with `b.git` and imports `lua.h`: MIT, 21,231 lines. Guess: some users will ask for it in the set. |

### 5.10 Language-level helpers

| Candidate | From | Verdict | Reason |
|---|---|---|---|
| minicoro | CF | Feature: coroutines | chibi's coroutines replace `cute_coroutine.h`. minicoro is stackful, so a coroutine can yield from any depth; chibi's are stackless, so every function between the coroutine and the yield must be a coroutine too. Last commit [2024-12-07](https://github.com/edubart/minicoro/commit/02dad0f8b7cbb12fe6e216ae7a76db15ca55cd7b). |
| `cute_sync.h`, CF's thread pool | CF | Feature: tasks; Std for threads | chibi's tasks replace the pool. Threads, atomics and locks are std, as Odin's `core:thread` and `core:sync` are in 3,300 lines. |
| `ckit.h` | CF | Feature: generics | Typed arrays, maps and string interning in the std. Whether maps are built into the language is not decided. |
| debugbreak.h | CF | Feature: a trap builtin | A builtin in a built-in package (the builtins row of 2026-11-23), as Odin's `intrinsics.debug_trap`; my proposal, not decided. debugbreak's last commit was [2021-07-02](https://github.com/scottt/debugbreak/commit/5dcbe41d2bd4712c8014aa7e843723ad7b40fd74). |
| pico_unit | CF | Feature: `std/build` test steps | The build program already has test steps (the build program row of 2026-11-26). |
| pico_qt | CF | Out | A 458-line quadtree; Box2D and Box3D hold their own broad-phase trees. |
| `cute_crash.h`, `cute_sym.h` | CF | Std later, about 7,500 lines | Crash reports and symbol lookup; chibi's compiler writes the debug info they read. Not a first-set need. |
| C math libraries | — | Feature: matrix and SIMD arrays | Matrices are column-major arrays (the matrices row of 2026-11-10), and SIMD vectors are arrays with lane operators (2026-11-23). Quaternions and helpers go in the std, 2,000 to 3,000 lines with generics against Odin's 13,106 in `linalg`. |

## 6. Recommendations (task 5)

### 6.1 The first set

Ordered by what a 2D or 3D game needs. Windows sizes are Odin's builds at `19039b3e`; the set would build its own.

| Need | Library | Version | License | Code lines | Windows build in Odin | Last commit; last release |
|---|---|---|---|---|---|---|
| Window, input | SDL3, decided | 3.4.20 | zlib | 493,069 | `SDL3.dll` 2.7 MB | 2026-11-25; 2026-11-03 |
| | SDL2, decided | 2.32.10 | zlib | 318,157 | `SDL2.dll` 1.5 MB | 2026-11-21; 2025-09-01 |
| Audio output and mixing | SDL3 | — | — | — | — | — |
| GPU | SDL3 GPU; Vulkan-Headers as headers only | 1.4.354 | Apache-2.0 | 22,697 in `vulkan_core.h` | none | 2026-11-20; tags only |
| Physics | Box2D, decided | 3.2.0 | MIT | 32,921 | 1.8 MB static | 2026-11-25; 2026-10-19 |
| | Box3D, decided | a pinned commit | MIT | 46,067 at 0.1.0 | 3.1 MB static | 2026-11-25; 2026-06-30 |
| Fonts | stb_truetype | 1.28 | public domain or MIT | 3,670 | 0.2 MB | 2026-11-19 (stb); no releases |
| | kb_text_shape | 2.32 | zlib | 26,771 | 0.7 MB | 2026-11-14; no releases |
| Images | SDL3 for PNG and BMP; stb_image for the rest | 2.34 | public domain or MIT | 5,662 | 0.3 MB | 2026-11-19; no releases |
| Audio decoding | SDL3 for WAV; stb_vorbis for Ogg | 1.25 | public domain or MIT | 4,131 | 0.2 MB | 2026-11-19; no releases |
| 3D models | cgltf | 1.15 | MIT | 7,313 | 0.4 MB | 2026-02-02; 2025-02-09 |
| UI | Dear ImGui with dear_bindings and the SDL3 and SDL_GPU backends | 1.93.1 | MIT | 44,798 C++, plus 12,863 and 1,625 (at 1.92.7) | Odin builds none | 2026-11-26; 2026-11-26 |
| Networking | ENet | 1.3.18 | MIT | 4,862 | 0.1 MB | 2026-06-23; 2024-04-14 |
| File system | SDL3 filesystem and storage; zip mounting from the std | — | — | — | — | — |

Odin's Windows builds of these libraries, without Dear ImGui, total about 11.4 MB (my arithmetic: `SDL3.dll` and `.lib`, `SDL2.dll` and `.lib`, the Box2D and Box3D static libraries, and the stb, kb_text_shape, cgltf and ENet libraries). Odin's whole Windows set is 149.1 MB.

### 6.2 What the standard library takes over

In the order games and the toolchain need them. Sizes are Odin's or CF's where named, and mine otherwise.

| Order | Std piece | Replaces | Size |
|---|---|---|---|
| 1 | Inflate and deflate | zlib; the core of PNG, zip and Aseprite reading | Odin 3,933 |
| 2 | JSON, driven by reflection | cute_json, the parser inside cgltf | Odin 4,408 |
| 3 | PNG, QOI, TGA, BMP and JPEG decoders | stb_image, cute_png, cute_jpg | Odin's `core:image` is 11,557 with encoders; 4,000 to 6,000 for decoders (my estimate) |
| 4 | Zip mounting over SDL3's file system | PhysicsFS | 1,000 to 2,000 (my estimate) |
| 5 | Sockets | SDL_net | Odin 6,270 |
| 6 | TLS 1.3 client and X.509 | s2n-tls, cute_tls on Linux; HTTPS for chibi's own git client | Odin 2,959 plus 2,093, plus AES-GCM, ChaCha20-Poly1305, SHA-2, X25519, P-256 and RSA verification from its 48,898-line `core:crypto` |
| 7 | Sound mixer over SDL3 audio streams | cute_sound | CF 1,919 |
| 8 | Rectangle packing, atlas cache, DDS, Aseprite | stb_rect_pack and the cute headers | 329, 1,526, 281 and 1,633 |
| 9 | glTF loader | cgltf, cute_model | 1,500 to 2,500 (my estimate) |
| 10 | LZ4 | lz4 | Odin 2,310 |
| 11 | Quaternions and linear-algebra helpers | CF's `cute_math.h` | 2,000 to 3,000 (my estimate) |
| 12 | Crash reports and symbol lookup | cute_crash, cute_sym | CF 7,495 |

### 6.3 License flags

- Everything in 6.1 is MIT, zlib, Apache-2.0, or public domain with an MIT fallback. Apache-2.0 asks that the license and any NOTICE file travel with the headers.
- **Out:** `libgme` (LGPL-2.1), which Odin's SDL2_mixer and SDL3_mixer builds load; mpg123 (LGPL-2.1), an SDL_mixer option; timidity in SDL_mixer, under the Perl Artistic License ([timidity.c](https://github.com/libsdl-org/SDL_mixer/blob/72a81869b45e249e8e67102db4e98dd2441f05a1/src/timidity/timidity.c#L6)); FreeType's GPL-2.0 option, which SDL_ttf users avoid by choosing the FTL; `d3dcompiler_47.dll`, which is Microsoft's closed redistributable.
- **Permissive with conditions:** the FTL requires a credit line in the documentation; libcurl, OpenEXR, cmark, PortMidi and the zstd, brotli and nghttp2 libraries inside Odin's curl build are permissive but not in the first set.
- Nothing among the candidates is commercial-only.

### 6.4 Maintenance flags

- **No commit in over two years:** fontstash (2019-04-22), GGPO (2019-11-07), debugbreak (2021-07-02) and microui (2024-08-13). None is in the set.
- **At the line:** minicoro's last commit is 2024-12-07 and its last release 2021-12-30. It is not in the set.
- **No release in over a year, commits continue:** PhysicsFS (release 2022-09-30), ENet (2024-04-14), cgltf (2025-02-09), SDL_ttf (2025-03-31) and SDL2 (2025-09-01). Pin a commit for each one that ships.
- CF's glad output comes from glad 0.1.36, the old generator; glad 2 is current ([last commit 2026-11-26](https://github.com/Dav1dde/glad/commit/77b3de37c6ab0bb4ceec49927e1fc8b30634f7a6)). chibi needs neither (5.3).

## 7. Not verified

**Counted differently from the rest.**
- My line counter follows scc's rules for comments and strings, and I checked it on a test file, but I did not compare it with scc on a whole project. Expect totals that differ from scc's by a few percent.
- SDL3's 493,069 lines include generated tables in its headers and every platform backend. HarfBuzz's 110,435 include its in-tree test sources.
- wgpu's Rust count covers four crates, not wgpu-native's other dependencies. DXC's comes from shader-backends.md.
- Lua 5.1.5 and 5.2.4 have no tags in the GitHub mirror, so I did not count them.

**Not read directly.**
- The sizes of Odin's LFS binaries come from pointer files. I did not inspect any binary, so I don't know which `.lib` files are import libraries for a DLL and which hold code.
- Whether Odin's macOS and Linux archives match the versions of its Windows builds. I checked their presence only.
- Whether a prebuilt Dear ImGui with the dcimgui C API needs the C++ runtime at link time on Linux and macOS.
- Whether the README's version column is stale for packages whose files I did not check: GLFW, ENet, cgltf, cmark, curl, OpenEXR, PortMidi, raylib and the SDL satellites.
- Whether SDL3's PNG loader handles every PNG a game uses, such as 16-bit, interlaced and paletted images. It uses stb_image, which does.

**My analysis, not measured.**
- Every std size marked "my estimate".
- That hot reload removes most demand for Lua, and that SDL3's audio streams are enough mixing for a first set.
- That a trap builtin belongs in a built-in package; the name and package are not decided.
