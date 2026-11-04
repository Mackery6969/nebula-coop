#include "Game/System/NebulaCoopWorld.hpp"
#include "nebula_module.h"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaCoopDemos.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/HitSensorInfo.hpp"
#include "Game/LiveActor/HitSensorKeeper.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/Coin.hpp"
#include "Game/MapObj/CoinGroup.hpp"
#include "Game/MapObj/StarPiece.hpp"
#include "Game/MapObj/StarPieceGroup.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataGalaxyStorage.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/System/GameEventValueChecker.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Player/NebulaRemotePlayer.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <math.h>
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>

namespace {
    enum Kind {
        Kind_Coin,
        Kind_PurpleCoin,
        Kind_StarBit,
        Kind_StarBitDrop,  // a star bit a followed thing let out (a crystal broken, an enemy defeated): shared once taken
        Kind_Item,
        Kind_Enemy,   // a plain enemy: gone once defeated, moved by its owner's game, hits shared
        Kind_Object,  // any other placed actor (a boss, a box, a switch, a platform): hits shared
        Kind_Part,    // an actor a placed one made as it was placed (a boss's part, a group's member): hits shared
    };

    struct Entry {
        LiveActor* mActor;  // nullptr once it stands for nothing (a pooled coin let out again for another)
        u32 mKey;
        u8 mKind;
        bool mAlive;  // as the last frame saw it
        bool mGone;   // reported, or the server said so: kept dead
        bool mGot;    // a coin or star bit the local player took
        bool mHitByOther;  // its last hit was another player's, played again here
        s16 mMotion;     // its slot in sMotions, -1 for none
        s16 mGoneDelay;  // frames before the server's gone removes it: its own death (a hit played again) may still play
        s16 mHitFrames;  // frames since a hit last played on it, up to cHitFrames
        u16 mDrops;      // coins it let out so far (each one's key)
        u16 mBits;       // star bits it let out so far (each one's key)
        u16 mLastHit;    // the last hit noted on it, and when (a hit isn't sent twice in a few frames)
        s32 mLastHitFrame;
    };

    // Things a scene follows at most, and the tables finding them by key and by actor (powers of two, twice as many).
    const s32 cMaxEntries = 8192;
    const u32 cTableSize = 16384;
    // A scene's first frames: things the stage's own setup kills then aren't reported (a coin hidden for a comet, an
    // enemy waiting for its switch), only noted.
    const s32 cSettleFrames = 30;
    // What one frame takes from the host at most.
    const u32 cTakePerCall = 64;
    const u32 cTakeCalls = 16;

    Entry sEntries[cMaxEntries];
    s32 sEntryNum = 0;
    s16 sTable[cTableSize];  // an entry's index + 1, 0 for none
    // Actors to their entries (the latest one for an actor).
    struct ActorSlot {
        const void* mActor;
        s16 mEntry;
    };
    ActorSlot sActors[cTableSize];
    bool sInRoom = false;
    bool sPlaced = false;
    bool sMissed = false;    // the local player missed: told once this scene
    bool sReady = false;     // the instance's record has all come
    bool sApplying = false;  // applying what the server said: not reported back
    s32 sFrames = 0;
    s32 sClock = 0;  // frames since the scene began

    // A placed object being made, and the actors it has made so far (its parts, keyed in that order).
    bool sPlacing = false;
    LiveActor* sMade[256];
    s32 sMadeNum = 0;

    // Hits: the messages that are the local player's own attacks (Mario's stomps, spins, ground pounds and kicks), and
    // how long one stays "just hit" (its death may still play out) or can't be sent again.
    const s32 cHitFrames = 180;
    const s32 cHitRepeatFrames = 12;
    LiveActor* sDispatch = nullptr;  // the actor a hit is being delivered to (its nerve may change for it)
    bool sReplaying = false;

    // Movement (sm64coopdx's sync objects): a slot for each enemy that moves.
    struct Motion {
        s32 mEntry;
        u32 mOwner;          // the player who moves it (their number), 0 for none known
        s32 mHeard;          // frames since its owner's last state, or since this game took it over
        f32 mOwnerDistance;  // how far the owner's player was from it
        NebulaCoopObjectState mState;  // the owner's last state
        s32 mAge;                      // frames since it came
        f32 mRotation[4];              // the turn drawn, eased toward the owner's
        bool mRotationSet;
        bool mLocked;        // following its owner now: its own AI doesn't change its nerve
        bool mHitClaim;      // the local player just hit it: taken over, whoever was nearer
        const Nerve* mHitNerve;  // the nerve a hit began (the others get it by playing the hit again)
        u32 mClasses[4];     // the nerve classes its own code has set (a nerve from another game must be one)
        u8 mClassNum;
        s32 mSendIn;         // frames to its next state, as its owner
        u32 mSentNerve;
        TVec3f mSentPosition;
    };
    const s32 cMaxMotions = 512;
    const u32 cMotionTableSize = 1024;
    Motion sMotions[cMaxMotions];
    s32 sMotionNum = 0;
    ActorSlot sMotionTable[cMotionTableSize];
    // An owner silent this long has let it go; a player this far from it doesn't take it over; a quarter nearer than
    // its owner does. States go out 15 times a second while it moves, 3 when it doesn't.
    const s32 cSilentFrames = 30;
    const f32 cSyncDistance = 6000.0f;
    const f32 cNearerRatio = 1.25f;
    const s32 cSendMoving = 4;
    const s32 cSendStill = 20;
    const s32 cClaimEvery = 15;
    // A follower further than this from where its owner has it jumps there; nearer, it eases there.
    const f32 cSnapDistance = 600.0f;
    const f32 cEase = 0.25f;
    u32 sSelf = 0;  // the local player's number
    MarioActor* sMario = nullptr;
    bool sSyncNerve = false;  // setting a nerve from another game's state

    // Switches: turned this frame (sent at the frame's end), the room's as last told (set again once the scene's own
    // setup is done), and switches the local player alone turns (an area he's in).
    struct SwitchSlot {
        u32 mId;
        bool mOn;
    };
    const s32 cMaxSwitchesTurned = 64;
    SwitchSlot sTurned[cMaxSwitchesTurned];
    s32 sTurnedNum = 0;
    const s32 cMaxRoomSwitches = 512;
    SwitchSlot sRoomSwitches[cMaxRoomSwitches];
    s32 sRoomSwitchNum = 0;
    s32 sLocalSwitches = 0;
    // A switch the game turns back and forth by itself (a timer's, one following where the local player is) is each
    // player's: once it turns cMaxFlips times in cFlipFrames, it isn't sent any more this scene.
    struct Flip {
        u32 mId;
        s32 mSince;
        u8 mCount;
        bool mLocal;
    };
    const s32 cMaxFlipped = 128;
    const s32 cFlipFrames = 120;
    const u8 cMaxFlips = 4;
    Flip sFlips[cMaxFlipped];
    s32 sFlipNum = 0;

