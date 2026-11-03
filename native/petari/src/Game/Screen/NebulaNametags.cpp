#include "Game/Screen/NebulaNametags.hpp"
#include "nebula_module.h"

#include "Game/Player/NebulaRemotePlayer.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/lyt/window.h>

using namespace NebulaUi;

namespace {
    // As many as there can be copies (NebulaRemotePlayer: 15 of each character).
    const s32 cTagsMax = 30;
    // Screen units (NebulaUi::readScreen): 456 high.
    const f32 cTextSize = 15.0f;  // near
    const f32 cPadX = 8.0f;
    const f32 cHeight = 22.0f;
    const f32 cRadius = 9.0f;
    // World units: full size this close, smaller further, gone past cFar.
    const f32 cNear = 1200.0f;
    const f32 cFar = 6000.0f;
    const f32 cSmallest = 0.6f;
    const s32 cFadeFrames = 8;

    const u32 cFill = 0x0A1442B0;  // the player list's night blue, see-through
    const u32 cRing = 0x6C8CE000;
    const u32 cMario = 0xFF7B6EFF;  // the player list's colours
    const u32 cLuigi = 0x7EE08AFF;

    struct Tag {
        Pane* mRoot;
        Window* mFrame;
        TextBox* mName;
        char mShown[32];  // the name it shows
        bool mShownLuigi;
        f32 mWidth;
        s32 mFade;  // 0 hidden to cFadeFrames shown
    };
};  // namespace

struct NebulaNametagsParts {
    NebulaPaneKit mText;    // InformationWindow: the game's message font
    NebulaPaneKit mWindow;  // SysInfoWindowMini: the window with a ring
    Pane* mRoot;
    Tag mTags[cTagsMax];
};

namespace {
    // Where a copy's name goes on the screen (screen units, from the centre, up), and how large: false when it can't be
    // seen (off the screen, behind the camera or a wall, too far).
    bool place(const NebulaRemotePlayer* pCopy, f32 width, f32 height, f32* pX, f32* pY, f32* pScale) {
        const TVec3f head = pCopy->getNametagPos();
        const TVec3f camera = MR::getCamPos();
        const TVec3f toHead(head.x - camera.x, head.y - camera.y, head.z - camera.z);
        if (toHead.dot(MR::getCamZdir()) <= 0.0f) {
            return false;
        }
        const f32 distance = toHead.length();
        if (distance > cFar) {
            return false;
        }
        TVec3f screen;
        MR::calcNormalizedScreenPosition(&screen, head);
        if (__builtin_fabsf(screen.x) > 1.1f || __builtin_fabsf(screen.y) > 1.1f) {
            return false;
        }
        // Behind the map (a wall, the planet): the line from the camera to the head runs into it.
        if (MR::isExistMapCollision(camera, toHead)) {
            return false;
        }
        *pX = screen.x * width * 0.5f;
        *pY = -screen.y * height * 0.5f;
        *pScale = distance <= cNear ? 1.0f : clamp(cNear / distance, cSmallest, 1.0f);
        return true;
    }

    // A tag's name and colour, written again only when they change, and its window fitted to it.
    void fill(Tag& rTag, const NebulaRemotePlayer* pCopy) {
        if (strcmp(rTag.mShown, pCopy->mName) == 0 && rTag.mShownLuigi == pCopy->mIsLuigi) {
            return;
        }
        nebula_snprintf(rTag.mShown, sizeof(rTag.mShown), "%s", pCopy->mName);
        rTag.mShownLuigi = pCopy->mIsLuigi;
        char16_t text[32];
        s32 i = 0;
        for (; i < 31 && rTag.mShown[i] != 0; i++) {
            text[i] = static_cast< char16_t >(static_cast< u8 >(rTag.mShown[i]));
        }
        text[i] = 0;
        setText(rTag.mName, text);
        const u32 color = rTag.mShownLuigi ? cLuigi : cMario;
        setTextColor(rTag.mName, color, color);
        f32 height;
        measure(rTag.mName, 4000.0f, &rTag.mWidth, &height);
        shape(rTag.mFrame, 0.0f, 0.0f, rTag.mWidth + cPadX * 2.0f, cHeight, cRadius);
        rTag.mName->mTranslate = nw4r::math::VEC3(0.0f, textCentre(0.0f, cTextSize), 0.0f);
    }
};  // namespace

NebulaNametags::NebulaNametags() : LayoutActor("NebulaNametags", true), mParts(nullptr) {
}

