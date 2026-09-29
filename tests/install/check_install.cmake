# Installs the build into a scratch prefix, packs it with CPack, and checks what a user would get:
# the editor, the player, the sample project, docs and license notices, and none of the private
# dependencies' headers or libraries. Then runs the installed player and editor to prove the
# installation works where it was put.
#
#   cmake -DBUILD_DIR=<build> -DSOURCE_DIR=<repo> -DWORK=<scratch dir> [-DCONFIG=<config>] -P check_install.cmake
if(NOT BUILD_DIR OR NOT SOURCE_DIR OR NOT WORK)
    message(FATAL_ERROR "BUILD_DIR, SOURCE_DIR and WORK are required")
endif()
set(prefix "${WORK}/prefix")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(install_config "")
set(cpack_config "")
if(CONFIG)
    set(install_config --config "${CONFIG}")
    set(cpack_config -C "${CONFIG}")
endif()

function(run description)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 240)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed (${result}):\n${output}\n${errors}")
    endif()
endfunction()

run("cmake --install" "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}" ${install_config})

set(exe "")
if(WIN32)
    set(exe ".exe")
endif()
set(sample "${prefix}/share/yk-engine/YK-DemoGame")
set(docs "${prefix}/share/doc/YKEngine")
foreach(required
        "${prefix}/bin/yk_editor${exe}" "${prefix}/bin/yk_player${exe}" "${prefix}/bin/yk${exe}"
        "${sample}/project.ykproj" "${sample}/scenes/level01.ykscene"
        "${docs}/README.md" "${docs}/THIRD_PARTY.md" "${docs}/editor.md"
        "${docs}/licenses/Box2D-MIT.txt" "${docs}/licenses/SDL3.txt" "${docs}/licenses/DearImGui-MIT.txt"
        "${docs}/licenses/ProggyForever-MIT.txt" "${docs}/licenses/stb_image-PD.txt"
        "${docs}/licenses/Inter-OFL-1.1.txt" "${docs}/licenses/JetBrainsMono-OFL-1.1.txt"
        "${docs}/licenses/Codicons-CC-BY-4.0.txt")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "The installation lacks ${required}")
    endif()
endforeach()

# Box2D, SDL and Dear ImGui are linked statically and stay private: nothing of theirs may be installed
# besides the notices, so the prefix holds exactly the programs and shared data.
file(GLOB top RELATIVE "${prefix}" "${prefix}/*")
list(SORT top)
if(NOT top STREQUAL "bin;share")
    message(FATAL_ERROR "The installation should contain only bin and share, found: ${top}")
endif()
file(GLOB programs RELATIVE "${prefix}/bin" "${prefix}/bin/*")
list(SORT programs)
if(NOT programs STREQUAL "yk${exe};yk_editor${exe};yk_player${exe}")
    message(FATAL_ERROR "Unexpected programs installed: ${programs}")
endif()
file(GLOB_RECURSE headers "${prefix}/*.h" "${prefix}/*.hpp")
file(GLOB_RECURSE libraries "${prefix}/*.a" "${prefix}/*.lib" "${prefix}/*.so*" "${prefix}/*.dylib")
if(headers OR libraries)
    message(FATAL_ERROR "The installation contains headers or libraries: ${headers} ${libraries}")
endif()

# The installed command line validates, and the installed player runs, the installed demo project.
set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_RENDER_DRIVER} software)
set(ENV{SDL_AUDIODRIVER} dummy)
run("the installed yk validate" "${prefix}/bin/yk${exe}" validate "${sample}")
run("the installed player" "${prefix}/bin/yk_player${exe}" --frames 90 --fixed --no-audio
    --capture "${WORK}/player.bmp" "${sample}")
if(NOT EXISTS "${WORK}/player.bmp")
    message(FATAL_ERROR "The installed player did not capture a frame")
endif()

# The installed editor finds the installed sample project from its welcome screen and plays it.
set(ENV{SAMPLE_DIR} "${sample}")
run("the installed editor" "${CMAKE_COMMAND}" -DEDITOR=${prefix}/bin/yk_editor${exe}
    -DSCRIPT=${SOURCE_DIR}/tests/install/open_sample.ykscript -DWORK=${WORK}/editor
    -P ${SOURCE_DIR}/tests/editor/run_script.cmake)

# CPack produces an archive of the same installation.
run("cpack" "${CMAKE_CPACK_COMMAND}" --config "${BUILD_DIR}/CPackConfig.cmake" -B "${WORK}/package"
    ${cpack_config})
file(GLOB archives "${WORK}/package/YKEngine-*.tar.gz" "${WORK}/package/YKEngine-*.zip")
if(NOT archives)
    message(FATAL_ERROR "cpack made no YKEngine archive in ${WORK}/package")
endif()
message(STATUS "Install check passed; archive: ${archives}")
