#include "Game/Scene/GameScene.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/NebulaLoadTrace.hpp"
#include "JSystem/JAudio2/JASWaveArcLoader.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include "Game/Util/GameLightSignal.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/GameEventFlagChecker.hpp"
#include "Game/System/GameEventFlagStorage.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <cstring>
#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/System/NebulaFrameGen.hpp"
#include <nebula_game_api.h>
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/AreaObj/AreaForm.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/AreaObj/WaterArea.hpp"
#include "Game/Map/OceanBowl.hpp"
#include "Game/Map/OceanRing.hpp"
#include "Game/Map/OceanSphere.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WaterPoint.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/SleepControllerHolder.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/NPC/EventDirector.hpp"
#include "Game/NPC/NPCFunction.hpp"
#include "Game/NPC/TalkDirector.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Scene/GameScenePauseControl.hpp"
#include "Game/Scene/GameSceneScenarioOpeningCameraState.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Screen/CometRetryButton.hpp"
#include "Game/Screen/GamePauseSequence.hpp"
#include "Game/Screen/GameStageClearSequence.hpp"
#include "Game/Screen/LensFlare.hpp"
#include "Game/Screen/MoviePlayingSequence.hpp"
#include "Game/Screen/ScreenAlphaCapture.hpp"
#include "Game/System/GalaxyMapController.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/NebulaRemotePlayer.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Animation/XanimeResource.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/System/ResourceInfo.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/JMapIdInfo.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#if !defined(NEBULA_RELEASE)
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaSequencer.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/JUtility/JUTNameTab.hpp"
#include <nebula_cooked_material.h>
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#endif
#include "Game/NPC/NebulaAchievementLuma.hpp"
#include "Game/Screen/NebulaAchievementBoard.hpp"
#include "Game/Screen/NebulaAchievementToast.hpp"
#include "Game/Screen/NebulaCoopChat.hpp"
#include "Game/Screen/NebulaCoopPopups.hpp"
#include "Game/Screen/NebulaPlayerList.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/Player/NebulaCoopBubble.hpp"
#include "Game/Player/NebulaCharacter.hpp"
#include "Game/System/NebulaCoopDemos.hpp"
#include "Game/System/NebulaCoopWorld.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include "Game/Util/RenderSignals.hpp"
#include "Game/System/NebulaHost.hpp"

namespace {
    CometRetryButton* getCometRetryButton() {
        return MR::getSceneObj< CometRetryButton >(SceneObj_CometRetryButton);
    }
};  // namespace

namespace NrvGameScene {
    NEW_NERVE(GameSceneScenarioOpeningCamera, GameScene, ScenarioOpeningCamera);
    NEW_NERVE(GameSceneScenarioStarter, GameScene, ScenarioStarter);
    NEW_NERVE(GameSceneAction, GameScene, SceneAction);
    NEW_NERVE(GameScenePauseMenu, GameScene, PauseMenu);
    NEW_NERVE(GameScenePowerStarGet, GameScene, PowerStarGet);
    NEW_NERVE(GameSceneGrandStarGet, GameScene, GrandStarGet);
    NEW_NERVE(GameSceneGameOver, GameScene, GameOver);
    NEW_NERVE(GameSceneCometRetryAfterMiss, GameScene, CometRetryAfterMiss);
    NEW_NERVE(GameSceneSaveAfterGameOver, GameScene, SaveAfterGameOver);
    NEW_NERVE(GameScenePlayMovie, GameScene, PlayMovie);
    NEW_NERVE(GameSceneTimeUp, GameScene, TimeUp);
    NEW_NERVE(GameSceneGalaxyMap, GameScene, GalaxyMap);
    NEW_NERVE(GameSceneStaffRoll, GameScene, StaffRoll);
    NEW_NERVE(GameSceneNebulaBoard, GameScene, NebulaBoard);
};  // namespace NrvGameScene

namespace {
#if !defined(NEBULA_RELEASE)
#include "../../../../../tools/test_driver/game_scene_walk.inc"
#endif
}  // namespace

GameScene::GameScene()
    : Scene("GameScene"), _14(0), mScenarioCamera(nullptr), mPauseCtrl(nullptr), mPauseSeq(nullptr), mStageClearSeq(nullptr), mDraw3D(true), _29(1) {
}

GameScene::~GameScene() {
    NebulaCharacter::endScene();
    NebulaCoopWorld::endScene();
    NebulaMods::fire(NEBULA_MOD_EVENT_STAGE_END, 0, nullptr, MR::getCurrentStageName());
    MR::destroySceneMessage();
    NPCFunction::deleteNPCData();
    MR::onStarPointerSceneOut();
}

