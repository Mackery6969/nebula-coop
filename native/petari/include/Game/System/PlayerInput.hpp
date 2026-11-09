#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include "Game/System/NebulaInput.hpp"

class InputTilt;
class InputButtons;
class InputSwing;
class InputInfoChecker;
class InputPointer;
class PlayerInputReader;
class InputRumble;
class InputStick;
struct PlayerInputSample;

class PlayerInput {
public:
    // `pReadInfo`: the reader of this pad's samples, which its parts set up as they are made.
    PlayerInput(s32, PlayerInputReader* pReadInfo);

    void update();
    // A neutral controller for the rest of the frame: no buttons, stick or swing (the pointer stays). A menu over the
    // running game reads the controller first, then holds it from the game.
    void holdForMenu();
    InputRumble* getRumbleInstance() const;
    s32 getBattery() const;
    void resetPad();
    void connected();
    void disconnected();
    static void connectCallback(s32, s32);
    static void extensionCallback(s32, s32);
    PlayerInputSample* getSample(u32) const;
    s32 getValidStatusCount() const;
    s32 getEnableAccelPastCount(u32) const;
    bool getAcceleration(TVec3f*, u32) const;
    bool getPastAcceleration(TVec3f*, s32, u32) const;

    s32 mChannel;
    PlayerInputReader* mReadInfo;
    InputButtons* mButton;
    InputPointer* mPointer;
    InputTilt* mMainTilt;
    InputSwing* mMainSwing;
    InputRumble* _18;
    InputRumble* _1C;
    InputStick* mStick;
    InputTilt* mSubTilt;
    InputSwing* mSubSwing;
    InputInfoChecker* mInfoChecker;
    bool mIsConnected;
    bool mHasMoveStick;
};

namespace MR {
    bool hasMoveStick(const PlayerInputSample*);
};  // namespace MR
