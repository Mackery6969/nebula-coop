# Generate build fingerprint from git metadata at configure time.
# Sets:
#   NEBULA_GIT_COMMIT  - short commit hash (7 chars)
#   NEBULA_GIT_DATE    - commit date (YYYY-MM-DD)
#   NEBULA_BUILD_FINGERPRINT - full fingerprint (e.g. "2.0.1+abc1234")

function(nebula_git_fingerprint)
    # Get short commit hash
    execute_process(
        COMMAND git rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE NEBULA_GIT_COMMIT_RAW
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_RESULT
    )
    if(GIT_RESULT EQUAL 0 AND NEBULA_GIT_COMMIT_RAW)
        set(NEBULA_GIT_COMMIT "${NEBULA_GIT_COMMIT_RAW}" PARENT_SCOPE)
    else()
        set(NEBULA_GIT_COMMIT "unknown" PARENT_SCOPE)
    endif()

    # Get commit date
    execute_process(
        COMMAND git log -1 --format=%Y-%m-%d
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE NEBULA_GIT_DATE_RAW
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_RESULT
    )
    if(GIT_RESULT EQUAL 0 AND NEBULA_GIT_DATE_RAW)
        set(NEBULA_GIT_DATE "${NEBULA_GIT_DATE_RAW}" PARENT_SCOPE)
    else()
        set(NEBULA_GIT_DATE "unknown" PARENT_SCOPE)
    endif()

    # Get the semantic version from the project() line
    file(STRINGS "${CMAKE_SOURCE_DIR}/CMakeLists.txt" _nebula_proj_line REGEX "^project\\(Nebula VERSION ")
    string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.([0-9]+)" _nebula_ver_match "${_nebula_proj_line}")
    set(_nebula_semver "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    if(NOT _nebula_semver)
        set(_nebula_semver "0.0.0")
    endif()

    set(NEBULA_BUILD_FINGERPRINT "${_nebula_semver}+${NEBULA_GIT_COMMIT}" PARENT_SCOPE)
endfunction()

nebula_git_fingerprint()
