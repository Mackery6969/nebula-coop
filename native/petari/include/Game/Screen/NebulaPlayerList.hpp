#pragma once

#include "Game/Screen/LayoutActor.hpp"

struct NebulaPlayerListParts;

// Online co-op's player list (docs/COOP_PLAN.md "Player list"), as sm64coopdx's: while Tab (a keyboard) or the
// Wii Remote's Minus (a controller's Share, Back, Select or View) is held, a window at the top of the screen with every
// player in the room by number, their name in their character's colour, the host marked, the galaxy and star each
// plays and the round trip to them. It shows over play and doesn't stop it. Built from the game's own window and font
// (NebulaPaneKit) and laid out for the screen's shape. GameScene runs it each frame and draws it over everything.
class NebulaPlayerList : public LayoutActor {
public:
    NebulaPlayerList();

    virtual void init(const TableIter& rIter);
    virtual void control();

    // GameScene: one frame, and the drawing over everything else.
    void update();
    void drawOnTop() const;

private:
    /* 0x20 */ NebulaPlayerListParts* mParts;
};
