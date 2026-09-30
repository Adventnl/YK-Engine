# A game that lives outside the engine: created with `yk new` and copied from the demo into folders
# that have nothing to do with the repository or the build tree (a system temp folder, a path with a
# space and an umlaut), then validated, run by the player from yet another working directory,
# exported, and the export run from a fourth. Nothing may depend on where the engine was built.
#
#   cmake -DTOOL=<yk> -DPLAYER=<yk_player> -DDEMO=<a project folder> -DWORK=<scratch dir> -P check_external.cmake
foreach(variable TOOL PLAYER DEMO WORK)
    if(NOT ${variable})
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()

# The system's temp folder: the repository and the build tree are both somewhere else.
set(temp "$ENV{TMPDIR}")
if(NOT temp)
    set(temp "$ENV{TEMP}")
endif()
if(NOT temp)
    set(temp "/tmp")
endif()
file(REAL_PATH "${temp}" temp)
string(RANDOM LENGTH 8 ALPHABET "abcdefghijklmnopqrstuvwxyz" suffix)
set(root "${temp}/yk external test ${suffix}")
if(WIN32)
    set(folder_name "Games Elsewhere")
else()
    set(folder_name "Spiele für Größe")
endif()
set(base "${root}/${folder_name}")
file(REMOVE_RECURSE "${WORK}" "${root}")
file(MAKE_DIRECTORY "${WORK}" "${base}" "${root}/cwd one" "${root}/cwd two")
set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_RENDER_DRIVER} software)
set(ENV{SDL_AUDIODRIVER} dummy)
set(ENV{YK_LOG_DIR} "${WORK}/logs")
set(ENV{YK_NO_DIALOGS} 1)

function(run description directory)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 540)
    if(NOT result EQUAL 0)
        file(REMOVE_RECURSE "${root}")
        message(FATAL_ERROR "${description} failed (${result}):\n${output}\n${errors}")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
endfunction()

# 1. A new project, made by the command line.
run("yk new" "${root}/cwd one" "${TOOL}" new "${base}/My Game" --name "Elsewhere")
foreach(required "${base}/My Game/project.ykproj" "${base}/My Game/scenes/main.ykscene")
    if(NOT EXISTS "${required}")
        file(REMOVE_RECURSE "${root}")
        message(FATAL_ERROR "yk new did not create ${required}")
    endif()
endforeach()
run("yk validate (new project)" "${root}/cwd two" "${TOOL}" validate "${base}/My Game")
run("yk info (new project)" "${root}/cwd two" "${TOOL}" info "${base}/My Game")
string(FIND "${last_output}" "Elsewhere" found)
if(found EQUAL -1)
    file(REMOVE_RECURSE "${root}")
    message(FATAL_ERROR "yk info does not name the project:\n${last_output}")
endif()
run("the player on the new project" "${root}/cwd two" "${PLAYER}" "${base}/My Game" --frames 30
    --fixed --no-audio --capture "${root}/new.bmp")
if(NOT EXISTS "${root}/new.bmp")
    file(REMOVE_RECURSE "${root}")
    message(FATAL_ERROR "The player drew nothing for the new project")
endif()

# 2. A copy of the demo game in a foreign place is a complete project of its own.
file(COPY "${DEMO}/" DESTINATION "${base}/Demo Copy")
run("yk validate (demo copy)" "${root}/cwd one" "${TOOL}" validate "${base}/Demo Copy")
run("the player on the demo copy" "${root}/cwd two" "${PLAYER}" "${base}/Demo Copy" --frames 60
    --fixed --no-audio --capture "${root}/demo.bmp")
if(NOT EXISTS "${root}/demo.bmp")
    file(REMOVE_RECURSE "${root}")
    message(FATAL_ERROR "The player drew nothing for the demo copy")
endif()

# 3. Export it (the tool finds the player beside itself) and run the result from another folder.
if(WIN32)
    set(target windows)
    set(exe ".exe")
elseif(APPLE)
    set(target macos)
else()
    set(target linux)
endif()
run("yk export" "${root}/cwd one" "${TOOL}" export "${base}/Demo Copy" --target ${target}
    --out "${base}/dist" --zip)
if(target STREQUAL "macos")
    set(game "${base}/dist/Cinder Vale.app/Contents/MacOS/CinderVale")
else()
    set(game "${base}/dist/Cinder-Vale-${target}/CinderVale${exe}")
endif()
if(NOT EXISTS "${game}")
    file(REMOVE_RECURSE "${root}")
    message(FATAL_ERROR "The exported game is missing: ${game}")
endif()
run("the exported game" "${root}/cwd two" "${game}" --frames 60 --fixed --no-audio)
# It logged where the system keeps logs (here redirected), by the game's name.
file(GLOB game_logs "${WORK}/logs/player.log")
if(NOT game_logs)
    file(REMOVE_RECURSE "${root}")
    message(FATAL_ERROR "The exported game wrote no log in ${WORK}/logs")
endif()

file(REMOVE_RECURSE "${root}")
message(STATUS "External project check passed")
