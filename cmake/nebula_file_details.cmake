# nebula_file_details(TARGET FILE DESCRIPTION [ICON path]): a Windows program's file details, and its icon (resource 1,
# the game window's). The version is an automatic build fingerprint from git (cmake/nebula_git_fingerprint.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/nebula_git_fingerprint.cmake")
set(NEBULA_RC_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/nebula_file_details.rc.in")

function(nebula_file_details target file description)
    if(NOT WIN32)
        return()
    endif()
    cmake_parse_arguments(arg "" "ICON" "" ${ARGN})
    set(NEBULA_RC_FILE "${file}")
    set(NEBULA_RC_DESCRIPTION "${description}")
    set(NEBULA_RC_ICON "")
    if(arg_ICON)
        set(NEBULA_RC_ICON "1 ICON \"${arg_ICON}\"")
    endif()
    # Extract semver components from the fingerprint for FILEVERSION/PRODUCTVERSION numeric fields.
    string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _nebula_fp_match "${NEBULA_BUILD_FINGERPRINT}")
    set(NEBULA_RC_MAJOR ${CMAKE_MATCH_1})
    set(NEBULA_RC_MINOR ${CMAKE_MATCH_2})
    set(NEBULA_RC_PATCH ${CMAKE_MATCH_3})
    configure_file("${NEBULA_RC_TEMPLATE}" "${CMAKE_CURRENT_BINARY_DIR}/${target}_details.rc" @ONLY)
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${target}_details.rc")
endfunction()
