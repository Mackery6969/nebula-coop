#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AstroDomeCameraController;
class GalaxyConfirmLayout;
class GalaxySelectBackButton;
class GalaxySelectInfo;
class MiniatureGalaxy;
class NebulaSelectPlayers;

class AstroDomeGalaxySelector : public LiveActor {
public:
    AstroDomeGalaxySelector(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void kill();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void showGalaxyInfo(const MiniatureGalaxy*);
    bool tryStartLectureDemo(const Nerve*);
    void showCoopPlayers();

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

    AstroDomeCameraController* mCameraController;
    GalaxySelectBackButton* mBackButton;
    GalaxySelectInfo* mInfo;
    GalaxyConfirmLayout* mConfirmLayout;
    bool _9C;
    // Online co-op: who plays in each miniature galaxy, under it, while the player picks one.
    NebulaSelectPlayers* mCoopPlayers;
};
