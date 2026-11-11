#include "Game/NPC/CometEventExecutorTimeLimit.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/TimeLimitLayout.hpp"
#include "Game/System/NebulaCoopWorld.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sCometTimeLimitDefault = NebulaFrameClock::stepsOfSeconds(30);
    // Online co-op: how far behind the instance's clock this one may run before it is moved on (its own appearing
    // takes some frames, and the server's word some milliseconds).
    const s32 sCoopClockSlack = NebulaFrameClock::stepsOfMilliseconds(1500);
};  // namespace

namespace NrvCometEventExecutorTimeLimit {
    NEW_NERVE(CometEventExecutorTimeLimitStartEvent, CometEventExecutorTimeLimit, StartEvent);
};  // namespace NrvCometEventExecutorTimeLimit

CometEventExecutorTimeLimit::CometEventExecutorTimeLimit(u32 timeLimit)
    : LiveActor("制限時間コメット実行者"), mTimeLimitLayout(nullptr), mTimeLimit(timeLimit) {
    lumacore::setClass(this, lumacore::kClassId< CometEventExecutorTimeLimit >);
}

void CometEventExecutorTimeLimit::init(const TableIter&) {
    MR::connectToSceneLayoutMovement(this);
    MR::invalidateClipping(this);

    mTimeLimitLayout = new TimeLimitLayout(getTimeLimit());
    mTimeLimitLayout->setDisplayModeOnNormal(true);
    mTimeLimitLayout->initWithoutIter();
    mTimeLimitLayout->kill();

    MR::connectToSceneLayout(mTimeLimitLayout);
    initNerve(GET_NERVE(CometEventExecutorTimeLimit, CometEventExecutorTimeLimitStartEvent));
    kill();
}

void CometEventExecutorTimeLimit::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(CometEventExecutorTimeLimit, CometEventExecutorTimeLimitStartEvent));
}

void CometEventExecutorTimeLimit::kill() {
    LiveActor::kill();
    mTimeLimitLayout->kill();
}

void CometEventExecutorTimeLimit::exeStartEvent() {
    if (MR::isFirstStep(this)) {
        mTimeLimitLayout->appear();
        mTimeLimitLayout->setTimeLimit(getTimeLimit());
        // Online co-op (docs/COOP_PLAN.md "Races and timers"): the clock is the instance's, everyone's together. A
        // player coming later counts from where the first one's is; the time up comes to all at once, and once
        // everyone in it missed the mission starts over with a fresh clock.
        NebulaCoopWorld::startMissionClock();
    }

    const s32 shared = NebulaCoopWorld::getMissionClock();
    if (shared > static_cast< s32 >(mTimeLimitLayout->getTime()) + ::sCoopClockSlack) {
        mTimeLimitLayout->catchUpTo(static_cast< u32 >(shared));
    }

    if (MR::canStartDemo() && mTimeLimitLayout->isReadyToTimeUp()) {
        MR::forceKillPlayerByWaterRace();
    }
}

u32 CometEventExecutorTimeLimit::getTimeLimit() const {
    return NebulaFrameClock::stepsOfSeconds(mTimeLimit != 0 ? mTimeLimit : ::sCometTimeLimitDefault);
}
