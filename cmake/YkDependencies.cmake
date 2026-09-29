# Third-party dependencies, all pinned by content:
#   Box2D and SDL3  - release archives verified by SHA-256
#   Dear ImGui      - editor only; full git commit
#
# Restricted networks: run scripts/fetch-deps.sh, then configure with -DYK_DEPS_DIR=<dir> (or export
# YK_DEPS_DIR). The verified checkouts are used instead of downloads.
include(FetchContent)

set(YK_DEPS_DIR "" CACHE PATH "Directory holding box2d/, SDL/ and imgui/ checkouts (scripts/fetch-deps.sh)")
if(NOT YK_DEPS_DIR AND DEFINED ENV{YK_DEPS_DIR})
    set(YK_DEPS_DIR "$ENV{YK_DEPS_DIR}")
endif()

# Points a FetchContent dependency at a local checkout when YK_DEPS_DIR provides one.
function(yk_prefer_local_source content_name directory marker)
    string(TOUPPER "${content_name}" upper)
    if(YK_DEPS_DIR AND EXISTS "${YK_DEPS_DIR}/${directory}/${marker}")
        set(FETCHCONTENT_SOURCE_DIR_${upper} "${YK_DEPS_DIR}/${directory}" CACHE PATH "" FORCE)
        message(STATUS "Using local ${content_name} source: ${YK_DEPS_DIR}/${directory}")
    endif()
endfunction()

FetchContent_Declare(box2d
    URL https://github.com/erincatto/box2d/archive/refs/tags/v3.1.1.tar.gz
    URL_HASH SHA256=fb6ef914b50f4312d7d921a600eabc12318bb3c55a0b8c0b90608fa4488ef2e4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
yk_prefer_local_source(box2d box2d CMakeLists.txt)
FetchContent_MakeAvailable(box2d)
# Box2D is linked statically and stays private, so its own install rules (headers, library, CMake
# package files) must not end up in an installed or packaged engine. Excluding its directory from
# "all" leaves its install script out of the installation; the library is still built because the
# engine links it.
set_property(DIRECTORY "${box2d_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)

# Configures and fetches SDL3 (runtime, player and editor).
macro(yk_fetch_sdl3)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    # Both URLs serve the byte-identical archive; the hash rejects anything else.
    FetchContent_Declare(SDL3
        URL https://www.libsdl.org/release/SDL3-3.2.28.tar.gz
            https://github.com/libsdl-org/SDL/releases/download/release-3.2.28/SDL3-3.2.28.tar.gz
        URL_HASH SHA256=1330671214d146f8aeb1ed399fc3e081873cdb38b5189d1f8bb6ab15bbc04211
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    yk_prefer_local_source(SDL3 SDL CMakeLists.txt)
    FetchContent_MakeAvailable(SDL3)
endmacro()

# Builds Dear ImGui (docking branch) with the SDL3 + SDL_Renderer backends as the `imgui` target.
macro(yk_fetch_imgui)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 9b4eb24cee2071e61dc1f9ef3e5228097cdde720) # v1.92.9-docking
    yk_prefer_local_source(imgui imgui imgui.h)
    FetchContent_MakeAvailable(imgui)
    add_library(imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")
    target_include_directories(imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
    target_link_libraries(imgui PUBLIC SDL3::SDL3)
    target_compile_features(imgui PUBLIC cxx_std_20)
    yk_sanitize(imgui)
endmacro()
