#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MarioActor;

// A bubble round a player (sm64coopdx's bhv_bubble_player): the air bubble's model, a little bigger than Mario,
// turning slowly and swelling, that bursts as its player is let out.
class NebulaBubble : public LiveActor {
public:
    NebulaBubble();

    virtual void init(const JMapInfoIter& rIter);
    virtual void calcAndSetBaseMtx();

    // Round `rCenter` this frame, appearing if it wasn't.
    void follow(const TVec3f& rCenter);
    // Burst, if it shows.
    void pop();
    // Gone without a burst.
    void hide();

    s32 mFrame;  // frames shown, for its swelling
};

// Online co-op's bubble on death (docs/COOP_PLAN.md "Bubble on death", sm64coopdx's mario_set_bubbled and
// act_bubbled): while the room has it on, a player whose Mario would miss with another player alive in the same part of
// the stage loses a life and floats in a bubble instead, flown with the stick (A rises, Z sinks), through walls, until
// it touches that player: then it bursts and the player plays on where they are, with full health. With nobody left to
// fly to, the bubble ends in the miss after all (its life given back, as the miss takes one).
//
// The part of the stage: where the stage starts, or the warp pipe the player last came out of (going back down that
// pipe returns to the part before), so a player down a pipe can't be reached from above, as sm64coopdx's areas.
// Each game decides for its own Mario, as sm64coopdx: no message is sent, the others see the bubble in its snapshot
// (NEBULA_COOP_PLAYER_BUBBLED).
namespace NebulaCoopBubble {
    // GameScene, as a scene starts: nothing bubbled, the stage's own part.
    void forgetAll();
    // GameScene, after the stage's actors are placed, in a room: the local player's bubble.
    void tryCreate();
    // A bubble for a remote player's copy (NebulaRemotePlayer), made while the scene is placed.
    NebulaBubble* createModel();

    // MarioActor's miss states, on their first step: true when the miss became a bubble (MarioActor's NebulaBubble
    // state then).
    bool tryStart(MarioActor* pActor);
    // MarioActor's NebulaBubble state, each frame: flying, then burst near another player or the miss.
    void updateLocal(MarioActor* pActor);
    bool isLocalBubbled();

    // The part of the stage the local player is in (NebulaCoopPlayer::area).
    u32 getArea();
    // A warp pipe took the local player from the pipe at `rEntrance` to the one at `rExit` (their placed positions).
    void noticePipeWarp(const TVec3f& rEntrance, const TVec3f& rExit);
    // The scene's miss demo started (a death that didn't pass through MarioActor's miss states first): no bubble now.
    void noticeGameOverDemo();
};  // namespace NebulaCoopBubble