void NebulaNametags::init(const JMapInfoIter& rIter) {
    initLayoutManager("InformationWindow", 0);
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaNametags";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));
    MR::connectToSceneLayout(this);

    NebulaNametagsParts* p = new NebulaNametagsParts();
    memset(static_cast< void* >(p), 0, sizeof(NebulaNametagsParts));
    new (&p->mText) NebulaPaneKit();
    new (&p->mWindow) NebulaPaneKit();
    Pane* pRoot = pManager->mLayout->mpRootPane;
    if (pRoot == nullptr || !p->mText.init(pManager->mLayoutHolder)) {
        nebula_log("NebulaNametags: no InformationWindow layout\n");
        return;
    }
    p->mWindow.init(MR::createAndAddLayoutHolder("SysInfoWindowMini.arc"));
    // The information window's own panes stay hidden; the tags are built beside them.
    for (nw4r::lyt::PaneList::Iterator it = pRoot->GetChildList().GetBeginIter(); it != pRoot->GetChildList().GetEndIter(); ++it) {
        it->SetVisible(false);
    }

    const Kits kits = {&p->mText, &p->mWindow, nullptr};
    p->mRoot = makeGroup(pRoot, "NbTags");
    if (p->mRoot == nullptr) {
        return;
    }
    for (s32 i = 0; i < cTagsMax; i++) {
        Tag& rTag = p->mTags[i];
        char name[17];
        nebula_snprintf(name, sizeof(name), "NbTag%d", i);
        rTag.mRoot = makeGroup(p->mRoot, name);
        if (rTag.mRoot == nullptr) {
            return;
        }
        nebula_snprintf(name, sizeof(name), "NbTagFrm%d", i);
        rTag.mFrame = makeWindow(kits, rTag.mRoot, name);
        nebula_snprintf(name, sizeof(name), "NbTagName%d", i);
        rTag.mName = makeText(kits, rTag.mRoot, name, 32);
        if (rTag.mFrame == nullptr || rTag.mName == nullptr) {
            rTag.mRoot = nullptr;
            return;
        }
        paint(rTag.mFrame, cFill, cRing);
        setTextStyle(rTag.mName, cTextSize, cMario, cMario, 4);
        rTag.mShown[0] = 0;
        rTag.mRoot->SetVisible(false);
    }
    setInfluencedAlphaRecursive(p->mRoot, 0);
    mParts = p;

    // The layout's pane list, counted when it was built, again for the added panes.
    pManager->rebuildPaneInfo();
    LayoutActor::appear();
}

void NebulaNametags::control() {
    NebulaNametagsParts* p = mParts;
    if (p == nullptr) {
        return;
    }
    f32 scaleX, width, height;
    readScreen(&scaleX, &width, &height);
    p->mRoot->mScale = nw4r::math::VEC2(scaleX, 1.0f);
    // The game's cutscenes are its own: no names over them.
    const bool demo = MR::isDemoActive();
    const s32 copyNum = NebulaRemotePlayer::getCopyNum();
    for (s32 i = 0; i < cTagsMax; i++) {
        Tag& rTag = p->mTags[i];
        if (rTag.mRoot == nullptr) {
            continue;
        }
        const NebulaRemotePlayer* pCopy = i < copyNum ? NebulaRemotePlayer::getCopy(i) : nullptr;
        const bool here = pCopy != nullptr && pCopy->mNumber != 0 && !MR::isDead(pCopy);
        f32 x = 0.0f, y = 0.0f, scale = 1.0f;
        const bool seen = here && !demo && place(pCopy, width, height, &x, &y, &scale);
        // Another player on this copy: their name from nothing, not the last one's fading.
        if (here && (strcmp(rTag.mShown, pCopy->mName) != 0 || rTag.mShownLuigi != pCopy->mIsLuigi)) {
            rTag.mFade = 0;
            fill(rTag, pCopy);
        }
        const s32 fade = rTag.mFade + (seen ? 1 : -1);
        rTag.mFade = fade < 0 ? 0 : fade > cFadeFrames ? cFadeFrames : fade;
        if (rTag.mFade == 0 || !here) {
            rTag.mFade = here ? rTag.mFade : 0;
            rTag.mRoot->SetVisible(false);
            continue;
        }
        rTag.mRoot->SetVisible(true);
        // Where it was last seen while it fades out.
        if (seen) {
            rTag.mRoot->mTranslate = nw4r::math::VEC3(x, y + cHeight * 0.5f * scale, 0.0f);
            rTag.mRoot->mScale = nw4r::math::VEC2(scale, scale);
        }
        rTag.mRoot->mAlpha = toAlpha(easeOut(static_cast< f32 >(rTag.mFade) / cFadeFrames));
    }
}
