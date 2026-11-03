#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AstroDomeCameraController;
class GalaxyConfirmLayout;
class GalaxySelectBackButton;
class GalaxySelectInfo;
class MiniatureGalaxy;
#ifdef TARGET_PC
class NebulaSelectPlayers;
#endif

class AstroDomeGalaxySelector : public LiveActor {
public:
    AstroDomeGalaxySelector(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void kill();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void showGalaxyInfo(const MiniatureGalaxy*);
    bool tryStartLectureDemo(const Nerve*);
#ifdef TARGET_PC
    void showCoopPlayers();
#endif

    void exeGalaxySelectStart();
    void exeGalaxySelect();
    void exeGalaxySelectCancel();
    void exeGalaxyConfirmStart();
    void exeGalaxyConfirm();
    void exeGalaxyConfirmCancel();
    void exeDemoJumpOut();
    void exeDemoDomeLecture();
    void exeGalaxyMoveLecture();
    void exeWaitStartDemo();

    /* 0x8C */ AstroDomeCameraController* mCameraController;
    /* 0x90 */ GalaxySelectBackButton* mBackButton;
    /* 0x94 */ GalaxySelectInfo* mInfo;
    /* 0x98 */ GalaxyConfirmLayout* mConfirmLayout;
    /* 0x9C */ bool _9C;
#ifdef TARGET_PC
    // Online co-op: who plays in each miniature galaxy, under it, while the player picks one.
    NebulaSelectPlayers* mCoopPlayers;
#endif
};
