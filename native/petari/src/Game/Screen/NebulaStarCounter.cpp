#include "Game/Screen/NebulaStarCounter.hpp"
#include "nebula_module.h"
#include "Game/System/NebulaHost.hpp"

#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/CountUpPaneRumbler.hpp"
#include "Game/Screen/CounterLayoutAppearer.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <cstring>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/picture.h>
#include <nw4r/lyt/textBox.h>

namespace {
    // sm64coopdx's mod's colours: red for this room's stars, green for every star.
    const u32 cRoomTint = 0xE81123FF;
    const u32 cAllTimeTint = 0x32B028FF;
    const s32 cTintedMax = 16;
    const s32 cCountMax = 99999;  // the all-time count's widest
    const f32 cLineStepFallback = 34.0f;
};  // namespace

namespace NrvNebulaStarCounter {
    NEW_NERVE(NebulaStarCounterNrvAppear, NebulaStarCounter, Appear);
    NEW_NERVE(NebulaStarCounterNrvWait, NebulaStarCounter, Wait);
    NEW_NERVE(NebulaStarCounterNrvCountUp, NebulaStarCounter, CountUp);
    NEW_NERVE(NebulaStarCounterNrvDisappear, NebulaStarCounter, Disappear);
};  // namespace NrvNebulaStarCounter

// The layout's star (its pictures and any text but the number) in the counter's colour. The layout's animations may
// set those colours again (its Flash): each one they write is tinted once more after them.
struct NebulaStarCounterTint {
    struct Entry {
        nw4r::lyt::Pane* mPane;
        bool mText;
        nw4r::ut::Color mWritten[4];
    };
    Entry mEntries[cTintedMax];
    s32 mNum;
    nw4r::ut::Color mTint;

    nw4r::ut::Color tinted(nw4r::ut::Color color) const {
        nw4r::ut::Color out = color;
        out.r = static_cast< u8 >(color.r * mTint.r / 255);
        out.g = static_cast< u8 >(color.g * mTint.g / 255);
        out.b = static_cast< u8 >(color.b * mTint.b / 255);
        return out;
    }

    void collect(nw4r::lyt::Pane* pPane) {
        // The number keeps its colour.
        if (pPane == nullptr || strcmp(pPane->mName, "Counter") == 0) {
            return;
        }
        const bool text = pPane->GetRuntimeTypeInfo()->IsDerivedFrom(&nw4r::lyt::TextBox::typeInfo);
        const bool picture = pPane->GetRuntimeTypeInfo()->IsDerivedFrom(&nw4r::lyt::Picture::typeInfo);
        if ((text || picture) && mNum < cTintedMax) {
            Entry& rEntry = mEntries[mNum++];
            rEntry.mPane = pPane;
            rEntry.mText = text;
            for (s32 i = 0; i < 4; i++) {
                // Never a colour the pane has: the first apply() tints them all.
                rEntry.mWritten[i] = 0x00000001;
            }
        }
        for (nw4r::lyt::PaneList::Iterator it = pPane->GetChildList().GetBeginIter(); it != pPane->GetChildList().GetEndIter(); ++it) {
            collect(&*it);
        }
    }

    void apply() {
        for (s32 i = 0; i < mNum; i++) {
            Entry& rEntry = mEntries[i];
            if (rEntry.mText) {
                nw4r::lyt::TextBox* pText = static_cast< nw4r::lyt::TextBox* >(rEntry.mPane);
                for (u32 c = 0; c < 2; c++) {
                    const nw4r::ut::Color now = pText->GetTextColor(c);
                    if (static_cast< u32 >(now) != static_cast< u32 >(rEntry.mWritten[c])) {
                        rEntry.mWritten[c] = tinted(now);
                        pText->SetTextColor(c, rEntry.mWritten[c]);
                    }
                }
            } else {
                nw4r::lyt::Picture* pPicture = static_cast< nw4r::lyt::Picture* >(rEntry.mPane);
                for (u32 c = 0; c < 4; c++) {
                    const nw4r::ut::Color now = pPicture->GetVtxColor(c);
                    if (static_cast< u32 >(now) != static_cast< u32 >(rEntry.mWritten[c])) {
                        rEntry.mWritten[c] = tinted(now);
                        pPicture->SetVtxColor(c, rEntry.mWritten[c]);
                    }
                }
            }
        }
    }
};

NebulaStarCounter::NebulaStarCounter(Kind kind)
    : LayoutActor(kind == Kind_Room ? "NebulaRoomStarCounter" : "NebulaAllTimeStarCounter", true), mKind(kind), mCount(0),
      mLayoutAppearer(nullptr), mPaneRumbler(nullptr), mTint(nullptr) {
}

