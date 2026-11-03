#include "Game/Screen/NebulaCoopChat.hpp"
#include "nebula_module.h"
#include "Game/System/NebulaHost.hpp"

#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/lyt/window.h>

namespace {
    using namespace NebulaUi;

    // In screen units (the screen is 456 high at every resolution): the text's size, the distance from the screen's
    // left and bottom edges, a strip's padding around its text, the space between strips and between a name and its
    // words, the widest a line runs (a share of the screen's width, and at most), and a strip's corners.
    const f32 cTextSize = 14.0f;
    const f32 cMargin = 10.0f;
    const f32 cPadX = 7.0f;
    const f32 cPadY = 4.0f;
    const f32 cGap = 3.0f;
    const f32 cNameGap = 5.0f;
    const f32 cWidthShare = 0.45f;
    const f32 cWidthMost = 420.0f;
    const f32 cWordsLeast = 80.0f;
    const f32 cRadius = 6.0f;
    // The typed line's cursor shows for this many frames, then hides as long.
    const s32 cBlinkFrames = 30;

    // RGBA: the strips are the game's window dimmed to see through (the typed line's darker, in a gold ring); the
    // words white, a join or leave grey, a command's words pale yellow (sm64coopdx's), a name in its character's
    // colour (Mario's red, Luigi's green).
    const u32 cStripFill = 0x0A0F1EB4;
    const u32 cInputFill = 0x0A0F1EDC;
    const u32 cGold = 0xEBC800FF;
    const u32 cWhite = 0xFFFFFFFF;
    const u32 cNotice = 0xBEBEC8FF;
    const u32 cCommand = 0xFFF982FF;
    const u32 cMario = 0xFF6E64FF;
    const u32 cLuigi = 0x6EF046FF;

    const s32 cTextLength = 192;

    struct Row {
        Pane* mGroup;
        Window* mStrip;
        TextBox* mName;
        TextBox* mText;
        u32 mId;  // the line it shows (0: none)
        f32 mWidth;
        f32 mHeight;
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

