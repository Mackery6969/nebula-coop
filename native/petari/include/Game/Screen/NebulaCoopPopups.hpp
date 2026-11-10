#pragma once

#include "Game/Screen/LayoutActor.hpp"

struct NebulaCoopPopupsParts;

// Online co-op's pop-ups (NebulaHostServices::coop_popup_take), sm64coopdx's: a small box in the screen's top-right
// corner for a few seconds as another player joins or leaves, goes to the Observatory or a galaxy, comes to or leaves
// this player's galaxy, dies or gets a Power Star. The newest is on top and pushes the others down as it slides in;
// each fades at its end. The host decides which come (the player turns each kind off on Options' PLAYER page) and
// sends none outside a room's game. GameScene runs and draws it over its 2D, as the chat.
class NebulaCoopPopups : public LayoutActor {
public:
    NebulaCoopPopups();

    virtual void init(const TableIter& rIter);
    virtual void control();

    // GameScene: one frame, and the drawing at the end of its 2D.
    void update();
    void drawOnTop() const;

private:
    NebulaCoopPopupsParts* mParts;
};
