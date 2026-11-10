#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class RunawayRabbit;
class RunawayTico;

class RunawayRabbitCollect : public LiveActor {
public:
    RunawayRabbitCollect(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;

    s32 calcCompleteRabbitCount() const;
    void linkMsgCtrl();
    void noticeAppearRabbit(RunawayRabbit* pRabbit);
    void noticeCaughtRabbit(RunawayRabbit* pRabbit);
    // Co-op: another player caught pRabbit: its group's other hiding places are left empty here too.
    void noticeCaughtRabbitByOtherPlayer(RunawayRabbit* pRabbit);
    // Co-op: another player's cutscene started the chase (it plays once for the room): started here as it ends.
    void startForOtherPlayer();
    void startChase();
    s32 getRabbitNum() const {
        return mRabbitNum;
    }
    RunawayRabbit* getRabbit(s32 index) const {
        return mRabbit[index];
    }
    void appearTico(RunawayTico* pTico, const Vec3f& rPos);

    void exeWait();
    void exeActive();

private:
    ActorCameraInfo* mCameraInfo;
    RunawayRabbit** mRabbit;
    s32 mRabbitNum;
    RunawayTico** mTico;
    s32 mTicoNum;
    s32 mCaughtRabbitCount;
    s32 mCompleteRabbitCount;
    s32 mBgmState;
    bool mIsAppearTico[3];
};
