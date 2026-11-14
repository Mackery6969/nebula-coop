#include "Game/System/NebulaCoopMods.hpp"
#include "Game/Player/NebulaCharacter.hpp"
#include "Game/Player/NebulaCharacters.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/EventUtil.hpp"

#include <cstring>

namespace {
    // The room's player numbers go to 16 (the host's max players at most).
    const s32 cSlots = 16;

    // A frame's view of the room: each index's player, or nobody.
    struct Room {
        bool mIn;
        s32 mSize;
        s32 mMe;
        bool mHost;
        bool mHas[cSlots];
        NebulaCoopListPlayer mPlayers[cSlots];
    };

    Room sNow = {};
    Room sBefore = {};

    bool valid(s32 index) {
        return index >= 0 && index < cSlots && sNow.mHas[index];
    }

    bool samePlaceAs(const NebulaCoopListPlayer& rA, const NebulaCoopListPlayer& rB) {
        return std::strcmp(rA.stage, rB.stage) == 0 && rA.scenario == rB.scenario;
    }

    void read(Room& rRoom) {
        std::memset(&rRoom, 0, sizeof(rRoom));
        NebulaCoopListPlayer list[cSlots];
        u32 size = 0;
        const u32 count = NebulaHost::coopList(list, cSlots, &size);
        if (size == 0) {
            return;
        }
        rRoom.mIn = true;
        rRoom.mSize = static_cast< s32 >(size);
        for (u32 i = 0; i < count && i < static_cast< u32 >(cSlots); i++) {
            const NebulaCoopListPlayer& rPlayer = list[i];
            const s32 index = static_cast< s32 >(rPlayer.number) - 1;
            if (index < 0 || index >= cSlots) {
                continue;
            }
            rRoom.mHas[index] = true;
            rRoom.mPlayers[index] = rPlayer;
            rRoom.mPlayers[index].name[sizeof(rPlayer.name) - 1] = '\0';
            rRoom.mPlayers[index].stage[sizeof(rPlayer.stage) - 1] = '\0';
            if ((rPlayer.flags & NEBULA_COOP_LIST_YOU) != 0) {
                rRoom.mMe = index;
                rRoom.mHost = (rPlayer.flags & NEBULA_COOP_LIST_HOST) != 0;
            }
        }
    }
}  // namespace

namespace NebulaCoopMods {
    void update() {
        if (!NebulaMods::hasCode()) {
            return;
        }
        sBefore = sNow;
        read(sNow);
        if (sNow.mIn && !sBefore.mIn) {
            // The players already there come with the room, as sm64coopdx's joined game: no join for each.
            NebulaMods::fire(NEBULA_MOD_EVENT_COOP_ROOM_JOINED, sNow.mMe);
            for (s32 i = 0; i < cSlots; i++) {
                if (sNow.mHas[i] && sNow.mPlayers[i].stage[0] != '\0') {
                    NebulaMods::fire(NEBULA_MOD_EVENT_COOP_PLAYER_PLACE, i, nullptr, sNow.mPlayers[i].stage);
                }
            }
            return;
        }
        if (!sNow.mIn) {
            if (sBefore.mIn) {
                NebulaMods::fire(NEBULA_MOD_EVENT_COOP_ROOM_CLOSED);
            }
            return;
        }
        for (s32 i = 0; i < cSlots; i++) {
            // An index is kept while its player stays (a name may change, live): a free one taken again needs a
            // join's handshake, never within one frame.
            const bool was = sBefore.mHas[i];
            const bool is = sNow.mHas[i];
            if (was && !is) {
                NebulaMods::fire(NEBULA_MOD_EVENT_COOP_PLAYER_LEAVE, i, nullptr, sBefore.mPlayers[i].name);
            }
            if (is && !was) {
                NebulaMods::fire(NEBULA_MOD_EVENT_COOP_PLAYER_JOIN, i, nullptr, sNow.mPlayers[i].name);
            }
            if (is && sNow.mPlayers[i].stage[0] != '\0' && (!was || !samePlaceAs(sBefore.mPlayers[i], sNow.mPlayers[i]))) {
                NebulaMods::fire(NEBULA_MOD_EVENT_COOP_PLAYER_PLACE, i, nullptr, sNow.mPlayers[i].stage);
            }
        }
    }

    s32 inRoom() {
        return sNow.mIn ? 1 : 0;
    }

    s32 isHost() {
        return !sNow.mIn || sNow.mHost ? 1 : 0;
    }

    s32 me() {
        return sNow.mIn ? sNow.mMe : 0;
    }

    s32 count() {
        if (!sNow.mIn) {
            return 1;
        }
        s32 n = 0;
        for (s32 i = 0; i < cSlots; i++) {
            n += sNow.mHas[i] ? 1 : 0;
        }
        return n;
    }

    s32 maxPlayers() {
        return sNow.mIn ? sNow.mSize : 1;
    }

    s32 connected(s32 index) {
        return sNow.mIn ? (valid(index) ? 1 : 0) : (index == 0 ? 1 : 0);
    }

    const char* name(s32 index) {
        return sNow.mIn && valid(index) ? sNow.mPlayers[index].name : "";
    }

    s32 character(s32 index) {
        if (!sNow.mIn) {
            return index == 0 ? (MR::isPlayerLuigi() ? 1 : 0) : -1;
        }
        return valid(index) ? ((sNow.mPlayers[index].flags & NEBULA_COOP_LIST_LUIGI) != 0 ? 1 : 0) : -1;
    }

    const char* stage(s32 index) {
        return sNow.mIn && valid(index) ? sNow.mPlayers[index].stage : "";
    }

    s32 mission(s32 index) {
        return sNow.mIn && valid(index) ? sNow.mPlayers[index].scenario : 0;
    }

    s32 samePlace(s32 index) {
        if (!sNow.mIn) {
            return index == 0 ? 1 : 0;
        }
        return valid(index) && valid(sNow.mMe) && samePlaceAs(sNow.mPlayers[index], sNow.mPlayers[sNow.mMe]) ? 1 : 0;
    }

    s32 stars(s32 index) {
        return sNow.mIn && valid(index) ? static_cast< s32 >(sNow.mPlayers[index].stars) : 0;
    }

    const char* characterId(s32 index) {
        if (!sNow.mIn) {
            return index == 0 ? NebulaCharacters::id(NebulaCharacter::index()) : "";
        }
        if (!valid(index)) {
            return "";
        }
        NebulaCoopListPlayer& rPlayer = sNow.mPlayers[index];
        rPlayer.character[sizeof(rPlayer.character) - 1] = '\0';
        // A host too old to say: its base.
        if (rPlayer.character[0] == '\0') {
            return (rPlayer.flags & NEBULA_COOP_LIST_LUIGI) != 0 ? "luigi" : "mario";
        }
        return rPlayer.character;
    }

    s32 ping(s32 index) {
        return sNow.mIn && valid(index) ? static_cast< s32 >(sNow.mPlayers[index].ping_ms) : 0;
    }
}  // namespace NebulaCoopMods
