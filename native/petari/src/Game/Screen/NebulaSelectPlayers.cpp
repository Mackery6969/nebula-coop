#include "Game/Screen/NebulaSelectPlayers.hpp"

#ifdef TARGET_PC
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/lyt/window.h>
#include <revolution/os.h>

using namespace NebulaUi;

namespace {
    const s32 cPlayersMax = 16;  // the most players a room holds (kCoopMaxPlayersLimit)
    const s32 cNamesMax = 3;     // names on a tag; the rest are counted (+2)
    // Screen units (NebulaUi::readScreen): 456 high.
    const f32 cTextSize = 13.0f;
    const f32 cPadX = 7.0f;
    const f32 cHeight = 20.0f;
    const f32 cRadius = 8.0f;
    const f32 cNameGap = 8.0f;
    const f32 cMaxWidth = 168.0f;  // wider, and the names are narrowed to fit
    // How far under a miniature galaxy's middle its tag goes, in the dome's world: as far as its name plate is over it
    // (MiniatureGalaxy's cNamePlateOffset, 1500), less the plate's own height.
    const f32 cWorldDrop = 1100.0f;
    const s32 cFadeFrames = 8;

    const u32 cFill = 0x0A1442C8;  // the nametags' night blue
    const u32 cRing = 0x6C8CE000;
    const u32 cMario = 0xFF7B6EFF;  // the player list's colours
    const u32 cLuigi = 0x7EE08AFF;
    const u32 cMore = 0xD2DCFFFF;

    struct Tag {
        Pane* mRoot;
        Window* mFrame;
        Pane* mLine;  // the names, narrowed together when they run too wide
        TextBox* mNames[cNamesMax];
        TextBox* mMore;
        // What it shows: the players' numbers (0 past the last), their characters and how many more.
        u32 mShown[cNamesMax];
        u32 mShownFlags[cNamesMax];
        u32 mShownMore;
        // What its owner asked for this frame.
        bool mRequested;
        char mStage[48];
        s32 mScenario;
        f32 mX;
        f32 mY;
        s32 mFade;  // 0 hidden to cFadeFrames shown
    };

    void toWide(wchar_t* pDst, s32 capacity, const char* pSrc) {
        s32 i = 0;
        for (; i < capacity - 1 && pSrc[i] != 0; i++) {
            pDst[i] = static_cast< wchar_t >(static_cast< u8 >(pSrc[i]));
        }
        pDst[i] = 0;
    }

    bool isInside(const NebulaCoopListPlayer& rPlayer, const char* pStage, s32 scenario) {
        return (rPlayer.flags & NEBULA_COOP_LIST_YOU) == 0 && strcmp(rPlayer.stage, pStage) == 0 &&
               (scenario == 0 || rPlayer.scenario == scenario);
    }
};  // namespace

struct NebulaSelectPlayersParts {
    NebulaPaneKit mText;    // InformationWindow: the game's message font
    NebulaPaneKit mWindow;  // SysInfoWindowMini: the window with a ring
    Pane* mRoot;
    Tag mTags[NebulaSelectPlayers::cSpotsMax];
    NebulaCoopListPlayer mPlayers[cPlayersMax];
    s32 mCount;
};

