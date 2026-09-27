# Third-party software

Engine code is proprietary. Dependency notices do not grant a license to engine code.
CMake FetchContent builds pinned, unmodified source archives statically. No floating
branches, system-library fallback or bundled upstream sample applications are used.

| Dependency | Release | License | Archive SHA-256 |
|---|---|---|---|
| Box2D | [3.1.1](https://github.com/erincatto/box2d/releases/tag/v3.1.1) | MIT | fb6ef914b50f4312d7d921a600eabc12318bb3c55a0b8c0b90608fa4488ef2e4 |
| SDL3 (optional runtime) | [3.2.28](https://www.libsdl.org/release/SDL3-3.2.28.tar.gz) | zlib | 1330671214d146f8aeb1ed399fc3e081873cdb38b5189d1f8bb6ab15bbc04211 |

Retained notices are in LICENSES/Box2D-MIT.txt, LICENSES/SDL3.txt,
LICENSES/SDL3-HIDAPI-BSD.txt, LICENSES/SDL3-yuv2rgb-BSD.txt and LICENSES/SDL3-fdlibm.txt.
Keep these with redistributed binaries. Upstream sources retain their own notices.
SDL's optional external HIDAPI libusb is disabled. Upstream tests/samples are not built.
Development tools (CMake, Ninja, compilers, clang-format) are not runtime dependencies.

The first configure downloads and verifies sources into build/<preset>/_deps.
Offline same-release source overrides FETCHCONTENT_SOURCE_DIR_BOX2D and
FETCHCONTENT_SOURCE_DIR_SDL3 are possible; callers must verify overridden checkouts,
because CMake source overrides bypass archive hashing. YK_RUNTIME=OFF omits SDL entirely.
