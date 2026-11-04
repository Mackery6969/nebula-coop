# Build fingerprints (docs/COOP_PLAN.md "Versions"): hashes of the sources that decide whether two builds work together,
# worked out at configure time, so no change ever bumps a number by hand and parallel changes never clash over one.
#
#   NEBULA_GAME_API_FINGERPRINT: native/include/nebula_game_api.h. The host refuses a game module built from another
#     (a stale smg_game beside a new host): the module and the host are separate build trees on Windows and SDL.
#   NEBULA_COOP_FINGERPRINT: what online play sends and how each game reads it (the game API, the co-op and network
#     runtime, the game's co-op code). A joiner with another is refused (galaxy/coop_session.h game_version).
#
# Each file is hashed with its line endings as LF, so a Windows checkout (CRLF) and a Linux one agree. A changed file
# configures the tree again (CMAKE_CONFIGURE_DEPENDS); the header is written only when a fingerprint changes, so only
# its includers build again.
#
# nebula_fingerprint(<nebula root> <petari dir> <output dir>) writes <output dir>/nebula_fingerprint.h.

# Each file by its name and its text's hash, in order of name: the same wherever the checkout (or NEBULA_PETARI_DIR) is.
function(_nebula_fingerprint_of out)
    set(lines "")
    foreach(file IN LISTS ARGN)
        file(READ "${file}" text)
        string(REPLACE "\r\n" "\n" text "${text}")
        string(SHA256 digest "${text}")
        get_filename_component(name "${file}" NAME)
        list(APPEND lines "${name} ${digest}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    endforeach()
    list(SORT lines)
    string(JOIN "\n" all ${lines})
    string(SHA256 all "${all}")
    set(${out} "${all}" PARENT_SCOPE)
endfunction()

function(nebula_fingerprint root petari out_dir)
    set(api_header "${root}/native/include/nebula_game_api.h")
    _nebula_fingerprint_of(api "${api_header}")

    file(GLOB coop_files CONFIGURE_DEPENDS
        "${root}/runtime/include/galaxy/coop_*.h"
        "${root}/runtime/include/galaxy/net/*.h"
        "${root}/runtime/src/coop_*.cpp"
        "${root}/runtime/src/net/*.cpp")
    file(GLOB_RECURSE game_coop_files CONFIGURE_DEPENDS
        "${petari}/include/Game/*Coop*.hpp" "${petari}/include/Game/*Coop*.h"
        "${petari}/src/Game/*Coop*.cpp" "${petari}/src/Game/*Coop*.c")
    list(APPEND coop_files ${game_coop_files})
    list(FILTER coop_files EXCLUDE REGEX "_tests\\.cpp$")
    _nebula_fingerprint_of(coop "${api_header}" ${coop_files})

    string(SUBSTRING "${api}" 0 8 api)
    string(SUBSTRING "${coop}" 0 12 coop)
    set(text "/* Written by cmake/nebula_fingerprint.cmake: do not edit. */\n")
    string(APPEND text "#define NEBULA_GAME_API_FINGERPRINT 0x${api}u\n")
    string(APPEND text "#define NEBULA_COOP_FINGERPRINT \"${coop}\"\n")
    set(path "${out_dir}/nebula_fingerprint.h")
    set(old "")
    if(EXISTS "${path}")
        file(READ "${path}" old)
    endif()
    if(NOT old STREQUAL text)
        file(WRITE "${path}" "${text}")
    endif()
    message(STATUS "Nebula fingerprints: game API ${api}, co-op ${coop}")
endfunction()
