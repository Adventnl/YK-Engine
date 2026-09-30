# Cross-compiles for 64-bit Windows with MinGW-w64 (Debian/Ubuntu: apt install mingw-w64):
#
#   cmake --preset windows-cross
#   cmake --build --preset windows-cross
#
# The result runs on Windows 10+ without any runtime DLLs (the C++ runtime, the pthread shim and
# SDL are linked statically) and, for smoke tests, under Wine. The posix thread model is required:
# the win32 one lacks std::thread and std::mutex.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(YK_MINGW_TRIPLE x86_64-w64-mingw32)
find_program(CMAKE_C_COMPILER NAMES ${YK_MINGW_TRIPLE}-gcc-posix ${YK_MINGW_TRIPLE}-gcc REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${YK_MINGW_TRIPLE}-g++-posix ${YK_MINGW_TRIPLE}-g++ REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES ${YK_MINGW_TRIPLE}-windres REQUIRED)
set(CMAKE_FIND_ROOT_PATH /usr/${YK_MINGW_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
