#ifdef TARGET_PC
#include "Game/Player/NebulaCoopBubble.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioDamage.hpp"
#include "Game/Player/NebulaRemotePlayer.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_game_api.h>
#include <revolution/os.h>

namespace {
    // A body's middle above its feet (NebulaRemotePlayer's), where the bubble is.
    const f32 cBodyCenter = 80.0f;
    // The bubble's size: the air bubble's model (about 130 round) a little over Mario, swelling by this much.
    const f32 cBubbleScale = 1.4f;
    const f32 cBubbleSwell = 0.1f;
    // Flying (sm64coopdx's act_bubbled at 60 frames a second): the stick's full tilt, and how much of the wanted speed
    // each frame takes (the rest is the last frame's, so it drifts and turns softly).
    const f32 cFlySpeed = 30.0f;
    const f32 cFlyEase = 0.1f;
    // Bursting: the bubble's middle this near the other player's, once these frames have passed (sm64coopdx's 120
    // units, after 20 of its frames).
    const f32 cPopReach = 150.0f;
    const s32 cPopAfter = 40;
    // With nobody to fly to for this long (a lost snapshot isn't nobody), the miss.
    const s32 cAloneFrames = 30;
    // Unharmed after bursting, blinking (sm64coopdx's invincTimer: 3 seconds).
    const u16 cInvincibleFrames = 180;
    // Warp pipes deep: where the player came out of each (the deepest last).
    const s32 cAreaDepth = 8;

    NebulaBubble* sLocal = nullptr;
    bool sBubbled = false;
    // The bubble ended in a miss: the miss states, entered again for it, keep it.
    bool sGiveUp = false;
    bool sGameOverDemo = false;
    s32 sPopWait = 0;
    s32 sAlone = 0;
    TVec3f sVelocity(0.0f, 0.0f, 0.0f);
    u32 sAreas[cAreaDepth];
    s32 sAreaNum = 0;

    s32 round(f32 value) {
        return static_cast< s32 >(value < 0.0f ? value - 0.5f : value + 0.5f);
    }

    f32 turn(f32 angle, f32 by) {
        angle += by;
        return angle >= 360.0f ? angle - 360.0f : angle;
    }

    // A placed position's key, the same in every player's game: its whole units, hashed (FNV-1a), never 0.
    u32 hashPosition(const TVec3f& rPos) {
        const s32 parts[3] = {round(rPos.x), round(rPos.y), round(rPos.z)};
        u32 hash = 2166136261u;
        for (u32 i = 0; i < 3; i++) {
            const u32 part = static_cast< u32 >(parts[i]);
            for (u32 b = 0; b < 4; b++) {
                hash = (hash ^ ((part >> (b * 8)) & 0xFFu)) * 16777619u;
            }
        }
        return hash != 0 ? hash : 1u;
    }

    bool isBubbleOn() {
        NebulaCoopRules rules = {};
        __NebulaCoopRules(&rules);
        return rules.bubble != 0;
    }

    TVec3f getCenter(const MarioActor* pActor) {
        return pActor->mPosition - pActor->getGravityVec() * cBodyCenter;
    }

    TVec3f getCenter(const NebulaRemotePlayer* pCopy) {
        return pCopy->mPosition + pCopy->mUp * cBodyCenter;
    }

    // The nearest player who can let the local one out: in this instance and its part of the stage, neither dead nor
    // in a bubble (sm64coopdx's nearest_antibubble_mario_state_to_object).
    const NebulaRemotePlayer* findRescuer(const TVec3f& rFrom) {
        const NebulaRemotePlayer* pNearest = nullptr;
        f32 nearest = 0.0f;
        const u32 area = NebulaCoopBubble::getArea();
        for (s32 i = 0; i < NebulaRemotePlayer::getCopyNum(); i++) {
            const NebulaRemotePlayer* pCopy = NebulaRemotePlayer::getCopy(i);
            if (pCopy == nullptr || pCopy->mNumber == 0 || pCopy->mState.area != area ||
                (pCopy->mState.flags & (NEBULA_COOP_PLAYER_DEAD | NEBULA_COOP_PLAYER_BUBBLED)) != 0) {
                continue;
            }
            const f32 distance = (getCenter(pCopy) - rFrom).length();
            if (pNearest == nullptr || distance < nearest) {
                pNearest = pCopy;
                nearest = distance;
            }
        }
        return pNearest;
    }

