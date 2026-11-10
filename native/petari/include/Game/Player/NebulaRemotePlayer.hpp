#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Player/MarioModelTint.hpp"
#include <nebula_game_api.h>

class NebulaBubble;
class XanimePlayer;
class J3DAnmTransform;

// Online co-op (docs/COOP_PLAN.md "Remote players"): another player in this instance, drawn as their Mario or Luigi
// where their snapshots put them, playing the animation they play. It has no physics or state of its own; its model is
// the player's own (Mario's or Luigi's, as MarioActor loads it) with Mario's animations (MarioAnimator's table), driven
// as GhostPlayer drives the cosmic ghost's. It is lit as MarioActor lights the local Mario (the player light of the
// light area it stands in, its ambient colour and the stage's fog), and plays Mario's spin effects on its own joints.
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
    // Shared objects (NebulaCoopWorld): the sensor another player's hit is played again from here, the copy of player
    // `number` (where it stands now), none when nobody shows them.
    static HitSensor* getHitSender(u32 number);
    // Where its name goes: above its head, along its up.
    TVec3f getNametagPos() const;
    // Nothing touches the local Mario now: dead, in a demo, bound to something (a Launch Star, a bubble...), a Boo,
    // hidden or out of the player's control. GameScene sends it as NEBULA_COOP_PLAYER_INTANGIBLE.
    static bool isLocalIntangible();
    // Races (docs/COOP_PLAN.md "Races and timers"): each player races their own rival, and while either the local
    // player or another one races, the two are see-through to each other and don't touch. The racing actors note it
    // each frame they run a race (the race manager, the cosmic racer, the ring swim); GameScene sends it as
    // NEBULA_COOP_PLAYER_RACING.
    static void noteLocalRacing();
    static bool isLocalRacing();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAnim();
    virtual void calcAndSetBaseMtx();
    virtual void draw() const;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void show(const NebulaCoopRemotePlayer& rRemote);
    void hide();
    // Whether the local Mario and this player may touch now (the rules, the first second of either, intangibility).
    bool isTouchable() const;
    // See-through and untouchable: the local player or this one races.
    bool isGhosted() const;
    // This player's attack, as they saw the local Mario, onto him.
    void tryAttackLocal();
    // The animation the snapshot names onto the copy: changed when it differs, played on at its speed here and eased
    // onto the snapshot's frame (snapped when far off), so it runs smoothly between snapshots and through its loops.
    void updateAnimation();
    // The bcks Mario's code swapped onto the group's tracks (NebulaCoopPlayer::tracks), the group's own elsewhere.
    void updateTracks();
    // Mario's effects for an animation (MarioAnimator's spinEntry, spinUpdate and spinClose): started as it starts,
    // stopped as it ends.
    void startAnimationEffect();
    void updateAnimationEffect();
    void stopAnimationEffect();
    // The effect the animation last started plays on this copy (its character's own), none for most.
    const char* getAnimationEffectName() const;
    // MarioActor::updateLightList's tint for this copy: its light's ambient colour, a white material.
    void updateLightTint();

    /* 0x90 */ bool mIsLuigi;
    u32 mNumber;  // the player shown (NebulaCoopRemotePlayer::number), 0 for none
    char mName[32];
    XanimePlayer* mXanimePlayer;
    NebulaCoopPlayer mState;  // the latest snapshot shown
    char mGroup[48];          // the animation whose effect last started, as the snapshot names it
    f32 mLastFrame;           // the snapshot's frame last shown, to tell a restarted animation
    char mTrackName[4][32];   // the swapped-in bck each track was last told to play ("" none), and that bck
    J3DAnmTransform* mTrackBck[4];
    MarioModelTint mLightTint; // updateLightTint's
    MarioModelTint mGhostTint; // its see-through look while isGhosted
    bool mGhosted;             // drawn so (its shadow off)
    TVec3f mFront;
    TVec3f mUp;
    u32 mRewindMs;    // NebulaCoopRemotePlayer::rewind_ms
    s32 mShownFrames; // frames since this copy began showing its player
    s32 mStompTimer;  // frames until their landing on the local Mario's head may squash him again
    bool mSolid;      // its sensor is on
    NebulaBubble* mBubble;  // round them while they're in a bubble (NebulaCoopBubble)
};
