#include "Game/Screen/NebulaPlayerList.hpp"

#ifdef TARGET_PC
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaAchievementBoard.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/lyt/window.h>
#include <revolution/os.h>
#include <revolution/wpad.h>

using namespace NebulaUi;

namespace {
    // Screen units (NebulaUi::readScreen): 456 high.
    const s32 cRowsMax = 16;          // the most players a room holds (kCoopMaxPlayersLimit)
    const f32 cMargin = 14.0f;        // the window from the screen's top and sides
    const f32 cMaxWidth = 620.0f;     // a wide screen keeps a window that reads as one
    const f32 cPad = 14.0f;           // the window's inside
    const f32 cHeaderHeight = 28.0f;  // the title's line
    const f32 cRowHeight = 23.0f;
    const f32 cRowGap = 2.0f;
    const f32 cRowInset = 8.0f;      // a row's text from its frame's sides
    const f32 cNumberWidth = 22.0f;  // the player's number, before the name
    const f32 cPingWidth = 52.0f;    // the round trip, at the row's right
    const f32 cStarsWidth = 44.0f;   // the Power Stars they got in this room, before it
    const f32 cColumnGap = 10.0f;
    const f32 cTitleSize = 17.0f;
    const f32 cTextSize = 14.0f;
    const f32 cSmallSize = 11.5f;
    const f32 cPanelRadius = 14.0f;
    const f32 cRowRadius = 8.0f;
    const f32 cDrop = 10.0f;  // how far the window falls in as it shows
    const s32 cFadeFrames = 6;
    const s32 cPingEvery = 30;  // frames between round trip updates: a number that changes every frame can't be read

    // The achievement board's colours: the game's night blue, its text white.
    const u32 cPanelFill = 0x0A1442D8;
    const u32 cPanelRing = 0x6C8CE0FF;
    const u32 cRowFill = 0xFFFFFF14;
    const u32 cRowRing = 0xFFFFFF00;
    const u32 cYouFill = 0x6C8CE060;  // this player's row
    const u32 cYouRing = 0x9CB8FFB0;
    const u32 cWhite = 0xFFFFFFFF;
    const u32 cSubText = 0xD2DCFFFF;
    const u32 cDimText = 0xA6AED6FF;
    const u32 cGold = 0xFFD860FF;
    const u32 cMario = 0xFF7B6EFF;  // a name in its character's colour, as sm64coopdx's
    const u32 cLuigi = 0x7EE08AFF;
    const u32 cPingGood = 0x9CE6A4FF;
    const u32 cPingFair = 0xFFD860FF;
    const u32 cPingPoor = 0xFF8A7AFF;
    const s32 cGlyphPowerStar = '7';  // the picture font's Power Star (MR::addPictureFontTag)

    struct Row {
        Pane* mRoot;
        Window* mFrame;
        TextBox* mNumber;
        TextBox* mName;
        TextBox* mTag;  // Host
        TextBox* mPlace;
        TextBox* mStars;
        TextBox* mPing;
        f32 mNameWidth;
        f32 mTagWidth;
        f32 mPlaceWidth;
    };

    wchar_t* appendAscii(wchar_t* pDst, wchar_t* pEnd, const char* pSrc) {
        while (pSrc != nullptr && *pSrc != 0 && pDst + 1 < pEnd) {
            *pDst++ = static_cast< wchar_t >(static_cast< u8 >(*pSrc++));
        }
        *pDst = 0;
        return pDst;
    }

    // The game's own message, when the disc has it.
    const wchar_t* gameMessage(const char* pFormat, const char* pStage, s32 scenario) {
        char id[96];
        snprintf(id, sizeof(id), pFormat, pStage, scenario);
        return MR::isExistGameMessage(id) ? MR::getGameMessageDirect(id) : nullptr;
    }

    // Where a player plays in the game's own words: the galaxy, then its star when one was chosen.
    void writePlace(wchar_t* pDst, s32 capacity, const NebulaCoopListPlayer& rPlayer) {
        wchar_t* const pEnd = pDst + capacity;
        if (rPlayer.stage[0] == 0) {
            NebulaText::get(pDst, capacity, "PlayerListArriving");
            return;
        }
        const wchar_t* pGalaxy = gameMessage("GalaxyName_%s", rPlayer.stage, 0);
        wchar_t* p = pGalaxy != nullptr ? append(pDst, pEnd, pGalaxy) : appendAscii(pDst, pEnd, rPlayer.stage);
        const wchar_t* pStar = rPlayer.scenario > 0 ? gameMessage("ScenarioName_%s%d", rPlayer.stage, rPlayer.scenario) : nullptr;
        if (pStar != nullptr) {
            p = append(p, pEnd, L" - ");
            append(p, pEnd, pStar);
        }
    }