    // Whether a switch the game turned is the instance's (not one it keeps turning by itself).
    bool isSharedSwitch(u32 id) {
        Flip* pFlip = nullptr;
        for (s32 i = 0; i < sFlipNum; i++) {
            if (sFlips[i].mId == id) {
                pFlip = &sFlips[i];
                break;
            }
        }
        if (pFlip == nullptr) {
            if (sFlipNum >= cMaxFlipped) {
                return true;
            }
            pFlip = &sFlips[sFlipNum++];
            pFlip->mId = id;
            pFlip->mSince = sClock;
            pFlip->mCount = 0;
            pFlip->mLocal = false;
        }
        if (pFlip->mLocal) {
            return false;
        }
        if (sClock - pFlip->mSince > cFlipFrames) {
            pFlip->mSince = sClock;
            pFlip->mCount = 0;
        }
        if (++pFlip->mCount > cMaxFlips) {
            nebula_log("[coop] switch %08x turns by itself: each player's own\n", id);
            pFlip->mLocal = true;
            return false;
        }
        return true;
    }

    // Things the server said were gone before this game had them (a coin an enemy drops here later).
    const s32 cMaxPending = 64;
    struct Pending {
        u32 mKey;
        u32 mHow;
    };
    Pending sPending[cMaxPending];
    s32 sPendingNext = 0;

    // The plain enemies whose defeat is the instance's (placed names). Not here: launchers and generators (what they
    // send out isn't placed), bosses, and enemies that come back by themselves (Bob-ombs, Dry Bones).
    const char* const cEnemyNames[] = {
        "Kuribo",         "KuriboChief",     "KuriboMini",       "Kabokuri",        "TakoHei",
        "Takobo",         "Karikari",        "KoteBug",          "Balloonfish",     "Jiraira",
        "OtaRock",        "Meramera",        "IceMeramera",      "EyeBeamer",       "BigBirikyu",
        "CocoSambo",      "SamboHead",       "DharmaSambo",      "BegomanBaby",     "BegomanSpring",
        "BegomanSpringHide", "BegomanSpike", "FireBubble",       "Pukupuku",        "BombTeresa",
        "Teresa",         "TeresaChief",     "TeresaWater",      "Unizo",           "UnizoLand",
        "UnizoShoal",     "Kiraira",         "KirairaRail",      "NokonokoLand",    "Gesso",
        "Poihana",        "Snakehead",       "SnakeheadSmall",   "Jellyfish",       "JellyfishElectric",
        "Kanina",         "KaninaRed",       "Mogu",             "Mogucchi",        "Hanachan",
        "JumpSpider",     "StringSpider",    "PackunPetit",      "StinkBugSmall",   "BasaBasa",
        "BasaBasaIce",    "Metbo",           "BombBird",         "MechanicKoopaMini", "HammerHeadPackun",
        "SearchBeamer",   "WaterBazooka",    "ElectricBazooka",
    };

    bool isEnemyName(const char* pName) {
        for (u32 i = 0; i < sizeof(cEnemyNames) / sizeof(cEnemyNames[0]); i++) {
            if (strcmp(pName, cEnemyNames[i]) == 0) {
                return true;
            }
        }
        return false;
    }

    bool isName(const char* pName, const char* pA, const char* pB = nullptr, const char* pC = nullptr,
                const char* pD = nullptr) {
        return strcmp(pName, pA) == 0 || (pB != nullptr && strcmp(pName, pB) == 0) ||
               (pC != nullptr && strcmp(pName, pC) == 0) || (pD != nullptr && strcmp(pName, pD) == 0);
    }

    u32 hashBytes(u32 hash, const void* pData, u32 size) {
        const u8* pBytes = static_cast< const u8* >(pData);
        for (u32 i = 0; i < size; i++) {
            hash = (hash ^ pBytes[i]) * 16777619u;
        }
        return hash;
    }

    u32 hashValue(u32 hash, u32 value) {
        return hashBytes(hash, &value, sizeof(value));
    }

    // A placement's key: its name, zone, link id and place, as the stage's files have them.
    u32 placementKey(const char* pName, const JMapInfoIter& rIter) {
        u32 hash = hashBytes(2166136261u, pName, strlen(pName));
        hash = hashValue(hash, static_cast< u32 >(MR::getPlacedZoneId(rIter)));
        s32 linkId = -1;
        MR::getJMapInfoLinkID(rIter, &linkId);
        hash = hashValue(hash, static_cast< u32 >(linkId));
        TVec3f trans(0.0f, 0.0f, 0.0f);
        MR::getJMapInfoTrans(rIter, &trans);
        hash = hashBytes(hash, &trans.x, sizeof(f32));
        hash = hashBytes(hash, &trans.y, sizeof(f32));
        hash = hashBytes(hash, &trans.z, sizeof(f32));
        return hash;
    }

    s32 findIndex(u32 key) {
        for (u32 slot = key & (cTableSize - 1);; slot = (slot + 1) & (cTableSize - 1)) {
            if (sTable[slot] == 0) {
                return -1;
            }
            if (sEntries[sTable[slot] - 1].mKey == key) {
                return sTable[slot] - 1;
            }
        }
    }

    u32 actorSlot(const void* pActor, u32 size) {
        u32 hash = static_cast< u32 >(reinterpret_cast< uintptr_t >(pActor) >> 4);
        hash ^= hash >> 15;
        hash *= 0x2c1b3c6du;
        hash ^= hash >> 12;
        return hash & (size - 1);
    }

    s32 findActor(const void* pActor) {
        if (pActor == nullptr) {
            return -1;
        }
        for (u32 i = 0, slot = actorSlot(pActor, cTableSize); i < cTableSize; i++, slot = (slot + 1) & (cTableSize - 1)) {
            if (sActors[slot].mActor == nullptr) {
                return -1;
            }
            if (sActors[slot].mActor == pActor) {
                const s32 index = sActors[slot].mEntry;
                return sEntries[index].mActor == pActor ? index : -1;
            }
        }
        return -1;
    }

    void putActor(ActorSlot* pTable, u32 size, const void* pActor, s16 index) {
        for (u32 i = 0, slot = actorSlot(pActor, size); i < size; i++, slot = (slot + 1) & (size - 1)) {
            if (pTable[slot].mActor == nullptr || pTable[slot].mActor == pActor) {
                pTable[slot].mActor = pActor;
                pTable[slot].mEntry = index;
                return;
            }
        }
    }

    Motion* findMotion(const void* pActor) {
        for (u32 i = 0, slot = actorSlot(pActor, cMotionTableSize); i < cMotionTableSize;
             i++, slot = (slot + 1) & (cMotionTableSize - 1)) {
            if (sMotionTable[slot].mActor == nullptr) {
                return nullptr;
            }
            if (sMotionTable[slot].mActor == pActor) {
                return &sMotions[sMotionTable[slot].mEntry];
            }
        }
        return nullptr;
    }

