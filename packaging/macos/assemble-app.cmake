# Builds "YK Engine.app" from the built programs and the files a user needs. Plain file copying, so
# it runs on any system: the macOS build uses it for the real application, and the test
# tests/macos/check_bundle.cmake runs it anywhere to prove the bundle layout works from a folder the
# program was never built in.
#
#   cmake -DOUT=<path of the .app> -DEDITOR=<yk_editor> -DPLAYER=<yk_player> -DTOOL=<yk>
#         -DSOURCE_DIR=<repository> -DVERSION=<x.y.z> [-DBUNDLE_ID=com.yk.engine]
#         [-DDEMO=<project folder>] -P assemble-app.cmake
#
# Layout (the bundle's Contents):
#   MacOS/yk_editor yk_player yk     the programs (the player is what the editor exports games with)
#   Resources/AppIcon.icns
#   Resources/YK-DemoGame, YK-ExplorationDemo and Fireboy-Watergirl-Demo (samples), licenses, docs
#   Info.plist
foreach(variable OUT EDITOR PLAYER TOOL SOURCE_DIR VERSION)
    if(NOT ${variable})
        message(FATAL_ERROR "${variable} is required")
    endif()
endforeach()
if(NOT BUNDLE_ID)
    set(BUNDLE_ID "com.yk.engine")
endif()
if(NOT DEMO)
    set(DEMO "${SOURCE_DIR}/YK-DemoGame")
endif()
set(YK_BUNDLE_ID "${BUNDLE_ID}")
set(YK_VERSION "${VERSION}")

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/Contents/MacOS" "${OUT}/Contents/Resources")
foreach(program ${EDITOR} ${PLAYER} ${TOOL})
    file(COPY "${program}" DESTINATION "${OUT}/Contents/MacOS"
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
endforeach()
configure_file("${SOURCE_DIR}/packaging/macos/Info.plist.in" "${OUT}/Contents/Info.plist" @ONLY)
file(COPY "${SOURCE_DIR}/packaging/icons/YKEngine.icns" DESTINATION "${OUT}/Contents/Resources")
file(RENAME "${OUT}/Contents/Resources/YKEngine.icns" "${OUT}/Contents/Resources/AppIcon.icns")
# Whatever the folder is called, the welcome screen looks for Resources/YK-DemoGame. A build with
# no demo project (YK_DEMO_PROJECT) simply has no sample to offer.
if(EXISTS "${DEMO}/project.ykproj")
    file(COPY "${DEMO}/" DESTINATION "${OUT}/Contents/Resources/YK-DemoGame"
        PATTERN "tools" EXCLUDE PATTERN "__pycache__" EXCLUDE PATTERN ".DS_Store" EXCLUDE)
endif()
if(EXISTS "${SOURCE_DIR}/YK-ExplorationDemo/project.ykproj")
    file(COPY "${SOURCE_DIR}/YK-ExplorationDemo/" DESTINATION
        "${OUT}/Contents/Resources/YK-ExplorationDemo"
        PATTERN ".DS_Store" EXCLUDE)
endif()
if(EXISTS "${SOURCE_DIR}/Fireboy-Watergirl-Demo/project.ykproj")
    file(COPY "${SOURCE_DIR}/Fireboy-Watergirl-Demo/" DESTINATION
        "${OUT}/Contents/Resources/Fireboy-Watergirl-Demo"
        PATTERN "tools" EXCLUDE PATTERN "__pycache__" EXCLUDE
        PATTERN ".DS_Store" EXCLUDE)
endif()
file(COPY "${SOURCE_DIR}/LICENSES/" DESTINATION "${OUT}/Contents/Resources/licenses")
file(COPY "${SOURCE_DIR}/README.md" "${SOURCE_DIR}/THIRD_PARTY.md" DESTINATION
    "${OUT}/Contents/Resources/docs")
file(COPY "${SOURCE_DIR}/packaging/update/macos.sh" DESTINATION "${OUT}/Contents/Resources")
file(GLOB documents "${SOURCE_DIR}/docs/*.md")
file(COPY ${documents} DESTINATION "${OUT}/Contents/Resources/docs")
message(STATUS "Assembled ${OUT}")
