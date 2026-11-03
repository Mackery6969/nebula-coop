#include "Game/Player/NebulaRemotePlayer.hpp"
#include "nebula_module.h"
#include "Game/System/NebulaHost.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioDamage.hpp"
#include "Game/Player/MarioFaint.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Player/NebulaCoopBubble.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <cstdio>
#include <cstring>

namespace {
    // Copies of each character a scene keeps: one for each other player a room of 16 may have.
    const s32 cMaxPerCharacter = 15;
    NebulaRemotePlayer* sCopies[cMaxPerCharacter * 2];
    s32 sCopyNum = 0;

    bool isUnit(const f32* pVec) {
        const f32 length = pVec[0] * pVec[0] + pVec[1] * pVec[1] + pVec[2] * pVec[2];
        return length > 0.25f && length < 4.0f;
    }

    // Player collisions: the room's rules this frame, frames since this scene's copies were made, and whether nothing
    // touches the local Mario now.
    NebulaCoopRules sRules = {};
    s32 sSceneFrames = 0;
    bool sLocalIntangible = true;
    // sm64coopdx keeps players from touching in their first 60 frames in an area: here the scene's and each copy's.
    const s32 cContactAfter = 60;

    // A body's middle above its feet, and how far two bodies reach each other (sm64coopdx's hitboxes: 160 high, its
    // attacks reaching 1.75 times both radii of 37).
    const f32 cBodyCenter = 80.0f;
    const f32 cBodyHeight = 160.0f;
    const f32 cAttackReach = 130.0f;
    // Frames before another landing on the local Mario's head squashes him again.
    const s32 cStompEvery = 30;

    // Where the local Mario was these last frames, for the others' attacks as they saw him (sm64coopdx's
    // lag_compensation_store): two thirds of a second.
    struct Sample {
        TVec3f mCenter;  // his body's middle
        TVec3f mGravity;
        bool mSpinning;
    };
    const s32 cHistoryNum = 40;
    Sample sHistory[cHistoryNum];
    s32 sHistoryNext = 0;
    s32 sHistoryNum = 0;

    MarioActor* getLocalMario() {
        if (!MR::isExistSceneObj(SceneObj_MarioHolder)) {
            return nullptr;
        }
        return MR::getMarioHolder()->getMarioActor();
    }

    void recordLocal(const MarioActor* pMario) {
        Sample& rSample = sHistory[sHistoryNext];
        rSample.mGravity = pMario->getGravityVec();
        rSample.mCenter = pMario->mPosition - rSample.mGravity * cBodyCenter;
        rSample.mSpinning = MR::isPlayerSwingAction();
        sHistoryNext = (sHistoryNext + 1) % cHistoryNum;
        if (sHistoryNum < cHistoryNum) {
            sHistoryNum++;
        }
    }

    // The local Mario `ms` ago (at 60 frames a second), as far back as is kept.
    const Sample& rewound(u32 ms) {
        s32 frames = static_cast< s32 >(ms * 60 / 1000);
        if (frames > sHistoryNum - 1) {
            frames = sHistoryNum - 1;
        }
        s32 index = sHistoryNext - 1 - frames;
        while (index < 0) {
            index += cHistoryNum;
        }
        return sHistory[index];
    }
};  // namespace

bool NebulaRemotePlayer::isLocalIntangible() {
    const MarioActor* pMario = getLocalMario();
    return pMario == nullptr || MR::isPlayerDead() || MR::isDemoActive() || MR::isPlayerInRush() ||
           pMario->mPlayerMode == PlayerMode_Teresa || MR::isPlayerHidden() || MR::isOffPlayerControl();
}