    u32 pingColor(u32 ms) {
        return ms < 100 ? cPingGood : ms < 200 ? cPingFair : cPingPoor;
    }

    bool isSamePlayer(const NebulaCoopListPlayer& rA, const NebulaCoopListPlayer& rB) {
        return strcmp(rA.name, rB.name) == 0 && strcmp(rA.stage, rB.stage) == 0 && rA.scenario == rB.scenario &&
               rA.number == rB.number && rA.flags == rB.flags && rA.stars == rB.stars;
    }
};  // namespace

struct NebulaPlayerListParts {
    NebulaPaneKit mText;    // InformationWindow: the game's message font
    NebulaPaneKit mWindow;  // SysInfoWindowMini: the window with a ring
    Pane* mRoot;
    Window* mPanel;
    TextBox* mTitle;
    TextBox* mRoomText;  // how many play, of the room's size
    Row mRows[cRowsMax];
    // The room as the host last gave it, and as the rows show it.
    NebulaCoopListPlayer mPlayers[cRowsMax];
    NebulaCoopListPlayer mShown[cRowsMax];
    s32 mCount;
    s32 mShownCount;
    u32 mRoomSize;
    u32 mShownRoomSize;
    f32 mScaleX;
    f32 mWidth;
    f32 mHeight;
    bool mArranged;
    s32 mFade;  // 0 hidden to cFadeFrames shown
    s32 mPingFrame;
};

namespace {
    Kits listKits(NebulaPlayerListParts* p) {
        const Kits kits = {&p->mText, &p->mWindow, nullptr};
        return kits;
    }

    // The rows' text for the players they show (the round trips apart): only what changed is written again. True when
    // anything did, and the rows must be placed again.
    bool fillRows(NebulaPlayerListParts* p) {
        bool changed = p->mShownCount != p->mCount || p->mShownRoomSize != p->mRoomSize;
        for (s32 i = 0; i < p->mCount; i++) {
            const NebulaCoopListPlayer& rPlayer = p->mPlayers[i];
            if (i < p->mShownCount && isSamePlayer(p->mShown[i], rPlayer)) {
                continue;
            }
            changed = true;
            Row& rRow = p->mRows[i];
            wchar_t text[160];
            char number[12];
            snprintf(number, sizeof(number), "%u", rPlayer.number);
            appendAscii(text, text + 12, number);
            setText(rRow.mNumber, text);
            appendAscii(text, text + 40, rPlayer.name);
            setText(rRow.mName, text);
            const u32 color = (rPlayer.flags & NEBULA_COOP_LIST_LUIGI) != 0 ? cLuigi : cMario;
            setTextColor(rRow.mName, color, color);
            text[0] = 0;
            if ((rPlayer.flags & NEBULA_COOP_LIST_HOST) != 0) {
                NebulaText::get(text, 32, "PlayerListHost");
            }
            setText(rRow.mTag, text);
            rRow.mTag->SetVisible(text[0] != 0);
            writePlace(text, 160, rPlayer);
            setText(rRow.mPlace, text);
            const u32 placeColor = rPlayer.stage[0] != 0 ? cSubText : cDimText;
            setTextColor(rRow.mPlace, placeColor, placeColor);
            // The Personal Star Counter: a Power Star and how many they got here, as sm64coopdx's list shows them; only
            // while the room plays the mod.
            char stars[12];
            snprintf(stars, sizeof(stars), "%u", rPlayer.stars > 9999u ? 9999u : rPlayer.stars);
            appendAscii(MR::addPictureFontTag(text, cGlyphPowerStar), text + 16, stars);
            setText(rRow.mStars, text);
            setTextColor(rRow.mStars, rPlayer.stars != 0 ? cWhite : cDimText, rPlayer.stars != 0 ? cWhite : cDimText);
            rRow.mStars->SetVisible((rPlayer.flags & NEBULA_COOP_LIST_STARS) != 0);

            f32 width, height;
            rRow.mPlace->mScale = nw4r::math::VEC2(1.0f, 1.0f);
            measure(rRow.mNumber, 4000.0f, &width, &height);
            measure(rRow.mName, 4000.0f, &rRow.mNameWidth, &height);
            measure(rRow.mTag, 4000.0f, &rRow.mTagWidth, &height);
            measure(rRow.mPlace, 4000.0f, &rRow.mPlaceWidth, &height);
            measure(rRow.mStars, 4000.0f, &width, &height);
            if (!rRow.mTag->IsVisible()) {
                rRow.mTagWidth = 0.0f;
            }
            p->mShown[i] = rPlayer;
            p->mShown[i].ping_ms = ~0u;  // written by writePings
        }
        if (changed) {
            wchar_t text[24];
            char count[24];
            snprintf(count, sizeof(count), "%d/%u", p->mCount, p->mRoomSize);
            appendAscii(text, text + 24, count);
            setText(p->mRoomText, text);
            f32 width, height;
            measure(p->mRoomText, 4000.0f, &width, &height);
        }
        p->mShownCount = p->mCount;
        p->mShownRoomSize = p->mRoomSize;
        return changed;
    }

