#pragma once


#include <JSystem/JGeometry/TVec.hpp>

class GameDataHolder;
struct NebulaCoopPlayer;
class HitSensor;
class JMapInfoIter;
class LiveActor;
class NameObj;
class Nerve;

// Online co-op's shared world (docs/COOP_PLAN.md "Shared world state"): the server decides it, the game reports what
// its own play did and applies what it is told, as sm64coopdx syncs its levels.
//
// The things it follows are placed from the stage's files: coins and purple coins (alone or in rail and circle
// groups), star bits (alone or in groups), 1-Up and Life mushrooms and Rainbow Stars, and the plain enemies. Each has
// a key from its placement (its zone, name, link id and place), the same in every player's game. A thing the local
// play kills (taken, defeated) is reported gone; a thing the server says is gone is removed (made dead, its dead
// switch turned on, without its drops or sounds) and kept dead if the game brings it back. Coins and star bits are
// each player's own (Galaxy has no 100-coin star): one another player took is only gone here. A purple coin another
// player took counts toward the 100 here too, as sm64coopdx's collect_coin does.
//
// The save: Power Stars and event flags the game gets, and event values it sets, are reported (GameDataHolder); the
// session's are applied as they come, and again after each scene starts, for whatever the game doesn't have. High
// scores (a star's most coins, a race's best time) are the best of anyone's; the observatory's star bit bank is the
// host's game's, which applies the joiners' changes to it.
//
// Shared objects (docs/COOP_PLAN.md "Shared objects", sm64coopdx's sync objects), with keys for every placed actor and
// the actors each one makes as it is placed (a boss's parts, a group's members):
// - Hits. The local Mario's attack on one of them (a stomp, a spin, a ground pound, a kick) is played again in each
//   other game in the instance from his copy there, so the thing reacts in every game: a Goomba squashed with its
//   animation, a box broken, a switch pressed, a boss hurt.
// - Movement. Each plain enemy is moved by one game, its owner: the player nearest it, as every game works it out
//   from where the players are (sm64coopdx's sync_object_should_own). As sm64ex-coop's object packets, the owner
//   sends its state when its nerve changes and otherwise every distance / 1000 seconds (a third of a second at
//   least, within 4000 units), each counted so a late one is dropped; the state steers the others' copies (how fast
//   it goes, its nerve when that changed, where it is: eased there) while each copy runs its own AI, unclipped while
//   it is followed or owned, as a synced object stays active, and goes for the nearest player as the owner's does.
//   A player entering the instance gets each enemy's latest state with its record.
// - Switches. A stage switch turned in one game is turned in the others in the instance; the instance's record keeps
//   them for players coming later. Switches an area turns while the player is in it stay each player's.
// - Coins an enemy drops, and star bits a followed thing lets out (a crystal broken, an enemy defeated), are keyed by
//   it and their order, so one taken is gone everywhere (counted for its taker).
namespace NebulaCoopWorld {
    // Whether the game is in a room's scene with its objects followed: the hooks below do nothing otherwise.
    extern bool sObjectsActive;
    // GameScene's end: its actors go, nothing is followed until the next scene.
    void endScene();
    // PlacementInfoOrdered, before making a placed object: the actors made until notePlaced are its parts.
    void beginPlacement();
    // LiveActor's constructor: an actor made (one of the placed object's parts while it is being placed).
    void noteMade(LiveActor* pActor);
    // HitSensor::receiveMessage: a message to an actor, delivered here, the local Mario's hits on followed things
    // noted for the others.
    bool deliverMessage(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    // LiveActor::setNerve: a nerve the actor's own code sets (never held back: its AI always runs).
    void noteNerve(LiveActor* pActor, const Nerve* pNerve);
    // LiveActor::movement's end: an enemy another game moves glides to where that game has it.
    void afterMovement(LiveActor* pActor);
    // calcAnmMtx after calcAndSetBaseMtx: an enemy another game moves is drawn turned as there for a while after
    // each of its states.
    void afterBaseMtx(LiveActor* pActor);
    // Whether `pActor` is a shared enemy (one whose movement is followed): it goes for the nearest player
    // (MR::getTargetPlayerPos), as sm64coopdx's behaviors do.
    bool isSharedEnemy(const LiveActor* pActor);
    // StageSwitchFunction: a switch turned (its zone << 16 | number, the zone 0xffff for global ones).
    void noteSwitch(u32 id, bool on);
    // Around switches that follow the local player alone (an area turning one while he is in it).
    void beginLocalSwitches();
    void endLocalSwitches();
    // CoinHolder::appearCoin: a coin `pHost` let out (an enemy's drop).
    void noteCoinDrop(const NameObj* pHost, LiveActor* pCoin);
    // CoinHolder::hopCoin, StarPieceDirector::hopPiece: whether `pHost`'s last hit was another player's, played again
    // here (a "?" block's coin or star bit, given straight to whoever hit it, is theirs: none hops here).
    bool isOtherPlayersHit(const NameObj* pHost);
    // StarPieceDirector::getDeadStarPiece: a pooled star bit let out again (for anything: a drop, a shot, a gift).
    void noteStarBitPooled(const LiveActor* pPiece);
    // StarPieceDirector::appearPiece: a star bit `pHost` let out (a crystal's, an enemy's).
    void noteStarBitDrop(const NameObj* pHost, LiveActor* pPiece);

    // GameScene::init, before the stage's actors are placed: the last scene's things are forgotten, and whether
    // this scene is in a room.
    void startScene();
    // PlacementInfoOrdered: an object placed from the stage's files, just made.
    void notePlaced(const char* pName, NameObj* pObj, const JMapInfoIter& rIter);
    // GameScene::init, once everything is placed: this scene's instance is told to the server, whose record of it
    // comes back.
    void endPlacement();
    // Each game scene frame: what the server told is applied, what the local play killed is reported.
    void update();
    // ChipBase::initAfterPlacement: a Star Chip (blue or yellow), wherever it came from (placed, in a box, a shell).
    void noteChip(LiveActor* pChip);
    // ChipBase::requestGet: the local player took this Star Chip: it counts toward its group for everyone.
    void noticeChipGot(LiveActor* pChip);
    // Coin::noticeGetCoin: the local player took this coin (gone for the others, counted only here).
    void noticeCoinGot(const LiveActor* pCoin);
    // StarPiece, as the local player gets it: it is gone for the others (a drop is reported only once taken).
    void noticeStarBitGot(const LiveActor* pPiece);
    // NebulaCoopBubble::tryStart, as a miss goes on (no bubble saved the local player): the player doesn't keep the
    // instance going until the scene starts over (sm64coopdx's death warp out of the level), once per scene.
    void noticeMiss();
    // Whether this scene is in a room.
    bool isInRoom();
    // GameScene's report of the local player: the moving thing he stands on, when the others' games have it too (a
    // placed actor's own collision, seen moving), and where he stands on it (sm64coopdx's platformSyncID).
    void notePlatform(NebulaCoopPlayer* pPlayer);
    // NebulaRemotePlayer::show: where another player standing on a moving thing stands on this game's copy of it
    // (each game moves its own, not always in step), false when this game doesn't have it.
    bool placeOnPlatform(const NebulaCoopPlayer& rPlayer, TVec3f* pPosition, TVec3f* pFront, TVec3f* pUp);
    // The mission's clock (docs/COOP_PLAN.md "Races and timers"): a speedy comet's time limit is the instance's, one
    // for everyone in it, as the first player's started. startMissionClock, as the local clock starts: the server is
    // told, and the frames the instance's has run come back (0 when this one is the first). getMissionClock: the
    // frames it has run since, -1 when none is known (or the scene isn't in a room).
    s32 startMissionClock();
    s32 getMissionClock();

    // GameDataHolder: the current save got a Power Star, an event flag, an event value.
    void noticePowerStar(const GameDataHolder* pHolder, const char* pGalaxyName, s32 scenarioNo);
    void noticeEventFlag(const GameDataHolder* pHolder, const char* pName);
    void noticeEventValue(const GameDataHolder* pHolder, const char* pName, u16 value);
    // GameDataHolder::addStockedStarPiece: the observatory's bank changed (the host's game keeps the room's; a joiner's
    // change goes to it).
    void noticeStockedStarBits(const GameDataHolder* pHolder, s32 change);
    // High scores, the room's the best of anyone's: a star's most coins (GameSequenceFunction's stage result), a race's
    // best time (GameDataHolder::setRaceBestTime).
    void noticeMostCoins(const GameDataHolder* pHolder, const char* pGalaxyName, s32 scenarioNo, s32 coins);
    void noticeRaceTime(const GameDataHolder* pHolder, const char* pRaceName, u32 time);
};  // namespace NebulaCoopWorld
