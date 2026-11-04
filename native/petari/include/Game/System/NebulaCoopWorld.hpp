#pragma once


class GameDataHolder;
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
// switch turned on, without its drops or sounds) and kept dead if the game brings it back. A coin another player
// took counts here too, a purple coin toward the 100, as sm64coopdx's collect_coin does.
//
// The save: Power Stars and event flags the game gets, and event values it sets, are reported (GameDataHolder); the
// session's are applied as they come, and again after each scene starts, for whatever the game doesn't have.
//
// Shared objects (docs/COOP_PLAN.md "Shared objects", sm64coopdx's sync objects), with keys for every placed actor and
// the actors each one makes as it is placed (a boss's parts, a group's members):
// - Hits. The local Mario's attack on one of them (a stomp, a spin, a ground pound, a kick) is played again in each
//   other game in the instance from his copy there, so the thing reacts in every game: a Goomba squashed with its
//   animation, a box broken, a switch pressed, a boss hurt.
// - Movement. Each plain enemy is moved by one game, its owner: the player nearest it, the host settling who. As
//   sm64ex-coop's object packets, the owner's states steer the others' copies (how fast it goes, its nerve when that
//   changed, where it is: they glide there) while each copy runs its own AI, unclipped while it is followed, as a
//   synced object stays active; its model is drawn turned as the owner's. A player who hits an enemy takes it over:
//   his stomps, spins and kicks, and his star bits' and fireballs' hits.
// - Switches. A stage switch turned in one game is turned in the others in the instance; the instance's record keeps
//   them for players coming later. Switches an area turns while the player is in it stay each player's.
// - Coins an enemy drops are keyed by the enemy, so one taken counts for everyone and is gone everywhere.
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
    // calcAnmMtx after calcAndSetBaseMtx: an enemy another game moves is drawn turned as there.
    void afterBaseMtx(LiveActor* pActor);
    // StageSwitchFunction: a switch turned (its zone << 16 | number, the zone 0xffff for global ones).
    void noteSwitch(u32 id, bool on);
    // Around switches that follow the local player alone (an area turning one while he is in it).
    void beginLocalSwitches();
    void endLocalSwitches();
    // CoinHolder::appearCoin: a coin `pHost` let out (an enemy's drop).
    void noteCoinDrop(const NameObj* pHost, LiveActor* pCoin);

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
    // Coin::noticeGetCoin: the local player took this coin (it counts for the others too).
    void noticeCoinGot(const LiveActor* pCoin);
    // NebulaCoopBubble::tryStart, as a miss goes on (no bubble saved the local player): the player doesn't keep the
    // instance going until the scene starts over (sm64coopdx's death warp out of the level), once per scene.
    void noticeMiss();
    // Whether this scene is in a room.
    bool isInRoom();

    // GameDataHolder: the current save got a Power Star, an event flag, an event value.
    void noticePowerStar(const GameDataHolder* pHolder, const char* pGalaxyName, s32 scenarioNo);
    void noticeEventFlag(const GameDataHolder* pHolder, const char* pName);
    void noticeEventValue(const GameDataHolder* pHolder, const char* pName, u16 value);
};  // namespace NebulaCoopWorld
