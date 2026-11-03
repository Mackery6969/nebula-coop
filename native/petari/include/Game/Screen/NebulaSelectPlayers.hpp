#pragma once

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"
#include <JSystem/JGeometry/TVec.hpp>

struct NebulaSelectPlayersParts;

namespace nw4r {
    namespace lyt {
        class Pane;
    };  // namespace lyt
};  // namespace nw4r

// Online co-op on the galaxy select and the star select (docs/COOP_PLAN.md "Instances"), as sm64coopdx's act select
// shows a player's head over the act they play: under a galaxy, or a star, the other players inside it, their names in
// their character's colour on a small dark window. Only in a room; the local player is never listed. Built from the
// game's own window and font (NebulaPaneKit), as the nametags are.
//
// Its owner names each place it shows, every frame it shows it (a spot, 0 to cSpotsMax - 1): a place that isn't named
// again fades out, as does one nobody else plays in.
class NebulaSelectPlayers : public LayoutActor {
public:
    enum { cSpotsMax = 16 };

    // `scene`: drawn with the scene's layouts (the dome's galaxy select). Otherwise its owner moves and draws it (the
    // star select, a scene of its own).
    NebulaSelectPlayers(bool scene);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    // The players in `pStage`, in its star `scenario` (or any of its stars when 0), under a point of the stage's
    // world (a miniature galaxy in the dome): nothing when it is off the screen or behind the camera.
    void showAtWorld(s32 spot, const char* pStage, s32 scenario, const TVec3f& rPos);
    // The same under a pane of another layout drawn on the same screen (a star of the star select), `drop` screen units
    // below its middle.
    void showAtPane(s32 spot, const char* pStage, s32 scenario, const nw4r::lyt::Pane* pPane, f32 drop);

private:
    void request(s32 spot, const char* pStage, s32 scenario, f32 x, f32 y);

    /* 0x20 */ NebulaSelectPlayersParts* mParts;
    /* 0x24 */ bool mScene;
};
#endif
