#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_game_api.h>

class NebulaBubble;
class XanimePlayer;

// Online co-op (docs/COOP_PLAN.md "Remote players"): another player in this instance, drawn as their Mario or Luigi
// where their snapshots put them, playing the animation they play. It has no physics or state of its own; its model is
// the player's own (Mario's or Luigi's, as MarioActor loads it) with Mario's animations (MarioAnimator's table), driven
// as GhostPlayer drives the cosmic ghost's.
//
// Player collisions (docs/COOP_PLAN.md "Player collisions", sm64coopdx's interact_player and interact_player_pvp) as
// the room's rules have them: like the cosmic ghost, its body sensor pushes the local Mario away and takes his stomps,
// spins and ground pounds; its player's own attacks reach the local Mario as that player saw him (lag compensation),
// and knock him back or hurt him. Each game moves only its own Mario, so no message is sent for any of it.
class NebulaRemotePlayer : public LiveActor {
public:
    explicit NebulaRemotePlayer(bool isLuigi);

    // GameScene, after the stage's own actors are placed (Mario's animations are ready then): the copies for this
    // scene, as many of each character as the room may have others, when there is a room.
    static void tryCreate();
    // GameScene, as a scene starts: the last scene's copies are gone.
    static void forgetAll();
    // Each game scene frame: the others in this instance as the host has them, each onto a copy of their character
    // (the same one while they stay), the copies nobody uses hidden.
    static void updateAll();
    // The copies this scene has (shown or not), for their nametags (NebulaNametags).
    static s32 getCopyNum();
    static const NebulaRemotePlayer* getCopy(s32 index);
    // Where its name goes: above its head, along its up.
    TVec3f getNametagPos() const;
    // Nothing touches the local Mario now: dead, in a demo, bound to something (a Launch Star, a bubble...), a Boo,
    // hidden or out of the player's control. GameScene sends it as NEBULA_COOP_PLAYER_INTANGIBLE.
    static bool isLocalIntangible();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void draw() const;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void show(const NebulaCoopRemotePlayer& rRemote);
    void hide();
    // Whether the local Mario and this player may touch now (the rules, the first second of either, intangibility).
    bool isTouchable() const;
    // This player's attack, as they saw the local Mario, onto him.
    void tryAttackLocal();

    /* 0x90 */ bool mIsLuigi;
    u32 mNumber;  // the player shown (NebulaCoopRemotePlayer::number), 0 for none
    char mName[32];
    XanimePlayer* mXanimePlayer;
    NebulaCoopPlayer mState;  // the latest snapshot shown
    char mGroup[48];          // the animation last started, as the snapshot names it
    char mBck[32];
    TVec3f mFront;
    TVec3f mUp;
    u32 mRewindMs;    // NebulaCoopRemotePlayer::rewind_ms
    s32 mShownFrames; // frames since this copy began showing its player
    s32 mStompTimer;  // frames until their landing on the local Mario's head may squash him again
    bool mSolid;      // its sensor is on
    NebulaBubble* mBubble;  // round them while they're in a bubble (NebulaCoopBubble)
};