NebulaRemotePlayer::NebulaRemotePlayer(bool isLuigi)
    : LiveActor(isLuigi ? "NebulaRemoteLuigi" : "NebulaRemoteMario"), mIsLuigi(isLuigi), mNumber(0), mXanimePlayer(nullptr),
      mFront(0.0f, 0.0f, 1.0f), mUp(0.0f, 1.0f, 0.0f), mRewindMs(0), mShownFrames(0), mStompTimer(0), mSolid(false),
      mBubble(nullptr) {
    mName[0] = '\0';
    mGroup[0] = '\0';
    mBck[0] = '\0';
    memset(&mState, 0, sizeof(mState));
}

void NebulaRemotePlayer::forgetAll() {
    sCopyNum = 0;
    sSceneFrames = 0;
    sHistoryNum = 0;
}

void NebulaRemotePlayer::tryCreate() {
    sCopyNum = 0;
    sSceneFrames = 0;
    sHistoryNum = 0;
    u32 roomSize = 0;
    NebulaHost::coopList(nullptr, 0, &roomSize);
    if (roomSize < 2) {
        return;
    }
    // Mario's animations, which the copies play, are the local player's.
    if (!MR::isExistSceneObj(SceneObj_MarioHolder) || MR::getMarioHolder()->getMarioActor() == nullptr) {
        return;
    }
    s32 perCharacter = static_cast< s32 >(roomSize) - 1;
    if (perCharacter > cMaxPerCharacter) {
        perCharacter = cMaxPerCharacter;
    }
    for (s32 i = 0; i < perCharacter * 2; i++) {
        NebulaRemotePlayer* pCopy = new NebulaRemotePlayer(i >= perCharacter);
        pCopy->initWithoutIter();
        pCopy->mBubble = NebulaCoopBubble::createModel();
        sCopies[sCopyNum++] = pCopy;
    }
    nebula_log("[coop] %d copies for the other players\n", sCopyNum);
}

s32 NebulaRemotePlayer::getCopyNum() {
    return sCopyNum;
}

const NebulaRemotePlayer* NebulaRemotePlayer::getCopy(s32 index) {
    return index >= 0 && index < sCopyNum ? sCopies[index] : nullptr;
}

TVec3f NebulaRemotePlayer::getNametagPos() const {
    // Mario stands about 160 tall: the name a little over his cap.
    return TVec3f(mPosition.x + mUp.x * 200.0f, mPosition.y + mUp.y * 200.0f, mPosition.z + mUp.z * 200.0f);
}

void NebulaRemotePlayer::updateAll() {
    if (sCopyNum == 0) {
        return;
    }
    NebulaHost::coopRules(&sRules);
    if (sSceneFrames < cContactAfter) {
        sSceneFrames++;
    }
    sLocalIntangible = isLocalIntangible();
    if (const MarioActor* pMario = getLocalMario()) {
        recordLocal(pMario);
    }
    // Static: the module links without __chkstk, so no frame may need a stack probe.
    static NebulaCoopRemotePlayer remote[cMaxPerCharacter * 2];
    u32 remoteNum = NebulaHost::coopRemote(remote, cMaxPerCharacter * 2);
    if (remoteNum > static_cast< u32 >(cMaxPerCharacter * 2)) {
        remoteNum = cMaxPerCharacter * 2;
    }
    bool placed[cMaxPerCharacter * 2] = {};
    // The copies showing someone still here keep them.
    for (s32 i = 0; i < sCopyNum; i++) {
        NebulaRemotePlayer* pCopy = sCopies[i];
        bool kept = false;
        for (u32 j = 0; j < remoteNum && pCopy->mNumber != 0; j++) {
            const bool isLuigi = (remote[j].player.flags & NEBULA_COOP_PLAYER_LUIGI) != 0;
            if (!placed[j] && remote[j].number == pCopy->mNumber && isLuigi == pCopy->mIsLuigi) {
                pCopy->show(remote[j]);
                placed[j] = true;
                kept = true;
                break;
            }
        }
        if (!kept) {
            pCopy->hide();
        }
    }
    // The rest onto free copies of their character.
    for (u32 j = 0; j < remoteNum; j++) {
        if (placed[j]) {
            continue;
        }
        const bool isLuigi = (remote[j].player.flags & NEBULA_COOP_PLAYER_LUIGI) != 0;
        for (s32 i = 0; i < sCopyNum; i++) {
            if (sCopies[i]->mNumber == 0 && sCopies[i]->mIsLuigi == isLuigi) {
                sCopies[i]->show(remote[j]);
                break;
            }
        }
    }
    // Player collisions, with everyone placed: a body only while the two may touch, and their attacks on the local
    // Mario.
    for (s32 i = 0; i < sCopyNum; i++) {
        NebulaRemotePlayer* pCopy = sCopies[i];
        if (pCopy->mNumber == 0) {
            continue;
        }
        if (pCopy->mStompTimer > 0) {
            pCopy->mStompTimer--;
        }
        const bool solid = pCopy->isTouchable();
        if (solid != pCopy->mSolid) {
            if (solid) {
                MR::validateHitSensors(pCopy);
            } else {
                MR::invalidateHitSensors(pCopy);
            }
            pCopy->mSolid = solid;
        }
        if (solid) {
            pCopy->tryAttackLocal();
        }
    }
}

