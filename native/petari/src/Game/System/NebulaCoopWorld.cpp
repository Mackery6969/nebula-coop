#include "Game/System/NebulaCoopWorld.hpp"
#include "nebula_module.h"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaCoopDemos.hpp"
#include "Game/LiveActor/LiveActor.hpp"
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
#include "Game/Util/SceneUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>

namespace {
    enum Kind {
        Kind_Coin,
        Kind_PurpleCoin,
        Kind_StarBit,
        Kind_Item,
        Kind_Enemy,
    };

    struct Entry {
        LiveActor* mActor;
        u32 mKey;
        u8 mKind;
        bool mAlive;  // as the last frame saw it
        bool mGone;   // reported, or the server said so: kept dead
        bool mGot;    // a coin the local player took
    };

    // Things a scene follows at most, and the table finding them by key (a power of two, twice as many).
    const s32 cMaxEntries = 4096;
    const u32 cTableSize = 8192;
    // A scene's first frames: things the stage's own setup kills then aren't reported (a coin hidden for a comet, an
    // enemy waiting for its switch), only noted.
    const s32 cSettleFrames = 30;
    // What one frame takes from the host at most.
    const u32 cTakePerCall = 64;
    const u32 cTakeCalls = 16;

    Entry sEntries[cMaxEntries];
    s32 sEntryNum = 0;
    s16 sTable[cTableSize];  // an entry's index + 1, 0 for none
    bool sInRoom = false;
    bool sPlaced = false;
    bool sApplying = false;  // applying what the server said: not reported back
    s32 sFrames = 0;

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