void GameScene::init() {
    // The character this scene plays, before anything asks (the player's archives just below, MarioActor's model).
    NebulaCharacter::startScene();
    NebulaRemotePlayer::forgetAll();
    NebulaCoopBubble::forgetAll();
    NebulaCoopWorld::startScene();
    SceneFunction::createHioBasicNode(this);
    SceneFunction::startStageFileLoad();
    MR::requestChangeArchivePlayer(MR::isPlayerLuigi() == false);
    initNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));
    SceneFunction::initForNameObj();
    SceneFunction::initForLiveActor();
    initEffect();
    MR::createSceneObj(SceneObj_CameraContext);
    MR::createSceneObj(SceneObj_NameObjGroup);
    MR::createSceneObj(SceneObj_PlanetGravityManager);
    MR::createSceneObj(SceneObj_MarioHolder);
    MR::createSceneObj(SceneObj_AudCameraWatcher);
    MR::createSceneObj(SceneObj_AudEffectDirector);
    MR::createSceneObj(SceneObj_AudBgmConductor);
    MR::createSceneObj(SceneObj_ResourceShare);
    MR::createSceneObj(SceneObj_EventSequencer);
    MR::createSceneObj(SceneObj_ScenePlayingResult);
    MR::createSceneObj(SceneObj_FurDrawManager);
    MR::createSceneObj(SceneObj_PlacementStateChecker);
    MR::createScreenAlphaSceneObj(0, 1.0f);
    MR::createSceneObj(SceneObj_GroupCheckManager);
    MR::createSceneObj(SceneObj_CinemaFrame);
    MR::createSceneObj(SceneObj_PlanetMapCreator);

    if (MR::isStageEpilogueDemo()) {
        MR::createSceneObj(SceneObj_StaffRoll);
    }

    // A plugin can start a movie in any scene.
    if (NebulaMods::hasCode()) {
        MR::createSceneObj(SceneObj_MoviePlayerSimple);
    }

    if (MR::isStageEpilogueDemo() || GameSequenceFunction::isNeedMoviePlayerForStorySequenceEvent()) {
        MR::createMoviePlayingSequence();
    }

    SceneFunction::waitDoneStageFileLoad();
    MR::waitEndChangeArchivePlayer();
    SceneFunction::startActorFileLoadCommon();
    MR::waitSceneInitializeResume();

    if (!MR::isScenarioDecided()) {
        MR::receiveAllRequestedFile();
    } else {
        SceneFunction::initAfterScenarioSelected();
        NPCFunction::createNPCData();
        SceneFunction::startActorFileLoadScenario();
        MR::createSceneObj(SceneObj_EventDirector);
        MR::initSceneMessage();
        MR::createSceneObj(SceneObj_CameraDirector);
        MR::createSceneObj(SceneObj_GameSceneLayoutHolder);
        MR::createSceneObj(SceneObj_StarPieceDirector);
        MR::createSceneObj(SceneObj_SceneWipeHolder);
        MR::createSceneObj(SceneObj_NamePosHolder);
        MR::createSceneObj(SceneObj_InformationObserver);

        if (MR::isGalaxyAnyCometAppearInCurrentStage()) {
            MR::createSceneObj(SceneObj_CometRetryButton);
        }

        LightFunction::initLightData();
        initSequences();
        SceneFunction::startActorPlacement();
        // Nebula's Achievement Luma joins the Observatory's own actors.
        NebulaAchievementLuma::tryCreate();
        // Online co-op: copies of the other players, when in a room.
        NebulaRemotePlayer::tryCreate();
        if (NebulaRemotePlayer::getCopyNum() > 0) {
            NebulaCoopBubble::tryCreate();
        }
        GameSceneFunction::loadScenarioWaveData();
        MR::createStarPiece();
        MR::completeCameraParameters();
        MR::initStarPointerGameScene();
        MR::initEventSystemAfterPlacement();
        MR::endInitLiveActorSystemInfo();
        MR::setInitializeStateAfterPlacement();
        MR::callMethodAllSceneNameObj(&NameObj::initAfterPlacement);
        SleepControlFunc::initSyncSleepController();
        // Online co-op: the instance's record is asked for while the scene still loads.
        NebulaCoopWorld::endPlacement();

        while (!GameSceneFunction::isLoadDoneScenarioWaveData()) {
            JASWaveArc::waitLoadDone();
        }
    }
}

namespace {
    // The achievement board asked for by the Achievement Luma: it opens once the scene is in plain play, not paused,
    // in a demo or behind a wipe; a scene that starts drops the request.
    bool sBoardRequested = false;
    // Tests (GALAXY_TEST_EVENT_FLAGS=AchievementBoard): asked for in the first scene with a board, past the restart the
    // test's Grand Stars cause.
    bool sTestBoard = false;

    // Discord (__NebulaPresence): the galaxy and its star in the game's own words, and the Power Stars collected.
    void reportPresence() {
        const char* pStage = MR::getCurrentStageName();
        const s32 scenario = MR::getCurrentScenarioNo();
        const char16_t* pGalaxy = MR::getGalaxyNameOnCurrentLanguage(pStage);
        const char16_t* pStar = scenario > 0 ? MR::getScenarioNameOnCurrentLanguage(pStage, scenario) : nullptr;
        NebulaHost::presence(reinterpret_cast< const u16* >(pGalaxy), reinterpret_cast< const u16* >(pStar), MR::getPowerStarNum());
    }

