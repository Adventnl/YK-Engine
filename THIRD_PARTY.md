# Third-party software

Engine, gameplay, editor and prototype code are proprietary. Dependency notices do not grant a
license to that code. Dependencies are built statically from pinned, unmodified sources. There are
no floating branches, system-library fallbacks or bundled upstream sample applications.

| Dependency | Version | License | Pinned by | Used by |
|---|---|---|---|---|
| Box2D | [3.1.1](https://github.com/erincatto/box2d/releases/tag/v3.1.1) | MIT | archive SHA-256 `fb6ef914b50f4312d7d921a600eabc12318bb3c55a0b8c0b90608fa4488ef2e4` (tag commit `8c661469c9507d3ad6fbd2fea3f1aa71669c2fe3`) | physics (private) |
| SDL3 | [3.2.28](https://www.libsdl.org/release/SDL3-3.2.28.tar.gz) | zlib | archive SHA-256 `1330671214d146f8aeb1ed399fc3e081873cdb38b5189d1f8bb6ab15bbc04211` (tag commit `7f3ae3d57459e59943a4ecfefc8f6277ec6bf540`) | window, input, renderer, audio; omitted by `YK_RUNTIME=OFF` |
| Dear ImGui | [1.92.9, docking branch](https://github.com/ocornut/imgui/tree/9b4eb24cee2071e61dc1f9ef3e5228097cdde720) | MIT | git commit `9b4eb24cee2071e61dc1f9ef3e5228097cdde720` | `yk_editor` only |
| stb_image | commit `013ac3beddff3dbffafd5177e7972067cd2b5083`, vendored as `third_party/stb_image.h` | used under its MIT alternative (also offered as public domain) | file SHA-256 `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` | PNG decoding |

Retained notices are in `LICENSES/`: `Box2D-MIT.txt`, `SDL3.txt`, `SDL3-HIDAPI-BSD.txt`,
`SDL3-yuv2rgb-BSD.txt`, `SDL3-fdlibm.txt`, `DearImGui-MIT.txt`, `ProggyForever-MIT.txt` and
`stb_image-PD.txt`. Keep them with redistributed binaries (`cmake --install` and `cpack` include
them). Upstream sources retain their own notices.

Dear ImGui also contains, unmodified, `imstb_truetype.h` (stb_truetype 1.26) and `imstb_rectpack.h`
(stb_rect_pack 1.01), both public domain (MIT alternative), and its scalable default UI font is a
partial copy of ProggyForever (MIT; `LICENSES/ProggyForever-MIT.txt`). The editor loads no font
files. Only the ImGui core and its SDL3 and SDL_Renderer backends are compiled; ImGui's demo and
example applications are not built.

Box2D's own install rules are suppressed (it is linked statically and stays private), so an installed
or packaged engine contains no Box2D headers or libraries. SDL's optional external HIDAPI libusb is
disabled and its tests and samples are not built. Development tools (CMake, Ninja, compilers,
clang-format) are not runtime dependencies.

## Getting the sources

By default the first configure downloads and verifies the sources into `build/<preset>/_deps`
(archives are hash-checked before use; the ImGui checkout is by full commit).

On a network that cannot fetch release archives run `scripts/fetch-deps.sh`, which makes shallow git
checkouts and refuses any whose commit differs from the ones recorded above, then set
`YK_DEPS_DIR=<that folder>` (environment variable or CMake cache entry). CMake source overrides
(`FETCHCONTENT_SOURCE_DIR_*`) bypass archive hashing, so callers who use them directly must verify
those checkouts themselves; `YK_DEPS_DIR` goes through the script's verification instead.
