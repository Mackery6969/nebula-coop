#include "Game/Boss/PoltaActionSequencer.hpp"
#include "Game/Boss/Polta.hpp"
#include "Game/Boss/PoltaActionBase.hpp"
#include "Game/Boss/PoltaBattleLv1.hpp"
#include "Game/Boss/PoltaBattleLv2.hpp"
#include "Game/Boss/PoltaDemo.hpp"
#include "Game/Boss/PoltaFunction.hpp"
#include "Game/Boss/PoltaWaitStart.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace NrvPoltaActionSequencer {
    NEW_NERVE(PoltaActionSequencerNrvWaitStart, PoltaActionSequencer, WaitStart);
    NEW_NERVE(PoltaActionSequencerNrvOpeningDemo, PoltaActionSequencer, OpeningDemo);
    NEW_NERVE(PoltaActionSequencerNrvBattleLv1, PoltaActionSequencer, BattleLv1);
    NEW_NERVE(PoltaActionSequencerNrvPowerUpDemo, PoltaActionSequencer, PowerUpDemo);
    NEW_NERVE(PoltaActionSequencerNrvBattleLv2, PoltaActionSequencer, BattleLv2);
    NEW_NERVE(PoltaActionSequencerNrvDownDemo, PoltaActionSequencer, DownDemo);
};  // namespace NrvPoltaActionSequencer

namespace {
    // The fight's phases in order (online co-op, NebulaCoopBossPhases).
    enum {
        Phase_Opening = 1,
        Phase_Lv1,
        Phase_PowerUp,
        Phase_Lv2,
        Phase_Down,
    };
    const Nerve* const cPhases[] = {
        GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvOpeningDemo), GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvBattleLv1), GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvPowerUpDemo),
        GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvBattleLv2), GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvDownDemo),
    };
    const s32 cPhaseNum = sizeof(cPhases) / sizeof(cPhases[0]);
};  // namespace

PoltaActionSequencer::PoltaActionSequencer(Polta* pPolta, const TableIter& rIter)
    : NerveExecutor("ポルタアクション管理"), mPoltaPtr(pPolta), mActionDemo(nullptr), mCurrentAction(nullptr), mActionWaitStart(nullptr),
      mActionPoltaBattleLv1(nullptr), mActionPoltaBattleLv2(nullptr) {
    initNerve(GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvWaitStart));
    mActionDemo = new PoltaDemo(pPolta);
    mActionDemo->init();
    mActionWaitStart = new PoltaWaitStart(pPolta);
    mActionPoltaBattleLv1 = new PoltaBattleLv1(pPolta);
    mActionPoltaBattleLv2 = new PoltaBattleLv2(pPolta);
    MR::declareStarPiece(mPoltaPtr, 24);
    mCoop.init(pPolta, cPhases, cPhaseNum);
}

void PoltaActionSequencer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mCurrentAction != nullptr) {
        mCurrentAction->attackSensor(pSender, pReceiver);
    }
}

bool PoltaActionSequencer::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mCurrentAction != nullptr) {
        return mCurrentAction->receiveMsgPlayerAttack(msg, pSender, pReceiver);
    }
    return false;
}

bool PoltaActionSequencer::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mCurrentAction != nullptr) {
        return mCurrentAction->receiveMsgEnemyAttack(msg, pSender, pReceiver);
    }
    return false;
}

bool PoltaActionSequencer::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (mCurrentAction != nullptr) {
        return mCurrentAction->receiveOtherMsg(msg, pSender, pReceiver);
    }
    return false;
}

bool PoltaActionSequencer::updateAction() {
    return mCurrentAction->update();
}

bool PoltaActionSequencer::startAction() {
    if (isNerve(GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvWaitStart))) {
        // Online co-op: another player's game began the fight: this one goes on from the room's phase, without the
        // opening.
        if (const s32 room = mCoop.getRoomPhase()) {
            coopJump(0, room);
            return true;
        }

        setNerve(GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvOpeningDemo));
        return true;
    }
    return false;
}

void PoltaActionSequencer::update() {
    if (const s32 phase = mCoop.update(this)) {
        coopJump(mCoop.phaseOf(mSpine->getCurrentNerve()), phase);
    }

    // The music the skipped scenes started (PoltaDemo's), once the local player is in the fight.
    if (const s32 music = mCoop.takeMusic()) {
        if (music >= Phase_Down) {
            MR::startAfterBossBGM();
        } else if (music >= Phase_Lv2) {
            MR::startStageBGM("MBGM_BOSS_09_B", false);
        } else if (music >= Phase_Lv1) {
            MR::startStageBGM("MBGM_BOSS_09_A", false);
        }
    }

    updateNerve();
}

// Online co-op: the room's fight is at `phase`, ahead of this game's (`from`, 0 not begun). The scenes between are
// skipped, their outcomes applied; the scene at `phase` plays if the local player is in the fight (the opening never),
// else it is skipped too.
void PoltaActionSequencer::coopJump(s32 from, s32 phase) {
    const bool watching = mCoop.isWatching();
    s32 to = phase == Phase_Opening ? Phase_Lv1 : phase;
    if (!watching && to == Phase_PowerUp) {
        to++;
    }

    if (from <= Phase_Opening && to > Phase_Opening) {
        // As the opening ends: the boss shown where it begins.
        MR::invalidateClipping(mPoltaPtr);
        MR::showModel(mPoltaPtr);
        mPoltaPtr->setStartPose();
    }

    if (from <= Phase_PowerUp && to > Phase_PowerUp) {
        mPoltaPtr->setStartPose();
    }

    mCoop.jumped(to);
    if (to == Phase_Down && !watching) {
        mCoop.keepMusic(Phase_Down);
        mCurrentAction = nullptr;
        mPoltaPtr->coopDefeat();
        setNerve(GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvWaitStart));
        return;
    }

    if (to != Phase_PowerUp && to != Phase_Down) {
        mCoop.keepMusic(to);
    }

    setNerve(mCoop.nerveOf(to));
}

void PoltaActionSequencer::exeWaitStart() {
    if (MR::isFirstStep(this)) {
        mActionWaitStart->appear();
        mCurrentAction = mActionWaitStart;
    }
}

void PoltaActionSequencer::exeOpeningDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = mActionDemo;
        mActionDemo->startOpeningDemo();
    }
    MR::updateActorStateAndNextNerve(this, mActionDemo, GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvBattleLv1));
}

void PoltaActionSequencer::exeBattleLv1() {
    if (MR::isFirstStep(this)) {
        mActionPoltaBattleLv1->appear();
        mCurrentAction = mActionPoltaBattleLv1;
    }
    if (updateAction()) {
        setNerve(GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvPowerUpDemo));
    }
}

void PoltaActionSequencer::exePowerUpDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = mActionDemo;
        mActionDemo->startPowerUpDemo();
    }
    MR::updateActorStateAndNextNerve(this, mActionDemo, GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvBattleLv2));
}

void PoltaActionSequencer::exeBattleLv2() {
    if (MR::isFirstStep(this)) {
        mActionPoltaBattleLv2->appear();
        mCurrentAction = mActionPoltaBattleLv2;
        mPoltaPtr->appearStarPiece(10);
    }
    if (updateAction()) {
        setNerve(GET_NERVE(PoltaActionSequencer, PoltaActionSequencerNrvDownDemo));
    }
}

void PoltaActionSequencer::exeDownDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = mActionDemo;
        mActionDemo->startDownDemo();
    }
    MR::updateActorState(this, mActionDemo);
}