    // Out of the bubble, by the other player: full health, blinking a while, falling from there.
    void popLocal(MarioActor* pActor) {
        sBubbled = false;
        if (sLocal != nullptr) {
            sLocal->pop();
        }
        pActor->mVelocity.zero();
        pActor->setNerve(GET_NERVE(MarioActor, MarioActorNrvWait));
        // Placed where the bubble burst, as a demo places him, standing ready to fall from there.
        MR::setPlayerPos(pActor->mPosition);
        pActor->resetCondition();
        pActor->mMaxHealth = 3;
        pActor->mHealth = 3;
        pActor->resetWaterLife();
        pActor->_F44 = true;
        MR::onBind(pActor);
        MR::validateHitSensors(pActor);
        pActor->mMario->mDamage->_16 = cInvincibleFrames;
        OSReport("[coop] out of the bubble\n");
    }

    // Nobody left to let the player out: the miss after all. It takes a life itself, so the bubble's comes back.
    void missLocal(MarioActor* pActor) {
        sBubbled = false;
        sGiveUp = true;
        if (sLocal != nullptr) {
            sLocal->pop();
        }
        MR::incPlayerLeft();
        pActor->mVelocity.zero();
        pActor->_F44 = true;
        MR::onBind(pActor);
        MR::validateHitSensors(pActor);
        pActor->setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOver));
        OSReport("[coop] nobody to fly to: the bubble ends in a miss\n");
    }
};  // namespace

NebulaBubble::NebulaBubble() : LiveActor("NebulaBubble"), mFrame(0) {
}

void NebulaBubble::init(const JMapInfoIter& rIter) {
    initModelManagerWithAnm("AirBubble", nullptr, false);
    MR::connectToSceneItem(this);
    initEffectKeeper(0, nullptr, false);
    initSound(2, false);
    MR::invalidateClipping(this);
    MR::startBck(this, "Move", nullptr);
    mScale.set(cBubbleScale, cBubbleScale, cBubbleScale);
    makeActorDead();
}

void NebulaBubble::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    MR::getJ3DModel(this)->setBaseScale(mScale);
}

void NebulaBubble::follow(const TVec3f& rCenter) {
    if (MR::isDead(this)) {
        appear();
        MR::startBck(this, "Move", nullptr);
    }
    mPosition = rCenter;
    // Turning slowly, and swelling a little (sm64coopdx's bubble's wobble, at 60 frames a second).
    mRotation.x = turn(mRotation.x, 0.8f);
    mRotation.y = turn(mRotation.y, 0.6f);
    mRotation.z = turn(mRotation.z, 0.5f);
    mFrame++;
    const f32 phase = static_cast< f32 >(mFrame) * 0.07f;
    const f32 swell = cBubbleScale + cBubbleSwell * MR::sin(phase);
    mScale.set(swell, swell + cBubbleSwell * MR::sin(phase * 1.9f), swell);
}

void NebulaBubble::pop() {
    if (MR::isDead(this)) {
        return;
    }
    MR::emitEffect(this, "RecoveryBubbleBreak");
    MR::startSound(this, "SE_OJ_AIR_BUBBLE_BREAK");
    makeActorDead();
}

void NebulaBubble::hide() {
    if (!MR::isDead(this)) {
        makeActorDead();
    }
}

void NebulaCoopBubble::forgetAll() {
    sLocal = nullptr;
    sBubbled = false;
    sGiveUp = false;
    sGameOverDemo = false;
    sPopWait = 0;
    sAlone = 0;
    sVelocity.zero();
    sAreaNum = 0;
}

void NebulaCoopBubble::tryCreate() {
    sLocal = createModel();
}

NebulaBubble* NebulaCoopBubble::createModel() {
    NebulaBubble* pBubble = new NebulaBubble();
    pBubble->initWithoutIter();
    return pBubble;
}