    // Online co-op (__NebulaCoopPlayer): the local player each scene frame, the instance it plays in and what other
    // players' copies of it draw. Nothing while the scene has no player (its first frames).
    void reportCoopPlayer() {
        if (!MR::isExistSceneObj(SceneObj_MarioHolder)) {
            return;
        }
        const MarioActor* pActor = MR::getMarioHolder()->getMarioActor();
        if (pActor == nullptr || pActor->mMarioAnim == nullptr) {
            return;
        }
        NebulaCoopPlayer player = {};
        nebula_snprintf(player.stage, sizeof(player.stage), "%s", MR::getCurrentStageName());
        player.scenario = MR::getCurrentScenarioNo();
        const auto copy = [](f32* pOut, const TVec3f& rVec) {
            pOut[0] = rVec.x;
            pOut[1] = rVec.y;
            pOut[2] = rVec.z;
        };
        copy(player.position, pActor->mPosition);
        copy(player.rotation, pActor->mRotation);
        copy(player.gravity, pActor->getGravityVec());
        if (const TVec3f* pVelocity = MR::getPlayerVelocity()) {
            copy(player.velocity, *pVelocity);
        }
        const char* pAnimation = MR::getPlayerCurrentBckName();
        nebula_snprintf(player.animation, sizeof(player.animation), "%s", pAnimation != nullptr ? pAnimation : "");
        player.animation_frame = pActor->mMarioAnim->getFrame();
        if (XanimePlayer* pXanime = pActor->mMarioAnim->mXanimePlayer) {
            nebula_snprintf(player.group, sizeof(player.group), "%s", pXanime->getCurrentAnimationName());
            player.animation_rate = pXanime->getFrameCtrl() != nullptr ? pXanime->getRate() : 1.0f;
            for (u32 i = 0; i < 4; i++) {
                player.track_weights[i] = pXanime->mWeights[i];
            }
            // The tracks Mario's code put another bck on (standing on a slope: its wait, not the group's run), by
            // their motion resource's name. Looked up only while one is swapped, and a pointer match is cheap.
            const XanimeGroupInfo* pGroup = pXanime->mCurrentAnimation;
            const XanimeCore* pCore = pXanime->getCore();
            if (pGroup != nullptr && pCore->mTrackList != nullptr) {
                const u32 count = pGroup->mBckTableVariant < pCore->mTrackCount ? pGroup->mBckTableVariant : pCore->mTrackCount;
                for (u32 i = 0; i < count && i < 4; i++) {
                    const void* pBck = pCore->mTrackList[i]._0;
                    if (pBck == nullptr || pBck == pGroup->_20[i]) {
                        continue;
                    }
                    const char* pName = pXanime->mResourceTable->mResourceHolder->mMotionResTable->findResName(pBck);
                    nebula_snprintf(player.tracks[i], sizeof(player.tracks[i]), "%s", pName != nullptr ? pName : "");
                }
            }
        }
        // The model's turn: its base matrix's Z (front) and Y (up) columns, with the turn MarioActor::calcAndSetBaseMtx
        // gives his root joint on top (Mario::createCorrectionMtx: a quick turn round, or the matrix of what holds
        // him), which the others' copies have no joint correction of their own for.
        MtxPtr pBase = MR::getJ3DModel(const_cast< MarioActor* >(pActor))->getBaseTRMtx();
        Mtx turn;
        NebulaMtxCopy(pBase, turn);
        if (XanimePlayer* pXanime = pActor->mMarioAnim->mXanimePlayer) {
            if (MtxPtr pCorrection = pXanime->getCore()->getJointTransform(0)->_64) {
                NebulaMtxConcat(pBase, pCorrection, turn);
            }
        }
        for (u32 i = 0; i < 3; i++) {
            player.front[i] = turn[i][2];
            player.up[i] = turn[i][1];
        }
        player.health = pActor->mHealth;
        player.mode = pActor->mPlayerMode;
        player.flags = (MR::isPlayerLuigi() ? NEBULA_COOP_PLAYER_LUIGI : 0u) | (MR::isPlayerDead() ? NEBULA_COOP_PLAYER_DEAD : 0u);
        // What the others' games need for player collisions: the attacks, and when nothing touches this player.
        if (MR::isPlayerSwingAction()) {
            player.flags |= NEBULA_COOP_PLAYER_SPIN;
        }
        if (MR::isPlayerHipDropFalling()) {
            player.flags |= NEBULA_COOP_PLAYER_HIP_DROP;
        }
        if (MR::isPlayerHipDropLand()) {
            player.flags |= NEBULA_COOP_PLAYER_HIP_DROP_LAND;
        }
        if (NebulaRemotePlayer::isLocalIntangible()) {
            player.flags |= NEBULA_COOP_PLAYER_INTANGIBLE;
        }
        if (NebulaCoopBubble::isLocalBubbled()) {
            player.flags |= NEBULA_COOP_PLAYER_BUBBLED;
        }
        if (NebulaRemotePlayer::isLocalRacing()) {
            player.flags |= NEBULA_COOP_PLAYER_RACING;
        }
        player.area = NebulaCoopBubble::getArea();
        NebulaHost::coopPlayer(&player);
    }
};  // namespace

