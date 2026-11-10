#include "Game/System/NebulaCoopBoss.hpp"
#include "nebula_module.h"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/System/NebulaCoopWorld.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/Tunable.hpp"

namespace {
    // How near the boss the local player is to be in its fight (its cutscenes and music his): a boss's planet or
    // arena, not the galaxy's others.
    NEBULA_TUNABLE(f32, cWatchDistance, 6000.0f);
};  // namespace

NebulaCoopBossPhases::NebulaCoopBossPhases() : mBoss(nullptr), mNerves(nullptr), mNum(0), mLocalPhase(0), mMusicPhase(0) {
}

void NebulaCoopBossPhases::init(LiveActor* pBoss, const Nerve* const* pNerves, s32 num) {
    mBoss = pBoss;
    mNerves = pNerves;
    mNum = num;
    mLocalPhase = 0;
    mMusicPhase = 0;
}

s32 NebulaCoopBossPhases::phaseOf(const Nerve* pNerve) const {
    for (s32 i = 0; i < mNum; i++) {
        if (mNerves[i] == pNerve) {
            return i + 1;
        }
    }
    return 0;
}

const Nerve* NebulaCoopBossPhases::nerveOf(s32 phase) const {
    return phase >= 1 && phase <= mNum ? mNerves[phase - 1] : nullptr;
}

s32 NebulaCoopBossPhases::getRoomPhase() const {
    if (mBoss == nullptr || !NebulaCoopWorld::sObjectsActive) {
        return 0;
    }
    for (s32 phase = mNum; phase >= 1; phase--) {
        if (NebulaCoopWorld::hasMark(mBoss, static_cast< u32 >(phase))) {
            return phase;
        }
    }
    return 0;
}

s32 NebulaCoopBossPhases::update(const NerveExecutor* pExecutor) {
    if (mBoss == nullptr || !NebulaCoopWorld::sObjectsActive || pExecutor->mSpine == nullptr) {
        return 0;
    }
    const s32 phase = phaseOf(pExecutor->mSpine->getCurrentNerve());
    if (phase > mLocalPhase) {
        // This game's own play got here: the instance's from now.
        nebula_log("[coop] boss %s: phase %d\n", mBoss->mName, static_cast< int >(phase));
        NebulaCoopWorld::noteMark(mBoss, static_cast< u32 >(phase));
    }
    if (phase != 0) {
        mLocalPhase = phase;
    }
    const s32 room = getRoomPhase();
    if (room <= mLocalPhase || MR::isDemoActive()) {
        return 0;
    }
    return room;
}

void NebulaCoopBossPhases::jumped(s32 phase) {
    nebula_log("[coop] boss %s: to the room's phase %d (from %d)%s\n", mBoss->mName, static_cast< int >(phase),
               static_cast< int >(mLocalPhase), isWatching() ? "" : ", away from the fight");
    mLocalPhase = phase;
}

bool NebulaCoopBossPhases::isWatching() const {
    const LiveActor* pPlayer = NebulaCoopWorld::getLocalPlayer();
    return mBoss != nullptr && pPlayer != nullptr && pPlayer->mPosition.distance(mBoss->mPosition) < cWatchDistance;
}

void NebulaCoopBossPhases::keepMusic(s32 phase) {
    mMusicPhase = phase;
}

s32 NebulaCoopBossPhases::takeMusic() {
    if (mMusicPhase == 0 || !isWatching()) {
        return 0;
    }
    const s32 phase = mMusicPhase;
    mMusicPhase = 0;
    return phase;
}

bool NebulaCoopBossPhases::isMusicHere(s32 phase) {
    if (mBoss == nullptr || !NebulaCoopWorld::sObjectsActive || isWatching()) {
        return true;
    }
    mMusicPhase = phase;
    return false;
}

namespace NebulaCoopBoss {
    bool isActive() {
        return NebulaCoopWorld::sObjectsActive;
    }

    bool isOtherPlayersHit() {
        return NebulaCoopWorld::isReplayingHit();
    }
};  // namespace NebulaCoopBoss
