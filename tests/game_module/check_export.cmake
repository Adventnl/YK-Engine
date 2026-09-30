# Exports the example game with the game's own command line and player (docs/BUILDING.md, "Game
# modules"), checks what a player would get, and runs the exported game: the exported program is a
# copy of the game's player, so it knows the game's component.
#
#   cmake -DTOOL=<spinner_game_tool> -DPLAYER=<spinner_game_player> -DPROJECT=<dir> -DWORK=<scratch dir> -P check_export.cmake
if(NOT TOOL OR NOT PLAYER OR NOT PROJECT OR NOT WORK)
    message(FATAL_ERROR "TOOL, PLAYER, PROJECT and WORK are required")
endif()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(run description)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 120)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed (${result}):\n${output}\n${errors}")
    endif()
endfunction()

set(ENV{YK_LOG_DIR} "${WORK}/logs") # Logs of the programs run here stay in the scratch folder.
set(exe "")
if(WIN32)
    set(target windows)
    set(exe ".exe")
elseif(APPLE)
    set(target macos)
else()
    set(target linux)
endif()
run("the game's yk export" "${TOOL}" export "${PROJECT}" --target ${target} --player "${PLAYER}"
    --out "${WORK}/exported")
if(target STREQUAL "macos")
    set(game_root "${WORK}/exported/Spinner Test.app/Contents/Resources")
    set(game "${WORK}/exported/Spinner Test.app/Contents/MacOS/SpinnerTest")
else()
    set(game_root "${WORK}/exported/Spinner-Test-${target}")
    set(game "${game_root}/SpinnerTest${exe}")
endif()
foreach(required "${game}" "${game_root}/data/project.ykproj" "${game_root}/data/scenes/main.ykscene"
        "${game_root}/README.txt" "${game_root}/licenses/SDL3.txt" "${game_root}/licenses/Box2D-MIT.txt")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "The exported game lacks ${required}")
    endif()
endforeach()
set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_RENDER_DRIVER} software)
set(ENV{SDL_AUDIODRIVER} dummy)
run("the exported game" "${game}" --frames 30 --fixed --no-audio)
