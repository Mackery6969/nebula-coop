#include "Game/Boss/BossKameckSequencer.hpp"
#include "Game/Boss/BossKameck.hpp"
#include "Game/Boss/BossKameckBattleDemo.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

BossKameckSequencer::BossKameckSequencer(const char* pName) : NerveExecutor(pName), mBossKameck(), mBattleDemo(), mCoop(), mCoopPowerUpPhase(0) {
}

void BossKameckSequencer::init(BossKameck* pBoss, const TableIter& rIter) {
    mBossKameck = pBoss;

    mBattleDemo = new BossKameckBattleDemo(pBoss, rIter);
    mBattleDemo->init();
}

void BossKameckSequencer::update() {
    if (const s32 phase = mCoop.update(this)) {
        coopJump(mCoop.phaseOf(mSpine->getCurrentNerve()), phase);
    }

    // The music the skipped scenes started: the boss's theme from the opening on, the after-boss one once beaten.
    if (const s32 music = mCoop.takeMusic()) {
        if (music >= mCoop.getPhaseNum()) {
            MR::startAfterBossBGM();
        } else if (music >= 2) {
            MR::startStageBGM("MBGM_BOSS_04", false);
        }
    }

    updateNerve();
}

void BossKameckSequencer::coopInit(const Nerve* const* pPhases, s32 num, s32 powerUpPhase) {
    mCoop.init(mBossKameck, pPhases, num);
    mCoopPowerUpPhase = powerUpPhase;
}

// The fight begins (its switch): another player's game began it already, this one goes on from the room's phase
// without the opening (the player arrives as at any arena). True when it did.
bool BossKameckSequencer::coopStart() {
    const s32 room = mCoop.getRoomPhase();
    if (room <= 0) {
        return false;
    }

    coopJump(0, room);
    return true;
}

// The scenes between `from` and the room's `phase` are skipped, their outcomes applied; the scene at `phase` plays if
// the local player is in the fight (the opening never: it was the first player's), else it is skipped too.
void BossKameckSequencer::coopJump(s32 from, s32 phase) {
    const s32 down = mCoop.getPhaseNum();
    const bool watching = mCoop.isWatching();
    s32 to = phase == 1 ? 2 : phase;
    if (!watching && to == mCoopPowerUpPhase) {
        to++;
    }

    if (!mBattleDemo->mIsDead) {
        mBattleDemo->kill();
    }

    if (from <= 1 && to > 1) {
        // As the appearing scene ends (BossKameckBattleDemo::exeAppearVs1).
        MR::showModel(mBossKameck);
        MR::validateShadowAll(mBossKameck);
        mBossKameck->endDemo();
        coopAfterOpening();
    }

    if (from <= mCoopPowerUpPhase && to > mCoopPowerUpPhase) {
        // The Magikoopas the power-up scene calls, there at once.
        mBossKameck->startDemoAppearKameck();
        mBossKameck->endDemoAppearKameck();
    }

    mCoop.jumped(to);
    if (to == down && !watching) {
        // Beaten where nobody here saw it (BossKameckBattleDemo::updateDown's outcome).
        mCoop.keepMusic(down);
        coopBeforeDown();
        mBossKameck->deadKameck();
        MR::appearPowerStarWithoutDemo(mBossKameck);
        mBossKameck->kill();
        return;
    }

    if (to != mCoopPowerUpPhase && to != down) {
        mCoop.keepMusic(to);
    }

    setNerve(mCoop.nerveOf(to));
}
