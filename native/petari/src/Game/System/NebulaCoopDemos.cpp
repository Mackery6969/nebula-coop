#include "Game/System/NebulaCoopDemos.hpp"
#include "nebula_module.h"
#include "Game/System/NebulaHost.hpp"
#include "Game/MapObj/PowerStar.hpp"
#include "Game/MapObj/PowerStarHolder.hpp"
#include "Game/NPC/EventDirector.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>

namespace {
    // A Power Star the room gave the current save, not yet taken by this player: its galaxy (by hash) and number.
    struct RoomStar {
        u32 mGalaxy;
        s32 mStar;
    };

    // The game has 242 stars across both characters; more than that is a room giving the same ones again.
    const s32 cMaxRoomStars = 256;

    RoomStar sRoomStars[cMaxRoomStars];
    s32 sRoomStarNum = 0;
    bool sInRoom = false;
    // Power Stars others in the instance took this scene (bit n for star n): shown here as had.
    u64 sTakenStars = 0;
    // A Grand Star another player took, to take this player along once play allows it, in its stage and scenario.
    s32 sPullStar = 0;
    char sPullStage[48];
    s32 sPullScenario = 0;
    // The get sequence being played was the room's pull: it isn't told back.
    bool sPulling = false;

    u32 hashName(const char* pName) {
        u32 hash = 2166136261u;
        for (const char* p = pName; *p != '\0'; p++) {
            hash = (hash ^ static_cast< u8 >(*p)) * 16777619u;
        }
        return hash;
    }

    s32 findRoomStar(const char* pGalaxyName, s32 starId) {
        const u32 galaxy = hashName(pGalaxyName);
        for (s32 i = 0; i < sRoomStarNum; i++) {
            if (sRoomStars[i].mGalaxy == galaxy && sRoomStars[i].mStar == starId) {
                return i;
            }
        }
        return -1;
    }

    bool isTakenStar(s32 starId) {
        return starId > 0 && starId < 64 && (sTakenStars & (1ull << starId)) != 0;
    }

    // The scene's Power Star with this number, when it has one.
    PowerStar* findPowerStar(s32 starId) {
        if (!MR::isExistSceneObj(SceneObj_EventDirector)) {
            return nullptr;
        }
        const PowerStarHolder* pHolder = EventFunction::getPowerStarHolder();
        if (pHolder == nullptr) {
            return nullptr;
        }
        for (s32 i = 0; i < pHolder->mNumInfos; i++) {
            if (pHolder->mInfos[i] != nullptr && pHolder->mInfos[i]->mStarNum == starId) {
                return pHolder->mInfos[i]->mStar;
            }
        }
        return nullptr;
    }

    // A Power Star another player took shows as one already had, as a finished mission's star does: still there to
    // take, for its get and return.
    void showTakenStars() {
        for (s32 starId = 1; starId < 64 && sTakenStars != 0; starId++) {
            if (!isTakenStar(starId)) {
                continue;
            }
            PowerStar* pStar = findPowerStar(starId);
            if (pStar != nullptr && !pStar->isShownAsHad()) {
                pStar->showAsHad();
            }
        }
    }
};  // namespace

namespace NebulaCoopDemos {
    void startScene(bool inRoom) {
        sInRoom = inRoom;
        sTakenStars = 0;
        sPulling = false;
        if (!inRoom) {
            sRoomStarNum = 0;
            sPullStar = 0;
        }
    }

    void noticeRoomStar(const char* pGalaxyName, s32 starId) {
        if (pGalaxyName == nullptr || sRoomStarNum >= cMaxRoomStars || findRoomStar(pGalaxyName, starId) >= 0) {
            return;
        }
        sRoomStars[sRoomStarNum].mGalaxy = hashName(pGalaxyName);
        sRoomStars[sRoomStarNum].mStar = starId;
        sRoomStarNum++;
    }

    void noticeStarGot(s32 starId, bool isGrand) {
        if (!sInRoom || starId <= 0) {
            return;
        }
        if (sPullStar == starId) {
            sPullStar = 0;
        }
        if (sPulling) {
            // Taken along by another's Grand Star: the others there have heard of it already.
            return;
        }
        NebulaCoopWorldEvent event = {};
        event.kind = NEBULA_COOP_WORLD_STAR_GOT;
        event.scenario = starId;
        event.value = isGrand ? NEBULA_COOP_STAR_GRAND : 0u;
        NebulaHost::coopWorldReport(&event);
    }

    void applyStarGot(s32 starId, bool isGrand) {
        if (!sInRoom || starId <= 0) {
            return;
        }
        if (isGrand) {
            nebula_log("[coop] another player took Grand Star %d here: this player goes along\n", static_cast< int >(starId));
            sPullStar = starId;
            nebula_snprintf(sPullStage, sizeof(sPullStage), "%s", MR::getCurrentStageName());
            sPullScenario = MR::getCurrentScenarioNo();
        } else if (starId < 64) {
            nebula_log("[coop] another player took star %d here\n", static_cast< int >(starId));
            sTakenStars |= 1ull << starId;
        }
    }

    bool updateInPlay(s32* pStarId) {
        if (!sInRoom) {
            return false;
        }
        showTakenStars();
        if (sPullStar == 0) {
            return false;
        }
        if (strcmp(sPullStage, MR::getCurrentStageName()) != 0 || sPullScenario != MR::getCurrentScenarioNo()) {
            // Another mission now: that Grand Star is this player's own to take there.
            sPullStar = 0;
            return false;
        }
        *pStarId = sPullStar;
        sPullStar = 0;
        sPulling = true;
        return true;
    }

    void beforeStageResult(const char* pClearedStageName, s32 clearedStarId) {
        if (pClearedStageName == nullptr) {
            return;
        }
        const s32 index = findRoomStar(pClearedStageName, clearedStarId);
        if (index < 0) {
            return;
        }
        sRoomStars[index] = sRoomStars[sRoomStarNum - 1];
        sRoomStarNum--;
        // The save at the scene's start, as the result's demos read it (what the star newly brings): without the
        // star, this player's first.
        GameDataHolder* pStart = GameDataFunction::getSceneStartGameDataHolder();
        if (pStart != nullptr && pStart != GameDataFunction::getCurrentGameDataHolder()) {
            nebula_log("[coop] %s star %d, the room's, is this player's first\n", pClearedStageName,
                     static_cast< int >(clearedStarId));
            pStart->setPowerStar(pClearedStageName, clearedStarId, false);
        }
    }
};  // namespace NebulaCoopDemos
