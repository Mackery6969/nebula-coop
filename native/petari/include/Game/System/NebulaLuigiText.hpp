#pragma once

#include "Game/Util/MessageTag.hpp"

class MessageData;

// The game's lines that talk about Luigi as someone else, where the player playing Luigi would be told about himself:
// Captain Toad in the Honeyhive Galaxy ("...and then we got lost with Luigi..."). For a Luigi player that page of the
// talk is left out. The game's other lines already fit Luigi (the player's name tag, the talks' Luigi branches); the
// lost-Luigi mission's lines stay as the game has them. Only this game's player counts, so in co-op each player reads
// the lines for their own character.
namespace NebulaLuigiText {
    // pText (the game's text for its message `index` in rData), or for a Luigi player and a line above, a copy without
    // the pages that name Luigi (System_PlayerName100, in the played language).
    const char16_t* fit(const MessageData& rData, u32 index, const char16_t* pText);

    // Whether the `units` units at pText (no tag cut in two) name pName outside a tag.
    inline bool names(const char16_t* pText, s32 units, const char16_t* pName) {
        for (s32 at = 0; at < units;) {
            if (pText[at] == cMessageTagEscape && at + 1 < units) {
                const s32 tag = getMessageTagUnits(pText[at + 1]);
                at += tag > 0 ? tag : 1;
                continue;
            }
            s32 i = 0;
            while (pName[i] != 0 && at + i < units && pText[at + i] == pName[i]) {
                i++;
            }
            if (pName[i] == 0) {
                return true;
            }
            at++;
        }
        return false;
    }

    // pSrc into pDst (`capacity` units with the terminator) without its pages (each up to and with its next-page tag,
    // <1:1>) that name pName outside a tag; its length, or -1 when it does not fit, pName is empty or every page names it.
    inline s32 dropPagesNaming(char16_t* pDst, s32 capacity, const char16_t* pSrc, const char16_t* pName) {
        if (pName == nullptr || pName[0] == 0) {
            return -1;
        }
        s32 length = 0;
        // Where the kept text's last next-page tag starts: a dropped last page takes it with it.
        s32 lastPageTag = -1;
        bool kept = false;
        while (*pSrc != 0) {
            // The page: up to and with its next-page tag, or to the end.
            s32 units = 0;
            bool pageTag = false;
            while (pSrc[units] != 0) {
                if (pSrc[units] == cMessageTagEscape && pSrc[units + 1] != 0) {
                    const s32 tag = getMessageTagUnits(pSrc[units + 1]);
                    pageTag = pSrc[units + 1] == cMessageTagNextPageHead && pSrc[units + 2] == cMessageTagNextPage;
                    units += tag > 0 ? tag : 1;
                    if (pageTag) {
                        break;
                    }
                } else {
                    units++;
                }
            }
            if (names(pSrc, units, pName)) {
                if (!pageTag && lastPageTag >= 0) {
                    length = lastPageTag;
                    lastPageTag = -1;
                }
            } else {
                if (length + units >= capacity) {
                    return -1;
                }
                for (s32 i = 0; i < units; i++) {
                    pDst[length++] = pSrc[i];
                }
                lastPageTag = pageTag ? length - 3 : -1;
                kept = true;
            }
            pSrc += units;
        }
        if (!kept) {
            return -1;
        }
        pDst[length] = 0;
        return length;
    }
};  // namespace NebulaLuigiText
