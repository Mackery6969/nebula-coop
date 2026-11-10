#include "Game/Boss/BossStinkBugActionSequencer.hpp"
#include "Game/Boss/BossStinkBug.hpp"
#include "Game/Boss/BossStinkBugActionFlyHigh.hpp"
#include "Game/Boss/BossStinkBugActionFlyLow.hpp"
#include "Game/Boss/BossStinkBugActionGround.hpp"
#include "Game/Boss/BossStinkBugAngryDemo.hpp"
#include "Game/Boss/BossStinkBugFinishDemo.hpp"
#include "Game/Boss/BossStinkBugFlyDemo.hpp"
#include "Game/Boss/BossStinkBugOpeningDemo.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Demo/DemoPositionController.hpp"
#include "Game/LiveActor/ActorJointCtrl.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace NrvBossStinkBugActionSequencer {
    NEW_NERVE(BossStinkBugActionSequencerNrvWaitStart, BossStinkBugActionSequencer, WaitStart);
    NEW_NERVE(BossStinkBugActionSequencerNrvOpeningDemo, BossStinkBugActionSequencer, OpeningDemo);
    NEW_NERVE(BossStinkBugActionSequencerNrvGround, BossStinkBugActionSequencer, Ground);
    NEW_NERVE(BossStinkBugActionSequencerNrvFlyDemo, BossStinkBugActionSequencer, FlyDemo);
    NEW_NERVE(BossStinkBugActionSequencerNrvFlyLow, BossStinkBugActionSequencer, FlyLow);
    NEW_NERVE(BossStinkBugActionSequencerNrvAngryDemo, BossStinkBugActionSequencer, AngryDemo);
    NEW_NERVE(BossStinkBugActionSequencerNrvFlyHigh, BossStinkBugActionSequencer, FlyHigh);
    NEW_NERVE(BossStinkBugActionSequencerNrvFinishDemo, BossStinkBugActionSequencer, FinishDemo);
    NEW_NERVE(BossStinkBugActionSequencerNrvEnd, BossStinkBugActionSequencer, End);
};  // namespace NrvBossStinkBugActionSequencer

namespace {
    // The fight's phases in order (online co-op, NebulaCoopBossPhases).
    enum {
        Phase_Opening = 1,
        Phase_Ground,
        Phase_FlyDemo,
        Phase_FlyLow,
        Phase_Angry,
        Phase_FlyHigh,
        Phase_Finish,
    };
    const Nerve* const cPhases[] = {
        GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvOpeningDemo), GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvGround), GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvFlyDemo), GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvFlyLow),
        GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvAngryDemo), GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvFlyHigh), GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvFinishDemo),
    };
    const s32 cPhaseNum = sizeof(cPhases) / sizeof(cPhases[0]);
};  // namespace

BossStinkBugActionSequencer::BossStinkBugActionSequencer(BossStinkBug* pStinkBug, const TableIter& rIter)
    : NerveExecutor("ボスカメムシアクション管理"), mStinkBug(pStinkBug), mCurrentAction(nullptr), mOpeningDemo(nullptr), mActionGround(nullptr),
      mFlyDemo(nullptr), mActionFlyLow(nullptr), mAngryDemo(nullptr), mActionFlyHigh(nullptr), mFinishDemo(nullptr) {
    mOpeningDemo = new BossStinkBugOpeningDemo(pStinkBug, rIter);
    mActionGround = new BossStinkBugActionGround(pStinkBug);
    mFlyDemo = new BossStinkBugFlyDemo(pStinkBug, rIter);
    mActionFlyLow = new BossStinkBugActionFlyLow(pStinkBug);
    mAngryDemo = new BossStinkBugAngryDemo(pStinkBug, rIter);
    mActionFlyHigh = new BossStinkBugActionFlyHigh(pStinkBug);
    mFinishDemo = new BossStinkBugFinishDemo(pStinkBug, rIter);
    initNerve(GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvWaitStart));
    MR::declareStarPiece(mStinkBug, 24);    mCoop.init(pStinkBug, cPhases, cPhaseNum);
}

bool BossStinkBugActionSequencer::isValidFollowId(s32 id) const {
    if (id == -1) {
        return true;
    }

    return mCurrentAction != nullptr && mCurrentAction->isValidFollowId(id);
}

void BossStinkBugActionSequencer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mCurrentAction != nullptr) {
        mCurrentAction->attackSensor(pSender, pReceiver);
    }
}

bool BossStinkBugActionSequencer::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return mCurrentAction != nullptr && mCurrentAction->receiveMsgPlayerAttack(msg, pSender, pReceiver);
}

bool BossStinkBugActionSequencer::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return mCurrentAction != nullptr && mCurrentAction->receiveOtherMsg(msg, pSender, pReceiver);
}

