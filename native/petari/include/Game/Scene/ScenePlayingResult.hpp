#pragma once

#include "Game/NameObj/NameObj.hpp"

// The coins and purple coins of the stage being played; star bits go straight to the save.
class ScenePlayingResult : public NameObj {
public:
    ScenePlayingResult();

    void incCoin(int num);
    s32 getCoinNum() const;
    void incPurpleCoin();

    /// @brief Online co-op: adds the coins another player in this instance took (sm64coopdx's collect_coin): the
    /// count only, without the life, the 1-Up or the player's own statistics.
    void incCoinOfOtherPlayer(int num);

    void incStarPiece(int num);
    s32 getStarPieceNum() const;

    s32 mCoinNum;
    s32 mPurpleCoinNum;
};
