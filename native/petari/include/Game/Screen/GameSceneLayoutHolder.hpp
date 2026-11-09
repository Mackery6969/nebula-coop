#pragma once

#include "Game/NameObj/NameObj.hpp"

class CameraInfo;
class CounterLayoutController;
class InformationMessage;
class MarioMeter;
class MarioSubMeter;
class MissLayout;
class NebulaAchievementBoard;
class NebulaAchievementToast;
class NebulaCoopChat;
class NebulaCoopPopups;
class NebulaPlayerList;
class NebulaNametags;
class NoteCounter;
class OneUpBoard;
class PurpleCoinCounter;
class YesNoLayout;

class GameSceneLayoutHolder : public NameObj {
public:
    /// @brief Creates a new `GameSceneLayoutHolder`.
    GameSceneLayoutHolder();

    virtual void movement() override;

    void createPurpleCoinCounter();
    PurpleCoinCounter* getPurpleCoinCounter() const;
    void initLifeCount(u32);
    void setLifeCount(u32);
    void setOxygenRatio(f32);
    void setBeePowerRatio(f32);
    void changeLifeMeterModeGround();
    void changeLifeMeterModeWater();
    void changeLifeMeterModeBee();
    void requestOneUp(s32);
    void requestPowerUpHPMeter();
    void requestForceAppearHPMeter();
    void startMiss();
    bool isMissAnimEnd() const;
    void activateDefaultGameLayout();
    void deactivateDefaultGameLayout();
    void invalidateStatusLayout();

    CameraInfo* mCameraInfo;
    InformationMessage* mInformationMessage;
    MarioMeter* mMarioMeter;
    MarioSubMeter* mMarioSubMeter;
    MissLayout* mMissLayout;
    NoteCounter* mNoteCounter;
    OneUpBoard* mOneUpBoard;
    YesNoLayout* mYesNoLayout;
    PurpleCoinCounter* mPurpleCoinCounter;
    bool mIsStatusInvalidatePending;  // movement() invalidates the status layout once
    CounterLayoutController* mCounterLayoutCtrl;
    bool mIsActiveDefaultGameLayout;
    // Nebula's achievement pop-up: GameScene runs and draws it (only in play, above everything else in 2D).
    NebulaAchievementToast* mAchievementToast;
    // Nebula's achievement board, in the Observatory (its Luma opens it); null elsewhere.
    NebulaAchievementBoard* mAchievementBoard;
    // Online co-op's chat: GameScene runs and draws it (over everything else in 2D, the pop-up included).
    NebulaCoopChat* mCoopChat;
    // Online co-op's pop-ups in the top-right corner: GameScene runs and draws them, as the chat.
    NebulaCoopPopups* mCoopPopups;
    // Online co-op's player list (Tab or the controller's Share held): GameScene runs and draws it over everything.
    NebulaPlayerList* mPlayerList;
    NebulaNametags* mNametags;
};

namespace MR {
    GameSceneLayoutHolder* getGameSceneLayoutHolder();
};  // namespace MR