void GameScene::start() {
    NebulaMods::fire(NEBULA_MOD_EVENT_STAGE_START, MR::getCurrentScenarioNo(), nullptr, MR::getCurrentStageName());
    AudWrap::getSceneMgr()->startScene();
    sBoardRequested = false;
    reportPresence();
    NebulaAchievements::evaluate();

    if (MR::isGlobalTimerEnd()) {
        MR::forceCloseWipeCircle();
    } else if (!MR::hasRetryGalaxySequence()) {
        startStagePlayFirst();
    } else {
        startStagePlayRetry();
    }
}

void GameScene::update() {
    NebulaMods::StageFrame stageFrame;
    updateMenusLeftOpen();
    mPauseCtrl->updateNerve();
    updateNerve();
    NebulaLoadTrace::gameScene();
    reportCoopPlayer();
    if (NebulaHost::coopMenu(NEBULA_COOP_MENU_LEAVE) == 1) {
        // The room ended under this player: as the HOME menu's Reset, the game starts again, its file select the
        // co-op main menu.
        AudWrap::getSystem()->preProcessToReset();
        GameSystemFunction::requestResetGameSystem(true);
    }
    NebulaCoopWorld::update();
    // A character switched in play (Options' PLAYER page): the player's spot reloaded as the other one.
    NebulaCharacter::update(isNerve(GET_NERVE(GameScene, GameSceneAction)));
    // Online co-op: a Grand Star another player in this instance took takes this player along (sm64coopdx's Bowser 3
    // Grand Star): its get sequence, as the star itself starts it, once play allows.
    if (isNerve(GET_NERVE(GameScene, GameSceneAction)) && !MR::isDemoActive() && !MR::isPlayerDead()) {
        s32 pulledStar = 0;
        if (NebulaCoopDemos::updateInPlay(&pulledStar)) {
            MR::sendStageResultSequenceParam(pulledStar);
            MR::requestGrandStarGetDemo();
        }
    }
    NebulaRemotePlayer::updateAll();
    NebulaAchievements::update();
    if (sTestBoard && MR::isStageAstroLocation() && MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mAchievementBoard != nullptr) {
        sTestBoard = false;
        requestShowNebulaBoard();
    }
    if (sBoardRequested && isNerve(GET_NERVE(GameScene, GameSceneAction)) && MR::isWipeOpen() && !MR::isWipeActive() &&
        !MR::isDemoActive()) {
        sBoardRequested = false;
        setNerve(GET_NERVE(GameScene, GameSceneNebulaBoard));
    }
    // Nebula's achievement pop-up shows as soon as one is earned, in play, cutscenes, menus and a star's get sequence
    // alike (the settings menu and the HOME Menu hold the whole scene, the pop-up with it).
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mAchievementToast != nullptr) {
        MR::getGameSceneLayoutHolder()->mAchievementToast->update();
    }
    // Online co-op's chat, likewise everywhere in the scene (the host sends nothing outside a room's game).
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mCoopChat != nullptr) {
        MR::getGameSceneLayoutHolder()->mCoopChat->update();
    }
    // Its pop-ups, likewise.
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mCoopPopups != nullptr) {
        MR::getGameSceneLayoutHolder()->mCoopPopups->update();
    }
    // Online co-op's player list while Tab or the controller's Share is held, over whatever the scene is doing.
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mPlayerList != nullptr) {
        MR::getGameSceneLayoutHolder()->mPlayerList->update();
    }

    bool isTimeUp = MR::isGlobalTimerEnd() && !isNerve(GET_NERVE(GameScene, GameSceneTimeUp)) && MR::isGreaterEqualStep(this, 2);

    if (isTimeUp) {
        setNerve(GET_NERVE(GameScene, GameSceneTimeUp));
    }
}

void GameScene::draw() const {
    // DLSS Frame Generation while the scene shows play: its own play, its openings and star demos, and no menu,
    // map, staff roll, movie or settings over it, nor the title and file select.
    NebulaFrameGen::report(!MR::isStageFileSelect() && !MR::isActiveMoviePlayer() && !NebulaSettingsMenu::isShown() &&
                           (isNerve(GET_NERVE(GameScene, GameSceneAction)) ||
                            isNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera)) ||
                            isNerve(GET_NERVE(GameScene, GameSceneScenarioStarter)) ||
                            isNerve(GET_NERVE(GameScene, GameScenePowerStarGet)) ||
                            isNerve(GET_NERVE(GameScene, GameSceneGrandStarGet))));
    // Where the camera is in water, how deep: ray tracing lights the water it looks through.
    {
        const f32 depth = MR::isCameraInWater() ? WaterAreaFunction::getCameraWaterDepth() : -1.0f;
        NebulaHost::gameContext(NEBULA_CONTEXT_CAMERA_WATER, depth < 0.0f ? 0 : static_cast< s32 >(depth) + 1);
    }
