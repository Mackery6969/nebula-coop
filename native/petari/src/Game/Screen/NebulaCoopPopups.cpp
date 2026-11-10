#include "Game/Screen/NebulaCoopPopups.hpp"
#include "nebula_module.h"
#include "Game/System/NebulaHost.hpp"

#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <cstring>
#include <nebula_game_api.h>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Pane.hpp>
#include <Game/Ui/TextBox.hpp>
#include <Game/Ui/Window.hpp>

namespace {
    using namespace NebulaUi;

    // In screen units (the screen is 456 high at every resolution): the text's size, the distance from the screen's top
    // and right edges, a box's padding around its text, the space between boxes and between its lines, the widest its
    // text runs, its corners.
    const f32 cTextSize = 13.0f;
    const f32 cMargin = 10.0f;
    const f32 cPadX = 9.0f;
    const f32 cPadY = 6.0f;
    const f32 cGap = 4.0f;
    const f32 cLineGap = 2.0f;
    const f32 cWidthMost = 200.0f;
    const f32 cRadius = 7.0f;
    // At most this many show at once; a new one past them pushes out the oldest.
    const s32 cShown = 5;
    // A box shows this many frames (sm64coopdx's six seconds), the last of them fading out.
    const s32 cLifeFrames = 360;
    const s32 cFadeFrames = 60;
    // Each frame the boxes slide this share of the way to their places (sm64coopdx's 0.75 left to go).
    const f32 cSlideKeep = 0.75f;

    // RGBA: the game's window dimmed to see through, its ring the player's character's colour (Mario's red, Luigi's
    // green); the words light grey (sm64coopdx's), a galaxy's name in the game's gold, its star's in white.
    const u32 cFill = 0x0A0F1EDC;
    const u32 cMario = 0xFF6E64FF;
    const u32 cLuigi = 0x6EF046FF;
    const u32 cWords = 0xDCDCDCFF;
    const u32 cGalaxy = 0xFFE650FF;
    const u32 cStar = 0xFFFFFFFF;

    const s32 cTextLength = 192;

    // A box's lines: what happened, then (where it says where) the galaxy and its star.
    enum { LineWhat, LineGalaxy, LineStar, LineNum };

    struct Box {
        Pane* mGroup;
        Window* mStrip;
        TextBox* mLines[LineNum];
        s32 mAge;  // frames shown; -1: free
        f32 mWidth;
        f32 mHeight;
        f32 mLineHeights[LineNum];
    };

    void toWide(char16_t* pDst, s32 capacity, const char* pSrc) {
        s32 n = 0;
        while (pSrc[n] != 0 && n < capacity - 1) {
            pDst[n] = static_cast< char16_t >(static_cast< unsigned char >(pSrc[n]));
            n++;
        }
        pDst[n] = 0;
    }

    // NebulaText's `pId` with {name} as `pName`.
    void fillName(char16_t* pDst, s32 capacity, const char* pId, const char16_t* pName) {
        char16_t pattern[cTextLength];
        NebulaText::get(pattern, cTextLength, pId);
        char16_t* p = pDst;
        char16_t* pEnd = pDst + capacity;
        *p = 0;
        for (const char16_t* s = pattern; *s != 0;) {
            if (s[0] == u'{' && s[1] == u'n' && s[2] == u'a' && s[3] == u'm' && s[4] == u'e' && s[5] == u'}') {
                p = append(p, pEnd, pName);
                s += 6;
                continue;
            }
            if (p + 1 < pEnd) {
                *p++ = *s;
                *p = 0;
            }
            s++;
        }
    }

    // The galaxy's name in the played language (GalaxyName_<stage>), or none.
    const char16_t* galaxyName(const char* pStage) {
        if (pStage[0] == 0) {
            return nullptr;
        }
        char id[96];
        nebula_snprintf(id, sizeof(id), "GalaxyName_%s", pStage);
        return MR::isExistGameMessage(id) ? MR::getGalaxyNameOnCurrentLanguage(pStage) : nullptr;
    }

    // The star's name (ScenarioName_<stage><scenario>), or none.
    const char16_t* starName(const char* pStage, s32 scenario) {
        if (pStage[0] == 0 || scenario <= 0) {
            return nullptr;
        }
        char id[96];
        nebula_snprintf(id, sizeof(id), "ScenarioName_%s%d", pStage, static_cast< int >(scenario));
        return MR::isExistGameMessage(id) ? MR::getScenarioNameOnCurrentLanguage(pStage, scenario) : nullptr;
    }
};  // namespace

struct NebulaCoopPopupsParts {
    NebulaPaneKit mText;    // InformationWindow: the game's message font
    NebulaPaneKit mWindow;  // SysInfoWindowMini: the window with a ring
    Pane* mRoot;
    Box mBoxes[cShown];
    // The boxes showing, the newest first (indices into mBoxes).
    s32 mOrder[cShown];
    s32 mCount;
    // How far above their places the boxes are as a new one slides in.
    f32 mSlide;
};

