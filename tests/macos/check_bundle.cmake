# The macOS application bundle, checked where it is not built: assembles "YK Engine.app" from the
# built programs into a folder with a space in its name that has nothing to do with the build tree,
# then uses it from there. The bundle-relative lookups (the player beside the editor in
# Contents/MacOS, the demo game, the licenses and the data of exported games in Contents/Resources)
# are path logic, so a Linux run is a real test of them; the macOS CI job also runs it with the real
# application (docs/BUILDING.md, macOS).
#
#   cmake -DEDITOR=<yk_editor> -DPLAYER=<yk_player> -DTOOL=<yk> -DSOURCE_DIR=<repo> -DVERSION=<x.y.z>
#         -DDEMO=<a project folder> -DWORK=<scratch dir> -P check_bundle.cmake
foreach(variable EDITOR PLAYER TOOL SOURCE_DIR VERSION DEMO WORK)
    if(NOT ${variable})
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(ENV{SDL_VIDEODRIVER} dummy)
set(ENV{SDL_RENDER_DRIVER} software)
set(ENV{SDL_AUDIODRIVER} dummy)
set(ENV{YK_LOG_DIR} "${WORK}/logs")

function(run description)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors
        TIMEOUT 240)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed (${result}):\n${output}\n${errors}")
    endif()
    set(last_output "${output}" PARENT_SCOPE)
endfunction()

set(app "${WORK}/Some Folder/YK Engine.app")
run("assembling the bundle" "${CMAKE_COMMAND}" "-DOUT=${app}" "-DEDITOR=${EDITOR}" "-DPLAYER=${PLAYER}"
    "-DTOOL=${TOOL}" "-DSOURCE_DIR=${SOURCE_DIR}" "-DVERSION=${VERSION}" "-DDEMO=${DEMO}"
    -P "${SOURCE_DIR}/packaging/macos/assemble-app.cmake")

get_filename_component(editor_name "${EDITOR}" NAME)
get_filename_component(player_name "${PLAYER}" NAME)
get_filename_component(tool_name "${TOOL}" NAME)
set(macos "${app}/Contents/MacOS")
set(resources "${app}/Contents/Resources")
foreach(required
        "${app}/Contents/Info.plist" "${macos}/${editor_name}" "${macos}/${player_name}"
        "${macos}/${tool_name}" "${resources}/AppIcon.icns"
        "${resources}/YK-DemoGame/project.ykproj" "${resources}/YK-DemoGame/assets/icon.png"
        "${resources}/licenses/SDL3.txt" "${resources}/licenses/Box2D-MIT.txt"
        "${resources}/docs/EDITOR.md" "${resources}/docs/THIRD_PARTY.md")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "The bundle lacks ${required}")
    endif()
endforeach()
if(EXISTS "${resources}/YK-DemoGame/tools")
    message(FATAL_ERROR "Development tools of the demo game ship in the bundle")
endif()

# Info.plist: filled in, names the program that is really there, and no template marker is left.
file(READ "${app}/Contents/Info.plist" plist)
foreach(expected "<string>${editor_name}</string>" "<string>com.yk.engine</string>"
        "<string>${VERSION}</string>" "<key>CFBundleIconFile</key>" "<string>ykproj</string>"
        "<string>YK Engine</string>")
    string(FIND "${plist}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Info.plist lacks ${expected}:\n${plist}")
    endif()
endforeach()
if(plist MATCHES "@[A-Z_]+@")
    message(FATAL_ERROR "Info.plist still has a template marker: ${CMAKE_MATCH_0}")
endif()
file(READ "${resources}/AppIcon.icns" magic LIMIT 4 HEX)
if(NOT magic STREQUAL "69636e73")
    message(FATAL_ERROR "AppIcon.icns is not an icon file (starts with ${magic})")
endif()

# Use the bundle from where it is. The command line finds the player beside it and the licenses
# in Resources, so an export made by it carries both.
run("yk targets" "${macos}/${tool_name}" targets)
string(FIND "${last_output}" "${macos}/${player_name}" found)
if(found EQUAL -1)
    message(FATAL_ERROR "yk targets did not find the player in the bundle:\n${last_output}")
endif()
string(FIND "${last_output}" "${resources}/licenses" found)
if(found EQUAL -1)
    message(FATAL_ERROR "yk targets did not find the licenses in Resources:\n${last_output}")
endif()
run("yk validate" "${macos}/${tool_name}" validate "${resources}/YK-DemoGame")

# An export for macOS made by the bundle's own tool: an app with Info.plist, the icon, the data in
# Resources/data and the notices. The player inside is this system's player (the only one there is
# here), which finds its data from a bundle-shaped folder exactly as it will on a Mac.
run("the bundle's yk export (macOS layout)" "${macos}/${tool_name}" export "${resources}/YK-DemoGame"
    --target macos --player "${macos}/${player_name}" --out "${WORK}/exported" --zip)
set(game "${WORK}/exported/Cinder Vale.app")
foreach(required "${game}/Contents/MacOS/CinderVale" "${game}/Contents/Info.plist"
        "${game}/Contents/Resources/AppIcon.icns" "${game}/Contents/Resources/data/project.ykproj"
        "${game}/Contents/Resources/data/scenes/level01.ykscene"
        "${game}/Contents/Resources/licenses/SDL3.txt" "${game}/Contents/Resources/README.txt"
        "${WORK}/exported/Cinder Vale.app.zip")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "The exported bundle lacks ${required}")
    endif()
endforeach()
file(READ "${game}/Contents/Info.plist" game_plist)
foreach(expected "<string>CinderVale</string>" "<key>CFBundleIconFile</key>"
        "Demo game made with YK Engine")
    string(FIND "${game_plist}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "The game's Info.plist lacks ${expected}:\n${game_plist}")
    endif()
endforeach()
# Nothing of the editor rides along in a game.
file(GLOB game_programs "${game}/Contents/MacOS/*")
list(LENGTH game_programs program_count)
if(NOT program_count EQUAL 1)
    message(FATAL_ERROR "A game should carry one program, found: ${game_programs}")
endif()
run("the exported bundle" "${game}/Contents/MacOS/CinderVale" --frames 60 --fixed --no-audio)

# The editor, from inside the bundle: its welcome screen finds the demo game in Resources.
file(REAL_PATH "${resources}/YK-DemoGame" sample)
set(ENV{SAMPLE_DIR} "${sample}")
run("the bundled editor" "${CMAKE_COMMAND}" "-DEDITOR=${macos}/${editor_name}"
    "-DSCRIPT=${SOURCE_DIR}/tests/install/open_sample.ykscript" "-DWORK=${WORK}/editor"
    -P "${SOURCE_DIR}/tests/editor/run_script.cmake")
message(STATUS "Bundle check passed: ${app}")