    void noteClass(Motion& rMotion, const Nerve* pNerve) {
        if (pNerve == nullptr) {
            return;
        }
        const u32 nerveClass = pNerve->nebulaClass();
        if (nerveClass == 0) {
            return;
        }
        for (u8 i = 0; i < rMotion.mClassNum; i++) {
            if (rMotion.mClasses[i] == nerveClass) {
                return;
            }
        }
        if (rMotion.mClassNum < 4) {
            rMotion.mClasses[rMotion.mClassNum++] = nerveClass;
        }
    }

    // An enemy's motion slot: its owner is none yet.
    void addMotion(s32 index) {
        if (sMotionNum >= cMaxMotions) {
            return;
        }
        Motion& rMotion = sMotions[sMotionNum];
        memset(static_cast< void* >(&rMotion), 0, sizeof(rMotion));
        rMotion.mEntry = index;
        rMotion.mHeard = cSilentFrames;
        LiveActor* pActor = sEntries[index].mActor;
        if (pActor->mSpine != nullptr) {
            noteClass(rMotion, pActor->mSpine->getCurrentNerve());
        }
        sEntries[index].mMotion = static_cast< s16 >(sMotionNum);
        putActor(sMotionTable, cMotionTableSize, pActor, static_cast< s16 >(sMotionNum));
        sMotionNum++;
    }

    // A thing to follow, unless it is followed already. Two placements alike (the same name, zone and place) take the
    // next key, in placement order, which is every player's game's.
    s32 add(u32 key, LiveActor* pActor, Kind kind) {
        if (pActor == nullptr || sEntryNum >= cMaxEntries || findActor(pActor) >= 0) {
            return -1;
        }
        if (key == 0) {
            key = 1;
        }
        while (findIndex(key) >= 0) {
            key = hashValue(key, 0x9e3779b9u);
        }
        Entry& rEntry = sEntries[sEntryNum];
        memset(static_cast< void* >(&rEntry), 0, sizeof(rEntry));
        rEntry.mActor = pActor;
        rEntry.mKey = key;
        rEntry.mKind = static_cast< u8 >(kind);
        rEntry.mMotion = -1;
        rEntry.mHitFrames = cHitFrames;
        rEntry.mLastHitFrame = -cHitRepeatFrames;
        u32 slot = key & (cTableSize - 1);
        while (sTable[slot] != 0) {
            slot = (slot + 1) & (cTableSize - 1);
        }
        sTable[slot] = static_cast< s16 >(sEntryNum + 1);
        putActor(sActors, cTableSize, pActor, static_cast< s16 >(sEntryNum));
        if (kind == Kind_Enemy && pActor->mSpine != nullptr) {
            addMotion(sEntryNum);
        }
        return sEntryNum++;
    }

    void report(u32 kind, u32 object, u32 value, s32 scenario, const char* pName) {
        NebulaCoopWorldEvent event = {};
        event.kind = kind;
        event.object = object;
        event.value = value;
        event.scenario = scenario;
        if (pName != nullptr) {
            nebula_snprintf(event.name, sizeof(event.name), "%s", pName);
        }
        NebulaHost::coopWorldReport(&event);
    }

    // Gone, as the server says: dead without its drops, its sound or its effect, its dead switch on as the game's
    // own death turns it (a star that appears once some enemies are defeated still does).
    void removeQuietly(Entry& rEntry) {
        LiveActor* pActor = rEntry.mActor;
        if (MR::isDead(pActor)) {
            return;
        }
        sApplying = true;
        if (MR::isValidSwitchDead(pActor)) {
            MR::onSwitchDead(pActor);
        }
        pActor->makeActorDead();
        sApplying = false;
    }

    void applyGone(u32 object, u32 how) {
        const s32 index = findIndex(object);
        if (index < 0 || sEntries[index].mActor == nullptr) {
            // Not here yet (a coin or star bit an enemy drops in this game a little later): gone once it comes.
            sPending[sPendingNext].mKey = object;
            sPending[sPendingNext].mHow = how;
            sPendingNext = (sPendingNext + 1) % cMaxPending;
            return;
        }
        Entry& rEntry = sEntries[index];
        if (rEntry.mGone) {
            // Already gone here (taken here too, the race lost): it doesn't count twice.
            return;
        }
        rEntry.mGone = true;
        const bool following = rEntry.mMotion >= 0 && sMotions[rEntry.mMotion].mLocked;
        if (rEntry.mGot && !MR::isDead(rEntry.mActor)) {
            // The local player took it too and it still flies to him: it ends its flight first.
            rEntry.mGoneDelay = 60;
        } else if (sReady && (rEntry.mHitFrames < cHitFrames || following) && !MR::isDead(rEntry.mActor)) {
            // Its death, from a hit played here or its owner's nerves, may still be playing (a Goomba squashed): it
            // ends first.
            rEntry.mGoneDelay = 150;
        } else {
            removeQuietly(rEntry);
            rEntry.mAlive = false;
        }
        // Coins and star bits are each player's own (Galaxy has no 100-coin star): another player's are only gone
        // here. Purple coins count toward the mission's 100 for everyone.
        if ((how & NEBULA_COOP_GONE_PURPLE) != 0) {
            MR::incPurpleCoin();
        }
    }

    // Where the local player's own attacks (not an enemy's, not a thing's own) reach: these are played again.
    bool isSharedHit(u32 msg) {
        switch (msg) {
        case ACTMES_PLAYER_PUNCH:
        case ACTMES_PLAYER_TRAMPLE:
        case ACTMES_PLAYER_HIP_DROP:
        case ACTMES_PLAYER_HIP_DROP_FLOOR:
        case ACTMES_PLAYER_UPPER_PUNCH:
        case ACTMES_FREEZE_ATTACK:
        case ACTMES_INVINCIBLE_ATTACK:
        case ACTMES_KICK:
            return true;
        default:
            return false;
        }
    }

    bool isHitShared(const Entry& rEntry) {
        return rEntry.mKind == Kind_Enemy || rEntry.mKind == Kind_Object || rEntry.mKind == Kind_Part;
    }

    s32 sensorIndex(const LiveActor* pActor, const HitSensor* pSensor) {
        const HitSensorKeeper* pKeeper = pActor->mSensorKeeper;
        if (pKeeper == nullptr) {
            return -1;
        }
        for (s32 i = 0; i < pKeeper->mSensorCount && i < 256; i++) {
            if (pKeeper->mSensorInfos[i] != nullptr && pKeeper->mSensorInfos[i]->mSensor == pSensor) {
                return i;
            }
        }
        return -1;
    }

