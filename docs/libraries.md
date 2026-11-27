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

The curated set row of 2026-11-27. Windows gets prebuilt libraries; other systems link the system's library. Versions and code lines come from [vendor-libraries.md](research/vendor-libraries.md) section 6.1.

| Library | Version | License | Code lines | Gives |
|---|---|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | 3.4.20 | Zlib | 493,069 | windows, input, audio output and mixing, the GPU API, PNG, BMP and WAV loading, files and storage |
| [SDL2](https://github.com/libsdl-org/SDL/tree/SDL2) | 2.32.10 | Zlib | 318,157 | the same for code written against SDL2 |
| [Box2D](https://github.com/erincatto/box2d) | 3.2.0 | MIT | 32,921 | 2D physics |
| [Box3D](https://github.com/erincatto/box3d) | a pinned commit; the only release is 0.1.0 | MIT | 46,067 at 0.1.0 | 3D physics |

## Proposed for the set, not decided

[vendor-libraries.md](research/vendor-libraries.md) section 6.1 recommends these. Each had a commit in 2026.

| Library | Version | License | Code lines | Gives |
|---|---|---|---|---|
| stb_truetype | 1.28 | public domain or MIT | 3,670 | font rasterizing |
| kb_text_shape | 2.32 | zlib | 26,771 | text shaping, in place of FreeType and HarfBuzz (245,000 lines) |
| stb_image | 2.34 | public domain or MIT | 5,662 | the image formats SDL3 doesn't load, until the standard library's decoders replace it |
| stb_vorbis | 1.25 | public domain or MIT | 4,131 | Ogg Vorbis decoding |
| cgltf | 1.15 | MIT | 7,313 | glTF models |
| Dear ImGui, with dear_bindings and the SDL3 and SDL_GPU backends | 1.93.1 | MIT | 44,798 C++, plus the C bindings | tool UI |
| ENet | 1.3.18 | MIT | 4,862 | reliable UDP |
| Vulkan-Headers, headers only | 1.4.354 | Apache-2.0 | 22,697 in `vulkan_core.h` | code that goes below SDL3's GPU API |

## Not shipped

- **Cute Framework**, as a whole (the curated set row of 2026-11-27). Box2D, Box3D and SDL3 come from it into the set. chibi's features make minicoro, glad, `cute_spirv.h` and debugbreak unnecessary. The standard library replaces PhysicsFS, `cute_png.h`, `cute_sound.h`, `cute_tls.h` and s2n-tls ([vendor-libraries.md](research/vendor-libraries.md) sections 5 and 6.2).
- **SDL_image, SDL_mixer, SDL_ttf and miniaudio.** SDL 3.4 covers what they would add, or they bring large or LGPL parts. Odin's SDL_mixer builds load the LGPL-2.1 `libgme.dll`, and SDL_ttf wraps FreeType ([vendor-libraries.md](research/vendor-libraries.md) section 5).

## TLS: not decided

[tls.md](research/tls.md) recommends vendoring no TLS for chibi's git client: Schannel on Windows, Network.framework on macOS, and the distribution's OpenSSL 3 (`libssl.so.3`) on Linux. If chibi vendors one, it should be rustls through rustls-ffi, or Mbed TLS where the archive must build from C alone. TLS written in chibi should come after self-hosting, ported from Go's `crypto/tls`. Zig's is not a safe reference: its verifier never checks basicConstraints, an open bug since 2026-06-21.
