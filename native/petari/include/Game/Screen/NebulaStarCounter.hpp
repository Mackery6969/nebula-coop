#pragma once

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;
class CounterLayoutAppearer;
struct NebulaStarCounterTint;

// sm64coopdx's Personal Star Counter on Galaxy's HUD (docs/COOP_PLAN.md "Personal Star Counter"): the save's star
// counter (StarCounter, its own layout) twice more under it, its star tinted. Red: the Power Stars this player got in
// the online co-op room, shown only in one. Green: all they ever got (NebulaStat_PowerStars). Each counts only stars new
// to the save. CounterLayoutController shows and hides them with the save's counter, so they come in and out as it
// does (when the player stands still) and count up as it does. Only while the room plays the mod (the host's choice).
class NebulaStarCounter : public LayoutActor {
public:
    enum Kind {
        Kind_Room,
        Kind_AllTime,
    };

    NebulaStarCounter(Kind kind);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();
    virtual void calcAnim();

    void disappear();
    void exeAppear();
    void exeWait();
    void exeCountUp();
    void exeDisappear();

private:
    // What it counts now; -1 for the room's outside a room (the counter stays hidden).
    s32 getCount() const;
    // False while the room doesn't play the Personal Star Counter (a built-in mod the host turns on).
    bool isWanted() const;
    // The screen's units from one counter to the next below: the layout's tallest part.
    f32 getLineStep() const;

    /* 0x20 */ Kind mKind;
    /* 0x24 */ s32 mCount;
    /* 0x28 */ CounterLayoutAppearer* mLayoutAppearer;
    /* 0x2C */ CountUpPaneRumbler* mPaneRumbler;
    /* 0x30 */ NebulaStarCounterTint* mTint;
};
#endif
