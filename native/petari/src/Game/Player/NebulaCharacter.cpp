#include "Game/Player/NebulaCharacter.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Player/NebulaCharacters.hpp"
#include "Game/Player/NebulaCoopBubble.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <nebula_game_api.h>
#include "nebula_module.h"
#include <cstdio>

namespace {
    enum State {
        State_Idle,       // playing the scene's character
        State_Closing,    // the screen fading out for a switch
        State_Reloading,  // the stage asked to start again
    };

    // The screen's fade out (and back in when a switch is called off), in frames.
    const s32 cFadeFrames = 20;
    // How long the started-again stage may take to let the player stand at their spot, in frames, before that's given
    // up (a demo that keeps the player).
    const s32 cRestoreWait = 600;

    bool sInScene = false;
    s32 sSceneCharacter = NebulaCharacters::cMario;
    // A mod's pick (set_player_character_id), -1 for none.
    s32 sOverride = -1;
    State sState = State_Idle;
    bool sSwitchReload = false;

    // The player's spot as the switch closed the screen, for the stage started again (sPending until it starts).
    bool sPending = false;
    bool sRestore = false;
    s32 sRestoreFrames = 0;
    char sStage[48];
    s32 sScenario = 0;
    Vec3f sPos(0.0f, 0.0f, 0.0f);
    Vec3f sFront(0.0f, 0.0f, 1.0f);
    u32 sHealth = 0;
    u32 sMaxHealth = 0;

    // The Options page's character: by its id, as the host keeps it; its base (NEBULA_COOP_MENU_CHARACTER) when the
    // id names no character here (a mod not running).
    s32 getWantedCharacter() {
        if (sOverride >= 0) {
            return sOverride;
        }
        const bool luigi = NebulaHost::coopMenu(NEBULA_COOP_MENU_CHARACTER) == 1;
        char wanted[NebulaCharacters::cIdSize];
        NebulaHost::coopCharacterWanted(wanted, sizeof(wanted));
        const s32 found = NebulaCharacters::find(wanted);
        if (found >= 0 && NebulaCharacters::isLuigi(found) == luigi) {
            return found;
        }
        return luigi ? NebulaCharacters::cLuigi : NebulaCharacters::cMario;
    }

    MarioActor* getLocalMario() {
        if (!MR::isExistSceneObj(SceneObj_MarioHolder)) {
            return nullptr;
        }
        return MR::getMarioHolder()->getMarioActor();
    }

    // A stage a switch may start again: not the main menu's, the prologue's or the ending's, nor a comet's (its timer,
    // or its one health, would start again with it).
    bool isStageSwitchable() {
        return !MR::isStageFileSelect() && !MR::isStageEpilogueDemo() && !MR::isEqualStageName("PeachCastleGardenGalaxy") &&
               !MR::isGalaxyAnyCometAppearInCurrentStage();
    }

    // The player is theirs to move: alive, out of any demo, launch or bubble.
    bool isPlayerFree(const MarioActor* pActor) {
        return pActor != nullptr && !MR::isPlayerDead() && !MR::isDemoActive() && !MR::isPlayerInRush() &&
               !MR::isOffPlayerControl() && !NebulaCoopBubble::isLocalBubbled();
    }

    void keepSpot(MarioActor* pActor) {
        nebula_snprintf(sStage, sizeof(sStage), "%s", MR::getCurrentStageName());
        sScenario = MR::getCurrentScenarioNo();
        sPos = pActor->mPosition;
        sFront = pActor->getMario()->mFrontVec;
        sHealth = pActor->mHealth;
        sMaxHealth = pActor->mMaxHealth;
    }

    // As the bubble lets a player out (NebulaCoopBubble's popLocal): placed there, standing, the camera behind.
    void restoreSpot(MarioActor* pActor) {
        pActor->mVelocity.zero();
        MR::setPlayerPos(sPos);
        pActor->resetCondition();
        MR::setPlayerFrontVec(sFront, 0);
        // A Life Mushroom's health stays; never more than the character has room for, never none.
        if (sMaxHealth > pActor->mMaxHealth) {
            pActor->mMaxHealth = sMaxHealth;
        }
        pActor->mHealth = sHealth > pActor->mMaxHealth ? pActor->mMaxHealth : sHealth;
        if (pActor->mHealth == 0) {
            pActor->mHealth = 1;
        }
        MR::resetCameraMan();
        nebula_log("[character] back at the spot as %s\n", NebulaCharacters::id(sSceneCharacter));
    }
};  // namespace

namespace NebulaCharacter {
    void startScene() {
        // The main menu's stage plays no one: the character is still being picked there (Host and Join take it).
        sInScene = !MR::isStageFileSelect();
        sSceneCharacter = getWantedCharacter();
        sState = State_Idle;
        sSwitchReload = false;
        sRestore = sPending;
        sPending = false;
        sRestoreFrames = 0;
    }

    void endScene() {
        sInScene = false;
    }

    s32 index() {
        return sInScene ? sSceneCharacter : getWantedCharacter();
    }

    bool isLuigi() {
        return NebulaCharacters::isLuigi(index());
    }

    s32 wanted() {
        return getWantedCharacter();
    }

    bool setOverride(s32 character) {
        if (character >= 0 && NebulaCharacters::at(character) == nullptr) {
            return false;
        }
        sOverride = character < 0 ? -1 : character;
        return true;
    }

    s32 getOverride() {
        return sOverride;
    }

    bool isSwitchReload() {
        return sSwitchReload;
    }

    void update(bool inPlay) {
        if (!sInScene || sState == State_Reloading) {
            return;
        }
        MarioActor* pActor = getLocalMario();

        // The stage a switch started again: the player back at their spot, once they may be moved.
        if (sRestore) {
            if (!MR::isEqualStageName(sStage) || MR::getCurrentScenarioNo() != sScenario || ++sRestoreFrames > cRestoreWait) {
                sRestore = false;
            } else if (inPlay && isPlayerFree(pActor)) {
                sRestore = false;
                restoreSpot(pActor);
            }
            return;
        }

        if (sState == State_Idle) {
            if (!inPlay || getWantedCharacter() == sSceneCharacter || !isStageSwitchable() || !isPlayerFree(pActor) || !MR::isWipeOpen() ||
                MR::isWipeActive()) {
                return;
            }
            MR::closeWipeFade(cFadeFrames);
            sState = State_Closing;
            return;
        }

        // Closing: called off when anything else takes the player (a miss, a demo, a menu), the screen opened again.
        if (!inPlay || !isPlayerFree(pActor)) {
            sState = State_Idle;
            if (!MR::isPlayerDead()) {
                MR::openWipeFade(cFadeFrames);
            }
            return;
        }
        if (!MR::isWipeBlank()) {
            return;
        }
        keepSpot(pActor);
        sPending = true;
        sSwitchReload = true;
        sState = State_Reloading;
        nebula_log("[character] switching to %s: %s %d starts again\n", NebulaCharacters::id(getWantedCharacter()), sStage, sScenario);
        if (MR::isStageAstroLocation()) {
            // The observatory has no retry: entered again as from its other domes, at the start this visit began at.
            // The restart point may still be the last galaxy's, a start the observatory does not have.
            MR::requestChangeStageInGameMoving(sStage, sScenario, MR::getCurrentMarioStartIdInfo());
        } else {
            MR::requestChangeStageAfterMiss();
        }
    }
};  // namespace NebulaCharacter