    // Another player's hit, played again from their copy here: the thing reacts as it did in their game.
    void replayHit(const NebulaCoopWorldEvent& rEvent) {
        const s32 index = findIndex(rEvent.object);
        if (index < 0) {
            return;
        }
        Entry& rEntry = sEntries[index];
        LiveActor* pActor = rEntry.mActor;
        if (pActor == nullptr || !isHitShared(rEntry) || rEntry.mGone || MR::isDead(pActor) || pActor->mSensorKeeper == nullptr) {
            return;
        }
        const HitSensorKeeper* pKeeper = pActor->mSensorKeeper;
        if (rEvent.scenario < 0 || rEvent.scenario >= pKeeper->mSensorCount || pKeeper->mSensorInfos[rEvent.scenario] == nullptr) {
            return;
        }
        HitSensor* pReceiver = pKeeper->mSensorInfos[rEvent.scenario]->mSensor;
        HitSensor* pSender = NebulaRemotePlayer::getHitSender(rEvent.player);
        if (pReceiver == nullptr || pSender == nullptr) {
            return;
        }
        LiveActor* pDispatch = sDispatch;
        sDispatch = pActor;
        sReplaying = true;
        pActor->receiveMessage(rEvent.value, pSender, pReceiver);
        sReplaying = false;
        sDispatch = pDispatch;
        rEntry.mHitFrames = 0;
        rEntry.mHitByOther = true;
    }

    void rememberRoomSwitch(u32 id, bool on) {
        for (s32 i = 0; i < sRoomSwitchNum; i++) {
            if (sRoomSwitches[i].mId == id) {
                sRoomSwitches[i].mOn = on;
                return;
            }
        }
        if (sRoomSwitchNum < cMaxRoomSwitches) {
            sRoomSwitches[sRoomSwitchNum].mId = id;
            sRoomSwitches[sRoomSwitchNum].mOn = on;
            sRoomSwitchNum++;
        }
    }

    void applySwitch(u32 id, bool on) {
        if (!MR::isExistSceneObj(SceneObj_StageSwitchContainer)) {
            return;
        }
        sApplying = true;
        StageSwitchFunction::nebulaSetSwitch(id, on);
        sApplying = false;
    }

    void applySave(const NebulaCoopWorldEvent& rEvent) {
        GameDataHolder* pHolder = GameDataFunction::getCurrentGameDataHolder();
        if (pHolder == nullptr) {
            return;
        }
        sApplying = true;
        if (rEvent.kind == NEBULA_COOP_WORLD_STAR) {
            if (pHolder->mAllGalaxyStorage->isExistAccessor(rEvent.name, rEvent.scenario) &&
                !pHolder->hasPowerStar(rEvent.name, rEvent.scenario)) {
                nebula_log("[coop] the room's star: %s %d\n", rEvent.name, static_cast< int >(rEvent.scenario));
                pHolder->setPowerStar(rEvent.name, rEvent.scenario, true);
                NebulaCoopDemos::noticeRoomStar(rEvent.name, rEvent.scenario);
            }
        } else if (rEvent.kind == NEBULA_COOP_WORLD_FLAG) {
            if (GameEventFlagTable::isExist(rEvent.name) && !pHolder->isOnGameEventFlag(rEvent.name)) {
                pHolder->tryOnGameEventFlag(rEvent.name);
            }
        } else if (rEvent.kind == NEBULA_COOP_WORLD_VALUE) {
            if (pHolder->mEventValueChecker->findIndex(rEvent.name) >= 0 &&
                pHolder->getGameEventValue(rEvent.name) != static_cast< s32 >(rEvent.value)) {
                pHolder->setGameEventValue(rEvent.name, static_cast< u16 >(rEvent.value));
            }
        }
        sApplying = false;
    }

    void takeFromServer() {
        // Static: the module links without __chkstk, so no frame may need a stack probe.
        static NebulaCoopWorldEvent events[cTakePerCall];
        for (u32 call = 0; call < cTakeCalls; call++) {
            const u32 count = NebulaHost::coopWorldTake(events, cTakePerCall);
            const u32 used = count < cTakePerCall ? count : cTakePerCall;
            for (u32 i = 0; i < used; i++) {
                events[i].name[sizeof(events[i].name) - 1] = '\0';
                if (events[i].kind == NEBULA_COOP_WORLD_GONE) {
                    applyGone(events[i].object, events[i].value);
                } else if (events[i].kind == NEBULA_COOP_WORLD_READY) {
                    sReady = true;
                } else if (events[i].kind == NEBULA_COOP_WORLD_HIT) {
                    replayHit(events[i]);
                } else if (events[i].kind == NEBULA_COOP_WORLD_SWITCH) {
                    rememberRoomSwitch(events[i].object, events[i].value != 0);
                    applySwitch(events[i].object, events[i].value != 0);
                } else if (events[i].kind == NEBULA_COOP_WORLD_STAR_GOT) {
                    NebulaCoopDemos::applyStarGot(events[i].scenario, (events[i].value & NEBULA_COOP_STAR_GRAND) != 0);
                } else if (events[i].kind != NEBULA_COOP_WORLD_READY) {
                    applySave(events[i]);
                }
            }
            if (count <= cTakePerCall) {
                return;
            }
        }
    }

    // A model's turn as a quaternion (x y z w) from its base matrix's rotation.
    void quatFromMtx(const MtxPtr pMtx, f32* pQuat) {
        const f32 trace = pMtx[0][0] + pMtx[1][1] + pMtx[2][2];
        f32 x, y, z, w;
        if (trace > 0.0f) {
            const f32 root = sqrtf(trace + 1.0f) * 2.0f;
            w = 0.25f * root;
            x = (pMtx[2][1] - pMtx[1][2]) / root;
            y = (pMtx[0][2] - pMtx[2][0]) / root;
            z = (pMtx[1][0] - pMtx[0][1]) / root;
        } else if (pMtx[0][0] > pMtx[1][1] && pMtx[0][0] > pMtx[2][2]) {
            const f32 root = sqrtf(1.0f + pMtx[0][0] - pMtx[1][1] - pMtx[2][2]) * 2.0f;
            w = (pMtx[2][1] - pMtx[1][2]) / root;
            x = 0.25f * root;
            y = (pMtx[0][1] + pMtx[1][0]) / root;
            z = (pMtx[0][2] + pMtx[2][0]) / root;
        } else if (pMtx[1][1] > pMtx[2][2]) {
            const f32 root = sqrtf(1.0f + pMtx[1][1] - pMtx[0][0] - pMtx[2][2]) * 2.0f;
            w = (pMtx[0][2] - pMtx[2][0]) / root;
            x = (pMtx[0][1] + pMtx[1][0]) / root;
            y = 0.25f * root;
            z = (pMtx[1][2] + pMtx[2][1]) / root;
        } else {
            const f32 root = sqrtf(1.0f + pMtx[2][2] - pMtx[0][0] - pMtx[1][1]) * 2.0f;
            w = (pMtx[1][0] - pMtx[0][1]) / root;
            x = (pMtx[0][2] + pMtx[2][0]) / root;
            y = (pMtx[1][2] + pMtx[2][1]) / root;
            z = 0.25f * root;
        }
        pQuat[0] = x;
        pQuat[1] = y;
        pQuat[2] = z;
        pQuat[3] = w;
    }

