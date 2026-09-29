cmake_minimum_required(VERSION 3.25)
project(YKEngine VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Windows builds require a 64-bit toolchain")
endif()
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
endif()
# A game can add the engine as a subdirectory (docs/BUILDING.md, "Game modules"): it then gets the
# libraries and programs, not the engine's own tests, installation or formatting targets.
if(NOT PROJECT_IS_TOP_LEVEL)
    set(BUILD_TESTING OFF)
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
include(YkGame)
yk_sanitize(box2d)

add_library(yk_engine STATIC src/core/Log.cpp src/core/Time.cpp src/core/Color.cpp src/core/Json.cpp
    src/core/FileIO.cpp src/audio/Tone.cpp src/animation/Animator.cpp src/input/Input.cpp
    src/input/InputMap.cpp src/input/ActionInput.cpp
    src/scene/EntityId.cpp src/scene/Property.cpp src/scene/Registry.cpp src/scene/Entity.cpp
    src/scene/Scene.cpp src/scene/SceneSerializer.cpp src/scene/RegistryDocs.cpp src/animation/AnimationSet.cpp
    src/animation/AnimationController.cpp
    src/assets/Project.cpp src/assets/TextureMeta.cpp src/assets/AssetSource.cpp src/assets/Zip.cpp
    src/assets/Export.cpp src/components/Components.cpp
    src/components/Effects.cpp
    src/runtime/Blackboard.cpp src/runtime/EventBus.cpp src/runtime/GameRuntime.cpp
    src/runtime/PhysicsBinding.cpp src/assets/Validation.cpp
    src/physics/World.cpp src/physics/Queries.cpp src/physics/Joints.cpp)
add_library(yk::engine ALIAS yk_engine)
target_include_directories(yk_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(yk_engine PRIVATE box2d)
target_compile_definitions(yk_engine PRIVATE YK_VERSION="${PROJECT_VERSION}")
yk_target(yk_engine)

# Reusable gameplay components (plates, doors, controllers...). Depends only on the engine.
add_library(yk_gameplay STATIC src/gameplay/Signals.cpp src/gameplay/Controller.cpp
    src/gameplay/Mechanisms.cpp src/gameplay/Interactions.cpp src/gameplay/Level.cpp
    src/gameplay/Registration.cpp)
add_library(yk::gameplay ALIAS yk_gameplay)
target_link_libraries(yk_gameplay PUBLIC yk_engine)
yk_target(yk_gameplay)

# The `yk` command line: validate, format, inspect and export projects. Headless. The program is
# a function (yk::host::runTool) so a game with components of its own can build its own copy.
add_library(yk_tool_host STATIC tools/yk/Tool.cpp)
add_library(yk::tool_host ALIAS yk_tool_host)
target_link_libraries(yk_tool_host PUBLIC yk::gameplay)
yk_target(yk_tool_host)
add_executable(yk tools/yk/main.cpp)
target_link_libraries(yk PRIVATE yk_tool_host)
yk_target(yk)

# Editor core: the open document with undo, picking and gizmo logic, project files and play sessions.
# No window, no UI toolkit: everything the editor does to a scene is tested headlessly through this.
add_library(yk_editor_core STATIC editor/core/EditorDocument.cpp editor/core/EditorGeometry.cpp
    editor/core/SceneInteraction.cpp editor/core/EditorProject.cpp editor/core/PlaySession.cpp
    editor/core/ConsoleLog.cpp editor/core/WorkbenchLayout.cpp)
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

    # The standalone player: runs any project. The program is a function (yk::host::runPlayer) so a
    # game with components of its own can build its own copy.
    add_library(yk_player_host STATIC player/Player.cpp player/KeyScript.cpp)
    add_library(yk::player_host ALIAS yk_player_host)
    target_include_directories(yk_player_host PRIVATE player)
    target_link_libraries(yk_player_host PUBLIC yk::gameplay PRIVATE SDL3::SDL3)
    yk_target(yk_player_host)
    add_executable(yk_player player/main.cpp)
    target_link_libraries(yk_player PRIVATE yk_player_host)
    yk_target(yk_player)

    # The editor: Dear ImGui panels over the editor core, the engine's renderer and a play session.
    yk_fetch_imgui()
    include(YkEmbed)
    yk_embed_files("${CMAKE_CURRENT_BINARY_DIR}/generated/EditorFonts.cpp" NAMESPACE yk::editor::ui::fontdata FILES
        interRegular=${CMAKE_CURRENT_SOURCE_DIR}/editor/resources/fonts/Inter-Regular.ttf
        interSemiBold=${CMAKE_CURRENT_SOURCE_DIR}/editor/resources/fonts/Inter-SemiBold.ttf
        jetBrainsMono=${CMAKE_CURRENT_SOURCE_DIR}/editor/resources/fonts/JetBrainsMono-Regular.ttf
        codicon=${CMAKE_CURRENT_SOURCE_DIR}/editor/resources/fonts/codicon.ttf)
    add_library(yk_editor_ui STATIC "${CMAKE_CURRENT_BINARY_DIR}/generated/EditorFonts.cpp" editor/ui/Theme.cpp
        editor/ui/UiCommon.cpp editor/ui/EditorState.cpp editor/ui/Shell.cpp editor/ui/Workbench.cpp
        editor/ui/Menus.cpp editor/ui/Welcome.cpp editor/ui/ProblemsPanel.cpp editor/ui/ProfilerPanel.cpp
        editor/ui/SideViews.cpp editor/ui/ProjectSettings.cpp editor/ui/AssetInspector.cpp
        editor/ui/HierarchyPanel.cpp editor/ui/InspectorPanel.cpp editor/ui/SceneViewPanel.cpp
        editor/ui/GameViewPanel.cpp editor/ui/AssetsPanel.cpp editor/ui/ConsolePanel.cpp
        editor/ui/Dialogs.cpp editor/ui/EditorApp.cpp editor/ui/EditorDriver.cpp)
    add_library(yk::editor_ui ALIAS yk_editor_ui)
    target_link_libraries(yk_editor_ui PUBLIC yk::editor_core imgui SDL3::SDL3)
    target_include_directories(yk_editor_ui PUBLIC editor)
    target_compile_definitions(yk_editor_ui PRIVATE YK_VERSION="${PROJECT_VERSION}"
        YK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    yk_target(yk_editor_ui)
    add_library(yk_editor_host STATIC editor/Host.cpp)
    add_library(yk::editor_host ALIAS yk_editor_host)
    target_link_libraries(yk_editor_host PUBLIC yk::editor_ui)
    yk_target(yk_editor_host)
    add_executable(yk_editor editor/main.cpp)
    target_link_libraries(yk_editor PRIVATE yk_editor_host)
    yk_target(yk_editor)
endif()
target_compile_definitions(yk_engine PUBLIC YK_HAS_RUNTIME=$<BOOL:${YK_RUNTIME}>)

if(BUILD_TESTING)
    foreach(suite core physics json animation scene game_runtime audio input assets effects)
        yk_add_test(${suite} tests/unit/${suite}_tests.cpp LIBS yk::engine TIMEOUT 30)
    endforeach()
    yk_add_test(gameplay tests/unit/gameplay_tests.cpp LIBS yk::gameplay TIMEOUT 120)
    yk_add_test(export tests/unit/export_tests.cpp LIBS yk::gameplay TIMEOUT 60)
    # The demo game is a separate project (YK-DemoGame) that only consumes the engine; this plays it.
    yk_add_test(demo tests/unit/demo_tests.cpp LIBS yk::gameplay TIMEOUT 120
        DEFINITIONS YK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    yk_add_test(editor_core tests/unit/editor_core_tests.cpp LIBS yk::editor_core TIMEOUT 120
        DEFINITIONS YK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    # An example game module (one component) and the programs built for it with yk_add_game_hosts:
    # proves a game with C++ of its own gets its own player, editor and command line.
    add_library(yk_spinner_module STATIC tests/game_module/SpinnerModule.cpp)
    target_link_libraries(yk_spinner_module PUBLIC yk::gameplay)
    target_include_directories(yk_spinner_module PUBLIC tests/game_module)
    yk_target(yk_spinner_module)
    yk_add_game_hosts(spinner_game MODULE yk_spinner_module HEADER SpinnerModule.hpp
        REGISTER spinner::registerComponents)
    set(YK_SPINNER_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/tests/game_module/project")
    # The game's own command line knows the component; the stock one does not.
    add_test(NAME game_module_tool COMMAND spinner_game_tool validate ${YK_SPINNER_PROJECT})
    add_test(NAME game_module_tool_stock COMMAND yk validate ${YK_SPINNER_PROJECT})
    set_tests_properties(game_module_tool_stock PROPERTIES WILL_FAIL TRUE)
    if(YK_RUNTIME)
        yk_add_test(game_module tests/game_module/game_module_tests.cpp
            LIBS yk_spinner_module yk_player_host TIMEOUT 60
            DEFINITIONS SPINNER_PROJECT="${YK_SPINNER_PROJECT}"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software;SDL_AUDIODRIVER=dummy")
        # Exported with the game's own tool and player, the game runs on its own.
        add_test(NAME game_module_export
            COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:spinner_game_tool>
                -DPLAYER=$<TARGET_FILE:spinner_game_player> -DPROJECT=${YK_SPINNER_PROJECT}
                -DWORK=${CMAKE_BINARY_DIR}/game-module-export
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/game_module/check_export.cmake)
        set_tests_properties(game_module_export PROPERTIES TIMEOUT 120)
    else()
        yk_add_test(game_module tests/game_module/game_module_tests.cpp LIBS yk_spinner_module
            TIMEOUT 60 DEFINITIONS SPINNER_PROJECT="${YK_SPINNER_PROJECT}")
    endif()
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
        # Sanitizer builds run this ten times slower: the budgets it checks and its timeout grow.
        set(yk_stress_slowdown 1)
        if(YK_SANITIZERS)
            set(yk_stress_slowdown 10)
        endif()
        yk_add_test(stress tests/integration/stress_tests.cpp LIBS yk::gameplay SDL3::SDL3
            DEFINITIONS YK_STRESS_SLOWDOWN=${yk_stress_slowdown}
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 600)
        yk_add_test(render_modes tests/integration/render_modes_tests.cpp LIBS yk::engine SDL3::SDL3
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software" TIMEOUT 30)
        # The editor, driven through its real UI by scripts (see editor/ui/EditorDriver.hpp).
        file(GLOB YK_EDITOR_SCRIPTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/editor/*.ykscript")
        foreach(script ${YK_EDITOR_SCRIPTS})
            get_filename_component(script_name "${script}" NAME_WE)
            # Scripts named demo_* start with the demo game (YK-DemoGame) open; the others start
            # from the welcome screen.
            set(script_project "")
            if(script_name MATCHES "^demo_")
                set(script_project -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/YK-DemoGame)
            endif()
            add_test(NAME editor_${script_name}
                COMMAND ${CMAKE_COMMAND} -DEDITOR=$<TARGET_FILE:yk_editor> -DSCRIPT=${script}
                    -DWORK=${CMAKE_BINARY_DIR}/editor-tests/${script_name} ${script_project}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor/run_script.cmake)
            set_tests_properties(editor_${script_name} PROPERTIES TIMEOUT 300)
        endforeach()
        # The example game's own editor, built with yk_add_game_hosts, shows and edits its component.
        add_test(NAME editor_game_module
            COMMAND ${CMAKE_COMMAND} -DEDITOR=$<TARGET_FILE:spinner_game_editor>
                -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/game_module/editor.ykscript
                -DWORK=${CMAKE_BINARY_DIR}/editor-tests/game_module -DPROJECT=${YK_SPINNER_PROJECT}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor/run_script.cmake)
        set_tests_properties(editor_game_module PROPERTIES TIMEOUT 300)
        # An editor started with a project that is not there reports it and stays open.
        add_test(NAME editor_bad_project
            COMMAND ${CMAKE_COMMAND} -DEDITOR=$<TARGET_FILE:yk_editor>
                -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/editor/startup/bad_project.ykscript
                -DWORK=${CMAKE_BINARY_DIR}/editor-tests/bad_project
                -DRAW_PROJECT=${CMAKE_BINARY_DIR}/editor-tests/bad_project/no-such-project
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor/run_script.cmake)
        set_tests_properties(editor_bad_project PROPERTIES TIMEOUT 120)
        # The window layout survives a restart: one editor process rearranges it, the next one
        # starts from what was saved.
        foreach(part write read)
            set(persist_args -DEDITOR=$<TARGET_FILE:yk_editor>
                -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/editor/persist/layout_${part}.ykscript
                -DWORK=${CMAKE_BINARY_DIR}/editor-tests/layout
                -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/YK-DemoGame -DPERSIST=ON)
            if(part STREQUAL "read")
                list(APPEND persist_args -DKEEP=ON)
            endif()
            add_test(NAME editor_layout_${part}
                COMMAND ${CMAKE_COMMAND} ${persist_args}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor/run_script.cmake)
            set_tests_properties(editor_layout_${part} PROPERTIES TIMEOUT 300)
        endforeach()
        set_tests_properties(editor_layout_write PROPERTIES FIXTURES_SETUP editor_layout)
        set_tests_properties(editor_layout_read PROPERTIES FIXTURES_REQUIRED editor_layout)
        # The installation and its CPack archive: contents, no private dependencies, and the
        # installed programs work where they were put.
        add_test(NAME install
            COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWORK=${CMAKE_BINARY_DIR}/install-test -DCONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/install/check_install.cmake)
        set_tests_properties(install PROPERTIES TIMEOUT 300)
    endif()
endif()

# Installation: the editor, the player, the command line, the demo project, documentation and
# license notices.
if(YK_RUNTIME AND PROJECT_IS_TOP_LEVEL)
    include(GNUInstallDirs)
    install(TARGETS yk_editor yk_player yk RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY YK-DemoGame DESTINATION ${CMAKE_INSTALL_DATADIR}/yk-engine
        PATTERN "tools" EXCLUDE PATTERN "__pycache__" EXCLUDE)
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
if(YK_CLANG_FORMAT AND PROJECT_IS_TOP_LEVEL)
    file(GLOB_RECURSE YK_FORMAT_FILES CONFIGURE_DEPENDS include/*.hpp src/*.hpp src/*.cpp tests/*.cpp
        tests/*.hpp tools/*.cpp player/*.hpp player/*.cpp editor/*.hpp editor/*.cpp)
    add_custom_target(format COMMAND ${YK_CLANG_FORMAT} -i ${YK_FORMAT_FILES} VERBATIM)
    add_custom_target(format-check COMMAND ${YK_CLANG_FORMAT} --dry-run --Werror ${YK_FORMAT_FILES} VERBATIM)
endif()