namespace {
    // A tag's names for the players inside its place (by number, as the host lists them): written again only when
    // they change, the window fitted to them. False when nobody is inside.
    bool fill(Tag& rTag, const NebulaSelectPlayersParts* p) {
        const NebulaCoopListPlayer* pInside[cNamesMax];
        s32 named = 0;
        u32 more = 0;
        for (s32 i = 0; i < p->mCount; i++) {
            if (!isInside(p->mPlayers[i], rTag.mStage, rTag.mScenario)) {
                continue;
            }
            if (named < cNamesMax) {
                pInside[named++] = &p->mPlayers[i];
            } else {
                more++;
            }
        }
        if (named == 0) {
            return false;
        }
        bool same = more == rTag.mShownMore;
        for (s32 i = 0; i < cNamesMax; i++) {
            same = same && rTag.mShown[i] == (i < named ? pInside[i]->number : 0u) &&
                   (i >= named || rTag.mShownFlags[i] == pInside[i]->flags);
        }
        if (same) {
            return true;
        }

        f32 width = 0.0f;
        f32 height;
        for (s32 i = 0; i < cNamesMax; i++) {
            TextBox* pName = rTag.mNames[i];
            rTag.mShown[i] = i < named ? pInside[i]->number : 0u;
            rTag.mShownFlags[i] = i < named ? pInside[i]->flags : 0u;
            pName->SetVisible(i < named);
            if (i >= named) {
                continue;
            }
            wchar_t text[32];
            toWide(text, 32, pInside[i]->name);
            setText(pName, text);
            const u32 color = (pInside[i]->flags & NEBULA_COOP_LIST_LUIGI) != 0 ? cLuigi : cMario;
            setTextColor(pName, color, color);
            f32 nameWidth;
            measure(pName, 4000.0f, &nameWidth, &height);
            width += (i > 0 ? cNameGap : 0.0f);
            pName->mTranslate = nw4r::math::VEC3(width, textCentre(0.0f, cTextSize), 0.0f);
            width += nameWidth;
        }
        rTag.mShownMore = more;
        rTag.mMore->SetVisible(more != 0);
        if (more != 0) {
            char count[12];
            snprintf(count, sizeof(count), "+%u", more);
            wchar_t text[12];
            toWide(text, 12, count);
            setText(rTag.mMore, text);
            f32 moreWidth;
            measure(rTag.mMore, 4000.0f, &moreWidth, &height);
            width += cNameGap;
            rTag.mMore->mTranslate = nw4r::math::VEC3(width, textCentre(0.0f, cTextSize), 0.0f);
            width += moreWidth;
        }

        // The line from its left, centred on the tag; narrowed when it runs past cMaxWidth.
        const f32 squeeze = width > cMaxWidth ? cMaxWidth / width : 1.0f;
        rTag.mLine->mScale = nw4r::math::VEC2(squeeze, 1.0f);
        rTag.mLine->mTranslate = nw4r::math::VEC3(-width * squeeze * 0.5f, 0.0f, 0.0f);
        shape(rTag.mFrame, 0.0f, 0.0f, width * squeeze + cPadX * 2.0f, cHeight, cRadius);
        return true;
    }
};  // namespace

NebulaSelectPlayers::NebulaSelectPlayers(bool scene) : LayoutActor("NebulaSelectPlayers", true), mParts(nullptr), mScene(scene) {
}

void NebulaSelectPlayers::init(const JMapInfoIter& rIter) {
    initLayoutManager("InformationWindow", 0);
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaSelectPlayers";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));
    if (mScene) {
        MR::connectToSceneLayout(this);
    }

    NebulaSelectPlayersParts* p = new NebulaSelectPlayersParts();
    memset(static_cast< void* >(p), 0, sizeof(NebulaSelectPlayersParts));
    new (&p->mText) NebulaPaneKit();
    new (&p->mWindow) NebulaPaneKit();
    Pane* pRoot = pManager->mLayout->mpRootPane;
    if (pRoot == nullptr || !p->mText.init(pManager->mLayoutHolder)) {
        OSReport("NebulaSelectPlayers: no InformationWindow layout\n");
        return;
    }
    p->mWindow.init(MR::createAndAddLayoutHolder("SysInfoWindowMini.arc"));
    // The information window's own panes stay hidden; the tags are built beside them.
    for (nw4r::lyt::PaneList::Iterator it = pRoot->GetChildList().GetBeginIter(); it != pRoot->GetChildList().GetEndIter(); ++it) {
        it->SetVisible(false);
    }

    const Kits kits = {&p->mText, &p->mWindow, nullptr};
    p->mRoot = makeGroup(pRoot, "NbSel");
    if (p->mRoot == nullptr) {
        return;
    }
    for (s32 i = 0; i < cSpotsMax; i++) {
        Tag& rTag = p->mTags[i];
        char name[17];
        snprintf(name, sizeof(name), "NbSel%d", i);
        rTag.mRoot = makeGroup(p->mRoot, name);
        if (rTag.mRoot == nullptr) {
            return;
        }
        snprintf(name, sizeof(name), "NbSelFrm%d", i);
        rTag.mFrame = makeWindow(kits, rTag.mRoot, name);
        snprintf(name, sizeof(name), "NbSelLine%d", i);
        rTag.mLine = makeGroup(rTag.mRoot, name);
        snprintf(name, sizeof(name), "NbSelMore%d", i);
        rTag.mMore = rTag.mLine != nullptr ? makeText(kits, rTag.mLine, name, 12) : nullptr;
        bool made = rTag.mFrame != nullptr && rTag.mMore != nullptr;
        for (s32 j = 0; made && j < cNamesMax; j++) {
            snprintf(name, sizeof(name), "NbSelName%d_%d", i, j);
            rTag.mNames[j] = makeText(kits, rTag.mLine, name, 32);
            made = rTag.mNames[j] != nullptr;
            if (made) {
                setTextStyle(rTag.mNames[j], cTextSize, cMario, cMario, 3);
            }
        }
        if (!made) {
            rTag.mRoot = nullptr;
            return;
        }
        paint(rTag.mFrame, cFill, cRing);
        setTextStyle(rTag.mMore, cTextSize, cMore, cMore, 3);
        rTag.mRoot->SetVisible(false);
    }
    setInfluencedAlphaRecursive(p->mRoot, 0);
    mParts = p;

    // The layout's pane list, counted when it was built, again for the added panes.
    pManager->rebuildPaneInfo();
    LayoutActor::appear();
}