void NebulaRemotePlayer::init(const JMapInfoIter& rIter) {
    // The player's own model: the local player's is already here; the other character's loads into the scene.
    initModelManagerWithAnm(mIsLuigi ? "Luigi" : "Mario", nullptr, false);
    MR::connectToScene(this, MR::MovementType_Player, MR::CalcAnimType_Player, MR::DrawBufferType_None, MR::DrawType_Player);

    // Mario's animations, as GhostPlayer plays them on the cosmic ghost.
    mXanimePlayer = new XanimePlayer(MR::getJ3DModel(this), MR::getPlayerXanimeResource());
    mXanimePlayer->duplicateSimpleGroup();
    mModelManager->mXanimePlayer = mXanimePlayer;
    mXanimePlayer->setDefaultAnimation("\212\356\226\173" /* 基本 */);
    mXanimePlayer->getCore()->enableJointTransform(MR::getJ3DModelData(this));
    mXanimePlayer->changeAnimation("\212\356\226\173" /* 基本 */);

    // The body the local Mario bumps into, as the cosmic ghost's; off until the two may touch.
    initHitSensor(1);
    MR::addHitSensorRide(this, "body", 4, 40.0f, TVec3f(0.0f, cBodyCenter, 0.0f));
    MR::invalidateHitSensors(this);

    MR::initShadowVolumeSphere(this, 50.0f);
    MR::onCalcShadow(this, nullptr);
    MR::onCalcShadowDropGravity(this, nullptr);
    MR::invalidateClipping(this);
    makeActorDead();
}

void NebulaRemotePlayer::show(const NebulaCoopRemotePlayer& rRemote) {
    const bool isNew = mNumber != rRemote.number;
    mNumber = rRemote.number;
    mRewindMs = rRemote.rewind_ms;
    if (isNew) {
        mShownFrames = 0;
        mStompTimer = 0;
    } else if (mShownFrames < cContactAfter) {
        mShownFrames++;
    }
    nebula_snprintf(mName, sizeof(mName), "%s", rRemote.name);
    mState = rRemote.player;
    mState.group[sizeof(mState.group) - 1] = '\0';
    mState.animation[sizeof(mState.animation) - 1] = '\0';
    mPosition.set(mState.position[0], mState.position[1], mState.position[2]);
    mRotation.set(mState.rotation[0], mState.rotation[1], mState.rotation[2]);
    mVelocity.set(mState.velocity[0], mState.velocity[1], mState.velocity[2]);
    if (isUnit(mState.gravity)) {
        mGravity.set(mState.gravity[0], mState.gravity[1], mState.gravity[2]);
    }
    if (isUnit(mState.front) && isUnit(mState.up)) {
        mFront.set(mState.front[0], mState.front[1], mState.front[2]);
        mUp.set(mState.up[0], mState.up[1], mState.up[2]);
    }
    if (isNew) {
        // Another player on this copy: their animation starts afresh.
        mGroup[0] = '\0';
        mBck[0] = '\0';
    }
    if (MR::isDead(this)) {
        appear();
    }
    // Their bubble, round their body's middle; bursting as they're let out.
    if (mBubble != nullptr) {
        if ((mState.flags & NEBULA_COOP_PLAYER_BUBBLED) != 0) {
            mBubble->follow(mPosition + mUp * cBodyCenter);
        } else if (!isNew) {
            mBubble->pop();
        } else {
            mBubble->hide();
        }
    }
}