bool BossStinkBugActionSequencer::startAction() {
    if (!isNerve(GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvWaitStart))) {
        return false;
    }

    // Online co-op: another player's game began the fight: this one goes on from the room's phase, without the
    // opening.
    if (const s32 room = mCoop.getRoomPhase()) {
        coopJump(0, room);
        return true;
    }

    setNerve(GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvOpeningDemo));
    return true;
}

void BossStinkBugActionSequencer::update() {
    if (const s32 phase = mCoop.update(this)) {
        coopJump(mCoop.phaseOf(mSpine->getCurrentNerve()), phase);
    }

    // The boss themes the fight phases start, once the local player is in the fight; the after-boss one once beaten.
    if (const s32 music = mCoop.takeMusic()) {
        if (music >= Phase_Finish) {
            MR::startAfterBossBGM();
        } else if (music >= Phase_FlyLow) {
            MR::startBossBGM(MR::BossBgmID_StinkBugB);
        } else if (music >= Phase_Ground) {
            MR::startBossBGM(MR::BossBgmID_StinkBugA);
        }
    }

    updateNerve();
}

// Online co-op: the room's fight is at `phase`, ahead of this game's (`from`, 0 not begun). The scenes between are
// skipped, the boss posed as at their ends; the scene at `phase` plays if the local player is in the fight (the opening
// never), else it is skipped too.
void BossStinkBugActionSequencer::coopJump(s32 from, s32 phase) {
    const bool watching = mCoop.isWatching();
    s32 to = phase == Phase_Opening ? Phase_Ground : phase;
    if (!watching && (to == Phase_FlyDemo || to == Phase_Angry)) {
        to++;
    }

    mCurrentAction = nullptr;
    if (from <= Phase_Opening && to > Phase_Opening) {
        mStinkBug->coopEndPose(mOpeningDemo->mDemoPositionController, "OpeningDemo");
    }

    if (from <= Phase_FlyDemo && to > Phase_FlyDemo) {
        // As the flight's scene (BossStinkBugFlyDemo): legs off the ground, the boss in the air where it ends.
        mStinkBug->offBindLeg();
        mStinkBug->mJointCtrl->endIKCtrlAll();
        mStinkBug->coopEndPose(mFlyDemo->mDemoPositionController, "FlyDemo");
    }

    if (from <= Phase_Angry && to > Phase_Angry) {
        MR::startBrk(mStinkBug, "Angry");
        mStinkBug->coopEndPose(mAngryDemo->mDemoPositionController, "AngryDemo");
    }

    mCoop.jumped(to);
    if (to == Phase_Finish && !watching) {
        // Beaten where nobody here saw it: gone, its Power Star out where it is placed.
        mCoop.keepMusic(Phase_Finish);
        MR::appearPowerStarWithoutDemo(mStinkBug);
        mStinkBug->kill();
        setNerve(GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvEnd));
        return;
    }

    setNerve(mCoop.nerveOf(to));
}

void BossStinkBugActionSequencer::exeWaitStart() {
}

void BossStinkBugActionSequencer::exeOpeningDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = mOpeningDemo;
    }
    MR::updateActorStateAndNextNerve(this, mOpeningDemo, GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvGround));
}

void BossStinkBugActionSequencer::exeGround() {
    if (MR::isFirstStep(this)) {
        if (mCoop.isMusicHere(Phase_Ground)) {
            MR::startBossBGM(MR::BossBgmID_StinkBugA);
        }
        mCurrentAction = mActionGround;
    }
    MR::updateActorStateAndNextNerve(this, mActionGround, GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvFlyDemo));
}

void BossStinkBugActionSequencer::exeFlyDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = mFlyDemo;
    }
    MR::updateActorStateAndNextNerve(this, mFlyDemo, GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvFlyLow));
}

void BossStinkBugActionSequencer::exeFlyLow() {
    if (MR::isFirstStep(this)) {
        mStinkBug->appearStarPiece(8);
        if (mCoop.isMusicHere(Phase_FlyLow)) {
            MR::startBossBGM(MR::BossBgmID_StinkBugB);
        }
        mCurrentAction = mActionFlyLow;
    }
    MR::updateActorStateAndNextNerve(this, mActionFlyLow, GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvAngryDemo));
}

void BossStinkBugActionSequencer::exeAngryDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = mAngryDemo;
    }
    MR::updateActorStateAndNextNerve(this, mAngryDemo, GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvFlyHigh));
}

void BossStinkBugActionSequencer::exeFlyHigh() {
    if (MR::isFirstStep(this)) {
        mStinkBug->appearStarPiece(16);
        mCurrentAction = mActionFlyHigh;
    }
    MR::updateActorStateAndNextNerve(this, mActionFlyHigh, GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvFinishDemo));
}

void BossStinkBugActionSequencer::exeFinishDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = mFinishDemo;
    }
    MR::updateActorStateAndNextNerve(this, mFinishDemo, GET_NERVE(BossStinkBugActionSequencer, BossStinkBugActionSequencerNrvEnd));
}

void BossStinkBugActionSequencer::exeEnd() {
}