    // The rotation part of a base matrix from a quaternion; its place stays.
    void setMtxRotation(MtxPtr pMtx, const f32* pQuat) {
        const f32 x = pQuat[0], y = pQuat[1], z = pQuat[2], w = pQuat[3];
        pMtx[0][0] = 1.0f - 2.0f * (y * y + z * z);
        pMtx[0][1] = 2.0f * (x * y - z * w);
        pMtx[0][2] = 2.0f * (x * z + y * w);
        pMtx[1][0] = 2.0f * (x * y + z * w);
        pMtx[1][1] = 1.0f - 2.0f * (x * x + z * z);
        pMtx[1][2] = 2.0f * (y * z - x * w);
        pMtx[2][0] = 2.0f * (x * z - y * w);
        pMtx[2][1] = 2.0f * (y * z + x * w);
        pMtx[2][2] = 1.0f - 2.0f * (x * x + y * y);
    }

    MtxPtr baseMtxOf(LiveActor* pActor) {
        if (pActor->mModelManager == nullptr || MR::getJ3DModel(pActor) == nullptr) {
            return nullptr;
        }
        return reinterpret_cast< MtxPtr >(&MR::getJ3DModel(pActor)->mBaseTransformMtx);
    }

    bool isKnownClass(const Motion& rMotion, u32 nerveClass) {
        for (u8 i = 0; i < rMotion.mClassNum; i++) {
            if (rMotion.mClasses[i] == nerveClass) {
                return true;
            }
        }
        return false;
    }

    // Another game's state of an enemy: it moves that game's copy, which this one follows.
    void applyState(const NebulaCoopObjectState& rState) {
        const s32 index = findIndex(rState.object);
        if (index < 0 || sEntries[index].mMotion < 0 || rState.player == 0 || rState.player == sSelf) {
            return;
        }
        Entry& rEntry = sEntries[index];
        Motion& rMotion = sMotions[rEntry.mMotion];
        LiveActor* pActor = rEntry.mActor;
        rMotion.mOwner = rState.player;
        rMotion.mHeard = 0;
        rMotion.mHitClaim = false;
        rMotion.mOwnerDistance = rState.distance;
        rMotion.mState = rState;
        rMotion.mAge = 0;
        if (pActor == nullptr || rEntry.mGone || MR::isDead(pActor)) {
            return;
        }
        // Its nerve, unless a hit began it (that comes as the hit, played again here) or this game's code never set
        // one of its class on it (another class's nerve would run this actor as something else).
        Spine* pSpine = pActor->mSpine;
        if (rState.nerve == 0 || (rState.flags & NEBULA_COOP_OBJECT_HIT_NERVE) != 0 || pSpine == nullptr) {
            return;
        }
        const Nerve* pCurrent = pSpine->getCurrentNerve();
        if (pCurrent != nullptr && pCurrent->nebulaId() == rState.nerve) {
            return;
        }
        const Nerve* pNerve = NebulaNerves::find(rState.nerve);
        if (pNerve == nullptr || !isKnownClass(rMotion, pNerve->nebulaClass())) {
            return;
        }
        sSyncNerve = true;
        pActor->setNerve(pNerve);
        sSyncNerve = false;
    }

    void takeStates() {
        // Static: the module links without __chkstk, so no frame may need a stack probe.
        static NebulaCoopObjectState states[64];
        for (u32 call = 0; call < 16; call++) {
            const u32 count = NebulaHost::coopObjectsTake(states, 64);
            const u32 used = count < 64 ? count : 64;
            for (u32 i = 0; i < used; i++) {
                applyState(states[i]);
            }
            if (count <= 64) {
                return;
            }
        }
    }

    void fillState(const Entry& rEntry, Motion& rMotion, f32 distance, NebulaCoopObjectState* pState) {
        LiveActor* pActor = rEntry.mActor;
        memset(pState, 0, sizeof(*pState));
        pState->object = rEntry.mKey;
        const Nerve* pNerve = pActor->mSpine != nullptr ? pActor->mSpine->getCurrentNerve() : nullptr;
        pState->nerve = pNerve != nullptr ? pNerve->nebulaId() : 0;
        pState->step = pActor->mSpine != nullptr && pActor->mSpine->mStep > 0 ? static_cast< u32 >(pActor->mSpine->mStep) : 0;
        if (rMotion.mHitClaim) {
            pState->flags |= NEBULA_COOP_OBJECT_HIT_CLAIM;
        }
        if (pNerve != nullptr && pNerve == rMotion.mHitNerve) {
            pState->flags |= NEBULA_COOP_OBJECT_HIT_NERVE;
        }
        pState->position[0] = pActor->mPosition.x;
        pState->position[1] = pActor->mPosition.y;
        pState->position[2] = pActor->mPosition.z;
        pState->velocity[0] = pActor->mVelocity.x;
        pState->velocity[1] = pActor->mVelocity.y;
        pState->velocity[2] = pActor->mVelocity.z;
        if (MtxPtr pMtx = baseMtxOf(pActor)) {
            quatFromMtx(pMtx, pState->rotation);
        }
        pState->distance = distance;
    }

