#pragma once

#include <nebula_types.h>

struct NebulaPlugin;
struct MarioConstTable;

// The characters a player can play, as sm64coopdx's character table (gCharacters, CT_MARIO...): the game's two, Mario
// (0) and Luigi (1), then the ones mods add (luma.json "characters", add_character), in the order they were added.
//
// A character is a look on one of the game's two players, its base ("like"): the base gives the animations
// (MarioAnime.arc, Luigi's swap table), voice, effects and the game's own Luigi code (gIsLuigi), and the character
// gives the model (ObjectData/<archive>.arc, its model named as the archive), and its own player numbers (a copy of
// its base's MarioConstTable that luma.json "player_constants" and set_player_constant change by its id or index).
// Its other models follow the game's names with the base's name swapped for the archive's: "MarioFace" becomes
// "WarioFace", "BeeMario" "BeeWario", "MarioHandL" "WarioHandL"; one the mod doesn't ship is the base's. The model
// needs the base's skeleton (its joint names), as a model replacing Mario.arc does.
//
// Characters are added while mods start, and stay for the run: an index names the same character until Nebula quits.
// In online co-op a character travels by its id (NebulaCoopPlayer::character), as a room's players run its mods.
namespace NebulaCharacters {
    const s32 cMario = 0;
    const s32 cLuigi = 1;
    // Mario, Luigi and 30 mod characters.
    const s32 cMax = 32;
    // An id's longest, its terminator included (NebulaCoopPlayer::character).
    const u32 cIdSize = 32;

    struct Character {
        char mId[cIdSize];      // "mario", "wario": lower case letters, digits, '_' and '-'
        char mName[cIdSize];    // as the Options page shows it ("WARIO")
        char mArchive[cIdSize]; // ObjectData/<mArchive>.arc and its model ("Mario", "Wario")
        bool mLuigi;            // its base: Luigi's animations, voice and code, else Mario's
        MarioConstTable* mTable;
        const NebulaPlugin* mMod;  // null: the game's
    };

    // How many characters there are (2 before any mod's), and one by index (null past the last).
    s32 count();
    const Character* at(s32 index);
    // A character by its id (any case): its index, or -1.
    s32 find(const char* pId);
    // A mod's character: its index, or -1 when it can't be added (a bad id, one already taken, a base that isn't
    // "mario" or "luigi", no room). `pName` null: its id in capitals. `pArchive` null: its id with a capital first letter.
    s32 add(const NebulaPlugin* pMod, const char* pId, const char* pName, const char* pLike, const char* pArchive);

    // Its base is Luigi (1 for Luigi and Luigi-based characters); Mario's for an index past the last.
    bool isLuigi(s32 index);
    // Its player numbers; null past the last.
    MarioConstTable* constTable(s32 index);
    // Its id; "mario" past the last.
    const char* id(s32 index);

    // One of the base's model or archive names as the character has it: `pGameName` ("MarioFace", "BeeLuigi") with the
    // base's name swapped for its archive's, when ObjectData holds that archive; else `pGameName`. The text lasts for
    // the run (actors keep their model's name).
    const char* modelName(s32 index, const char* pGameName);
    // `pName` is one of a mod character's models standing for one of the game's that `pIsGameModel` takes ("WarioFace"
    // for "MarioFace"): for the game's lists of player models (MR::createModel's PlayerModels).
    bool isModelLike(const char* pName, bool (*pIsGameModel)(const char*));
};  // namespace NebulaCharacters
