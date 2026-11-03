#pragma once

#include "Game/Screen/LayoutActor.hpp"

struct NebulaNametagsParts;

// Online co-op's nametags (docs/COOP_PLAN.md "Remote players"), as sm64coopdx's: each other player's name over their
// copy's head (NebulaRemotePlayer), in their character's colour on a small dark window, in the game's own font. A name
// fades out behind a wall, far away, off the screen and during the game's cutscenes, and shrinks with distance. Drawn
// with the HUD.
class NebulaNametags : public LayoutActor {
public:
    NebulaNametags();

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

private:
    /* 0x20 */ NebulaNametagsParts* mParts;
};
