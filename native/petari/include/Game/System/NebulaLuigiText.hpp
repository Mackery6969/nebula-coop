#pragma once

#include "Game/Util/MessageTag.hpp"

class MessageData;

// The game's lines that talk about Luigi as someone else, where the player playing Luigi would be told about himself:
// Captain Toad in the Honeyhive Galaxy ("we got lost with Luigi..."). For a Luigi player those lines name Mario instead.
// The game's other lines already fit Luigi (the player's name tag, the talks' Luigi branches); the lost-Luigi mission's
// lines stay as the game has them. Only this game's player counts, so in co-op each player reads the lines for their own
// character.
namespace NebulaLuigiText {
    // The picture tags of Mario's and Luigi's faces, and the text colors their names are in (red and green).
    const char16_t cMarioPicture = 18;
    const char16_t cLuigiPicture = 28;
    const u8 cMarioColor = 1;
    const u8 cLuigiColor = 2;

    // pText (the game's text for its message `index` in rData), or for a Luigi player and a line above, a copy that names
    // Mario: his name (System_PlayerName000) for Luigi's (System_PlayerName100) in the played language, his face and color.
    const char16_t* fit(const MessageData& rData, u32 index, const char16_t* pText);

    // pSrc into pDst (`capacity` units with the terminator) with each pFrom outside a tag as pTo, Luigi's face as Mario's
    // and the green text color as the red; its length, or -1 when it does not fit (or pFrom is empty).
    inline s32 nameMario(char16_t* pDst, s32 capacity, const char16_t* pSrc, const char16_t* pFrom, const char16_t* pTo) {
        if (pFrom == nullptr || pFrom[0] == 0) {
            return -1;
        }
        s32 length = 0;
        while (*pSrc != 0) {
            s32 units = 0;
            const char16_t* pPut = pSrc;
            const s32 tagStart = length;
            bool tag = false;
            if (pSrc[0] == cMessageTagEscape && pSrc[1] != 0) {
                // A tag, whole: its data is not text.
                units = getMessageTagUnits(pSrc[1]);
                units = units > 0 ? units : 1;
                tag = true;
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
            if (tag && units >= 3) {
                // The tag's group is its head's high byte, its number the next unit; a color's index is the high byte
                // of the unit after.
                const u32 group = pDst[tagStart + 1] >> 8;
                const char16_t number = pDst[tagStart + 2];
                if (group == MessageTagGroup_Picture && number == cLuigiPicture) {
                    pDst[tagStart + 2] = cMarioPicture;
                } else if (group == MessageTagGroup_System && number == 0 && units >= 4 &&
                           (pDst[tagStart + 3] >> 8) == cLuigiColor) {
                    pDst[tagStart + 3] = static_cast< char16_t >((pDst[tagStart + 3] & 0xFF) | (cMarioColor << 8));
                }
            }
        }
        pDst[length] = 0;
        return length;
    }
};  // namespace NebulaLuigiText
