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
option(YK_RUNTIME "Build SDL window, input and renderer support" ON)
option(YK_SANITIZERS "Enable address/undefined sanitizers on project targets" OFF)
set(BUILD_SHARED_LIBS OFF)
if(YK_SANITIZERS AND MSVC)
    # Remove incompatible debug checks before dependency targets inherit these flags.
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(YkTargets)
include(YkDependencies)
yk_sanitize(box2d)

add_library(yk_engine STATIC src/core/Log.cpp src/core/Time.cpp src/core/Color.cpp src/core/Json.cpp
    src/core/FileIO.cpp src/animation/Animator.cpp src/input/Input.cpp
    src/scene/EntityId.cpp src/scene/Property.cpp src/scene/Registry.cpp src/scene/Entity.cpp
    src/scene/Scene.cpp src/scene/SceneSerializer.cpp src/animation/AnimationSet.cpp
    src/assets/Project.cpp src/assets/AssetSource.cpp src/components/Components.cpp
    src/runtime/Blackboard.cpp src/runtime/EventBus.cpp src/runtime/GameRuntime.cpp
    src/runtime/PhysicsBinding.cpp
    src/physics/World.cpp src/physics/Queries.cpp src/physics/Joints.cpp)
add_library(yk::engine ALIAS yk_engine)
target_include_directories(yk_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(yk_engine PRIVATE box2d)
yk_target(yk_engine)

if(YK_RUNTIME)
    yk_fetch_sdl3()
    target_sources(yk_engine PRIVATE src/platform/Application.cpp src/graphics/Renderer.cpp
        src/graphics/PhysicsDebug.cpp)
    target_link_libraries(yk_engine PRIVATE SDL3::SDL3)

endif()
target_compile_definitions(yk_engine PUBLIC YK_HAS_RUNTIME=$<BOOL:${YK_RUNTIME}>)

if(BUILD_TESTING)
    foreach(suite core physics json animation scene game_runtime)
        yk_add_test(${suite} tests/unit/${suite}_tests.cpp LIBS yk::engine TIMEOUT 30)
    endforeach()
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
    file(GLOB_RECURSE YK_FORMAT_FILES CONFIGURE_DEPENDS include/*.hpp src/*.hpp src/*.cpp tests/*.cpp
        tests/*.hpp)
    add_custom_target(format COMMAND ${YK_CLANG_FORMAT} -i ${YK_FORMAT_FILES} VERBATIM)
    add_custom_target(format-check COMMAND ${YK_CLANG_FORMAT} --dry-run --Werror ${YK_FORMAT_FILES} VERBATIM)
endif()