    // A thing to follow. Two placements alike (the same name, zone and place) take the next key, in placement order,
    // which is every player's game's.
    void add(u32 key, LiveActor* pActor, Kind kind) {
        if (pActor == nullptr || sEntryNum >= cMaxEntries) {
            return;
        }
        if (key == 0) {
            key = 1;
        }
        while (findIndex(key) >= 0) {
            key = hashValue(key, 0x9e3779b9u);
        }
        Entry& rEntry = sEntries[sEntryNum];
        rEntry.mActor = pActor;
        rEntry.mKey = key;
        rEntry.mKind = static_cast< u8 >(kind);
        rEntry.mAlive = false;
        rEntry.mGone = false;
        rEntry.mGot = false;
        u32 slot = key & (cTableSize - 1);
        while (sTable[slot] != 0) {
            slot = (slot + 1) & (cTableSize - 1);
        }
        sTable[slot] = static_cast< s16 >(sEntryNum + 1);
        sEntryNum++;
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
        if (index < 0) {
            return;
        }
        Entry& rEntry = sEntries[index];
        if (rEntry.mGone) {
            // Already gone here (taken here too, the race lost): it doesn't count twice.
            return;
        }
        rEntry.mGone = true;
        removeQuietly(rEntry);
        rEntry.mAlive = false;
        if ((how & NEBULA_COOP_GONE_COIN) != 0) {
            MR::incCoinOfOtherPlayer(1);
        }
        if ((how & NEBULA_COOP_GONE_PURPLE) != 0) {
            MR::incPurpleCoin();
        }
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
        NebulaCoopWorldEvent events[cTakePerCall];
        for (u32 call = 0; call < cTakeCalls; call++) {
            const u32 count = NebulaHost::coopWorldTake(events, cTakePerCall);
            const u32 used = count < cTakePerCall ? count : cTakePerCall;
            for (u32 i = 0; i < used; i++) {
                events[i].name[sizeof(events[i].name) - 1] = '\0';
                if (events[i].kind == NEBULA_COOP_WORLD_GONE) {
                    applyGone(events[i].object, events[i].value);
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

    // A save change of the current save, not one the server's being applied, while in a room.
    bool isReported(const GameDataHolder* pHolder) {
        return !sApplying && sInRoom && pHolder == GameDataFunction::getCurrentGameDataHolder();
    }
};  // namespace

namespace NebulaCoopWorld {
    void startScene() {
        sEntryNum = 0;
        memset(sTable, 0, sizeof(sTable));
        sPlaced = false;
        sFrames = 0;
        u32 roomSize = 0;
        NebulaHost::coopList(nullptr, 0, &roomSize);
        sInRoom = roomSize != 0;
        NebulaCoopDemos::startScene(sInRoom);
    }

    void notePlaced(const char* pName, NameObj* pObj, const JMapInfoIter& rIter) {
        if (!sInRoom || pName == nullptr || pObj == nullptr) {
            return;
        }
        if (isName(pName, "Coin", "PurpleCoin")) {
            Coin* pCoin = static_cast< Coin* >(pObj);
            add(placementKey(pName, rIter), pCoin, pCoin->mIsPurpleCoin ? Kind_PurpleCoin : Kind_Coin);
        } else if (isName(pName, "RailCoin", "CircleCoinGroup", "PurpleRailCoin", "CirclePurpleCoinGroup")) {
            CoinGroup* pGroup = static_cast< CoinGroup* >(pObj);
            const u32 key = placementKey(pName, rIter);
            for (u32 i = 0; i < pGroup->mCoinCount; i++) {
                add(hashValue(key, i), pGroup->mCoinArray[i], pGroup->mIsPurpleCoinGroup ? Kind_PurpleCoin : Kind_Coin);
            }
        } else if (isName(pName, "StarPieceGroup", "StarPieceFlow")) {
            StarPieceGroup* pGroup = static_cast< StarPieceGroup* >(pObj);
            const u32 key = placementKey(pName, rIter);
            for (s32 i = 0; i < pGroup->mNumPieces; i++) {
                add(hashValue(key, static_cast< u32 >(i)), pGroup->mPieces[i], Kind_StarBit);
            }
        } else if (isName(pName, "StarPiece")) {
            add(placementKey(pName, rIter), static_cast< StarPiece* >(pObj), Kind_StarBit);
        } else if (isName(pName, "BenefitItemOneUp", "BenefitItemLifeUp", "BenefitItemInvincible")) {
            add(placementKey(pName, rIter), static_cast< LiveActor* >(pObj), Kind_Item);
        } else if (isEnemyName(pName)) {
            add(placementKey(pName, rIter), static_cast< LiveActor* >(pObj), Kind_Enemy);
        }
    }

    void endPlacement() {
        if (!sInRoom) {
            return;
        }
        for (s32 i = 0; i < sEntryNum; i++) {
            sEntries[i].mAlive = !MR::isDead(sEntries[i].mActor);
        }
        sPlaced = true;
        nebula_log("[coop] the instance follows %d placed things\n", sEntryNum);
        report(NEBULA_COOP_WORLD_ENTER, 0, 0, MR::getCurrentScenarioNo(), MR::getCurrentStageName());
    }

    void update() {
        if (!sInRoom || !sPlaced) {
            return;
        }
        takeFromServer();
        const bool settling = sFrames < cSettleFrames;
        if (settling) {
            sFrames++;
        }
        for (s32 i = 0; i < sEntryNum; i++) {
            Entry& rEntry = sEntries[i];
            const bool alive = !MR::isDead(rEntry.mActor);
            if (alive && rEntry.mGone) {
                // Back by the game's own doing (a group shown again, a switch): the server's record wins.
                removeQuietly(rEntry);
                rEntry.mAlive = false;
                continue;
            }
            // A coin is the instance's once a player takes it (sm64coopdx's collect_coin): one a timer, a switch or a
            // black hole takes goes in every game alike, and comes back with a challenge tried again.
            const bool isCoin = rEntry.mKind == Kind_Coin || rEntry.mKind == Kind_PurpleCoin;
            if (!alive && rEntry.mAlive && !rEntry.mGone && !settling && (!isCoin || rEntry.mGot)) {
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
    }

    void noticeCoinGot(const LiveActor* pCoin) {
        if (!sInRoom || sApplying) {
            return;
        }
        for (s32 i = 0; i < sEntryNum; i++) {
            if (sEntries[i].mActor == pCoin) {
                sEntries[i].mGot = true;
                return;
            }
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
