# Third-party software

Engine and game code are proprietary; these dependency notices do not grant a license to project code.

SDL3 is the sole library dependency. CMake FetchContent builds the unmodified official
[SDL 3.2.28 release](https://www.libsdl.org/release/SDL3-3.2.28.tar.gz) statically.
Archive SHA-256: `1330671214d146f8aeb1ed399fc3e081873cdb38b5189d1f8bb6ab15bbc04211`.
SDL reports revision `SDL-release-3.2.28-0-g7f3ae3d57`. No system SDL fallback or floating branch is used.

The first configure downloads source into the selected build tree's `_deps` directory.
Subsequent configurations reuse it. An offline source checkout of the **same release**
can be provided through CMake's `FETCHCONTENT_SOURCE_DIR_SDL3` override; the caller
must verify that checkout because source overrides bypass archive hash verification.

Retained notices for SDL and bundled components:

- `LICENSES/SDL3.txt`: SDL zlib license.
- `LICENSES/SDL3-HIDAPI-BSD.txt`: SDL's embedded HIDAPI; this project selects its BSD license option.
- `LICENSES/SDL3-yuv2rgb-BSD.txt`: embedded yuv2rgb BSD license.
- `LICENSES/SDL3-fdlibm.txt`: embedded Sun fdlibm notice.

Keep these notices with redistributed binaries. Upstream source retains its original
notices. Test/example targets from SDL are disabled. Optional external HIDAPI libusb
support is disabled to avoid silently depending on a developer's libusb installation.
Native macOS linkage was inspected: only operating-system libraries/frameworks remain.
CMake, Ninja, clang-format and compilers are development tools, not runtime libraries.
The checker sprite is generated in project code and has no external art dependency.
