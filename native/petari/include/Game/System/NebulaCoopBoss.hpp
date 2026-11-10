#pragma once

class LiveActor;
class Nerve;
class NerveExecutor;

// Online co-op's boss fights (docs/COOP_PLAN.md "Boss fights"), as sm64coopdx's bosses: the fight is the room's and
// its cutscenes are for the players there. A fight is its boss's phases in order (its sequence's nerves, counted from
// 1). Each game tells the instance the phases its own play reaches (NebulaCoopWorld::noteMark, kept in the instance's
// record for players coming later), and a game whose boss is behind the instance's goes on to the instance's phase:
// - The opening (the scene meeting the boss) plays once, for whoever's game began the fight: a player coming later
//   lands as on any planet, the boss already fighting.
// - The fight's other scenes (the boss hurt, angry, beaten) play for each player in the fight (near the boss) as their
//   game reaches them; a player away misses them, only their outcome applied (an egg broken, the Power Star out).
// - The music those scenes start (the boss's theme for its phase) plays for a player once they come to the fight.
// Another player's hits that begin a scene only their own player plays out (a tail pulled) aren't played again here:
// the phase they end in comes instead. Out of a room nothing changes.
class NebulaCoopBossPhases {
public:
    NebulaCoopBossPhases();

    // `pBoss` the placed boss (its key), `pNerves` its sequence's phases in order (phase 1 the first), `num` of them.
    void init(LiveActor* pBoss, const Nerve* const* pNerves, s32 num);
    // The phase of a nerve of the sequence (0 for one that isn't a phase: waiting to start).
    s32 phaseOf(const Nerve* pNerve) const;
    const Nerve* nerveOf(s32 phase) const;
    s32 getPhaseNum() const {
        return mNum;
    }
    // The phase anyone's game in the instance reached (0 none, or out of a room).
    s32 getRoomPhase() const;
    // Each frame, before the sequence runs (`pExecutor` it): the phase this game's play reached is told to the
    // instance. Returns the phase to go to (the instance's, ahead of this game's) once the boss can go (no cutscene
    // holds the local player), 0 otherwise.
    s32 update(const NerveExecutor* pExecutor);
    // The sequence went to `phase` to follow the instance (not this game's play): it isn't told back.
    void jumped(s32 phase);
    // Whether the local player is in the fight (near the boss): its cutscenes play here.
    bool isWatching() const;
    // A scene skipped here would have started the music of `phase`: it plays once the local player comes to the fight.
    // takeMusic gives that phase then (0 otherwise), once.
    void keepMusic(s32 phase);
    s32 takeMusic();

private:
    LiveActor* mBoss;
    const Nerve* const* mNerves;
    s32 mNum;
    s32 mLocalPhase;
    s32 mMusicPhase;
};

namespace NebulaCoopBoss {
    // Whether this game is in a room's scene (the fight is the instance's).
    bool isActive();
    // Whether a hit being delivered now is another player's, played again here from their copy.
    bool isOtherPlayersHit();
};  // namespace NebulaCoopBoss
