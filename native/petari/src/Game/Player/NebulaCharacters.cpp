#include "Game/Player/NebulaCharacters.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/System/LumaRegistry.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_format.h>
#include <nebula_mod_api.h>
#include "nebula_module.h"
#include <cstring>

namespace {
    NebulaCharacters::Character sCharacters[NebulaCharacters::cMax];
    s32 sCount = 0;

    // The model names a character asked for (modelName), each the game's name and the character's, kept for the run:
    // the actors made from them keep the text.
    struct ModelName {
        s32 mCharacter;
        char mGame[48];
        char mName[64];
    };
    const s32 cModelNamesMax = 256;
    ModelName sModelNames[cModelNamesMax];
    s32 sModelNameCount = 0;

    void copyText(char* pOut, u32 size, const char* pText) {
        nebula_snprintf(pOut, size, "%s", pText);
    }

    void addBuiltIn(const char* pId, const char* pName, const char* pArchive, bool luigi) {
        NebulaCharacters::Character& rCharacter = sCharacters[sCount];
        copyText(rCharacter.mId, sizeof(rCharacter.mId), pId);
        copyText(rCharacter.mName, sizeof(rCharacter.mName), pName);
        copyText(rCharacter.mArchive, sizeof(rCharacter.mArchive), pArchive);
        rCharacter.mLuigi = luigi;
        rCharacter.mTable = getMarioConstGameTable(luigi ? 1 : 0);
        rCharacter.mMod = nullptr;
        sCount++;
    }

    // The game's two, before anything else asks.
    void initBuiltIns() {
        if (sCount == 0) {
            addBuiltIn("mario", "MARIO", "Mario", false);
            addBuiltIn("luigi", "LUIGI", "Luigi", true);
        }
    }

    // An id: 1 to 31 of a-z, 0-9, '_' and '-' (read in any case, kept in lower case), as a co-op message and a
    // settings file carry it.
    bool isGoodId(const char* pId) {
        if (pId == nullptr || pId[0] == '\0' || std::strlen(pId) >= NebulaCharacters::cIdSize) {
            return false;
        }
        for (const char* c = pId; *c != '\0'; c++) {
            const char lower = (*c >= 'A' && *c <= 'Z') ? static_cast< char >(*c - 'A' + 'a') : *c;
            if (!((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '_' || lower == '-')) {
                return false;
            }
        }
        return true;
    }

    // An archive's name: printable, no path.
    bool isGoodArchive(const char* pArchive) {
        if (pArchive == nullptr || pArchive[0] == '\0' || std::strlen(pArchive) >= NebulaCharacters::cIdSize) {
            return false;
        }
        for (const char* c = pArchive; *c != '\0'; c++) {
            if (*c <= ' ' || *c > '~' || *c == '/' || *c == '\\' || *c == '.') {
                return false;
            }
        }
        return true;
    }

    bool isArchiveInObjectData(const char* pName) {
        char path[96];
        nebula_snprintf(path, sizeof(path), "/ObjectData/%s.arc", pName);
        return MR::isFileExist(path, false);
    }

    // `pText` with its first `pFind` swapped for `pPut`, into `pOut`: false when `pFind` isn't in it, or it doesn't fit.
    bool swapWord(const char* pText, const char* pFind, const char* pPut, char* pOut, u32 size) {
        const char* pAt = std::strstr(pText, pFind);
        if (pAt == nullptr) {
            return false;
        }
        const s32 written = nebula_snprintf(pOut, size, "%.*s%s%s", static_cast< int >(pAt - pText), pText, pPut, pAt + std::strlen(pFind));
        return written > 0 && static_cast< u32 >(written) < size;
    }
};  // namespace

namespace NebulaCharacters {
    s32 count() {
        initBuiltIns();
        return sCount;
    }

    const Character* at(s32 index) {
        initBuiltIns();
        return index >= 0 && index < sCount ? &sCharacters[index] : nullptr;
    }

    s32 find(const char* pId) {
        initBuiltIns();
        if (pId == nullptr) {
            return -1;
        }
        for (s32 i = 0; i < sCount; i++) {
            if (MR::isEqualStringCase(sCharacters[i].mId, pId)) {
                return i;
            }
        }
        return -1;
    }

