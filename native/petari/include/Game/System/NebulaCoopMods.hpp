#pragma once

#include <nebula_types.h>

// Online co-op for mods (nebula_mod_api.h's coop_* calls and NEBULA_MOD_EVENT_COOP_*), as sm64coopdx's Lua has
// gNetworkPlayers and its player hooks: the room's players by index (the host 0, then as they joined), read from the
// host's player list once a frame, and the events of what changed since the last frame.
namespace NebulaCoopMods {
    // Each frame on the game's thread (GameSystem::update), before the mods' frame: the room's players read again,
    // and room_joined, player_join, player_leave, player_place and room_closed sent for what changed. Nothing while
    // no mod runs.
    void update();

    s32 inRoom();
    s32 isHost();
    s32 me();
    s32 count();
    s32 maxPlayers();
    s32 connected(s32 index);
    const char* name(s32 index);
    s32 character(s32 index);
    const char* stage(s32 index);
    s32 mission(s32 index);
    s32 samePlace(s32 index);
    s32 stars(s32 index);
    s32 ping(s32 index);
    // The id of the character they play ("" for nobody).
    const char* characterId(s32 index);
};  // namespace NebulaCoopMods
