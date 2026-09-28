cmake_minimum_required(VERSION 3.25)
project(YKEngine VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows builds require a 64-bit toolchain")
endif()
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
endif()
include(CTest)
include(FetchContent)
option(YK_RUNTIME "Build SDL window, input and renderer support" ON)
option(YK_SANITIZERS "Enable address/undefined sanitizers on project targets" OFF)
set(BUILD_SHARED_LIBS OFF)
if(YK_SANITIZERS AND MSVC)
    # Remove incompatible debug checks before dependency targets inherit these flags.
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()
FetchContent_Declare(box2d
    URL https://github.com/erincatto/box2d/archive/refs/tags/v3.1.1.tar.gz
    URL_HASH SHA256=fb6ef914b50f4312d7d921a600eabc12318bb3c55a0b8c0b90608fa4488ef2e4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(box2d)

function(yk_sanitize target)
    if(NOT YK_SANITIZERS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address /Zi)
        target_link_options(${target} PUBLIC /INCREMENTAL:NO)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PUBLIC -fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "YK_SANITIZERS requires MSVC, Clang or GCC")
    endif()
endfunction()
yk_sanitize(box2d)

function(yk_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
    endif()
    yk_sanitize(${target})
endfunction()

add_library(yk_engine STATIC src/core/Log.cpp src/core/Time.cpp src/core/GameFoundation.cpp src/input/Input.cpp
    src/physics/World.cpp src/physics/Queries.cpp src/physics/Joints.cpp)
add_library(yk::engine ALIAS yk_engine)
target_include_directories(yk_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(yk_engine PRIVATE box2d)
yk_target(yk_engine)

if(YK_RUNTIME)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://www.libsdl.org/release/SDL3-3.2.28.tar.gz
        URL_HASH SHA256=1330671214d146f8aeb1ed399fc3e081873cdb38b5189d1f8bb6ab15bbc04211
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(SDL3)
    target_sources(yk_engine PRIVATE src/platform/Application.cpp src/graphics/Renderer.cpp
        src/graphics/PhysicsDebug.cpp)
    target_link_libraries(yk_engine PRIVATE SDL3::SDL3)

    add_executable(elemental_escape game/main.cpp)
    target_link_libraries(elemental_escape PRIVATE yk::engine SDL3::SDL3)
    yk_target(elemental_escape)
    set(YK_GENERATED_PLAYER_PNG "${CMAKE_CURRENT_BINARY_DIR}/generated-assets/sprites/player.png")
    add_custom_command(OUTPUT "${YK_GENERATED_PLAYER_PNG}"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_assets.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/player.png.base64"
                "${YK_GENERATED_PLAYER_PNG}"
        DEPENDS assets/sprites/player.png.base64 scripts/generate_assets.py
        COMMENT "Generating repository-safe PNG assets" VERBATIM)
    add_custom_target(elemental_escape_assets DEPENDS "${YK_GENERATED_PLAYER_PNG}")
    add_dependencies(elemental_escape elemental_escape_assets)
    add_custom_command(TARGET elemental_escape POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:elemental_escape>/assets/levels"
                "$<TARGET_FILE_DIR:elemental_escape>/assets/sprites"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/levels"
                "$<TARGET_FILE_DIR:elemental_escape>/assets/levels"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites/player.anim"
                "${YK_GENERATED_PLAYER_PNG}"
                "$<TARGET_FILE_DIR:elemental_escape>/assets/sprites")
    install(TARGETS elemental_escape RUNTIME DESTINATION . BUNDLE DESTINATION .)
    install(DIRECTORY assets/levels DESTINATION assets)
    install(FILES assets/sprites/player.anim "${YK_GENERATED_PLAYER_PNG}"
        DESTINATION assets/sprites)
    install(FILES README.md THIRD_PARTY.md DESTINATION .)
endif()
target_compile_definitions(yk_engine PUBLIC YK_HAS_RUNTIME=$<BOOL:${YK_RUNTIME}>)

if(BUILD_TESTING)
    foreach(suite core physics)
        add_executable(yk_${suite}_tests tests/unit/${suite}_tests.cpp)
        target_link_libraries(yk_${suite}_tests PRIVATE yk::engine)
        yk_target(yk_${suite}_tests)
        add_test(NAME ${suite} COMMAND yk_${suite}_tests)
        set_tests_properties(${suite} PROPERTIES TIMEOUT 30)
    endforeach()
    add_executable(yk_game_foundation_tests tests/unit/game_foundation_tests.cpp)
    target_link_libraries(yk_game_foundation_tests PRIVATE yk::engine)
    yk_target(yk_game_foundation_tests)
    add_test(NAME game_foundation COMMAND yk_game_foundation_tests)
    if(YK_RUNTIME)
        add_executable(yk_runtime_tests tests/integration/runtime_tests.cpp)
        target_link_libraries(yk_runtime_tests PRIVATE yk::engine SDL3::SDL3)
        yk_target(yk_runtime_tests)
        add_test(NAME runtime COMMAND yk_runtime_tests)
        set_tests_properties(runtime PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 30)
    endif()
endif()

find_program(YK_CLANG_FORMAT NAMES clang-format)
if(YK_CLANG_FORMAT)
    file(GLOB_RECURSE YK_FORMAT_FILES CONFIGURE_DEPENDS include/*.hpp src/*.hpp src/*.cpp game/*.cpp
        tests/*.cpp)
    add_custom_target(format COMMAND ${YK_CLANG_FORMAT} -i ${YK_FORMAT_FILES} VERBATIM)
    add_custom_target(format-check COMMAND ${YK_CLANG_FORMAT} --dry-run --Werror ${YK_FORMAT_FILES} VERBATIM)
endif()