NebulaCoopPopups::NebulaCoopPopups() : LayoutActor("NebulaCoopPopups", true), mParts(nullptr) {
}

void NebulaCoopPopups::init(const TableIter& rIter) {
    initLayoutManager("InformationWindow", 0);
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaCoopPopups";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));

    NebulaCoopPopupsParts* p = new NebulaCoopPopupsParts();
    Pane* pLayoutRoot = pManager->mLayout->mpRootPane;
    if (pLayoutRoot == nullptr || !p->mText.init(pManager->mLayoutHolder, "InformationWindow", pManager->mLayout)) {
        nebula_log("NebulaCoopPopups: no InformationWindow layout\n");
        delete p;
        return;
    }
    // Without the ringed window, the information window's stands in (makeWindow).
    p->mWindow.init("SysInfoWindowMini", pManager->mLayout);
    // The information window's own panes stay hidden; the pop-ups are built beside them.
    for (Ui::Pane* it : pLayoutRoot->GetChildList()) {
        it->SetVisible(false);
    }
    const Kits kits = {&p->mText, &p->mWindow, nullptr};
    p->mRoot = makeGroup(pLayoutRoot, "NpPopups");
    if (p->mRoot == nullptr) {
        delete p;
        return;
    }

    char name[16];
    for (s32 i = 0; i < cShown; i++) {
        Box& rBox = p->mBoxes[i];
        nebula_snprintf(name, sizeof(name), "NpBox%d", static_cast< int >(i));
        rBox.mGroup = makeGroup(p->mRoot, name);
        if (rBox.mGroup == nullptr) {
            delete p;
            return;
        }
        nebula_snprintf(name, sizeof(name), "NpStrip%d", static_cast< int >(i));
        rBox.mStrip = makeWindow(kits, rBox.mGroup, name);
        if (rBox.mStrip == nullptr) {
            delete p;
            return;
        }
        paint(rBox.mStrip, cFill, cMario);
        static const u32 cColours[LineNum] = {cWords, cGalaxy, cStar};
        for (s32 line = 0; line < LineNum; line++) {
            nebula_snprintf(name, sizeof(name), "NpText%d_%d", static_cast< int >(i), static_cast< int >(line));
            rBox.mLines[line] = makeText(kits, rBox.mGroup, name, cTextLength);
            if (rBox.mLines[line] == nullptr) {
                delete p;
                return;
            }
            setTextStyle(rBox.mLines[line], cTextSize, cColours[line], cColours[line], 0);
        }
        setInfluencedAlphaRecursive(rBox.mGroup, 0);
        rBox.mGroup->SetVisible(false);
        rBox.mAge = -1;
    }
    p->mCount = 0;
    p->mSlide = 0.0f;

    mParts = p;
    LayoutActor::appear();
}

void NebulaCoopPopups::update() {
    movement();
    calcAnim();
}

void NebulaCoopPopups::drawOnTop() const {
    if (mParts != nullptr && mParts->mCount > 0) {
        draw();
    }
}

