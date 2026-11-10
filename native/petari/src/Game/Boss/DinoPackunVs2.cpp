#include "Game/Boss/DinoPackunVs2.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunBall.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace NrvDinoPackunVs2 {
    NEW_NERVE(DinoPackunVs2NrvWaitStart, DinoPackunVs2, WaitStart);
    NEW_NERVE(DinoPackunVs2NrvOpeningDemo, DinoPackunVs2, OpeningDemo);
    NEW_NERVE(DinoPackunVs2NrvBattleEgg, DinoPackunVs2, BattleEgg);
    NEW_NERVE(DinoPackunVs2NrvCryDemo, DinoPackunVs2, CryDemo);
    NEW_NERVE(DinoPackunVs2NrvBattleLv1, DinoPackunVs2, BattleLv1);
    NEW_NERVE(DinoPackunVs2NrvAngryDemo, DinoPackunVs2, AngryDemo);
    NEW_NERVE(DinoPackunVs2NrvBattleLv2, DinoPackunVs2, BattleLv2);
    NEW_NERVE(DinoPackunVs2NrvBattleLv3, DinoPackunVs2, BattleLv3);
    NEW_NERVE(DinoPackunVs2NrvDownDemo, DinoPackunVs2, DownDemo);
};  // namespace NrvDinoPackunVs2

namespace {
    // The fight's phases in order (online co-op, NebulaCoopBossPhases); DinoPackun::coopStartMusic counts them alike.
    enum {
        Phase_Opening = 1,
        Phase_Egg,
        Phase_Cry,
        Phase_Lv1,
        Phase_Angry,
        Phase_Lv2,
        Phase_Lv3,
        Phase_Down,
    };
    const Nerve* const cPhases[] = {
        GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvOpeningDemo), GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvBattleEgg), GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvCryDemo),
        GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvBattleLv1), GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvAngryDemo), GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvBattleLv2),
        GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvBattleLv3), GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvDownDemo),
    };
    const s32 cPhaseNum = sizeof(cPhases) / sizeof(cPhases[0]);
};  // namespace

DinoPackunVs2::DinoPackunVs2(DinoPackun* pPackun) : DinoPackunSequencer("ディノパックン2戦目進行", pPackun) {
    mDemo = nullptr;
    mEgg = nullptr;
    mBattleLv1 = nullptr;
    mTrackFireHolder = nullptr;
    mFireHolder = nullptr;
}

void DinoPackunVs2::init() {
    initNerve(GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvWaitStart));
    mCoop.init(mParent, cPhases, cPhaseNum);
    mTrackFireHolder = new DinoPackunTrackFireHolder(64);
    mFireHolder = new DinoPackunFireHolder(16);
    mDemo = new DinoPackunDemo(mParent);
    mDemo->init();
    mEgg = new DinoPackunBattleEggVs2(mParent);
    mEgg->setTrackFire(mTrackFireHolder);
    mEgg->init();
    mBattleLv1 = new DinoPackunBattleVs2Lv1(mParent);
    mBattleLv1->setTrackFire(mTrackFireHolder);
    mBattleLv1->setFireBall(mFireHolder);
    mBattleLv1->init();
    mParent->mBall->mIsBlackDrag = true;
    MR::declareStarPiece(mParent, 24);
}

void DinoPackunVs2::start() {
    if (isNerve(GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvWaitStart))) {
        // Online co-op: another player's game began the fight. This one goes on from the room's phase (update), the
        // opening not played again: the player lands as on any planet.
        if (mCoop.getRoomPhase() > 0) {
            return;
        }

        setNerve(GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvOpeningDemo));
    }
}

void DinoPackunVs2::update() {
    if (const s32 phase = mCoop.update(this)) {
        coopJump(phase);
    }

    if (const s32 music = mCoop.takeMusic()) {
        DinoPackun::coopStartMusic(music);
    }

    DinoPackunSequencer::update();
}

