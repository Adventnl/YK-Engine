yk_add_test(blackthorn_frontend tests/frontend_tests.cpp LIBS yk::gameplay TIMEOUT 60
    DEFINITIONS YK_EXPLORATION_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
yk_add_test(blackthorn_chapter1 tests/exploration_tests.cpp LIBS yk::gameplay TIMEOUT 60
    DEFINITIONS YK_EXPLORATION_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
yk_add_test(blackthorn_chapter2 tests/castle_level2_tests.cpp LIBS yk::gameplay TIMEOUT 120
    DEFINITIONS YK_EXPLORATION_PROJECT_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(YK_RUNTIME)
    add_test(NAME blackthorn_search_render COMMAND yk_player
        ${CMAKE_CURRENT_SOURCE_DIR} --scene scenes/courtyard.ykscene --fixed --no-audio --frames 520
        --keys "S@0-56,D@58-149,S@151-181,A@183-205,S@207-283,A@285-359,E@361-363"
        --capture ${CMAKE_BINARY_DIR}/blackthorn-search.bmp)
    # This scripted playback renders 520 display-resolution frames in software.
    # Avoid CPU contention with simultaneous editor and packaging captures.
    set_tests_properties(blackthorn_search_render PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software;SDL_AUDIODRIVER=dummy")
    add_test(NAME editor_blackthorn COMMAND ${CMAKE_COMMAND}
        -DEDITOR=$<TARGET_FILE:yk_editor> -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_project.ykscript
        -DWORK=${CMAKE_BINARY_DIR}/editor-tests/blackthorn -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${PROJECT_SOURCE_DIR}/tests/editor/run_script.cmake)
    set_tests_properties(editor_blackthorn PROPERTIES TIMEOUT 600)
endif()
