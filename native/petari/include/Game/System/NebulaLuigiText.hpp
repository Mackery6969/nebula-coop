#pragma once

#include "Game/Util/MessageTag.hpp"

class MessageData;

// The game's lines that name Mario where they mean the player, for a player playing Luigi: Peach's letter ("Dear Mario,")
// and her call in the opening ("Mario!"). The game's other lines already fit Luigi (the player's name tag, the talks'
// Luigi branches), and the lines about the lost Luigi stay as the game has them. Only this game's player counts, so in
// co-op each player reads the lines for their own character.
namespace NebulaLuigiText {
    // pText (the game's text for pMessageId in rData), or for a Luigi player and a line above, a copy with Luigi's name
    // (System_PlayerName100) for Mario's (System_PlayerName000), in the played language.
    const char16_t* fit(const MessageData& rData, const char* pMessageId, const char16_t* pText);

    // pSrc into pDst (`capacity` units with the terminator) with each pFrom outside a tag as pTo; its length, or -1 when it
    // does not fit (or pFrom is empty).
    inline s32 replaceName(char16_t* pDst, s32 capacity, const char16_t* pSrc, const char16_t* pFrom, const char16_t* pTo) {
        if (pFrom == nullptr || pFrom[0] == 0) {
            return -1;
        }
        s32 length = 0;
        while (*pSrc != 0) {
            s32 units = 0;
            const char16_t* pPut = pSrc;
            if (pSrc[0] == cMessageTagEscape && pSrc[1] != 0) {
                // A tag, whole: its data is not text.
                units = getMessageTagUnits(pSrc[1]);
                units = units > 0 ? units : 1;
                pSrc += units;
            } else {
                s32 i = 0;
                while (pFrom[i] != 0 && pSrc[i] == pFrom[i]) {
                    i++;
                }
                if (pFrom[i] == 0) {
                    pPut = pTo;
                    while (pTo[units] != 0) {
                        units++;
                    }
                    pSrc += i;
                } else {
                    units = 1;
                    pSrc++;
                }
            }
            if (length + units >= capacity) {
                return -1;
            }
            for (s32 i = 0; i < units; i++) {
                pDst[length++] = pPut[i];
            }
        }
        pDst[length] = 0;
        return length;
    }
};  // namespace NebulaLuigiText