    // Each frame: which enemies this game moves (sm64coopdx's sync_object_should_own: the nearest player's, the host
    // settling it), their states sent, and which ones follow another game's.
    void updateMotions() {
        if (sMotionNum == 0 || sSelf == 0) {
            return;
        }
        // Static: the module links without __chkstk, so no frame may need a stack probe.
        static NebulaCoopObjectState states[64];
        u32 stateNum = 0;
        const TVec3f* pMarioPos = sMario != nullptr ? &sMario->mPosition : nullptr;
        for (s32 i = 0; i < sMotionNum; i++) {
            Motion& rMotion = sMotions[i];
            Entry& rEntry = sEntries[rMotion.mEntry];
            LiveActor* pActor = rEntry.mActor;
            if (rMotion.mHeard < cSilentFrames) {
                rMotion.mHeard++;
            }
            if (rMotion.mSendIn > 0) {
                rMotion.mSendIn--;
            }
            if (pActor == nullptr) {
                rMotion.mLocked = false;
                continue;
            }
            // Its hit nerve is over once it has another.
            const Nerve* pCurrent = pActor->mSpine->getCurrentNerve();
            if (rMotion.mHitNerve != nullptr && pCurrent != rMotion.mHitNerve) {
                rMotion.mHitNerve = nullptr;
            }
            const bool alive = !rEntry.mGone && !MR::isDead(pActor);
            const bool awake = alive && !MR::isClipped(pActor);
            const f32 distance = pMarioPos != nullptr ? pMarioPos->distance(pActor->mPosition) : 1.0e9f;
            const bool followed = rMotion.mOwner != 0 && rMotion.mOwner != sSelf && rMotion.mHeard < cSilentFrames;
            rMotion.mLocked = alive && followed;
            bool send = false;
            if (rMotion.mOwner == sSelf) {
                if (!awake) {
                    // Gone, or stopped here (out of sight): another player near it may take it over.
                    rMotion.mOwner = 0;
                    continue;
                }
                const u32 nerve = pCurrent != nullptr ? pCurrent->nebulaId() : 0;
                send = rMotion.mSendIn == 0 || nerve != rMotion.mSentNerve || rMotion.mHitClaim;
            } else if (alive && rMotion.mHitClaim) {
                send = true;
            } else if (awake && distance < cSyncDistance && rMotion.mSendIn == 0) {
                // Nobody moves it, or this player is a quarter nearer than its owner: taken over (the host may say no:
                // the owner's states then keep coming).
                send = !followed || distance * cNearerRatio < rMotion.mOwnerDistance;
                if (!send) {
                    rMotion.mSendIn = cClaimEvery;
                }
            }
            if (!send || stateNum >= 64) {
                continue;
            }
            fillState(rEntry, rMotion, distance, &states[stateNum++]);
            const bool moved = rMotion.mSentPosition.distance(pActor->mPosition) > 1.0f;
            rMotion.mOwner = sSelf;
            rMotion.mHeard = 0;
            rMotion.mLocked = false;
            rMotion.mHitClaim = false;
            rMotion.mSentNerve = states[stateNum - 1].nerve;
            rMotion.mSentPosition = pActor->mPosition;
            rMotion.mSendIn = moved ? cSendMoving : cSendStill;
        }
        if (stateNum > 0) {
            NebulaHost::coopObjectsReport(states, stateNum);
        }
    }

    // The switches this frame turned, to the server; once the scene's own setup is done, the room's set again (the
    // stage's setup may have turned some back).
    void updateSwitches(bool settled) {
        for (s32 i = 0; i < sTurnedNum; i++) {
            report(NEBULA_COOP_WORLD_SWITCH, sTurned[i].mId, sTurned[i].mOn ? 1 : 0, 0, nullptr);
            rememberRoomSwitch(sTurned[i].mId, sTurned[i].mOn);
        }
        sTurnedNum = 0;
        if (settled) {
            for (s32 i = 0; i < sRoomSwitchNum; i++) {
                applySwitch(sRoomSwitches[i].mId, sRoomSwitches[i].mOn);
            }
        }
    }

    // A save change of the current save, not one the server's being applied, while in a room.
    bool isReported(const GameDataHolder* pHolder) {
        return !sApplying && sInRoom && pHolder == GameDataFunction::getCurrentGameDataHolder();
    }

    // A pooled actor let out again stands for nothing it stood for before (a gone one would be kept dead).
    void forgetPooled(const LiveActor* pActor) {
        const s32 had = findActor(pActor);
        if (had >= 0) {
            sEntries[had].mActor = nullptr;
        }
    }

    // A drop just keyed: what the server said of it before it came applies now.
    void applyPending(s32 index) {
        sEntries[index].mAlive = true;
        for (s32 i = 0; i < cMaxPending; i++) {
            if (sPending[i].mKey == sEntries[index].mKey && sPending[i].mKey != 0) {
                sPending[i].mKey = 0;
                applyGone(sEntries[index].mKey, sPending[i].mHow);
            }
        }
    }
};  // namespace

namespace NebulaCoopWorld {
    bool sObjectsActive = false;

    void startScene() {
        sEntryNum = 0;
        memset(sTable, 0, sizeof(sTable));
        memset(sActors, 0, sizeof(sActors));
        sMotionNum = 0;
        memset(sMotionTable, 0, sizeof(sMotionTable));
        sPlaced = false;
        sMissed = false;
        sReady = false;
        sFrames = 0;
        sClock = 0;
        sPlacing = false;
        sMadeNum = 0;
        sTurnedNum = 0;
        sRoomSwitchNum = 0;
        sLocalSwitches = 0;
        sFlipNum = 0;
        memset(sPending, 0, sizeof(sPending));
        sObjectsActive = false;
        sSelf = 0;
        sMario = nullptr;
        u32 roomSize = 0;
        NebulaHost::coopList(nullptr, 0, &roomSize);
        sInRoom = roomSize != 0;
        if (sInRoom) {
            static NebulaCoopListPlayer players[16];
            const u32 count = NebulaHost::coopList(players, 16, &roomSize);
            for (u32 i = 0; i < count && i < 16; i++) {
                if ((players[i].flags & NEBULA_COOP_LIST_YOU) != 0) {
                    sSelf = players[i].number;
                }
            }
        }
        NebulaCoopDemos::startScene(sInRoom);
    }

    void endScene() {
        sObjectsActive = false;
        sPlacing = false;
        sMario = nullptr;
    }

    void beginPlacement() {
        sPlacing = sInRoom;
        sMadeNum = 0;
    }

    void noteMade(LiveActor* pActor) {
        if (sPlacing && sMadeNum < 256) {
            sMade[sMadeNum++] = pActor;
        }
    }

    void notePlaced(const char* pName, NameObj* pObj, const JMapInfoIter& rIter) {
        sPlacing = false;
        if (!sInRoom || pName == nullptr || pObj == nullptr) {
            return;
        }
        const u32 key = placementKey(pName, rIter);
        if (isName(pName, "Coin", "PurpleCoin")) {
            Coin* pCoin = static_cast< Coin* >(pObj);
            add(key, pCoin, pCoin->mIsPurpleCoin ? Kind_PurpleCoin : Kind_Coin);
        } else if (isName(pName, "RailCoin", "CircleCoinGroup", "PurpleRailCoin", "CirclePurpleCoinGroup")) {
            CoinGroup* pGroup = static_cast< CoinGroup* >(pObj);
            for (u32 i = 0; i < pGroup->mCoinCount; i++) {
                add(hashValue(key, i), pGroup->mCoinArray[i], pGroup->mIsPurpleCoinGroup ? Kind_PurpleCoin : Kind_Coin);
            }
        } else if (isName(pName, "StarPieceGroup", "StarPieceFlow")) {
            StarPieceGroup* pGroup = static_cast< StarPieceGroup* >(pObj);
            for (s32 i = 0; i < pGroup->mNumPieces; i++) {
                add(hashValue(key, static_cast< u32 >(i)), pGroup->mPieces[i], Kind_StarBit);
            }
        } else if (isName(pName, "StarPiece")) {
            add(key, static_cast< StarPiece* >(pObj), Kind_StarBit);
        } else if (isName(pName, "BenefitItemOneUp", "BenefitItemLifeUp", "BenefitItemInvincible")) {
            add(key, static_cast< LiveActor* >(pObj), Kind_Item);
        } else if (isEnemyName(pName)) {
            add(key, static_cast< LiveActor* >(pObj), Kind_Enemy);
        }
        // Every other placed actor, and the actors each one made as it was placed (in that order, every game's), for
        // the hits on them. What isn't an actor (an area, a camera) was never made through LiveActor.
        for (s32 i = 0; i < sMadeNum; i++) {
            if (sMade[i] == pObj) {
                add(key, sMade[i], Kind_Object);
            }
        }
        for (s32 i = 0; i < sMadeNum; i++) {
            if (sMade[i] != pObj) {
                add(hashValue(hashValue(key, 0x50415254u /* PART */), static_cast< u32 >(i)), sMade[i], Kind_Part);
            }
        }
        sMadeNum = 0;
    }