#if !defined(NEBULA_RELEASE)
#include "../../../../../tools/test_driver/game_scene_draw_start.inc"
#endif
    // The settings' galaxy select (its ONLINE page's cheats) names its galaxies and stars in the game's own words, asked once.
    if (NebulaHost::galaxySelectNamesWanted() != 0) {
        char id[256];
        u32 stars = 0;
        for (u32 i = 0; NebulaHost::galaxySelectStage(i, id, sizeof(id), &stars) != 0; ++i) {
            NebulaHost::galaxySelectName(i, 0, reinterpret_cast< const u16* >(MR::getGalaxyNameOnCurrentLanguage(id)));
            for (u32 star = 1; star <= stars; ++star) {
                NebulaHost::galaxySelectName(i, star,
                                         reinterpret_cast< const u16* >(MR::getScenarioNameOnCurrentLanguage(id, star)));
            }
        }
    }
#if !defined(NEBULA_RELEASE)
#include "../../../../../tools/test_driver/game_scene_actions.inc"
#endif
    MR::drawInit();
    LightFunction::initLightRegisterAll();
    CategoryList::execute(MR::DrawType_MiiFaceIcon);
    CategoryList::execute(MR::DrawType_MiiFaceNew);
    drawMirror();
    draw3D();
    draw2D();
    MR::reinitGX();
}

void GameScene::calcAnim() {
    if (isPlayMovie()) {
        SceneFunction::executeCalcAnimListOnPlayingMovie();
    } else {
        SceneFunction::executeCalcAnimList();
    }

    if (!isNerve(GET_NERVE(GameScene, GameSceneTimeUp))) {
        CategoryList::execute(MR::CalcAnimType_AnimParticleIgnorePause);
    }

    SceneFunction::executeCalcViewAndEntryList();
}

void GameScene::notifyEndScenarioStarter() {
    setNerve(GET_NERVE(GameScene, GameSceneAction));
}

void GameScene::requestPlayMovieDemo() {
    setNerve(GET_NERVE(GameScene, GameScenePlayMovie));
}

void GameScene::requestStartGameOverDemo() {
    NebulaCoopBubble::noticeGameOverDemo();
    if (!isNerve(GET_NERVE(GameScene, GameSceneGameOver))) {
        setNerve(GET_NERVE(GameScene, GameSceneGameOver));
    }
}

void GameScene::requestEndGameOverDemo() {
    setNerve(GET_NERVE(GameScene, GameSceneSaveAfterGameOver));
}

void GameScene::requestEndMissDemo() {
    if (MR::isExistSceneObj(SceneObj_CometRetryButton)) {
        setNerve(GET_NERVE(GameScene, GameSceneCometRetryAfterMiss));
    } else {
        MR::requestChangeStageAfterMiss();
    }
}

void GameScene::requestPowerStarGetDemo() {
    setNerve(GET_NERVE(GameScene, GameScenePowerStarGet));
}

void GameScene::requestGrandStarGetDemo() {
    NebulaAchievements::onGrandStarGet();
    setNerve(GET_NERVE(GameScene, GameSceneGrandStarGet));
}

void GameScene::setNerveAfterPauseMenu() {
    // The world may have gone on to a miss or a demo under the menu.
    if (!isNerve(GET_NERVE(GameScene, GameScenePauseMenu))) {
        return;
    }
    setNerve(GET_NERVE(GameScene, GameSceneAction));
}

bool GameScene::isExecScenarioOpeningCamera() const {
    return isNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));
}

bool GameScene::isExecScenarioStarter() const {
    return isNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
}

bool GameScene::isExecStageClearDemo() const {
    return isNerve(GET_NERVE(GameScene, GameScenePowerStarGet)) || isNerve(GET_NERVE(GameScene, GameSceneGrandStarGet));
}

void GameScene::exeScenarioOpeningCamera() {
    mScenarioCamera->update();
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();

    if (mScenarioCamera->isDone()) {
        if (!MR::isBeginScenarioStarter()) {
            MR::activateDefaultGameLayout();
            MR::tryFrameToScreenCinemaFrame();
            MR::stopSubBGM(0);
            MR::stopStageBGM(0);
            MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
            setNerve(GET_NERVE(GameScene, GameSceneAction));
        } else {
            setNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
        }
    }
}

void GameScene::exeCometRetryAfterMiss() {
    CometRetryButton* pCometRetryButton = ::getCometRetryButton();

    if (MR::isFirstStep(this)) {
        ::getCometRetryButton()->appear();
        MR::forceOpenWipeCircle();
    }

    pCometRetryButton->movement();
}