void NebulaCoopPopups::control() {
    NebulaCoopPopupsParts* p = mParts;
    if (p == nullptr) {
        return;
    }

    // The boxes age; one past its time is free again.
    s32 kept = 0;
    for (s32 i = 0; i < p->mCount; i++) {
        Box& rBox = p->mBoxes[p->mOrder[i]];
        if (++rBox.mAge >= cLifeFrames) {
            rBox.mAge = -1;
            rBox.mGroup->SetVisible(false);
            continue;
        }
        p->mOrder[kept++] = p->mOrder[i];
    }
    p->mCount = kept;

    // What came: each into a free box (or the oldest's), on top.
    f32 scaleX, width, height;
    readScreen(&scaleX, &width, &height);
    NebulaCoopPopup popup;
    bool sounded = false;
    while (NebulaHost::coopPopupTake(&popup) != 0) {
        popup.name[sizeof(popup.name) - 1] = 0;
        popup.stage[sizeof(popup.stage) - 1] = 0;
        s32 index = -1;
        if (p->mCount == cShown) {
            index = p->mOrder[--p->mCount];
        } else {
            for (s32 i = 0; i < cShown; i++) {
                if (p->mBoxes[i].mAge < 0) {
                    index = i;
                    break;
                }
            }
        }
        if (index < 0) {
            continue;
        }
        Box& rBox = p->mBoxes[index];

        char16_t player[32];
        toWide(player, 32, popup.name);
        const char* pWhat = "CoopChatJoined";
        bool where = false;
        bool star = false;
        switch (popup.kind) {
        case NEBULA_COOP_POPUP_LEFT: pWhat = "CoopChatLeft"; break;
        case NEBULA_COOP_POPUP_OBSERVATORY:
            // The Observatory in the game's own words where it names it (as a galaxy), else in Nebula's.
            if (galaxyName("AstroGalaxy") != nullptr) {
                pWhat = "CoopPopupEntered";
                nebula_snprintf(popup.stage, sizeof(popup.stage), "%s", "AstroGalaxy");
                where = true;
            } else {
                pWhat = "CoopPopupObservatory";
            }
            break;
        case NEBULA_COOP_POPUP_GALAXY:
            pWhat = "CoopPopupEntered";
            where = true;
            star = true;
            break;
        case NEBULA_COOP_POPUP_ENTERED_HERE: pWhat = "CoopPopupEnteredHere"; break;
        case NEBULA_COOP_POPUP_LEFT_HERE: pWhat = "CoopPopupLeftHere"; break;
        case NEBULA_COOP_POPUP_DIED: pWhat = "CoopPopupDied"; break;
        case NEBULA_COOP_POPUP_STAR:
            pWhat = "CoopPopupStar";
            where = true;
            break;
        default: break;
        }
        const char16_t* pLines[LineNum] = {nullptr, where ? galaxyName(popup.stage) : nullptr,
                                           star ? starName(popup.stage, popup.scenario) : nullptr};
        char16_t what[cTextLength];
        fillName(what, cTextLength, pWhat, player);
        pLines[LineWhat] = what;
        // A galaxy the game has no name for (another build's stage) is named as the stage is.
        char16_t stage[48];
        if (where && pLines[LineGalaxy] == nullptr && popup.stage[0] != 0) {
            toWide(stage, 48, popup.stage);
            pLines[LineGalaxy] = stage;
        }

        rBox.mWidth = 0.0f;
        rBox.mHeight = 0.0f;
        for (s32 line = 0; line < LineNum; line++) {
            TextBox* pText = rBox.mLines[line];
            rBox.mLineHeights[line] = 0.0f;
            if (pLines[line] == nullptr || pLines[line][0] == 0) {
                pText->SetVisible(false);
                continue;
            }
            f32 lineWidth, lineHeight;
            wrapWords(pText, pLines[line], cWidthMost, &lineWidth, &lineHeight);
            pText->SetVisible(true);
            rBox.mLineHeights[line] = lineHeight;
            rBox.mWidth = lineWidth > rBox.mWidth ? lineWidth : rBox.mWidth;
            rBox.mHeight += (rBox.mHeight > 0.0f ? cLineGap : 0.0f) + lineHeight;
        }
        paint(rBox.mStrip, cFill, popup.character == 1 ? cLuigi : cMario);
        rBox.mAge = 0;
        rBox.mGroup->SetVisible(true);

        for (s32 i = p->mCount; i > 0; i--) {
            p->mOrder[i] = p->mOrder[i - 1];
        }
        p->mOrder[0] = index;
        p->mCount++;
        // It slides in from above the others' places, pushing them down.
        p->mSlide += rBox.mHeight + cPadY * 2.0f + cGap;
        if (!sounded) {
            MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
            sounded = true;
        }
    }
    if (p->mCount == 0) {
        p->mSlide = 0.0f;
        return;
    }

    // The boxes from the top right down, the newest first, sliding to their places.
    p->mRoot->mScale = Vec2f(scaleX, 1.0f);
    const f32 right = width * 0.5f - cMargin;
    f32 top = height * 0.5f - cMargin + p->mSlide;
    p->mSlide *= cSlideKeep;
    if (p->mSlide < 0.5f) {
        p->mSlide = 0.0f;
    }
    for (s32 i = 0; i < p->mCount; i++) {
        Box& rBox = p->mBoxes[p->mOrder[i]];
        const f32 w = rBox.mWidth + cPadX * 2.0f;
        const f32 h = rBox.mHeight + cPadY * 2.0f;
        const f32 left = right - w;
        shape(rBox.mStrip, left + w * 0.5f, top - h * 0.5f, w, h, cRadius);
        f32 y = top - cPadY;
        for (s32 line = 0; line < LineNum; line++) {
            if (rBox.mLineHeights[line] <= 0.0f) {
                continue;
            }
            rBox.mLines[line]->mTranslate = Vec3f(left + cPadX, y, 0.0f);
            y -= rBox.mLineHeights[line] + cLineGap;
        }
        // The last second fades, slowly at first (sm64coopdx's squared fade).
        f32 alpha = 1.0f;
        const s32 leftFrames = cLifeFrames - rBox.mAge;
        if (leftFrames < cFadeFrames) {
            alpha = static_cast< f32 >(leftFrames) / static_cast< f32 >(cFadeFrames);
            alpha *= alpha;
        }
        rBox.mGroup->mAlpha = toAlpha(alpha);
        top -= h + cGap;
    }
}
