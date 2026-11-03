#pragma once


class GameDataHolder;
class JMapInfoIter;
class LiveActor;
class NameObj;

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
namespace NebulaCoopWorld {
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

    // GameDataHolder: the current save got a Power Star, an event flag, an event value.
    void noticePowerStar(const GameDataHolder* pHolder, const char* pGalaxyName, s32 scenarioNo);
    void noticeEventFlag(const GameDataHolder* pHolder, const char* pName);
    void noticeEventValue(const GameDataHolder* pHolder, const char* pName, u16 value);
};  // namespace NebulaCoopWorld
