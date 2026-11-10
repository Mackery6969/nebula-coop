#include "Game/Boss/DinoPackunVs1.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunBattleVs1Lv1.hpp"
#include "Game/Boss/DinoPackunBattleVs1Lv2.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace NrvDinoPackunVs1 {
    NEW_NERVE(DinoPackunVs1NrvWaitStart, DinoPackunVs1, WaitStart);
    NEW_NERVE(DinoPackunVs1NrvOpeningDemo, DinoPackunVs1, OpeningDemo);
    NEW_NERVE(DinoPackunVs1NrvBattleEgg, DinoPackunVs1, BattleEgg);
    NEW_NERVE(DinoPackunVs1NrvCryDemo, DinoPackunVs1, CryDemo);
    NEW_NERVE(DinoPackunVs1NrvBattleLv1, DinoPackunVs1, BattleLv1);
    NEW_NERVE(DinoPackunVs1NrvAngryDemo, DinoPackunVs1, AngryDemo);
    NEW_NERVE(DinoPackunVs1NrvBattleLv2, DinoPackunVs1, BattleLv2);
    NEW_NERVE(DinoPackunVs1NrvBattleLv3, DinoPackunVs1, BattleLv3);
    NEW_NERVE(DinoPackunVs1NrvDownDemo, DinoPackunVs1, DownDemo);
};  // namespace NrvDinoPackunVs1

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
        GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvOpeningDemo), GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvBattleEgg), GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvCryDemo),
        GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvBattleLv1), GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvAngryDemo), GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvBattleLv2),
        GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvBattleLv3), GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvDownDemo),
    };
    const s32 cPhaseNum = sizeof(cPhases) / sizeof(cPhases[0]);
};  // namespace

DinoPackunVs1::DinoPackunVs1(DinoPackun* pPackun)
    : DinoPackunSequencer("ディノパックン1戦目進行", pPackun), mDemo(nullptr), mEgg(nullptr), mBattleLv1(nullptr), mBattleLv2(nullptr) {
}

void DinoPackunVs1::init() {
    initNerve(GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvWaitStart));
    mCoop.init(mParent, cPhases, cPhaseNum);
    mDemo = new DinoPackunDemo(mParent);
    mDemo->init();
    mEgg = new DinoPackunBattleEgg(mParent);
    mEgg->init();
    mBattleLv1 = new DinoPackunBattleVs1Lv1(mParent);
    mBattleLv1->init();
    mBattleLv2 = new DinoPackunBattleVs1Lv2(mParent);
    mBattleLv2->init();
    MR::declareStarPiece(mParent, 24);
}

void DinoPackunVs1::start() {
    if (isNerve(GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvWaitStart))) {
        // Online co-op: another player's game began the fight. This one goes on from the room's phase (update), the
        // opening not played again: the player lands as on any planet.
        if (mCoop.getRoomPhase() > 0) {
            return;
        }

        setNerve(GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvOpeningDemo));
    }
}

void DinoPackunVs1::update() {
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
void DinoPackunVs1::coopJump(s32 phase) {
    const s32 from = mCoop.phaseOf(mSpine->getCurrentNerve());
    const bool watching = mCoop.isWatching();
    s32 to = phase == Phase_Opening ? Phase_Egg : phase;
    if (!watching && (to == Phase_Cry || to == Phase_Angry)) {
        to++;
    }

    mCurrentAction = nullptr;
    mParent->endDamageCamera();
    if (from == 0) {
        mParent->coopStart();
    }

    if (from <= Phase_Opening && to > Phase_Opening) {
        mParent->coopSkipOpening();
    }
    if (from <= Phase_Egg && to > Phase_Egg) {
        mParent->coopBreakEgg();
    }

    mCoop.jumped(to);
    if (to == Phase_Down && !watching) {
        // Beaten where nobody here saw it: gone, its Power Star out; the music after a boss once the player comes.
        mCoop.keepMusic(Phase_Down);
        mParent->coopDefeat();
        setNerve(GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvWaitStart));
        return;
    }

    if (to != Phase_Cry && to != Phase_Angry && to != Phase_Down) {
        // A fight phase: the music its scene started, once the player is in the fight.
        mCoop.keepMusic(to);
    }

    setNerve(mCoop.nerveOf(to));
}

void DinoPackunVs1::exeOpeningDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = nullptr;
        mDemo->startOpeningDemo();
    }

    MR::updateActorStateAndNextNerve(this, mDemo, GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvBattleEgg));
}

void DinoPackunVs1::exeBattleEgg() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = mEgg;
    }

    MR::updateActorStateAndNextNerve(this, mEgg, GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvCryDemo));
}

void DinoPackunVs1::exeCryDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = nullptr;
        mDemo->startCryDemo();
    }

    MR::updateActorStateAndNextNerve(this, mDemo, GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvBattleLv1));
}

void DinoPackunVs1::exeBattleLv1() {
    if (MR::isFirstStep(this)) {
        mParent->appearStarPiece(8);
        mCurrentAction = mBattleLv1;
    }

    MR::updateActorStateAndNextNerve(this, mBattleLv1, GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvAngryDemo));
}

void DinoPackunVs1::exeAngryDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = nullptr;
        mDemo->startAngryDemo();
    }

    MR::updateActorStateAndNextNerve(this, mDemo, GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvBattleLv2));
}

void DinoPackunVs1::exeBattleLv2() {
    if (MR::isFirstStep(this)) {
        mParent->appearStarPiece(8);
        mBattleLv2->setMiddleBattle();
        mBattleLv2->setStartFromDemo();
        mCurrentAction = mBattleLv2;
    }

    MR::updateActorStateAndNextNerve(this, mBattleLv2, GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvBattleLv3));
}

void DinoPackunVs1::exeBattleLv3() {
    if (MR::isFirstStep(this)) {
        mParent->appearStarPiece(8);
        mBattleLv2->setLastBattle();
        mBattleLv2->setStartContinueBattle();
        mCurrentAction = mBattleLv2;
    }

    MR::updateActorStateAndNextNerve(this, mBattleLv2, GET_NERVE(DinoPackunVs1, DinoPackunVs1NrvDownDemo));
}

void DinoPackunVs1::exeDownDemo() {
    if (MR::isFirstStep(this)) {
        mCurrentAction = nullptr;
        mDemo->startDownDemo();
    }

    MR::updateActorState(this, mDemo);
}

void DinoPackunVs1::exeWaitStart() {
}