    // The round trips, every cPingEvery frames (and as a row first shows): none for this player.
    void writePings(NebulaPlayerListParts* p, bool now) {
        if (!now && ++p->mPingFrame < cPingEvery) {
            return;
        }
        p->mPingFrame = 0;
        for (s32 i = 0; i < p->mCount; i++) {
            const NebulaCoopListPlayer& rPlayer = p->mPlayers[i];
            const u32 ms = (rPlayer.flags & NEBULA_COOP_LIST_YOU) != 0 ? 0u : rPlayer.ping_ms;
            if (p->mShown[i].ping_ms == ms) {
                continue;
            }
            p->mShown[i].ping_ms = ms;
            Row& rRow = p->mRows[i];
            wchar_t text[16] = {0};
            if (ms != 0) {
                char ping[16];
                snprintf(ping, sizeof(ping), "%u ms", ms > 9999u ? 9999u : ms);
                appendAscii(text, text + 16, ping);
            }
            setText(rRow.mPing, text);
            setTextColor(rRow.mPing, pingColor(ms), pingColor(ms));
            f32 width, height;
            measure(rRow.mPing, 4000.0f, &width, &height);
        }
    }

    // The window and its rows for the screen's shape and the players: at the top of the screen, as wide as reads well.
    void arrange(NebulaPlayerListParts* p) {
        readScreen(&p->mScaleX, &p->mWidth, &p->mHeight);
        p->mArranged = true;
        const s32 count = p->mCount > 0 ? p->mCount : 1;
        f32 panelWidth = p->mWidth - cMargin * 2.0f;
        panelWidth = panelWidth > cMaxWidth ? cMaxWidth : panelWidth;
        const f32 rowsHeight = count * cRowHeight + (count - 1) * cRowGap;
        const f32 panelHeight = cPad + cHeaderHeight + rowsHeight + cPad;
        const f32 top = p->mHeight * 0.5f - cMargin;
        shape(p->mPanel, 0.0f, top - panelHeight * 0.5f, panelWidth, panelHeight, cPanelRadius);

        const f32 left = -panelWidth * 0.5f + cPad;
        const f32 right = panelWidth * 0.5f - cPad;
        const f32 headerY = top - cPad - cHeaderHeight * 0.5f;
        p->mTitle->mTranslate = nw4r::math::VEC3(left + cRowInset, textCentre(headerY, cTitleSize), 0.0f);
        p->mRoomText->mTranslate = nw4r::math::VEC3(right - cRowInset, textCentre(headerY, cTextSize), 0.0f);

        for (s32 i = 0; i < cRowsMax; i++) {
            Row& rRow = p->mRows[i];
            if (i >= p->mCount) {
                rRow.mRoot->SetVisible(false);
                continue;
            }
            const NebulaCoopListPlayer& rPlayer = p->mPlayers[i];
            const bool you = (rPlayer.flags & NEBULA_COOP_LIST_YOU) != 0;
            const f32 y = top - cPad - cHeaderHeight - i * (cRowHeight + cRowGap) - cRowHeight * 0.5f;
            rRow.mRoot->SetVisible(true);
            rRow.mRoot->mTranslate = nw4r::math::VEC3(0.0f, y, 0.0f);
            shape(rRow.mFrame, 0.0f, 0.0f, right - left, cRowHeight, cRowRadius);
            paint(rRow.mFrame, you ? cYouFill : cRowFill, you ? cYouRing : cRowRing);

            // Number, name and Host from the left; the round trip at the right, the stars and the place before it,
            // the place narrowed to fit.
            f32 x = left + cRowInset;
            rRow.mNumber->mTranslate = nw4r::math::VEC3(x, textCentre(0.0f, cSmallSize), 0.0f);
            x += cNumberWidth;
            rRow.mName->mTranslate = nw4r::math::VEC3(x, textCentre(0.0f, cTextSize), 0.0f);
            x += rRow.mNameWidth;
            if (rRow.mTagWidth > 0.0f) {
                x += 6.0f;
                rRow.mTag->mTranslate = nw4r::math::VEC3(x, textCentre(0.0f, cSmallSize), 0.0f);
                x += rRow.mTagWidth;
            }
            const f32 pingRight = right - cRowInset;
            rRow.mPing->mTranslate = nw4r::math::VEC3(pingRight, textCentre(0.0f, cSmallSize), 0.0f);
            const f32 starsRight = pingRight - cPingWidth - cColumnGap;
            rRow.mStars->mTranslate = nw4r::math::VEC3(starsRight, textCentre(0.0f, cSmallSize), 0.0f);
            // Without the room's Personal Star Counter the place takes the stars' column.
            const f32 placeRight = rRow.mStars->IsVisible() ? starsRight - cStarsWidth - cColumnGap : starsRight;
            const f32 room = placeRight - (x + cColumnGap);
            const f32 squeeze = rRow.mPlaceWidth > room && rRow.mPlaceWidth > 0.0f ? (room > 0.0f ? room : 0.0f) / rRow.mPlaceWidth : 1.0f;
            rRow.mPlace->mScale = nw4r::math::VEC2(squeeze, 1.0f);
            rRow.mPlace->mTranslate = nw4r::math::VEC3(placeRight, textCentre(0.0f, cSmallSize), 0.0f);
            rRow.mPlace->SetVisible(squeeze > 0.2f);
        }
    }
};  // namespace

