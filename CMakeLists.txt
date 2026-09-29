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
    src/core/FileIO.cpp src/audio/Tone.cpp src/animation/Animator.cpp src/input/Input.cpp
    src/input/InputMap.cpp src/input/ActionInput.cpp
    src/scene/EntityId.cpp src/scene/Property.cpp src/scene/Registry.cpp src/scene/Entity.cpp
    src/scene/Scene.cpp src/scene/SceneSerializer.cpp src/scene/RegistryDocs.cpp src/animation/AnimationSet.cpp
    src/animation/AnimationController.cpp
    src/assets/Project.cpp src/assets/TextureMeta.cpp src/assets/AssetSource.cpp src/components/Components.cpp
    src/components/Effects.cpp
    src/runtime/Blackboard.cpp src/runtime/EventBus.cpp src/runtime/GameRuntime.cpp
    src/runtime/PhysicsBinding.cpp src/assets/Validation.cpp
    src/physics/World.cpp src/physics/Queries.cpp src/physics/Joints.cpp)
add_library(yk::engine ALIAS yk_engine)
target_include_directories(yk_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(yk_engine PRIVATE box2d)
yk_target(yk_engine)

# Reusable gameplay components (plates, doors, controllers...). Depends only on the engine.
add_library(yk_gameplay STATIC src/gameplay/Signals.cpp src/gameplay/Controller.cpp
    src/gameplay/Mechanisms.cpp src/gameplay/Interactions.cpp src/gameplay/Level.cpp
    src/gameplay/Registration.cpp)
add_library(yk::gameplay ALIAS yk_gameplay)
target_link_libraries(yk_gameplay PUBLIC yk_engine)
yk_target(yk_gameplay)

# The prototype game module: game-specific rules and entity templates, kept out of the engine.
add_library(yk_prototype_game STATIC game/PrototypeGame.cpp game/Modules.cpp)
add_library(yk::prototype_game ALIAS yk_prototype_game)
target_link_libraries(yk_prototype_game PUBLIC yk_gameplay)
target_include_directories(yk_prototype_game PUBLIC game)
yk_target(yk_prototype_game)
add_executable(yk_make_prototype game/tools/make_prototype.cpp)
target_link_libraries(yk_make_prototype PRIVATE yk::prototype_game)
yk_target(yk_make_prototype)

# Editor core: the open document with undo, picking and gizmo logic, project files and play sessions.
# No window, no UI toolkit: everything the editor does to a scene is tested headlessly through this.
add_library(yk_editor_core STATIC editor/core/EditorDocument.cpp editor/core/EditorGeometry.cpp
    editor/core/SceneInteraction.cpp editor/core/EditorProject.cpp editor/core/PlaySession.cpp
    editor/core/ConsoleLog.cpp)
add_library(yk::editor_core ALIAS yk_editor_core)
target_link_libraries(yk_editor_core PUBLIC yk_gameplay)
target_include_directories(yk_editor_core PUBLIC editor)
yk_target(yk_editor_core)

if(YK_RUNTIME)
    yk_fetch_sdl3()
    target_sources(yk_engine PRIVATE src/platform/Application.cpp src/graphics/Renderer.cpp
        src/graphics/PhysicsDebug.cpp src/graphics/BitmapFont.cpp src/graphics/SceneRenderer.cpp
        src/audio/SdlAudio.cpp src/graphics/GameView.cpp)
    target_link_libraries(yk_engine PRIVATE SDL3::SDL3)

    # The standalone player: runs any project with the linked game modules.
    add_executable(yk_player player/main.cpp player/KeyScript.cpp)
    target_include_directories(yk_player PRIVATE player)
    target_link_libraries(yk_player PRIVATE yk::prototype_game SDL3::SDL3)
    yk_target(yk_player)

    # The editor: Dear ImGui panels over the editor core, the engine's renderer and a play session.
    yk_fetch_imgui()
    add_library(yk_editor_ui STATIC
        editor/ui/UiCommon.cpp editor/ui/EditorState.cpp editor/ui/Shell.cpp
        editor/ui/HierarchyPanel.cpp editor/ui/InspectorPanel.cpp editor/ui/SceneViewPanel.cpp
        editor/ui/GameViewPanel.cpp editor/ui/AssetsPanel.cpp editor/ui/ConsolePanel.cpp
        editor/ui/Dialogs.cpp editor/ui/EditorApp.cpp editor/ui/EditorDriver.cpp)
    add_library(yk::editor_ui ALIAS yk_editor_ui)
    target_link_libraries(yk_editor_ui PUBLIC yk::editor_core imgui SDL3::SDL3)
    target_include_directories(yk_editor_ui PUBLIC editor)
    target_compile_definitions(yk_editor_ui PRIVATE YK_VERSION="${PROJECT_VERSION}"
        YK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    yk_target(yk_editor_ui)
    add_executable(yk_editor editor/main.cpp)
    target_link_libraries(yk_editor PRIVATE yk::editor_ui yk::prototype_game)
    yk_target(yk_editor)
endif()
target_compile_definitions(yk_engine PUBLIC YK_HAS_RUNTIME=$<BOOL:${YK_RUNTIME}>)

if(BUILD_TESTING)
    foreach(suite core physics json animation scene game_runtime audio input assets effects)
        yk_add_test(${suite} tests/unit/${suite}_tests.cpp LIBS yk::engine TIMEOUT 30)
    endforeach()
    yk_add_test(gameplay tests/unit/gameplay_tests.cpp LIBS yk::gameplay TIMEOUT 120)
    yk_add_test(prototype tests/unit/prototype_tests.cpp LIBS yk::prototype_game TIMEOUT 120
        DEFINITIONS YK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    yk_add_test(editor_core tests/unit/editor_core_tests.cpp LIBS yk::editor_core yk::prototype_game
        TIMEOUT 120 DEFINITIONS YK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    if(YK_RUNTIME)
        add_executable(yk_runtime_tests tests/integration/runtime_tests.cpp)
        target_link_libraries(yk_runtime_tests PRIVATE yk::engine SDL3::SDL3)
        yk_target(yk_runtime_tests)
        add_test(NAME runtime COMMAND yk_runtime_tests)
        set_tests_properties(runtime PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 30)
        yk_add_test(sdl_audio tests/integration/sdl_audio_tests.cpp LIBS yk::engine SDL3::SDL3
            ENVIRONMENT "SDL_AUDIODRIVER=dummy" TIMEOUT 30)
        yk_add_test(scene_render tests/integration/scene_render_tests.cpp LIBS yk::engine SDL3::SDL3
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 30)
        yk_add_test(render_modes tests/integration/render_modes_tests.cpp LIBS yk::engine SDL3::SDL3
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 30)
        # The editor, driven through its real UI by scripts (see editor/ui/EditorDriver.hpp).
        file(GLOB YK_EDITOR_SCRIPTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/editor/*.ykscript")
        foreach(script ${YK_EDITOR_SCRIPTS})
            get_filename_component(script_name "${script}" NAME_WE)
            # Scripts named sample_* start with the Elemental Prototype project open; the others
            # start from the welcome screen.
            set(script_project "")
            if(script_name MATCHES "^sample_")
                set(script_project -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/projects/elemental-prototype)
            endif()
            add_test(NAME editor_${script_name}
                COMMAND ${CMAKE_COMMAND} -DEDITOR=$<TARGET_FILE:yk_editor> -DSCRIPT=${script}
                    -DWORK=${CMAKE_BINARY_DIR}/editor-tests/${script_name} ${script_project}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor/run_script.cmake)
            set_tests_properties(editor_${script_name} PROPERTIES TIMEOUT 300)
        endforeach()
        # The installation and its CPack archive: contents, no private dependencies, and the
        # installed programs work where they were put.
        add_test(NAME install
            COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWORK=${CMAKE_BINARY_DIR}/install-test -DCONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/install/check_install.cmake)
        set_tests_properties(install PROPERTIES TIMEOUT 300)
    endif()
endif()

# Installation: the editor, the player, the sample project, documentation and license notices.
if(YK_RUNTIME)
    include(GNUInstallDirs)
    install(TARGETS yk_editor yk_player RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY projects/elemental-prototype
        DESTINATION ${CMAKE_INSTALL_DATADIR}/yk-engine/projects)
    install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY LICENSES/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/licenses)
    install(FILES README.md THIRD_PARTY.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    set(CPACK_PACKAGE_NAME "YKEngine")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VENDOR "YK")
    set(CPACK_GENERATOR "TGZ")
    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
    endif()
    include(CPack)
endif()

find_program(YK_CLANG_FORMAT NAMES clang-format)
if(YK_CLANG_FORMAT)
    file(GLOB_RECURSE YK_FORMAT_FILES CONFIGURE_DEPENDS include/*.hpp src/*.hpp src/*.cpp tests/*.cpp
        tests/*.hpp game/*.hpp game/*.cpp player/*.hpp player/*.cpp editor/*.hpp editor/*.cpp)
    add_custom_target(format COMMAND ${YK_CLANG_FORMAT} -i ${YK_FORMAT_FILES} VERBATIM)
    add_custom_target(format-check COMMAND ${YK_CLANG_FORMAT} --dry-run --Werror ${YK_FORMAT_FILES} VERBATIM)
endif()