// Online co-op: the room's fight is at `phase`, ahead of this game's. The scenes between are skipped, their outcomes
// applied; the scene at `phase` plays if the local player is in the fight (the opening never: it was the first
// player's), else it is skipped too.
void DinoPackunVs2::coopJump(s32 phase) {
    const s32 from = mCoop.phaseOf(mSpine->getCurrentNerve());
    const bool watching = mCoop.isWatching();
    s32 to = phase == Phase_Opening ? Phase_Egg : phase;
    if (!watching && (to == Phase_Cry || to == Phase_Angry)) {
        to++;
    }

    mCurrentAction = nullptr;
    mParent->endDamageCamera();
    mTrackFireHolder->killAll();
    mFireHolder->killAll();
    if (from == 0) {
        mParent->coopStart();
    }

    if (from <= Phase_Opening && to > Phase_Opening) {
        mParent->coopSkipOpening();
        MR::startBrk(mParent, "Awake");
        MR::emitEffect(mParent->getBallModel(), "TailFire");
        MR::startBrk(mParent->getBallModel(), "Heat");
    }
    if (from <= Phase_Egg && to > Phase_Egg) {
        mParent->coopBreakEgg();
    }

    if (from <= Phase_Angry && to > Phase_Angry) {
        MR::emitEffect(mParent->getBallModel(), "TailFire");
        MR::startBrk(mParent->getBallModel(), "Heat");
    }

    mCoop.jumped(to);
    if (to == Phase_Down && !watching) {
        // Beaten where nobody here saw it: gone, its Power Star out; the music after a boss once the player comes.
        mCoop.keepMusic(Phase_Down);
        mParent->coopDefeat();
        setNerve(GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvWaitStart));
        return;
    }

    if (to != Phase_Cry && to != Phase_Angry && to != Phase_Down) {
        // A fight phase: the music its scene started, once the player is in the fight.
        mCoop.keepMusic(to);
    }

    setNerve(mCoop.nerveOf(to));
}

void DinoPackunVs2::exeOpeningDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = nullptr;
        mDemo->startOpeningDemo();
        MR::startBrk(mParent, "Awake");
        MR::emitEffect(mParent->getBallModel(), "TailFire");
        MR::startBrk(mParent->getBallModel(), "Heat");
    }

    MR::updateActorStateAndNextNerve(this, mDemo, GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvBattleEgg));
}

void DinoPackunVs2::exeBattleEgg() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = mEgg;
    }

    MR::updateActorStateAndNextNerve(this, mEgg, GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvCryDemo));
}

void DinoPackunVs2::exeCryDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = nullptr;
        mDemo->startCryDemo();
    }

    MR::updateActorStateAndNextNerve(this, mDemo, GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvBattleLv1));
}

void DinoPackunVs2::exeBattleLv1() {
    if (MR::isFirstStep(this)) {
        mParent->appearStarPiece(8);
        mBattleLv1->setBattleLevel(1);
        mCurrentAction = mBattleLv1;
    }

    MR::updateActorStateAndNextNerve(this, mBattleLv1, GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvAngryDemo));
}

void DinoPackunVs2::exeAngryDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = nullptr;
        mDemo->startAngryDemo();
        MR::emitEffect(mParent->getBallModel(), "TailFire");
        MR::startBrk(mParent->getBallModel(), "Heat");
    }

    MR::updateActorStateAndNextNerve(this, mDemo, GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvBattleLv2));
}

void DinoPackunVs2::exeBattleLv2() {
    if (MR::isFirstStep(this)) {
        mParent->appearStarPiece(8);
        mBattleLv1->setBattleLevel(2);
        mCurrentAction = mBattleLv1;
    }

    MR::updateActorStateAndNextNerve(this, mBattleLv1, GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvBattleLv3));
}

void DinoPackunVs2::exeBattleLv3() {
    if (MR::isFirstStep(this)) {
        mParent->appearStarPiece(8);
        mBattleLv1->setBattleLevel(3);
        mCurrentAction = mBattleLv1;
    }

    MR::updateActorStateAndNextNerve(this, mBattleLv1, GET_NERVE(DinoPackunVs2, DinoPackunVs2NrvDownDemo));
}

void DinoPackunVs2::exeDownDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = nullptr;
        mDemo->startDownDemo();
    }

    if (MR::updateActorState(this, mDemo)) {
        return;
    }
}

void DinoPackunVs2::exeWaitStart() {
    return;
}