    void endPlacement() {
        if (!sInRoom) {
            return;
        }
        for (s32 i = 0; i < sEntryNum; i++) {
            sEntries[i].mAlive = !MR::isDead(sEntries[i].mActor);
        }
        sPlaced = true;
        sObjectsActive = true;
        nebula_log("[coop] the instance follows %d placed things, %d of them moving\n", sEntryNum, sMotionNum);
        report(NEBULA_COOP_WORLD_ENTER, 0, 0, MR::getCurrentScenarioNo(), MR::getCurrentStageName());
    }

    void update() {
        if (!sInRoom || !sPlaced) {
            return;
        }
        sClock++;
        sMario = MR::isExistSceneObj(SceneObj_MarioHolder) ? MR::getMarioHolder()->getMarioActor() : nullptr;
        takeFromServer();
        takeStates();
        const bool settling = sFrames < cSettleFrames;
        if (settling) {
            sFrames++;
        }
        for (s32 i = 0; i < sEntryNum; i++) {
            Entry& rEntry = sEntries[i];
            if (rEntry.mActor == nullptr) {
                continue;
            }
            if (rEntry.mHitFrames < cHitFrames) {
                rEntry.mHitFrames++;
            }
            const bool alive = !MR::isDead(rEntry.mActor);
            if (alive && rEntry.mGone) {
                if (rEntry.mGoneDelay > 0) {
                    // Its own death still plays (a hit played here): the server's record waits for it a while.
                    rEntry.mGoneDelay--;
                    continue;
                }
                // Back by the game's own doing (a group shown again, a switch): the server's record wins.
                removeQuietly(rEntry);
                rEntry.mAlive = false;
                continue;
            }
            // A coin is the instance's once a player takes it (sm64coopdx's collect_coin): one a timer, a switch or a
            // black hole takes goes in every game alike, and comes back with a challenge tried again.
            // A coin or star bit a player takes is gone for everyone (counted only for its taker); one a thing let
            // out that isn't taken (it fell away) stays each game's.
            const bool isCoin = rEntry.mKind == Kind_Coin || rEntry.mKind == Kind_PurpleCoin;
            const bool needsGot = isCoin || rEntry.mKind == Kind_StarBitDrop;
            const bool reportsGone = isCoin || rEntry.mKind == Kind_StarBit || rEntry.mKind == Kind_StarBitDrop || rEntry.mKind == Kind_Item || rEntry.mKind == Kind_Enemy;
            if (reportsGone && !alive && rEntry.mAlive && !rEntry.mGone && !settling && (!needsGot || rEntry.mGot)) {
                rEntry.mGone = true;
                u32 how = 0;
                if (isCoin) {
                    how = rEntry.mKind == Kind_PurpleCoin ? NEBULA_COOP_GONE_PURPLE : NEBULA_COOP_GONE_COIN;
                }
                report(NEBULA_COOP_WORLD_GONE, rEntry.mKey, how, 0, nullptr);
            }
            if (alive && isCoin) {
                // Taken again only by a new take: a coin shown again (a timer group's) isn't counted from the last.
                rEntry.mGot = false;
            }
            rEntry.mAlive = alive;
        }
        updateMotions();
        updateSwitches(sFrames == cSettleFrames && settling);
    }

    void noticeMiss() {
        if (!sInRoom || !sPlaced || sMissed) {
            return;
        }
        sMissed = true;
        report(NEBULA_COOP_WORLD_MISS, 0, 0, 0, nullptr);
        nebula_log("[coop] a miss: the instance goes on without this player until the scene starts over\n");
    }

    bool isInRoom() {
        return sInRoom;
    }

    bool deliverMessage(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
        LiveActor* pActor = pReceiver->mHost;
        // The local Mario's own attack on a followed thing: noted for the others to play again.
        if (sReplaying || sMario == nullptr || pSender == nullptr || pSender->mHost != sMario || !isSharedHit(msg)) {
            return pActor->receiveMessage(msg, pSender, pReceiver);
        }
        const s32 index = findActor(pActor);
        if (index < 0 || !isHitShared(sEntries[index]) || sEntries[index].mGone) {
            return pActor->receiveMessage(msg, pSender, pReceiver);
        }
        LiveActor* pDispatch = sDispatch;
        sDispatch = pActor;
        const bool taken = pActor->receiveMessage(msg, pSender, pReceiver);
        sDispatch = pDispatch;
        if (!taken) {
            return false;
        }
        Entry& rEntry = sEntries[index];
        rEntry.mHitFrames = 0;
        rEntry.mHitByOther = false;
        if (rEntry.mMotion >= 0) {
            // A hit takes it over (sm64coopdx: who interacts with an object sends its update).
            Motion& rMotion = sMotions[rEntry.mMotion];
            rMotion.mHitClaim = true;
            rMotion.mOwner = sSelf;
            rMotion.mHeard = 0;
            rMotion.mLocked = false;
        }
        if (rEntry.mLastHit == msg && sClock - rEntry.mLastHitFrame < cHitRepeatFrames) {
            return true;
        }
        const s32 sensor = sensorIndex(pActor, pReceiver);
        if (sensor >= 0) {
            rEntry.mLastHit = static_cast< u16 >(msg);
            rEntry.mLastHitFrame = sClock;
            report(NEBULA_COOP_WORLD_HIT, rEntry.mKey, msg, sensor, nullptr);
        }
        return true;
    }

    bool allowNerve(LiveActor* pActor, const Nerve* pNerve) {
        Motion* pMotion = findMotion(pActor);
        if (pMotion == nullptr) {
            return true;
        }
        noteClass(*pMotion, pNerve);
        if (sSyncNerve) {
            return true;
        }
        if (sDispatch == pActor) {
            // A hit (the local player's, or another's played again) begins it: the others get the hit itself.
            pMotion->mHitNerve = pNerve;
            return true;
        }
        // Following another game's copy: its own AI waits for that game's nerve.
        return !pMotion->mLocked;
    }

