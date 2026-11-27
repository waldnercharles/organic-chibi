# Libraries

This file lists the third-party code in chibi: what the compiler vendors, which programs it runs, and what chibi ships or plans to ship. Agents tell the owner before they vendor a library, and add it here (the Process row of 2026-11-26 in [decisions.md](decisions.md)).

## Vendored in the compiler

| Library | Version | License | Size | Used for |
|---|---|---|---|---|
| [xxHash](https://github.com/Cyan4973/xxHash) | 0.8.3 | BSD-2-Clause | 7,280 lines | `XXH3_64bits` in the name table (`src/names.h`), `XXH64` in `str_hash` (`src/str.c`) |
| pico_unit, from [pico_headers](https://github.com/empyreanx/pico_headers) | not stated in the file | zlib or public domain | 509 lines | the C tests in `test/` |

## Programs the compiler runs

These are not vendored. The user installs them.

| Program | When |
|---|---|
| clang | Importing a C header on a cache miss (the C headers row of 2026-11-09). `translate/` reads its JSON AST. |
| A C compiler, `$CC` or `cc` | Compiling the C backend's output. |
| git | Fetching a `b.git` dependency that isn't cached (the dependencies row of 2026-11-26). Windows doesn't include git, so a build with a git dependency needs Git for Windows. |

## Shipped with chibi: decided, not vendored yet

The 2026-11-09 goal names prebuilt archives of SDL3 and Cute Framework (CF). The dependencies row of 2026-11-26 makes them part of a curated set, prebuilt for Windows, with bindings from the C import.

| Library | Version | License | Gives |
|---|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | 3.4.0, the version CF requires | Zlib | windows, input, audio, the GPU API |
| [Cute Framework](https://github.com/RandyGaul/cute_framework) | `6b00978a`, the commit the shader research read | zlib or public domain | 2D game framework on SDL3 |

### What Cute Framework brings with it

A prebuilt CF archive contains these libraries too. Read from CF's `CMakeLists.txt` and `libraries/` at `6b00978a`.

CMake downloads these when CF is built:

| Library | Version | License | Notes |
|---|---|---|---|
| [Box2D](https://github.com/erincatto/box2d) | v3.1.1 | MIT | 2D physics |
| [Box3D](https://github.com/erincatto/box3d) | v0.1.0 | MIT | 3D physics |
| [PhysicsFS](https://github.com/icculus/physfs) | commit `9d18d36b` | Zlib | file system over archives |
| SDL3 | 3.4.0 | Zlib | listed above |
| [s2n-tls](https://github.com/aws/s2n-tls) | v1.6.3 | Apache-2.0 | Linux only. TLS for `cute_tls.h`. Links the system's libcrypto. |
| wgpu-native | v29.0.1.1 | not checked | Only with `CF_WEBGPU`, which is off by default. |

CF's repository holds these:

| Library | License | Notes |
|---|---|---|
| Dear ImGui, with the `dcimgui` C bindings | MIT | |
| glad 0.1.36 output | not stated in the file | OpenGL 3.3 and GLES 3.0 loader |
| `khrplatform.h` | Khronos MIT-style | |
| stb_truetype v1.26, stb_vorbis v1.09 | MIT or public domain | |
| minicoro | MIT No Attribution or public domain | stackful coroutines |
| debugbreak.h | BSD-style | |
| pico_qt, pico_unit | zlib or public domain | |
| CF's own `cute_*.h` headers | CF's license | among them `cute_spirv.h` (shaders), `cute_tls.h`, `cute_png.h`, `cute_sound.h`, `cute_net.h` |

CF has had no glslang, SDL_shadercross or DXC since July 2026, when `cute_spirv.h` replaced them ([shader-languages.md](research/shader-languages.md)).

## Not intended yet

- **TLS for chibi's own git client.** The client that replaces the `git` command needs HTTPS. Windows and macOS include TLS (Secure Channel and Network.framework), and CF's `cute_tls.h` already wraps both in 1,735 lines. Linux has no TLS in the OS, so CF uses s2n-tls with the system's libcrypto. The alternative is TLS written in chibi. The owner decides when the client is built.

## Open

- **The license of chibi's own wrappers.** The dependencies row ships chibi wrappers where a C API is awkward, and games link them. Under the compiler's GPL-3.0-only, every game that uses a wrapper would have to be GPL. Old Chibi licensed its runtime LGPL-3.0-only to avoid this (the license row of 2026-11-09). This needs a decision before the first wrapper ships.