void GameScene::exeSaveAfterGameOver() {
    // Online co-op: a game over doesn't end the game (back at the main menu the player would leave the room, and a
    // host would close it for everyone). As sm64coopdx's game over (four lives, back to the castle), the player gets
    // four lives and goes back to the Observatory, or plays the stage again where there is none to go back to. A
    // joiner plays the host's save, which the host's game writes: theirs isn't written (as their Quit doesn't).
    const bool inRoom = NebulaCoopWorld::isInRoom();
    if (MR::isFirstStep(this)) {
        MR::incPlayerGameOverNum();
        if (!inRoom || NebulaHost::coopMenu(NEBULA_COOP_MENU_JOINED) != 1) {
            GameSequenceFunction::startGameDataSaveSequence(true, true);
        }
        MR::startStarPointerModePauseMenu(this);
    }

    if (!GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        MR::endStarPointerMode(this);
        if (!inRoom) {
            MR::requestChangeSceneAfterGameOver();
        } else {
            GameDataFunction::addPlayerLeft(4 - GameDataFunction::getPlayerLeft());
            if (MR::isOnGameEventFlagUseAstroDome() && !MR::isStageAstroLocation()) {
                MR::requestChangeStageGoBackAstroDome();
            } else {
                MR::requestChangeStageAfterMiss();
            }
        }
    }
}

void GameScene::exePlayMovie() {
    if (!MR::isActiveMoviePlayer() && !MR::isMoviePlayingOnSequence()) {
        setNerve(GET_NERVE(GameScene, GameSceneAction));
        SceneFunction::movementStopSceneController();
        SceneFunction::executeMovementList();
    } else {
        SceneFunction::executeMovementListOnPlayingMovie();
    }
}

void GameScene::exeGalaxyMap() {
    if (MR::isFirstStep(this)) {
        MR::startStarPointerModePauseMenu(this);
    }

    if (!MR::isActiveGalaxyMapLayout()) {
        MR::endStarPointerMode(this);
        setNerve(GET_NERVE(GameScene, GameSceneAction));
    } else {
        CategoryList::execute(MR::MovementType_LayoutOnPause);
    }
}

void GameScene::initSequences() {
    GameStageClearSequence* pStageClearSeq = new GameStageClearSequence();
    pStageClearSeq->initWithoutIter();
    mStageClearSeq = pStageClearSeq;

    GamePauseSequence* pPauseSeq = new GamePauseSequence();
    pPauseSeq->initWithoutIter();
    mPauseSeq = pPauseSeq;

    mScenarioCamera = new GameSceneScenarioOpeningCameraState();
    mPauseCtrl = new GameScenePauseControl(this);
    mPauseCtrl->registerNervePauseMenu(GET_NERVE(GameScene, GameScenePauseMenu));

    mPauseSeq->initWindowMenu(MR::Functor(mPauseCtrl, &GameScenePauseControl::requestPauseMenuOff));
}

void GameScene::initEffect() {
    // The pools grow past this when a scene asks for more.
    SceneFunction::initEffectSystem(6144, 512);
}

void GameScene::drawMirror() const {
    if (isDrawMirror()) {
        MotionSignal::mirror(true);
        NebulaMtxCopy(MR::getMirrorCameraViewMtx(), j3dSys.mViewMtx);
        MR::loadProjectionMtx();
        CategoryList::entryDrawBufferMirror();
        J3DPass::writeAlpha(false);
        J3DPass::writeColor(true);
        CategoryList::drawOpa(MR::DrawBufferType_MirrorMapObj);
        CategoryList::drawXlu(MR::DrawBufferType_MirrorMapObj);
        CategoryList::execute(MR::DrawType_CaptureScreenIndirect);
        MotionSignal::mirror(false);
        MR::clearZBuffer();
        NebulaColor8 c = {0, 0, 255, 0};
        MR::fillScreen(c);
    }
}

// inline
bool GameScene::isPlayMovie() const {
    return MR::isActiveMoviePlayer() || MR::isMoviePlayingOnSequence() || isNerve(GET_NERVE(GameScene, GameScenePlayMovie));
}