void NebulaSelectPlayers::request(s32 spot, const char* pStage, s32 scenario, f32 x, f32 y) {
    if (mParts == nullptr || spot < 0 || spot >= cSpotsMax || pStage == nullptr) {
        return;
    }
    Tag& rTag = mParts->mTags[spot];
    // Another place on this spot: its names from nothing, not the last one's fading.
    if (strcmp(rTag.mStage, pStage) != 0 || rTag.mScenario != scenario) {
        snprintf(rTag.mStage, sizeof(rTag.mStage), "%s", pStage);
        rTag.mScenario = scenario;
        rTag.mFade = 0;
        memset(rTag.mShown, 0, sizeof(rTag.mShown));
        rTag.mShownMore = 0;
    }
    rTag.mRequested = true;
    rTag.mX = x;
    rTag.mY = y;
}

void NebulaSelectPlayers::showAtWorld(s32 spot, const char* pStage, s32 scenario, const TVec3f& rPos) {
    const TVec3f under(rPos.x, rPos.y - cWorldDrop, rPos.z);
    const TVec3f camera = MR::getCamPos();
    const TVec3f toUnder(under.x - camera.x, under.y - camera.y, under.z - camera.z);
    if (toUnder.dot(MR::getCamZdir()) <= 0.0f) {
        return;
    }
    TVec3f screen;
    MR::calcNormalizedScreenPosition(&screen, under);
    if (__builtin_fabsf(screen.x) > 1.0f || __builtin_fabsf(screen.y) > 1.0f) {
        return;
    }
    f32 scaleX, width, height;
    readScreen(&scaleX, &width, &height);
    request(spot, pStage, scenario, screen.x * width * 0.5f, -screen.y * height * 0.5f);
}

void NebulaSelectPlayers::showAtPane(s32 spot, const char* pStage, s32 scenario, const nw4r::lyt::Pane* pPane, f32 drop) {
    if (pPane == nullptr) {
        return;
    }
    // The pane's place on the layout's canvas, from its middle (up), into screen units.
    f32 scaleX, width, height;
    readScreen(&scaleX, &width, &height);
    request(spot, pStage, scenario, pPane->mGlbMtx._03 / scaleX, pPane->mGlbMtx._13 - drop);
}

void NebulaSelectPlayers::control() {
    NebulaSelectPlayersParts* p = mParts;
    if (p == nullptr) {
        return;
    }
    // The host's list, only while a tag shows or is asked for: nothing outside a room.
    bool wanted = false;
    for (s32 i = 0; i < cSpotsMax; i++) {
        wanted = wanted || p->mTags[i].mRequested || p->mTags[i].mFade > 0;
    }
    if (!wanted) {
        p->mRoot->SetVisible(false);
        return;
    }
    u32 roomSize = 0;
    const u32 count = __NebulaCoopList(p->mPlayers, cPlayersMax, &roomSize);
    p->mCount = static_cast< s32 >(count > static_cast< u32 >(cPlayersMax) ? cPlayersMax : count);

    f32 scaleX, width, height;
    readScreen(&scaleX, &width, &height);
    p->mRoot->SetVisible(true);
    p->mRoot->mScale = nw4r::math::VEC2(scaleX, 1.0f);
    for (s32 i = 0; i < cSpotsMax; i++) {
        Tag& rTag = p->mTags[i];
        if (rTag.mRoot == nullptr) {
            continue;
        }
        // Shown while its owner names it and someone is inside; the names stay as they were while it fades out.
        const bool shown = rTag.mRequested && fill(rTag, p);
        rTag.mRequested = false;
        const s32 fade = rTag.mFade + (shown ? 1 : -1);
        rTag.mFade = fade < 0 ? 0 : fade > cFadeFrames ? cFadeFrames : fade;
        if (rTag.mFade == 0) {
            rTag.mRoot->SetVisible(false);
            continue;
        }
        rTag.mRoot->SetVisible(true);
        rTag.mRoot->mTranslate = nw4r::math::VEC3(rTag.mX, rTag.mY - cHeight * 0.5f, 0.0f);
        rTag.mRoot->mAlpha = toAlpha(easeOut(static_cast< f32 >(rTag.mFade) / cFadeFrames));
    }
}
#endif
