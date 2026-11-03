#pragma once


// Online co-op's cutscenes and transitions (docs/COOP_PLAN.md "Cutscenes and transitions"), as sm64coopdx has them:
// each player's demos are its own and stop nobody else, and the save's progress reaches everyone without its
// cutscene. What moves a player along still does:
// - A star taken in an instance is told to the others there (sm64coopdx's collect_star): a Grand Star takes them
//   along to its get and return, as sm64coopdx's Bowser 3 Grand Star does; a Power Star shows for them as one already
//   had (as a finished mission's does), theirs in the save, and they play on or take it for its return.
// - A star the room gave this player, taken later by the player itself, has its first return: the story's demos
//   (Rosalina's talks, the domes, the first trip to the Observatory) play as they would have, not the short one for
//   a star already had.
namespace NebulaCoopDemos {
    // GameScene::init: whether this scene is in a room (out of one, the room's stars are forgotten).
    void startScene(bool inRoom);
    // NebulaCoopWorld: the room gave the current save a Power Star it didn't have.
    void noticeRoomStar(const char* pGalaxyName, s32 starId);
    // GameScene: the local player took a star (its get sequence requested), to tell the instance.
    void noticeStarGot(s32 starId, bool isGrand);
    // NebulaCoopWorld: another player in the instance took a star.
    void applyStarGot(s32 starId, bool isGrand);
    // Each game scene frame while it plays (GameScene's action, no demo running): a star another player took shows
    // as had here. True with a Grand Star to take this player along: the scene then plays its get (`pStarId`).
    bool updateInPlay(s32* pStarId);
    // GameSequenceProgress::requestGalaxyMove, a star's result, the scene's start save just kept: a star the room gave
    // and the player now took is left out of it, so the result's demos are the star's first.
    void beforeStageResult(const char* pClearedStageName, s32 clearedStarId);
};  // namespace NebulaCoopDemos
