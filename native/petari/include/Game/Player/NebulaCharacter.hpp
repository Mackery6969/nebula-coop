#pragma once

// The player's character (Options' PLAYER page, NEBULA_COOP_MENU_CHARACTER), switched live as sm64coopdx's: Mario or
// Luigi in any save, the save itself the same for both.
//
// The game builds the player's model, animations, power-up forms and effects once, as a stage starts, for one
// character. So each game scene plays the character wanted as it started (MR::isPlayerLuigi), and a switch in play
// reloads the player's spot: the screen fades, the stage starts again as a retry does (no life lost, no lives shown),
// and the player stands where they stood, facing the same way, with the health they had. What a retry resets resets
// (enemies, the coins count, a stage's own progress the room doesn't keep). In a comet the switch waits for the next
// stage start, so a timer can't be wound back by it. The others see the new character as soon as the reload is done:
// it travels in the player's snapshot (NEBULA_COOP_PLAYER_LUIGI) and the session (Session::set_character).
//
// The character is one of NebulaCharacters' (Mario, Luigi, or a mod's): the one Options' PLAYER page names
// (NebulaHost::coopCharacterWanted, by id), unless a mod picked one for the player (set_player_character_id, as
// sm64coopdx's overrideModelIndex), which a switch reloads to as well.
namespace NebulaCharacter {
    // GameScene::init, first: the scene plays the character wanted now (but the main menu's: see isLuigi).
    void startScene();
    // ~GameScene: no scene, so MR::isPlayerLuigi follows the wanted character again.
    void endScene();
    // The character played (NebulaCharacters' index): the scene's, or the wanted one outside a game scene and in the
    // main menu's.
    s32 index();
    // Its base is Luigi (MR::isPlayerLuigi).
    bool isLuigi();
    // The character wanted now: a mod's pick, else the Options page's (Mario's when its id names no character here).
    s32 wanted();
    // A mod's pick for the player (-1: none, the Options page's again), played from the next start or by a switch's
    // reload. False for an index with no character.
    bool setOverride(s32 index);
    s32 getOverride();
    // Each GameScene frame. `inPlay`: the scene plays (its Action nerve: no menu, map or star sequence over it).
    void update(bool inPlay);
    // GameSequenceProgress, as the reload is asked for: a switch's, so no lives display as after a miss.
    bool isSwitchReload();
};  // namespace NebulaCharacter