NebulaPlayerList::NebulaPlayerList() : LayoutActor("NebulaPlayerList", true), mParts(nullptr) {
}

void NebulaPlayerList::init(const JMapInfoIter& rIter) {
    // A reset while the last scene's list showed never let go of the sharp 2D.
    setSharp(SharpPlayerList, false);
    initLayoutManager("InformationWindow", 0);
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaPlayerList";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));

    NebulaPlayerListParts* p = new NebulaPlayerListParts();
    memset(static_cast< void* >(p), 0, sizeof(NebulaPlayerListParts));
    new (&p->mText) NebulaPaneKit();
    new (&p->mWindow) NebulaPaneKit();
    Pane* pRoot = pManager->mLayout->mpRootPane;
    if (pRoot == nullptr || !p->mText.init(pManager->mLayoutHolder)) {
        OSReport("NebulaPlayerList: no InformationWindow layout\n");
        return;
    }
    p->mWindow.init(MR::createAndAddLayoutHolder("SysInfoWindowMini.arc"));
    // The information window's own panes stay hidden; the list is built beside them.
    for (nw4r::lyt::PaneList::Iterator it = pRoot->GetChildList().GetBeginIter(); it != pRoot->GetChildList().GetEndIter(); ++it) {
        it->SetVisible(false);
    }

    const Kits kits = listKits(p);
    p->mRoot = makeGroup(pRoot, "NbPlList");
    if (p->mRoot == nullptr) {
        return;
    }
    p->mPanel = makeWindow(kits, p->mRoot, "NbPlPanel");
    p->mTitle = makeText(kits, p->mRoot, "NbPlTitle", 48);
    p->mRoomText = makeText(kits, p->mRoot, "NbPlRoom", 24);
    if (p->mPanel == nullptr || p->mTitle == nullptr || p->mRoomText == nullptr) {
        return;
    }
    paint(p->mPanel, cPanelFill, cPanelRing);
    wchar_t title[48];
    NebulaText::get(title, 48, "PlayerListTitle");
    setText(p->mTitle, title);
    setTextStyle(p->mTitle, cTitleSize, cWhite, cWhite, 3);
    setTextStyle(p->mRoomText, cTextSize, cGold, cGold, 5);
    f32 width, height;
    measure(p->mTitle, 4000.0f, &width, &height);

    for (s32 i = 0; i < cRowsMax; i++) {
        Row& rRow = p->mRows[i];
        char name[17];
        snprintf(name, sizeof(name), "NbPlRow%d", i);
        rRow.mRoot = makeGroup(p->mRoot, name);
        if (rRow.mRoot == nullptr) {
            return;
        }
        snprintf(name, sizeof(name), "NbPlFrm%d", i);
        rRow.mFrame = makeWindow(kits, rRow.mRoot, name);
        snprintf(name, sizeof(name), "NbPlNum%d", i);
        rRow.mNumber = makeText(kits, rRow.mRoot, name, 12);
        snprintf(name, sizeof(name), "NbPlName%d", i);
        rRow.mName = makeText(kits, rRow.mRoot, name, 40);
        snprintf(name, sizeof(name), "NbPlTag%d", i);
        rRow.mTag = makeText(kits, rRow.mRoot, name, 32);
        snprintf(name, sizeof(name), "NbPlPlace%d", i);
        rRow.mPlace = makeText(kits, rRow.mRoot, name, 160);
        snprintf(name, sizeof(name), "NbPlStar%d", i);
        rRow.mStars = makeText(kits, rRow.mRoot, name, 16);
        snprintf(name, sizeof(name), "NbPlPing%d", i);
        rRow.mPing = makeText(kits, rRow.mRoot, name, 16);
        if (rRow.mFrame == nullptr || rRow.mNumber == nullptr || rRow.mName == nullptr || rRow.mTag == nullptr ||
            rRow.mPlace == nullptr || rRow.mStars == nullptr || rRow.mPing == nullptr) {
            rRow.mRoot = nullptr;
            return;
        }
        setTextStyle(rRow.mNumber, cSmallSize, cDimText, cDimText, 3);
        setTextStyle(rRow.mName, cTextSize, cMario, cMario, 3);
        setTextStyle(rRow.mTag, cSmallSize, cGold, cGold, 3);
        setTextStyle(rRow.mPlace, cSmallSize, cSubText, cSubText, 5);
        setTextStyle(rRow.mStars, cSmallSize, cWhite, cWhite, 5);
        setTextStyle(rRow.mPing, cSmallSize, cPingGood, cPingGood, 5);
        rRow.mRoot->SetVisible(false);
    }
    setInfluencedAlphaRecursive(p->mRoot, 0);
    p->mRoot->SetVisible(false);
    mParts = p;

    // The layout's pane list, counted when it was built, again for the added panes.
    pManager->rebuildPaneInfo();
    LayoutActor::appear();
}