    // The width `pString` takes in `pText` (its font and size), on one line.
    f32 widthOf(TextBox* pText, const char16_t* pString) {
        setText(pText, pString);
        f32 width, height;
        measure(pText, 4000.0f, &width, &height);
        return width;
    }
};  // namespace

struct NebulaCoopChatParts {
    NebulaPaneKit mText;    // InformationWindow: the game's message font
    NebulaPaneKit mWindow;  // SysInfoWindowMini: the window with a ring
    Pane* mRoot;
    Row mRows[NEBULA_COOP_CHAT_LINES];
    Pane* mInput;
    Window* mInputStrip;
    TextBox* mInputMark;
    TextBox* mInputText;
    TextBox* mCursor;
    u32 mInputSerial;
    bool mTyping;
    f32 mCursorX;
    f32 mMarkWidth;
    f32 mCursorWidth;
    f32 mLineHeight;
    s32 mBlink;
    bool mShown;
    f32 mScaleX;
    f32 mWidth;
    f32 mHeight;
    NebulaCoopChatView mView;
};

NebulaCoopChat::NebulaCoopChat() : LayoutActor("NebulaCoopChat", true), mParts(nullptr) {
}

void NebulaCoopChat::init(const JMapInfoIter& rIter) {
    initLayoutManager("InformationWindow", 0);
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaCoopChat";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));

    NebulaCoopChatParts* p = new NebulaCoopChatParts();
    Pane* pLayoutRoot = pManager->mLayout->mpRootPane;
    if (pLayoutRoot == nullptr || !p->mText.init(pManager->mLayoutHolder)) {
        nebula_log("NebulaCoopChat: no InformationWindow layout\n");
        delete p;
        return;
    }
    // Without the ringed window, the information window's stands in (makeWindow).
    p->mWindow.init(MR::createAndAddLayoutHolder("SysInfoWindowMini.arc"));
    // The information window's own panes stay hidden; the chat is built beside them.
    for (nw4r::lyt::PaneList::Iterator it = pLayoutRoot->GetChildList().GetBeginIter(); it != pLayoutRoot->GetChildList().GetEndIter(); ++it) {
        it->SetVisible(false);
    }
    const Kits kits = {&p->mText, &p->mWindow, nullptr};
    p->mRoot = makeGroup(pLayoutRoot, "NcChat");
    if (p->mRoot == nullptr) {
        delete p;
        return;
    }

    char name[16];
    for (s32 i = 0; i < NEBULA_COOP_CHAT_LINES; i++) {
        Row& rRow = p->mRows[i];
        nebula_snprintf(name, sizeof(name), "NcRow%d", static_cast< int >(i));
        rRow.mGroup = makeGroup(p->mRoot, name);
        if (rRow.mGroup == nullptr) {
            delete p;
            return;
        }
        nebula_snprintf(name, sizeof(name), "NcStrip%d", static_cast< int >(i));
        rRow.mStrip = makeWindow(kits, rRow.mGroup, name);
        nebula_snprintf(name, sizeof(name), "NcName%d", static_cast< int >(i));
        rRow.mName = makeText(kits, rRow.mGroup, name, 32);
        nebula_snprintf(name, sizeof(name), "NcText%d", static_cast< int >(i));
        rRow.mText = makeText(kits, rRow.mGroup, name, cTextLength);
        if (rRow.mStrip == nullptr || rRow.mName == nullptr || rRow.mText == nullptr) {
            delete p;
            return;
        }
        paint(rRow.mStrip, cStripFill, cStripFill);
        setTextStyle(rRow.mName, cTextSize, cWhite, cWhite, 0);
        setTextStyle(rRow.mText, cTextSize, cWhite, cWhite, 0);
        setInfluencedAlphaRecursive(rRow.mGroup, 0);
        rRow.mGroup->SetVisible(false);
    }

    p->mInput = makeGroup(p->mRoot, "NcInput");
    if (p->mInput == nullptr) {
        delete p;
        return;
    }
    p->mInputStrip = makeWindow(kits, p->mInput, "NcInStrip");
    p->mInputMark = makeText(kits, p->mInput, "NcInMark", 4);
    p->mInputText = makeText(kits, p->mInput, "NcInText", cTextLength);
    p->mCursor = makeText(kits, p->mInput, "NcInCursor", 4);
    if (p->mInputStrip == nullptr || p->mInputMark == nullptr || p->mInputText == nullptr || p->mCursor == nullptr) {
        delete p;
        return;
    }
    paint(p->mInputStrip, cInputFill, cGold);
    setTextStyle(p->mInputMark, cTextSize, cGold, cGold, 0);
    setTextStyle(p->mInputText, cTextSize, cWhite, cWhite, 0);
    setTextStyle(p->mCursor, cTextSize, cGold, cGold, 0);
    f32 width;
    p->mCursorWidth = widthOf(p->mCursor, u"|");
    p->mMarkWidth = widthOf(p->mInputMark, u">");
    measure(p->mInputMark, 4000.0f, &width, &p->mLineHeight);
    p->mInput->SetVisible(false);

    mParts = p;
    // The layout's pane list, counted when it was built, again for the added panes.
    pManager->rebuildPaneInfo();
    LayoutActor::appear();
}

void NebulaCoopChat::update() {
    movement();
    calcAnim();
}

void NebulaCoopChat::drawOnTop() const {
    if (mParts != nullptr && mParts->mShown) {
        draw();
    }
}

void NebulaCoopChat::control() {
    NebulaCoopChatParts* p = mParts;
    if (p == nullptr) {
        return;
    }
    NebulaCoopChatView& rView = p->mView;
    NebulaHost::coopChat(&rView);
    const u32 count = rView.line_count < NEBULA_COOP_CHAT_LINES ? rView.line_count : NEBULA_COOP_CHAT_LINES;
    p->mShown = count > 0 || rView.typing != 0;
    if (!p->mShown) {
        p->mTyping = false;
        return;
    }

    // A new screen shape sets every line again (its width bounds them).
    f32 scaleX, width, height;
    readScreen(&scaleX, &width, &height);
    const bool reshaped = __builtin_fabsf(width - p->mWidth) > 0.5f || __builtin_fabsf(height - p->mHeight) > 0.5f || scaleX != p->mScaleX;
    p->mScaleX = scaleX;
    p->mWidth = width;
    p->mHeight = height;
    p->mRoot->mScale = nw4r::math::VEC2(scaleX, 1.0f);
    f32 limit = width * cWidthShare;
    limit = limit > cWidthMost ? cWidthMost : limit;
    const f32 left = -width * 0.5f + cMargin;
    f32 bottom = -height * 0.5f + cMargin;

    // The typed line, at the bottom: as much of it around the cursor as fits.
    const bool typing = rView.typing != 0;
    if (typing && (!p->mTyping || reshaped || rView.input_serial != p->mInputSerial)) {
        rView.input[NEBULA_COOP_CHAT_TEXT - 1] = 0;
        char16_t text[NEBULA_COOP_CHAT_TEXT];
        toWide(text, NEBULA_COOP_CHAT_TEXT, rView.input);
        s32 length = 0;
        while (text[length] != 0) {
            length++;
        }
        const s32 cursor = static_cast< s32 >(rView.cursor) < length ? static_cast< s32 >(rView.cursor) : length;
        const f32 room = limit - p->mMarkWidth - cNameGap - p->mCursorWidth;
        char16_t shown[NEBULA_COOP_CHAT_TEXT];
        s32 start = 0;
        s32 end = length;
        for (;;) {
            s32 n = 0;
            for (s32 i = start; i < end; i++) {
                shown[n++] = text[i];
            }
            shown[n] = 0;
            if (widthOf(p->mInputText, shown) <= room || end - start <= 1) {
                break;
            }
            if (start < cursor) {
                start++;
            } else {
                end--;
            }
        }
        // The cursor after the letters before it.
        char16_t before[NEBULA_COOP_CHAT_TEXT];
        s32 n = 0;
        for (s32 i = start; i < cursor; i++) {
            before[n++] = text[i];
        }
        before[n] = 0;
        p->mCursorX = n > 0 ? widthOf(p->mInputText, before) : 0.0f;
        widthOf(p->mInputText, shown);
        setText(p->mInputMark, u">");
        setText(p->mCursor, u"|");
        p->mInputSerial = rView.input_serial;
        p->mBlink = 0;
    }
    p->mTyping = typing;
    p->mInput->SetVisible(typing);
    if (typing) {
        const f32 w = limit + cPadX * 2.0f;
        const f32 h = p->mLineHeight + cPadY * 2.0f;
        shape(p->mInputStrip, left + w * 0.5f, bottom + h * 0.5f, w, h, cRadius);
        const f32 top = bottom + h - cPadY;
        const f32 textLeft = left + cPadX + p->mMarkWidth + cNameGap;
        p->mInputMark->mTranslate = nw4r::math::VEC3(left + cPadX, top, 0.0f);
        p->mInputText->mTranslate = nw4r::math::VEC3(textLeft, top, 0.0f);
        p->mCursor->mTranslate = nw4r::math::VEC3(textLeft + p->mCursorX - p->mCursorWidth * 0.5f, top, 0.0f);
        p->mCursor->SetVisible((p->mBlink / cBlinkFrames) % 2 == 0);
        p->mBlink++;
        bottom += h + cGap;
    }

    // The lines, the newest lowest. Each row shows the line in its place in the view; a row whose line changed (or
    // a new screen shape) sets its words again.
    for (u32 i = 0; i < NEBULA_COOP_CHAT_LINES; i++) {
        Row& rRow = p->mRows[i];
        if (i >= count) {
            rRow.mGroup->SetVisible(false);
            rRow.mId = 0;
            continue;
        }
        NebulaCoopChatLine& rLine = rView.lines[i];
        if (rRow.mId != rLine.id || reshaped) {
            rLine.name[sizeof(rLine.name) - 1] = 0;
            rLine.text[sizeof(rLine.text) - 1] = 0;
            char16_t name[32];
            toWide(name, 32, rLine.name);
            f32 nameWidth = 0.0f;
            f32 nameHeight = 0.0f;
            f32 textWidth, textHeight;
            if (rLine.kind == NEBULA_COOP_CHAT_MESSAGE) {
                char16_t label[36];
                append(append(label, label + 36, name), label + 36, u":");
                setText(rRow.mName, label);
                measure(rRow.mName, 4000.0f, &nameWidth, &nameHeight);
                const u32 colour = rLine.character == 1 ? cLuigi : cMario;
                setTextColor(rRow.mName, colour, colour);
                rRow.mName->SetVisible(true);
                char16_t text[NEBULA_COOP_CHAT_TEXT];
                toWide(text, NEBULA_COOP_CHAT_TEXT, rLine.text);
                f32 room = limit - nameWidth - cNameGap;
                room = room < cWordsLeast ? cWordsLeast : room;
                setTextColor(rRow.mText, cWhite, cWhite);
                wrapWords(rRow.mText, text, room, &textWidth, &textHeight);
                rRow.mWidth = nameWidth + cNameGap + textWidth;
            } else if (rLine.kind == NEBULA_COOP_CHAT_NOTICE) {
                // A command's words: its text in the played language when the host names one, else as it came.
                rRow.mName->SetVisible(false);
                char16_t text[cTextLength];
                if (strncmp(rLine.text, "CoopCmd", 7) == 0 && NebulaText::has(rLine.text)) {
                    fillName(text, cTextLength, rLine.text, name);
                } else {
                    toWide(text, cTextLength, rLine.text);
                }
                setTextColor(rRow.mText, cCommand, cCommand);
                wrapWords(rRow.mText, text, limit, &textWidth, &textHeight);
                rRow.mWidth = textWidth;
            } else {
                rRow.mName->SetVisible(false);
                char16_t text[cTextLength];
                fillName(text, cTextLength, rLine.kind == NEBULA_COOP_CHAT_JOINED ? "CoopChatJoined" : "CoopChatLeft", name);
                setTextColor(rRow.mText, cNotice, cNotice);
                wrapWords(rRow.mText, text, limit, &textWidth, &textHeight);
                rRow.mWidth = textWidth;
            }
            rRow.mHeight = textHeight > nameHeight ? textHeight : nameHeight;
            rRow.mId = rLine.id;
        }
        rRow.mGroup->SetVisible(true);
    }
    for (s32 i = static_cast< s32 >(count) - 1; i >= 0; i--) {
        Row& rRow = p->mRows[i];
        const f32 w = rRow.mWidth + cPadX * 2.0f;
        const f32 h = rRow.mHeight + cPadY * 2.0f;
        shape(rRow.mStrip, left + w * 0.5f, bottom + h * 0.5f, w, h, cRadius);
        const f32 top = bottom + h - cPadY;
        const bool named = rRow.mName->IsVisible();
        const f32 nameWidth = named ? rRow.mName->mSize.width - 2.0f : 0.0f;
        rRow.mName->mTranslate = nw4r::math::VEC3(left + cPadX, top, 0.0f);
        rRow.mText->mTranslate = nw4r::math::VEC3(left + cPadX + (named ? nameWidth + cNameGap : 0.0f), top, 0.0f);
        rRow.mGroup->mAlpha = static_cast< u8 >(rView.lines[i].alpha > 255 ? 255 : rView.lines[i].alpha);
        bottom += h + cGap;
    }
}