void NebulaRemotePlayer::hide() {
    mNumber = 0;
    mName[0] = '\0';
    mShownFrames = 0;
    if (mBubble != nullptr) {
        mBubble->hide();
    }
    if (!MR::isDead(this)) {
        makeActorDead();
    }
}

void NebulaRemotePlayer::control() {
    if (mXanimePlayer == nullptr) {
        return;
    }
    // A new animation as the snapshot names it: its group, else (Mario's code played a bck of its own) the bck.
    if (strcmp(mGroup, mState.group) != 0 || strcmp(mBck, mState.animation) != 0) {
        nebula_snprintf(mGroup, sizeof(mGroup), "%s", mState.group);
        nebula_snprintf(mBck, sizeof(mBck), "%s", mState.animation);
        if (mGroup[0] != '\0') {
            mXanimePlayer->changeAnimation(mGroup);
        }
        const char* pPlaying = mXanimePlayer->getCurrentBckName();
        if (mBck[0] != '\0' && (pPlaying == nullptr || strcmp(pPlaying, mBck) != 0)) {
            mXanimePlayer->changeSimpleBck(mBck);
        }
    }
    for (u32 i = 0; i < 4; i++) {
        mXanimePlayer->changeTrackWeight(i, mState.track_weights[i]);
    }
    if (mXanimePlayer->getFrameCtrl() != nullptr) {
        MR::setBckRate(this, mState.animation_rate);
        MR::getBckCtrl(this)->setFrame(mState.animation_frame);
    }
}

bool NebulaRemotePlayer::isTouchable() const {
    if (sRules.interaction == NEBULA_COOP_INTERACTION_NONSOLID || sLocalIntangible) {
        return false;
    }
    if (sSceneFrames < cContactAfter || mShownFrames < cContactAfter) {
        return false;
    }
    return (mState.flags & (NEBULA_COOP_PLAYER_INTANGIBLE | NEBULA_COOP_PLAYER_DEAD)) == 0 && mState.mode != PlayerMode_Teresa;
}