void NebulaPlayerList::update() {
    movement();
    calcAnim();
}

void NebulaPlayerList::drawOnTop() const {
    if (mParts != nullptr && mParts->mFade > 0) {
        draw();
    }
}

void NebulaPlayerList::control() {
    NebulaPlayerListParts* p = mParts;
    if (p == nullptr) {
        return;
    }
    // Held: Tab from the host (none while Nebula's settings menu takes the keyboard), or the Wii Remote's Minus, the
    // controller's Share (Back, Select, View) unless the player bound it elsewhere, which in a room doesn't pause
    // (PauseButtonCheckerInGame). Not Minus while the achievement board, which changes tabs with it, is open.
    const bool tab = (static_cast< u32 >(__NebulaCoopMenu(NEBULA_COOP_MENU_KEYS)) & NEBULA_COOP_KEY_TAB) != 0;
    const NebulaAchievementBoard* pBoard = MR::getGameSceneLayoutHolder()->mAchievementBoard;
    const bool minus = MR::testCorePadButtonMinus(WPAD_CHAN0) && (pBoard == nullptr || !pBoard->isOpen());
    const bool held = tab || minus;

    // Only in a room: the host's list each frame while it shows (how many, where each plays, their round trips).
    bool shown = false;
    if (held || p->mFade > 0) {
        u32 roomSize = 0;
        const u32 count = __NebulaCoopList(p->mPlayers, cRowsMax, &roomSize);
        p->mCount = static_cast< s32 >(count > static_cast< u32 >(cRowsMax) ? cRowsMax : count);
        p->mRoomSize = roomSize;
        shown = held && p->mCount > 0;
    }
    const s32 fade = p->mFade + (shown ? 1 : -1);
    p->mFade = fade < 0 ? 0 : fade > cFadeFrames ? cFadeFrames : fade;
    setSharp(SharpPlayerList, p->mFade > 0);
    if (p->mFade == 0) {
        p->mRoot->SetVisible(false);
        p->mPingFrame = 0;
        return;
    }

    // Laid out again when a player comes, goes or moves, and for a new screen shape (a resized window).
    const bool firstFrame = !p->mRoot->IsVisible();
    const bool changed = fillRows(p);
    f32 scaleX, width, height;
    readScreen(&scaleX, &width, &height);
    if (changed || !p->mArranged || __builtin_fabsf(width - p->mWidth) > 0.5f || __builtin_fabsf(height - p->mHeight) > 0.5f ||
        scaleX != p->mScaleX) {
        arrange(p);
    }
    writePings(p, firstFrame || changed);

    const f32 t = easeOut(static_cast< f32 >(p->mFade) / cFadeFrames);
    p->mRoot->SetVisible(true);
    p->mRoot->mScale = nw4r::math::VEC2(p->mScaleX, 1.0f);
    p->mRoot->mAlpha = toAlpha(t);
    p->mRoot->mTranslate = nw4r::math::VEC3(0.0f, (1.0f - t) * cDrop, 0.0f);
}
#endif