    s32 add(const NebulaPlugin* pMod, const char* pId, const char* pName, const char* pLike, const char* pArchive) {
        initBuiltIns();
        if (!isGoodId(pId) || find(pId) >= 0 || sCount >= cMax) {
            return -1;
        }
        bool luigi = false;
        if (pLike != nullptr && pLike[0] != '\0') {
            const s32 like = find(pLike);
            if (like != cMario && like != cLuigi) {
                return -1;
            }
            luigi = like == cLuigi;
        }
        Character& rCharacter = sCharacters[sCount];
        for (u32 i = 0; i < sizeof(rCharacter.mId); i++) {
            const char c = pId[i];
            rCharacter.mId[i] = (c >= 'A' && c <= 'Z') ? static_cast< char >(c - 'A' + 'a') : c;
            if (c == '\0') {
                break;
            }
        }
        if (pName != nullptr && pName[0] != '\0') {
            copyText(rCharacter.mName, sizeof(rCharacter.mName), pName);
        } else {
            for (u32 i = 0; i < sizeof(rCharacter.mName); i++) {
                const char c = rCharacter.mId[i];
                rCharacter.mName[i] = (c >= 'a' && c <= 'z') ? static_cast< char >(c - 'a' + 'A') : c;
                if (c == '\0') {
                    break;
                }
            }
        }
        if (pArchive != nullptr && pArchive[0] != '\0') {
            if (!isGoodArchive(pArchive)) {
                return -1;
            }
            copyText(rCharacter.mArchive, sizeof(rCharacter.mArchive), pArchive);
        } else {
            copyText(rCharacter.mArchive, sizeof(rCharacter.mArchive), rCharacter.mId);
            if (rCharacter.mArchive[0] >= 'a' && rCharacter.mArchive[0] <= 'z') {
                rCharacter.mArchive[0] = static_cast< char >(rCharacter.mArchive[0] - 'a' + 'A');
            }
        }
        rCharacter.mLuigi = luigi;
        rCharacter.mTable = newMarioConstTable(luigi);
        rCharacter.mMod = pMod;
        const s32 index = sCount++;
        lumacore::registryAdd(lumacore::RegistryKind_Character, rCharacter.mId, nullptr, pMod, -1, index);
        nebula_log("characters: %s (%s) added, like %s, model %s\n", rCharacter.mId, pMod != nullptr && pMod->id != nullptr ? pMod->id : "a plugin",
                   luigi ? "luigi" : "mario", rCharacter.mArchive);
        return index;
    }

    bool isLuigi(s32 index) {
        const Character* pCharacter = at(index);
        return pCharacter != nullptr && pCharacter->mLuigi;
    }

    MarioConstTable* constTable(s32 index) {
        const Character* pCharacter = at(index);
        return pCharacter != nullptr ? pCharacter->mTable : nullptr;
    }

    const char* id(s32 index) {
        const Character* pCharacter = at(index);
        return pCharacter != nullptr ? pCharacter->mId : "mario";
    }

    const char* modelName(s32 index, const char* pGameName) {
        const Character* pCharacter = at(index);
        if (pCharacter == nullptr || pCharacter->mMod == nullptr || pGameName == nullptr) {
            return pGameName;
        }
        for (s32 i = 0; i < sModelNameCount; i++) {
            if (sModelNames[i].mCharacter == index && MR::isEqualString(sModelNames[i].mGame, pGameName)) {
                return sModelNames[i].mName;
            }
        }
        // The base's own name in it (Luigi's models are "...Luigi..."), else Mario's (the hands and the bee's wings
        // are Mario's for both).
        char name[64];
        const char* pBase = pCharacter->mLuigi ? "Luigi" : "Mario";
        if (!swapWord(pGameName, pBase, pCharacter->mArchive, name, sizeof(name)) &&
            !swapWord(pGameName, "Mario", pCharacter->mArchive, name, sizeof(name))) {
            return pGameName;
        }
        if (sModelNameCount >= cModelNamesMax || std::strlen(pGameName) >= sizeof(sModelNames[0].mGame)) {
            return pGameName;
        }
        ModelName& rKept = sModelNames[sModelNameCount++];
        rKept.mCharacter = index;
        copyText(rKept.mGame, sizeof(rKept.mGame), pGameName);
        // Not shipped: the base's.
        copyText(rKept.mName, sizeof(rKept.mName), isArchiveInObjectData(name) ? name : pGameName);
        if (!MR::isEqualString(rKept.mName, pGameName)) {
            nebula_log("characters: %s's %s is %s\n", pCharacter->mId, pGameName, rKept.mName);
        }
        return rKept.mName;
    }

    bool isModelLike(const char* pName, bool (*pIsGameModel)(const char*)) {
        if (pName == nullptr || pIsGameModel == nullptr) {
            return false;
        }
        char game[64];
        for (s32 i = cLuigi + 1; i < sCount; i++) {
            const Character& rCharacter = sCharacters[i];
            if ((swapWord(pName, rCharacter.mArchive, "Mario", game, sizeof(game)) && pIsGameModel(game)) ||
                (swapWord(pName, rCharacter.mArchive, "Luigi", game, sizeof(game)) && pIsGameModel(game))) {
                return true;
            }
        }
        return false;
    }
};  // namespace NebulaCharacters