bool NebulaCoopBubble::tryStart(MarioActor* pActor) {
    // A race lost (forceKill's 1, 2 and 5) isn't a death; a game over isn't bubbled (sm64coopdx bubbles with lives
    // left only), nor a miss the scene already plays.
    if (sLocal == nullptr || sBubbled || sGiveUp || sGameOverDemo || !isBubbleOn()) {
        return false;
    }
    if (pActor->_39D == 1 || pActor->_39D == 2 || pActor->_39D == 5 || MR::isGlobalTimerEnd() || MR::getPlayerLeft() <= 0) {
        return false;
    }
    if (findRescuer(getCenter(pActor)) == nullptr) {
        return false;
    }

    // A life, as sm64coopdx's bubble takes one.
    MR::decPlayerLeft();
    // A press (setPress) left Mario flattened and the pointer in its death mode: both end here.
    if (pActor->_390 != 0) {
        pActor->_390 = 0;
        MR::endStarPointerMode(pActor);
    }
    pActor->_B90 = false;
    pActor->_B91 = false;
    pActor->resetCondition();
    pActor->mHealth = 0;
    pActor->mVelocity.zero();
    // Nothing touches him: no enemy, item or death area (his status doesn't run in the bubble), and no wall.
    MR::invalidateHitSensors(pActor);
    pActor->_F44 = false;
    MR::offBind(pActor);
    MR::startBckPlayer("SpaceWait", static_cast< s32 >(20));
    pActor->setNerve(GET_NERVE(MarioActor, MarioActorNrvNebulaBubble));
    sBubbled = true;
    sPopWait = cPopAfter;
    sAlone = 0;
    sVelocity.zero();
    sLocal->follow(getCenter(pActor));
    MR::startSound(sLocal, "SE_OJ_AIR_BUBBLE_BREAK");
    OSReport("[coop] in a bubble (%d lives left)\n", MR::getPlayerLeft());
    return true;
}

void NebulaCoopBubble::updateLocal(MarioActor* pActor) {
    if (!sBubbled) {
        return;
    }
    // As the miss states keep him: no animation of his own over the bubble's, the model where he is.
    pActor->calcHeadPos();
    pActor->updateRealMtx();
    pActor->_F44 = false;
    MR::offBind(pActor);

    const TVec3f center = getCenter(pActor);
    const NebulaRemotePlayer* pTarget = findRescuer(center);
    if (pTarget == nullptr || !isBubbleOn()) {
        if (++sAlone >= cAloneFrames) {
            missLocal(pActor);
            return;
        }
    } else {
        sAlone = 0;
    }

    // Flown where the camera looks: the stick across and ahead, A up and Z down, eased (sm64coopdx's 0.9 and 0.1).
    TVec3f want = MR::getCamXdir() * MR::getSubPadStickX(WPAD_CHAN0) + MR::getCamZdir() * MR::getSubPadStickY(WPAD_CHAN0);
    if (MR::testCorePadButtonA(WPAD_CHAN0)) {
        want += MR::getCamYdir();
    }
    if (MR::testSubPadButtonZ(WPAD_CHAN0)) {
        want -= MR::getCamYdir();
    }
    if (want.length() > 1.0f) {
        MR::normalize(&want);
    }
    sVelocity = sVelocity * (1.0f - cFlyEase) + want * (cFlySpeed * cFlyEase);
    pActor->mVelocity = sVelocity;

    if (sLocal != nullptr) {
        sLocal->follow(center + sVelocity);
    }

    if (sPopWait > 0) {
        sPopWait--;
    } else if (pTarget != nullptr && (getCenter(pTarget) - center).length() < cPopReach) {
        popLocal(pActor);
    }
}

bool NebulaCoopBubble::isLocalBubbled() {
    return sBubbled;
}

u32 NebulaCoopBubble::getArea() {
    return sAreaNum > 0 ? sAreas[sAreaNum - 1] : 0u;
}

void NebulaCoopBubble::noticePipeWarp(const TVec3f& rEntrance, const TVec3f& rExit) {
    // Back down the pipe the player came out of: the part before. Otherwise a deeper part, the exit's.
    if (sAreaNum > 0 && sAreas[sAreaNum - 1] == hashPosition(rEntrance)) {
        sAreaNum--;
        return;
    }
    if (sAreaNum == cAreaDepth) {
        for (s32 i = 1; i < cAreaDepth; i++) {
            sAreas[i - 1] = sAreas[i];
        }
        sAreaNum--;
    }
    sAreas[sAreaNum++] = hashPosition(rExit);
}

void NebulaCoopBubble::noticeGameOverDemo() {
    sGameOverDemo = true;
}
#endif