void GameScene::draw3D() const {
    if (!mDraw3D) {
        return;
    }

    if (isPlayMovie()) {
        return;
    }

    f32 sceneCamera[4][4];
    MotionSignal::sceneCamera(sceneCamera);
    NebulaHost::gameStage(MR::getCurrentStageName());
    NebulaRender::sceneMarker(NEBULA_SCENE_3D_BEGIN, sceneCamera);
    MotionSignal::sceneBegin();
    GameLightSignal::frame();
    CategoryList::execute(MR::DrawType_AstroDomeSkyClear);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    MR::setDefaultViewportAndScissor();
    J3DPass::writeColor(true);
    J3DPass::writeAlpha(true);
    J3DPass::constantAlpha(true, 0);
    CategoryList::drawOpa(MR::DrawBufferType_ClippedMapParts);
    CategoryList::execute(MR::DrawType_ClipArea);
    CategoryList::execute(MR::DrawType_FallOutFieldDraw);
    SceneFunction::executeDrawBufferListNormalOpaBeforeVolumeShadow();
    J3DPass::writeAlpha(true);
    CategoryList::execute(MR::DrawType_ShadowVolume);
    J3DPass::writeColor(true);
    J3DPass::constantAlpha(true, 0);
    SceneFunction::executeDrawBufferListNormalOpaBeforeSilhouette();
    CategoryList::execute(MR::DrawType_0x28);
    SceneFunction::executeDrawSilhouetteAndFillShadow();
    SceneFunction::executeDrawAlphaShadow();
    CategoryList::execute(MR::DrawType_BrightSun);
    MR::setLensFlareDrawSyncToken();
    J3DPass::writeAlpha(false);
    J3DPass::constantAlpha(false, 0);
    SceneFunction::executeDrawBufferListNormalOpa();
    SceneFunction::executeDrawListOpa();
    CategoryList::drawOpa(MR::DrawBufferType_UNK_0x18);
    MR::sceneOpaqueEnd();
    SceneFunction::executeDrawBufferListNormalXlu();
    SceneFunction::executeDrawListXlu();
    CategoryList::drawXlu(MR::DrawBufferType_UNK_0x18);
    CategoryList::execute(MR::DrawType_ShadowSurface);
    CategoryList::execute(MR::DrawType_EffectDraw3D);
    CategoryList::execute(MR::DrawType_EffectDrawForBloomEffect);
    CategoryList::execute(MR::DrawType_CenterScreenBlur);
    CategoryList::execute(MR::DrawType_CaptureScreenIndirect);
    SceneFunction::executeDrawAfterIndirect();
    CategoryList::execute(MR::DrawType_0x33);
    MR::setStarPointerDrawSyncToken();
    MR::setTalkDirectorDrawSyncToken();
    SceneFunction::executeDrawImageEffect();
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    CategoryList::execute(MR::DrawType_EffectDrawAfterImageEffect);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    MR::loadLightPlayer();
    CategoryList::execute(MR::DrawType_0x24);
    CategoryList::execute(MR::DrawType_0x3B);
    NebulaRender::sceneMarker(NEBULA_SCENE_3D_END, nullptr);
    MR::clearZBuffer();
}

void GameScene::draw2D() const {
    MotionSignal::scene2D(true);
    if (isPlayMovie()) {
        SceneFunction::executeDrawList2DMovie();
    } else {
        SceneFunction::executeDrawList2DNormal();
    }

    CategoryList::execute(MR::DrawType_CameraCover);
    CategoryList::execute(MR::DrawType_LayoutOnPause);
    mPauseSeq->draw();
    MR::drawInitFor2DModel();
    CategoryList::drawOpa(MR::DrawBufferType_0x26);
    CategoryList::drawXlu(MR::DrawBufferType_0x26);
    CategoryList::execute(MR::DrawType_WipeLayout);
    // Nebula's achievement board over everything while it is open (its pop-up: drawOverlay).
    if (isNerve(GET_NERVE(GameScene, GameSceneNebulaBoard))) {
        MR::getGameSceneLayoutHolder()->mAchievementBoard->drawOnTop();
    }
    MotionSignal::scene2D(false);
}

void GameScene::drawOverlay() const {
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mAchievementToast != nullptr) {
        MR::getGameSceneLayoutHolder()->mAchievementToast->drawOnTop();
    }
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mCoopChat != nullptr) {
        MR::getGameSceneLayoutHolder()->mCoopChat->drawOnTop();
    }
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mCoopPopups != nullptr) {
        MR::getGameSceneLayoutHolder()->mCoopPopups->drawOnTop();
    }
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mPlayerList != nullptr) {
        MR::getGameSceneLayoutHolder()->mPlayerList->drawOnTop();
    }
}

bool GameScene::isValidScenarioOpeningCamera() const {
    return MR::hasStartAnimCamera() != false;
}

void GameScene::startStagePlayFirst() {
    if (isValidScenarioOpeningCamera()) {
        setNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));

        if (!MR::isBeginScenarioStarter()) {
            MR::stopSubBGM(0);
            MR::stopStageBGM(0);
            MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
        }
    } else if (MR::isBeginScenarioStarter()) {
        MR::forceToBlankCinemaFrame();
        MR::forceOpenWipeFade();
        setNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
    } else {
        if (MR::isStageBeginFadeWipe()) {
            MR::openWipeFade(180);
        } else if (MR::isStageBeginTitleWipe()) {
            MR::openWipeFade(30);
        } else if (MR::isStageBeginWithoutWipe()) {
            MR::forceOpenWipeFade();
        } else {
            MR::openWipeCircle();
        }

        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
        MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
        MR::executeOnWelcomeAndRetry();
        setNerve(GET_NERVE(GameScene, GameSceneAction));
    }
}

void GameScene::startStagePlayRetry() {
    MR::openWipeCircle();

    if (MR::isEqualStageName("SurfingLv1Galaxy") && MR::getPlayerRestartIdInfo()->_0 == 1) {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
    } else if (MR::isEqualStageName("SurfingLv2Galaxy") && MR::getPlayerRestartIdInfo()->_0 == 1) {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
    } else {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
        MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
    }

    MR::executeOnWelcomeAndRetry();
    setNerve(GET_NERVE(GameScene, GameSceneAction));
}