void NebulaStarCounter::init(const JMapInfoIter& rIter) {
    // The save's own counter, with room in its number for an all-time count.
    initLayoutManagerWithTextBoxBufferLength("StarCounter", 2, 8);
    MR::createAndAddPaneCtrl(this, "Counter", 1);

    mLayoutAppearer = new CounterLayoutAppearer(this, TVec2f(-50.0f, 0.0f));
    mPaneRumbler = new CountUpPaneRumbler(this, "Counter");

    mTint = new NebulaStarCounterTint();
    mTint->mNum = 0;
    mTint->mTint = mKind == Kind_Room ? cRoomTint : cAllTimeTint;
    mTint->collect(getLayoutManager()->mLayout->mpRootPane);
    static bool sReported = false;
    if (!sReported) {
        sReported = true;
        nebula_log("NebulaStarCounter: %d of StarCounter's panes tinted\n", mTint->mNum);
        for (s32 i = 0; i < mTint->mNum; i++) {
            nebula_log("  %s (%s)\n", mTint->mEntries[i].mPane->mName, mTint->mEntries[i].mText ? "text" : "picture");
        }
    }

    MR::connectToSceneLayout(this);
    initNerve(GET_NERVE(NebulaStarCounter, NebulaStarCounterNrvAppear));
}

s32 NebulaStarCounter::getCount() const {
    if (mKind == Kind_Room) {
        return NebulaHost::coopStarCount();
    }
    const u64 count = NebulaAchievements::getStat(NebulaStat_PowerStars);
    return count > static_cast< u64 >(cCountMax) ? cCountMax : static_cast< s32 >(count);
}

bool NebulaStarCounter::isWanted() const {
    return NebulaHost::option(NEBULA_OPTION_STAR_COUNTER) != 0;
}

f32 NebulaStarCounter::getLineStep() const {
    // The tallest picture or text of the layout (the star, or the number), from layout units to the screen's.
    f32 tallest = 0.0f;
    for (s32 i = 0; i < mTint->mNum; i++) {
        const nw4r::lyt::Pane* pPane = mTint->mEntries[i].mPane;
        const f32 height = pPane->mSize.height * pPane->mScale.y;
        tallest = height > tallest ? height : tallest;
    }
    const nw4r::lyt::Pane* pCounter = getLayoutManager()->mLayout->mpRootPane->FindPaneByName("Counter", true);
    if (pCounter != nullptr && pCounter->mSize.height * pCounter->mScale.y > tallest) {
        tallest = pCounter->mSize.height * pCounter->mScale.y;
    }
    if (tallest <= 0.0f) {
        return cLineStepFallback;
    }
    TVec2f top, bottom;
    MR::convertLayoutPosToScreenPos(&top, TVec2f(0.0f, 0.0f));
    MR::convertLayoutPosToScreenPos(&bottom, TVec2f(0.0f, -tallest));
    const f32 step = bottom.y - top.y;
    return step > 1.0f ? step : cLineStepFallback;
}

void NebulaStarCounter::appear() {
    mCount = getCount();
    if (mCount < 0 || !isWanted()) {
        // The room's count outside a room, or the room doesn't play the mod.
        kill();
        return;
    }
    // Under the save's counter: the room's first, then the all-time one (first when there is no room).
    const s32 slot = mKind == Kind_Room || NebulaHost::coopStarCount() < 0 ? 1 : 2;
    mLayoutAppearer->appear(TVec2f(0.0f, getLineStep() * slot));
    mPaneRumbler->reset();
    MR::startAnim(this, "Wait", 1);

    MR::setTextBoxNumberRecursive(this, "Counter", mCount);
    setNerve(GET_NERVE(NebulaStarCounter, NebulaStarCounterNrvAppear));
    LayoutActor::appear();
    MR::requestMovementOn(this);
}

void NebulaStarCounter::disappear() {
    if (MR::isDead(this)) {
        return;
    }
    MR::requestMovementOn(this);
    setNerve(GET_NERVE(NebulaStarCounter, NebulaStarCounterNrvDisappear));
}

void NebulaStarCounter::control() {
    mLayoutAppearer->update();
    mPaneRumbler->update();
}

void NebulaStarCounter::calcAnim() {
    LayoutActor::calcAnim();
    mTint->apply();
}

void NebulaStarCounter::exeAppear() {
    if (mLayoutAppearer->isAppeared()) {
        setNerve(GET_NERVE(NebulaStarCounter, NebulaStarCounterNrvWait));
    }
}

void NebulaStarCounter::exeWait() {
    // Turned off while shown: out at once rather than with the save's counter. Asked once a second (the host's
    // settings are a copy each time).
    if (getNerveStep() % 60 == 0 && !isWanted()) {
        disappear();
        return;
    }
    const s32 count = getCount();
    if (count < mCount) {
        // A new room starts from none.
        mCount = count < 0 ? 0 : count;
        MR::setTextBoxNumberRecursive(this, "Counter", mCount);
    } else if (count > mCount) {
        setNerve(GET_NERVE(NebulaStarCounter, NebulaStarCounterNrvCountUp));
    }
}

void NebulaStarCounter::exeCountUp() {
    if (MR::isFirstStep(this)) {
        mCount++;

        MR::setTextBoxNumberRecursive(this, "Counter", mCount);
        MR::startAnim(this, "Flash", 0);
        mPaneRumbler->start();
    }

    if (MR::isAnimStopped(this, 0) && !mPaneRumbler->isRumbling()) {
        setNerve(GET_NERVE(NebulaStarCounter, NebulaStarCounterNrvWait));
    }
}

void NebulaStarCounter::exeDisappear() {
    if (MR::isFirstStep(this)) {
        mLayoutAppearer->disappear();
    }

    if (mLayoutAppearer->isDisappeared()) {
        kill();
    }
}
