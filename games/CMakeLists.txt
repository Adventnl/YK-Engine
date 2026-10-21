# Independent game projects. No files from this directory enter the engine libraries.
if(NOT BUILD_TESTING)
    return()
endif()
add_subdirectory(brothers-run)
add_subdirectory(invisible-castle)

if(YK_RUNTIME)
    foreach(part write read)
        set(persist_args -DEDITOR=$<TARGET_FILE:yk_editor>
            -DSCRIPT=${PROJECT_SOURCE_DIR}/tests/editor/persist/layout_${part}.ykscript
            -DWORK=${CMAKE_BINARY_DIR}/editor-tests/layout
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/invisible-castle -DPERSIST=ON)
        if(part STREQUAL "read")
            list(APPEND persist_args -DKEEP=ON)
        endif()
        add_test(NAME editor_layout_${part} COMMAND ${CMAKE_COMMAND} ${persist_args}
            -P ${PROJECT_SOURCE_DIR}/tests/editor/run_script.cmake)
        set_tests_properties(editor_layout_${part} PROPERTIES TIMEOUT 600)
    endforeach()
    set_tests_properties(editor_layout_write PROPERTIES FIXTURES_SETUP editor_layout)
    set_tests_properties(editor_layout_read PROPERTIES FIXTURES_REQUIRED editor_layout)
    add_test(NAME external_project COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:yk> -DPLAYER=$<TARGET_FILE:yk_player>
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/invisible-castle -DWORK=${CMAKE_BINARY_DIR}/external-project-test
        -P ${PROJECT_SOURCE_DIR}/tests/project/check_external.cmake)
    set_tests_properties(external_project PROPERTIES TIMEOUT 600)
    if(NOT WIN32)
        add_test(NAME macos_bundle COMMAND ${CMAKE_COMMAND}
            -DEDITOR=$<TARGET_FILE:yk_editor> -DPLAYER=$<TARGET_FILE:yk_player>
            -DTOOL=$<TARGET_FILE:yk> -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DVERSION=${PROJECT_VERSION} -DWORK=${CMAKE_BINARY_DIR}/macos-bundle-test
            -P ${PROJECT_SOURCE_DIR}/tests/macos/check_bundle.cmake)
        set_tests_properties(macos_bundle PROPERTIES TIMEOUT 600)
        add_test(NAME diagnostics_crash COMMAND ${CMAKE_COMMAND}
            -DPLAYER=$<TARGET_FILE:yk_player> -DEDITOR=$<TARGET_FILE:yk_editor>
            -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/invisible-castle
            -DSCRIPT=${PROJECT_SOURCE_DIR}/tests/diagnostics/after_crash.ykscript
            -DWORK=${CMAKE_BINARY_DIR}/diagnostics-test
            -P ${PROJECT_SOURCE_DIR}/tests/diagnostics/check_crash.cmake)
        set_tests_properties(diagnostics_crash PROPERTIES TIMEOUT 240)
    endif()
    add_test(NAME install COMMAND ${CMAKE_COMMAND}
        -DBUILD_DIR=${CMAKE_BINARY_DIR} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DWORK=${CMAKE_BINARY_DIR}/install-test -DCONFIG=$<CONFIG>
        -P ${PROJECT_SOURCE_DIR}/tests/install/check_install.cmake)
    set_tests_properties(install PROPERTIES TIMEOUT 600)
endif()
