# Runs the editor headlessly with a UI script and fails when the script reports a problem.
#   cmake -DEDITOR=<yk_editor> -DSCRIPT=<file.ykscript> -DWORK=<scratch dir> [-DPROJECT=<dir>]
#         [-DPERSIST=ON] [-DKEEP=ON] -P run_script.cmake
# PERSIST keeps the window layout (it is normally ignored and not saved); KEEP reuses WORK from an
# earlier run instead of starting empty (to check that a layout survives a restart).
# The editor draws with SDL's software renderer on the dummy video driver, so no display is needed.
if(NOT EDITOR OR NOT SCRIPT OR NOT WORK)
    message(FATAL_ERROR "EDITOR, SCRIPT and WORK are required")
endif()
if(NOT KEEP)
    file(REMOVE_RECURSE "${WORK}")
endif()
file(MAKE_DIRECTORY "${WORK}/tmp" "${WORK}/shots" "${WORK}/settings")
set(ENV{YK_TEST_TMP} "${WORK}/tmp")
set(ENV{SHOT_DIR} "${WORK}/shots")
set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_RENDER_DRIVER} software)
set(ENV{SDL_AUDIODRIVER} dummy)
# Where an export made by the scripts ends up on this system: "Platformer" is the project the
# workflow script creates, "Cinder Vale Deluxe" the product the settings script names.
if(CMAKE_HOST_WIN32)
    set(ENV{YK_TARGET} windows)
    set(ENV{YK_EXPORT_DATA} "Platformer-windows/data")
    set(ENV{YK_EXPORT_FOLDER} "Cinder-Vale-Deluxe-windows")
    set(ENV{YK_EXE_SUFFIX} ".exe")
    set(ENV{YK_DELUXE_DATA} "Cinder-Vale-Deluxe-windows/data")
elseif(CMAKE_HOST_APPLE)
    set(ENV{YK_TARGET} macos)
    set(ENV{YK_EXPORT_DATA} "Platformer.app/Contents/Resources/data")
    set(ENV{YK_EXPORT_FOLDER} "Cinder Vale Deluxe.app/Contents/MacOS")
    set(ENV{YK_EXE_SUFFIX} "")
    set(ENV{YK_DELUXE_DATA} "Cinder Vale Deluxe.app/Contents/Resources/data")
else()
    set(ENV{YK_TARGET} linux)
    set(ENV{YK_EXPORT_DATA} "Platformer-linux/data")
    set(ENV{YK_EXPORT_FOLDER} "Cinder-Vale-Deluxe-linux")
    set(ENV{YK_EXE_SUFFIX} "")
    set(ENV{YK_DELUXE_DATA} "Cinder-Vale-Deluxe-linux/data")
endif()
set(arguments --no-audio --size 1440x810 --settings-dir "${WORK}/settings" --script "${SCRIPT}"
    --failure-dir "${WORK}/shots")
if(NOT PERSIST)
    list(APPEND arguments --fresh-layout)
endif()
if(PROJECT)
    # Scripts edit and save; give them a private copy of the project.
    get_filename_component(project_name "${PROJECT}" NAME)
    if(NOT KEEP OR NOT EXISTS "${WORK}/project/${project_name}")
        file(COPY "${PROJECT}" DESTINATION "${WORK}/project")
    endif()
    list(APPEND arguments "${WORK}/project/${project_name}")
endif()
execute_process(COMMAND "${EDITOR}" ${arguments}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 240)
if(NOT result EQUAL 0)
    message("${output}")
    message("${errors}")
    message(FATAL_ERROR "The editor script ${SCRIPT} failed (exit code ${result}); screenshots are in ${WORK}/shots")
endif()