void NebulaRemotePlayer::tryAttackLocal() {
    const u32 flags = mState.flags;
    const bool revamped = sRules.pvp == NEBULA_COOP_PVP_REVAMPED;
    // Their attack and how far it throws (sm64coopdx's scaler): a ground pound as its kick, a spin as its punch, which
    // revamped makes a stagger on the spot. A Rainbow Star's player throws whoever they touch.
    f32 scale = 1.0f;
    f32 reach = cAttackReach;
    bool stagger = false;
    if (mState.mode == PlayerMode_Invincible) {
        scale = 2.0f;
    } else if ((flags & NEBULA_COOP_PLAYER_HIP_DROP) != 0) {
        scale = revamped ? 1.9f : 2.0f;
    } else if ((flags & NEBULA_COOP_PLAYER_HIP_DROP_LAND) != 0 && revamped) {
        // Revamped: the landing reaches round it too (sm64coopdx's overlap scale, 0.3 more).
        scale = 1.9f;
        reach *= 1.3f;
    } else if ((flags & NEBULA_COOP_PLAYER_SPIN) != 0) {
        stagger = revamped;
    } else {
        return;
    }

    MarioActor* pMario = getLocalMario();
    // Already being knocked back (the next hit waits for it to end), or invincible.
    if (pMario == nullptr || pMario->mPlayerMode == PlayerMode_Invincible || !pMario->mMario->isEnableAddDamage()) {
        return;
    }
    // Where the local Mario was as they saw him.
    const Sample& rPast = rewound(mRewindMs);
    // Revamped: two spins meet and neither wins.
    if (stagger && rPast.mSpinning) {
        return;
    }
    const TVec3f center = mPosition + mUp * cBodyCenter;
    const TVec3f offset = rPast.mCenter - center;
    const f32 down = offset.dot(rPast.mGravity);
    const TVec3f across = offset - rPast.mGravity * down;
    if (down >= cBodyHeight || down <= -cBodyHeight || across.length() >= reach) {
        return;
    }

    // Thrown away from them, the knockback strength times the attack's scale (sm64coopdx's speeds are a 30 Hz
    // frame's, Galaxy's a 60 Hz one's); friendly fire costs health as an enemy's hit does.
    TVec3f away;
    MR::vecKillElement(pMario->mPosition - mPosition, pMario->getGravityVec(), &away);
    if (!MR::normalizeOrZero(&away)) {
        away = -pMario->mMario->mFrontVec;
    }
    const f32 knockback = static_cast< f32 >(sRules.knockback) * scale * 0.5f;
    const bool hurt = sRules.interaction == NEBULA_COOP_INTERACTION_FRIENDLY_FIRE;
    Mario* pPlayer = pMario->mMario;
    if (stagger && !pMario->isJumping()) {
        // (No slide while hanging: Galaxy's own stagger keeps it zero there.)
        if ((hurt ? pPlayer->faint(away) : pPlayer->doFlipWeak(away)) && !MR::isNearZero(pPlayer->mFaint->_18)) {
            pPlayer->mFaint->_18.setLength(knockback);
        }
    } else if (hurt ? pPlayer->damage(away) : pPlayer->doFlipLarge(away)) {
        // Thrown up as far as away, as sm64coopdx's; the damage's own rise is part of it.
        const f32 rise = pMario->getConst().getTable()->mJumpHeightDamage;
        pPlayer->mDamage->setVecSize(knockback, knockback > rise ? knockback - rise : 0.0f);
    }
}

void NebulaRemotePlayer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }
    MarioActor* pMario = getLocalMario();
    if (pMario == nullptr) {
        return;
    }
    // Their landing on the local Mario's head squashes him a moment, unharmed (sm64coopdx's bounceSquishTimer: they
    // bounce off in their own game); otherwise he's pushed out of them.
    const TVec3f& rGravity = pMario->getGravityVec();
    if ((mPosition - pMario->mPosition).dot(rGravity) < -cBodyHeight * 0.75f && mVelocity.dot(rGravity) > 0.0f) {
        if (mStompTimer == 0 && (mState.flags & NEBULA_COOP_PLAYER_HIP_DROP) == 0) {
            MR::sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_VERYWEAK, pReceiver, pSender);
            mStompTimer = cStompEvery;
        }
        return;
    }
    MR::sendMsgPush(pReceiver, pSender);
}

bool NebulaRemotePlayer::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    // The local Mario's stomp bounces him off their head, and his spin or ground pound lands as on an enemy; what it
    // does to them is their own game's to say.
    return MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg) || MR::isMsgPlayerSpinAttack(msg);
}

void NebulaRemotePlayer::calcAndSetBaseMtx() {
    TPos3f mtx;
    MR::makeMtxUpFrontPos(&mtx, mUp, mFront, mPosition);
    MR::setBaseTRMtx(this, mtx);
    MR::getJ3DModel(this)->setBaseScale(mScale);
}

void NebulaRemotePlayer::draw() const {
    if (MR::isDead(this)) {
        return;
    }
    J3DModelX* pModel = reinterpret_cast< J3DModelX* >(MR::getJ3DModel(this));
    pModel->viewCalc2();
    pModel->mFlags.clear();
    pModel->setDynamicList(nullptr);
    pModel->setDrawView(0);
    pModel->directDraw(nullptr);
}