bool GameScene::isPermitToPauseMenu() const {
    return !MR::isStageDisablePauseMenu() && isNerve(GET_NERVE(GameScene, GameSceneAction)) && !MR::isDemoActive() && !MR::isWipeActive() &&
           !MR::isWipeBlank() && !MR::isPlayerDead() && !MR::isPlayerDamaging();
}

void GameScene::requestShowGalaxyMap() {
    setNerve(GET_NERVE(GameScene, GameSceneGalaxyMap));
}

void GameScene::requestShowNebulaBoard() {
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mAchievementBoard != nullptr) {
        sBoardRequested = true;
    }
}

// The pointer in its menu mode, the board's own frame each frame, and the world on under it.
void GameScene::exeNebulaBoard() {
    NebulaAchievementBoard* pBoard = MR::getGameSceneLayoutHolder()->mAchievementBoard;
    if (MR::isFirstStep(this)) {
        MR::startStarPointerModePauseMenu(this);
        pBoard->open();
    }
    pBoard->update();
    if (!pBoard->isOpen()) {
        MR::endStarPointerMode(this);
        setNerve(GET_NERVE(GameScene, GameSceneAction));
    } else {
        CategoryList::execute(MR::MovementType_LayoutOnPause);
        updateWorldUnderMenu();
    }
}

void GameScene::updateWorldUnderMenu() {
    MR::holdPlayerInputsForMenu();
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::updateMenusLeftOpen() {
    if (mPauseSeq != nullptr && mPauseSeq->isActive() && !isNerve(GET_NERVE(GameScene, GameScenePauseMenu)) &&
        !mPauseSeq->closeForWorld()) {
        mPauseSeq->movement();
        CategoryList::execute(MR::MovementType_LayoutOnPause);
        MR::holdPlayerInputsForMenu();
    }
    NebulaAchievementBoard* pBoard =
        MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) ? MR::getGameSceneLayoutHolder()->mAchievementBoard : nullptr;
    if (pBoard != nullptr && pBoard->isOpen() && !isNerve(GET_NERVE(GameScene, GameSceneNebulaBoard))) {
        pBoard->update();
        MR::holdPlayerInputsForMenu();
        if (!pBoard->isOpen()) {
            MR::endStarPointerMode(this);
        }
    }
}

void GameScene::requestStaffRoll() {
    setNerve(GET_NERVE(GameScene, GameSceneStaffRoll));
}

bool GameScene::isDrawMirror() const {
    if (!MR::isExistMirrorCamera()) {
        return false;
    }

    if (isPlayMovie()) {
        return false;
    }

    return MR::isPlayerInAreaObj("MirrorArea");
}

void GameScene::stageClear() {
    if (MR::isFirstStep(this)) {
        // The star just collected (PowerStar sent it with the get sequence's request) unlocks what it completes now,
        // while its get sequence plays, not when the hub records it.
        NebulaAchievements::evaluate();
        NebulaAchievements::onStarGet();
        // Online co-op: the others in the instance hear of it.
        if (GameSequenceFunction::hasStageResultSequence() &&
            MR::isEqualString(GameSequenceFunction::getClearedStageName(), MR::getCurrentStageName())) {
            NebulaCoopDemos::noticeStarGot(GameSequenceFunction::getClearedPowerStarId(),
                                           isNerve(GET_NERVE(GameScene, GameSceneGrandStarGet)));
        }
        if (isNerve(GET_NERVE(GameScene, GameScenePowerStarGet))) {
            mStageClearSeq->startPowerStarGetDemo();
        } else if (isNerve(GET_NERVE(GameScene, GameSceneGrandStarGet))) {
            mStageClearSeq->startGrandStarGetDemo();
        }
    }

    mStageClearSeq->movement();
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::exeStaffRoll() {
    MR::getSceneNameObjMovementController()->movement();
    CategoryList::execute(MR::MovementType_Layout);
    CategoryList::execute(MR::DrawType_LayoutDecoration);
    CategoryList::execute(MR::MovementType_WipeLayout);
}

void GameScene::exeTimeUp() {
    if (MR::isFirstStep(this)) {
        MR::startGlobalTimerTimeUp();
    }
}

void GameScene::exeGameOver() {
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::exeGrandStarGet() {
    stageClear();
}

void GameScene::exePowerStarGet() {
    stageClear();
}

void GameScene::exePauseMenu() {
    if (MR::isFirstStep(this)) {
    }

    mPauseSeq->movement();
    CategoryList::execute(MR::MovementType_LayoutOnPause);
    // Nebula's world plays on under the pause menu (docs/COOP_PLAN.md); a miss or a demo takes the screen back.
    if ((MR::isPlayerDead() || MR::isDemoActive()) && mPauseSeq->closeForWorld()) {
        setNerve(GET_NERVE(GameScene, GameSceneAction));
    }
    updateWorldUnderMenu();
}

void GameScene::exeSceneAction() {
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::exeScenarioStarter() {
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}
