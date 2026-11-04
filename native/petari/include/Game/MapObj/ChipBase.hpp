#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FlashingCtrl;
class MapPartsRailMover;
class NameObjArchiveListCollector;
class PartsModel;

class ChipBase : public LiveActor {
public:
    enum Type { Type_Blue = 0, Type_Yellow = 1 };

    ChipBase(const char*, s32, const char*);

    virtual ~ChipBase() {
    }

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initModel(const JMapInfoIter&);
    void initSensor();
    void initShadow(const JMapInfoIter&);
    void initJMapParam(const JMapInfoIter&);
    void deactive();
    void setGroupID(s32);
    void setHost(LiveActor*);
    void appearWait();
    void appearFlashing(s32);
    bool requestGet(HitSensor*, HitSensor*);
    bool requestShow();
    bool requestHide();
    bool requestStartControl();
    bool requestEndControl();
    void exeDeactive();
    void exeWait();
    void exeControled();
    void exeFlashing();
    void exeHide();
    bool hasAirBubble() const {
        return mAirBubble != nullptr;
    }
    void exeGot();
    bool isGettable() const;
    // Online co-op: another player in the instance took this chip. It counts toward its group here too (the group's
    // Launch Star appears for everyone), without the oxygen or the sound it gives its taker.
    void takeForOtherPlayer();
    static bool isNeedBubble(const JMapInfoIter&);
    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    FlashingCtrl* mFlashingCtrl;
    MapPartsRailMover* mRailMover;
    PartsModel* mAirBubble;
    const char* mChipName;
    LiveActor* mHost;
    TVec3f mClippingRange;
    s32 mGroupID;
    s32 mChipType;
    bool _B4;
    bool mIsCalcShadow;
};
