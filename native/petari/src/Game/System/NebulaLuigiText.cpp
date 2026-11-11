#include "Game/System/NebulaLuigiText.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/Util/EventUtil.hpp"

namespace {
    // The lines, each with room for its copy (the game's longest is under 400 units in any of its languages).
    const char* const cLines[] = {
        // Captain Toad when you find him in the Honeyhive Galaxy: "...then we got lost with Luigi...". The Toad
        // Brigade's lines about the lost Luigi elsewhere belong to his mission and stay as they are.
        "ForestHomeZone_Kinopio001",
        "ForestHomeZone_Kinopio002",
        "ForestHomeZone_Kinopio009",
        "ForestHomeZone_Kinopio010",
    };
    const s32 cLineCount = sizeof(cLines) / sizeof(cLines[0]);
    const s32 cLineRoom = 512;
    char16_t sCopies[cLineCount][cLineRoom];

    // The lines' numbers in the message data last asked about (the game's or the system's, read when it changes).
    const MessageFileHeader* sIndexedFile = nullptr;
    s32 sIndices[cLineCount];

    const char16_t* findText(const MessageData& rData, const char* pMessageId) {
        const s32 index = rData.findMessageIndex(pMessageId);
        if (index < 0 || static_cast< u32 >(index) >= rData.getMessageNum()) {
            return nullptr;
        }
        return rData.mText + rData.mMessages[index].mText;
    }
}  // namespace

namespace NebulaLuigiText {
    const char16_t* fit(const MessageData& rData, u32 index, const char16_t* pText) {
        if (pText == nullptr) {
            return pText;
        }
        if (sIndexedFile != rData.mFile) {
            sIndexedFile = rData.mFile;
            for (s32 i = 0; i < cLineCount; i++) {
                sIndices[i] = rData.findMessageIndex(cLines[i]);
            }
        }
        for (s32 i = 0; i < cLineCount; i++) {
            if (sIndices[i] < 0 || static_cast< u32 >(sIndices[i]) != index) {
                continue;
            }
            if (!MR::isPlayerLuigi()) {
                return pText;
            }
            const char16_t* pLuigi = findText(rData, "System_PlayerName100");
            const char16_t* pMario = findText(rData, "System_PlayerName000");
            if (pMario == nullptr || pLuigi == nullptr || nameMario(sCopies[i], cLineRoom, pText, pLuigi, pMario) < 0) {
                return pText;
            }
            return sCopies[i];
        }
        return pText;
    }
};  // namespace NebulaLuigiText
