#pragma once

#include "Game/System/NebulaCoopBoss.hpp"
#include "Game/System/NerveExecutor.hpp"

class BossKameck;
class BossKameckBattleDemo;
class HitSensor;
class TableIter;

class BossKameckSequencer : public NerveExecutor {
public:
    BossKameckSequencer(const char*);

    virtual ~BossKameckSequencer() override {
    }

    virtual void start() {
    }

    virtual void init(BossKameck*, const TableIter&);
    virtual void update();
    virtual void attackSensor(HitSensor*, HitSensor*) = 0;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) = 0;

    // Online co-op (NebulaCoopBossPhases): the phases are opening (1), the fights with the power-up scene among them,
    // and the beaten scene (the last). coopJump goes on to the room's `phase` from `from` (0 not begun);
    // coopAfterOpening and coopBeforeDown are a fight's own outcomes of the scenes skipped.
    void coopInit(const Nerve* const* pPhases, s32 num, s32 powerUpPhase);
    bool coopStart();
    void coopJump(s32 from, s32 phase);
    virtual void coopAfterOpening() {
    }
    virtual void coopBeforeDown() {
    }

    BossKameck* mBossKameck;
    BossKameckBattleDemo* mBattleDemo;
    NebulaCoopBossPhases mCoop;
    s32 mCoopPowerUpPhase;
};