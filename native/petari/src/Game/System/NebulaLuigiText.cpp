#include "Game/System/NebulaLuigiText.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    // The lines, each with room for its Luigi copy: a short line of the game's in any of its languages ("Dear Mario,",
    // "Querido Mario,", "マリオへ").
    const char* const cLines[] = {
        "Layout_PeachLetterTo",          // the letter in the opening
        "Layout_PeachLetterMiniTo",      // the same letter, small
        "Layout_StoryDemoPeachTalk000",  // Peach's call as Bowser takes the castle
    };
    const s32 cLineCount = sizeof(cLines) / sizeof(cLines[0]);
    const s32 cLineRoom = 128;
    char16_t sCopies[cLineCount][cLineRoom];

    const char16_t* findText(const MessageData& rData, const char* pMessageId) {
        const s32 index = rData.findMessageIndex(pMessageId);
        TalkMessageInfo info;
        if (index < 0 || !rData.getMessage(&info, static_cast< u32 >(index))) {
            return nullptr;
        }
        return info.mMessage;
    }
}  // namespace

namespace NebulaLuigiText {
    const char16_t* fit(const MessageData& rData, const char* pMessageId, const char16_t* pText) {
        if (pText == nullptr) {
            return pText;
        }
        for (s32 i = 0; i < cLineCount; i++) {
            if (!MR::isEqualString(pMessageId, cLines[i])) {
                continue;
            }
            if (!MR::isPlayerLuigi()) {
                return pText;
            }
            const char16_t* pMario = findText(rData, "System_PlayerName000");
            const char16_t* pLuigi = findText(rData, "System_PlayerName100");
            if (pMario == nullptr || pLuigi == nullptr || replaceName(sCopies[i], cLineRoom, pText, pMario, pLuigi) < 0) {
                return pText;
            }
            return sCopies[i];
        }
        return pText;
    }
};  // namespace NebulaLuigiText
