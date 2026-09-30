# What a crash leaves behind, through the real programs: a log, a crash report with a stack trace,
# and (for the editor) a notice on the next start. Uses the crash-on-purpose option of both.
#
#   cmake -DPLAYER=<yk_player> -DEDITOR=<yk_editor> -DPROJECT=<dir> -DSCRIPT=<after_crash.ykscript>
#         -DWORK=<scratch dir> -P check_crash.cmake
foreach(variable PLAYER EDITOR PROJECT SCRIPT WORK)
    if(NOT ${variable})
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/shots")
set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_RENDER_DRIVER} software)
set(ENV{SDL_AUDIODRIVER} dummy)
set(ENV{YK_NO_DIALOGS} 1)

# The player crashes on purpose after a few frames.
set(ENV{YK_LOG_DIR} "${WORK}/player-logs")
execute_process(COMMAND "${PLAYER}" "${PROJECT}" --no-audio --frames 300 --debug-crash abort
    RESULT_VARIABLE result ERROR_VARIABLE errors TIMEOUT 60)
if(result EQUAL 0)
    message(FATAL_ERROR "The player should have crashed:\n${errors}")
endif()
file(GLOB reports "${WORK}/player-logs/crash-player-*.txt")
list(LENGTH reports count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "Expected one player crash report in ${WORK}/player-logs, found: ${reports}")
endif()
file(READ "${reports}" report)
foreach(expected "YK Engine crash report" "Application: " "SIGABRT" "Stack trace")
    if(NOT report MATCHES "${expected}")
        message(FATAL_ERROR "The crash report lacks '${expected}':\n${report}")
    endif()
endforeach()
file(READ "${WORK}/player-logs/player.log" log)
if(NOT log MATCHES "Loaded scene")
    message(FATAL_ERROR "The player's log lacks its start-up:\n${log}")
endif()

# The editor crashes with an invalid memory access; the log and the report are beside its settings.
unset(ENV{YK_LOG_DIR})
file(COPY "${PROJECT}" DESTINATION "${WORK}/project")
get_filename_component(project_name "${PROJECT}" NAME)
execute_process(COMMAND "${EDITOR}" "${WORK}/project/${project_name}" --no-audio --size 1280x720
        --settings-dir "${WORK}/settings" --frames 300 --debug-crash segv
    RESULT_VARIABLE result ERROR_VARIABLE errors TIMEOUT 60)
if(result EQUAL 0)
    message(FATAL_ERROR "The editor should have crashed:\n${errors}")
endif()
file(GLOB reports "${WORK}/settings/logs/crash-editor-*.txt")
list(LENGTH reports count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "Expected one editor crash report, found: ${reports}")
endif()
file(READ "${reports}" report)
if(NOT report MATCHES "SIGSEGV")
    message(FATAL_ERROR "The editor's crash report lacks SIGSEGV:\n${report}")
endif()

# The next start says so.
set(ENV{SHOT_DIR} "${WORK}/shots")
execute_process(COMMAND "${EDITOR}" --no-audio --size 1280x720 --settings-dir "${WORK}/settings"
        --script "${SCRIPT}" --failure-dir "${WORK}/shots"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 60)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "The editor did not report the earlier crash (${result}):\n${output}\n${errors}")
endif()
# ... and only once.
file(GLOB markers "${WORK}/settings/logs/editor-*.session")
if(markers)
    message(FATAL_ERROR "A stale session marker is left behind: ${markers}")
endif()
message(STATUS "Crash reporting check passed")