    void afterMovement(LiveActor* pActor) {
        Motion* pMotion = findMotion(pActor);
        if (pMotion == nullptr || !pMotion->mLocked) {
            return;
        }
        const NebulaCoopObjectState& rState = pMotion->mState;
        // Where its owner has it now: its last place, moved on as fast as it went then.
        const f32 age = static_cast< f32 >(pMotion->mAge < 30 ? pMotion->mAge : 30);
        const TVec3f there(rState.position[0] + rState.velocity[0] * age, rState.position[1] + rState.velocity[1] * age,
                           rState.position[2] + rState.velocity[2] * age);
        pMotion->mAge++;
        if (pActor->mPosition.distance(there) > cSnapDistance) {
            pActor->mPosition.set(there);
        } else {
            pActor->mPosition.set(pActor->mPosition + (there - pActor->mPosition) * cEase);
        }
        pActor->mVelocity.set(rState.velocity[0], rState.velocity[1], rState.velocity[2]);
    }

    void afterBaseMtx(LiveActor* pActor) {
        Motion* pMotion = findMotion(pActor);
        if (pMotion == nullptr || !pMotion->mLocked) {
            return;
        }
        const f32* pTarget = pMotion->mState.rotation;
        const f32 norm = pTarget[0] * pTarget[0] + pTarget[1] * pTarget[1] + pTarget[2] * pTarget[2] + pTarget[3] * pTarget[3];
        MtxPtr pMtx = baseMtxOf(pActor);
        if (norm < 0.5f || pMtx == nullptr) {
            return;
        }
        // Eased toward the owner's turn (the short way round), drawn so.
        f32* pRot = pMotion->mRotation;
        if (!pMotion->mRotationSet) {
            memcpy(pRot, pTarget, sizeof(f32) * 4);
            pMotion->mRotationSet = true;
        } else {
            const f32 dot = pRot[0] * pTarget[0] + pRot[1] * pTarget[1] + pRot[2] * pTarget[2] + pRot[3] * pTarget[3];
            const f32 sign = dot < 0.0f ? -1.0f : 1.0f;
            for (s32 i = 0; i < 4; i++) {
                pRot[i] += (pTarget[i] * sign - pRot[i]) * 0.3f;
            }
        }
        const f32 length = sqrtf(pRot[0] * pRot[0] + pRot[1] * pRot[1] + pRot[2] * pRot[2] + pRot[3] * pRot[3]);
        if (length < 0.001f) {
            return;
        }
        f32 unit[4] = {pRot[0] / length, pRot[1] / length, pRot[2] / length, pRot[3] / length};
        setMtxRotation(pMtx, unit);
        pMtx[0][3] = pActor->mPosition.x;
        pMtx[1][3] = pActor->mPosition.y;
        pMtx[2][3] = pActor->mPosition.z;
    }

    void noteSwitch(u32 id, bool on) {
        if (!sObjectsActive || sApplying || sLocalSwitches > 0 || sFrames < cSettleFrames) {
            return;
        }
        const bool shared = isSharedSwitch(id);
        for (s32 i = 0; i < sTurnedNum; i++) {
            if (sTurned[i].mId == id) {
                if (shared) {
                    sTurned[i].mOn = on;
                } else {
                    sTurned[i] = sTurned[--sTurnedNum];
                }
                return;
            }
        }
        if (!shared) {
            return;
        }
        if (sTurnedNum < cMaxSwitchesTurned) {
            sTurned[sTurnedNum].mId = id;
            sTurned[sTurnedNum].mOn = on;
            sTurnedNum++;
        }
    }

    void beginLocalSwitches() {
        sLocalSwitches++;
    }

    void endLocalSwitches() {
        if (sLocalSwitches > 0) {
            sLocalSwitches--;
        }
    }

    void noteCoinDrop(const NameObj* pHost, LiveActor* pCoin) {
        if (!sObjectsActive || pCoin == nullptr) {
            return;
        }
        forgetPooled(pCoin);
        const s32 host = findActor(pHost);
        if (host < 0) {
            return;
        }
        Entry& rHost = sEntries[host];
        const u32 key = hashValue(hashValue(rHost.mKey, 0x44524f50u /* DROP */), rHost.mDrops++);
        const s32 index = add(key, pCoin, Kind_Coin);
        if (index >= 0) {
            applyPending(index);
        }
    }

    bool isOtherPlayersHit(const NameObj* pHost) {
        if (!sObjectsActive) {
            return false;
        }
        const s32 index = findActor(pHost);
        return index >= 0 && sEntries[index].mHitByOther && sEntries[index].mHitFrames < cHitFrames;
    }

    void noteStarBitPooled(const LiveActor* pPiece) {
        if (sObjectsActive && pPiece != nullptr) {
            forgetPooled(pPiece);
        }
    }

    void noteStarBitDrop(const NameObj* pHost, LiveActor* pPiece) {
        if (!sObjectsActive || pPiece == nullptr) {
            return;
        }
        const s32 host = findActor(pHost);
        if (host < 0) {
            return;
        }
        Entry& rHost = sEntries[host];
        const u32 key = hashValue(hashValue(rHost.mKey, 0x42495453u /* BITS */), rHost.mBits++);
        const s32 index = add(key, pPiece, Kind_StarBitDrop);
        if (index >= 0) {
            applyPending(index);
        }
    }

    void noticeCoinGot(const LiveActor* pCoin) {
        if (!sInRoom || sApplying) {
            return;
        }
        const s32 index = findActor(pCoin);
        if (index >= 0) {
            sEntries[index].mGot = true;
        }
    }

    void noticeStarBitGot(const LiveActor* pPiece) {
        if (!sInRoom || sApplying) {
            return;
        }
        const s32 index = findActor(pPiece);
        if (index >= 0 && (sEntries[index].mKind == Kind_StarBit || sEntries[index].mKind == Kind_StarBitDrop)) {
            sEntries[index].mGot = true;
        }
    }

    void noticePowerStar(const GameDataHolder* pHolder, const char* pGalaxyName, s32 scenarioNo) {
        if (isReported(pHolder) && pGalaxyName != nullptr) {
            report(NEBULA_COOP_WORLD_STAR, 0, 0, scenarioNo, pGalaxyName);
        }
    }

    void noticeEventFlag(const GameDataHolder* pHolder, const char* pName) {
        if (isReported(pHolder) && pName != nullptr) {
            report(NEBULA_COOP_WORLD_FLAG, 0, 0, 0, pName);
        }
    }

    void noticeEventValue(const GameDataHolder* pHolder, const char* pName, u16 value) {
        // Each player's own count of misses isn't the room's.
        if (isReported(pHolder) && pName != nullptr && strcmp(pName, "MissNum") != 0) {
            report(NEBULA_COOP_WORLD_VALUE, 0, value, 0, pName);
        }
    }
};  // namespace NebulaCoopWorld
