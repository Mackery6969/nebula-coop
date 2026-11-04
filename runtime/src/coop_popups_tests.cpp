// Online co-op's pop-ups (galaxy/coop_popups.h): who joined, left, went where, died or got a star, as the room's players
// change from one call to the next, and the PLAYER page's switches.
#include "galaxy/coop_popups.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {
int s_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++s_failures;
    }
}

using namespace galaxy::coop;
using Kind = Popup::Kind;

SessionPlayer player(galaxy::net::PeerId id, const char* name, const char* stage = "", int scenario = 0,
                     bool local = false) {
    SessionPlayer p;
    p.id = id;
    p.name = name;
    p.stage = stage;
    p.scenario = scenario;
    p.local = local;
    p.character = id == 2u ? galaxy::CoopCharacter::Luigi : galaxy::CoopCharacter::Mario;
    return p;
}

// Every pop-up waiting, in order.
std::vector<Popup> drain(Popups& popups, std::uint64_t now_ms) {
    std::vector<Popup> out;
    while (auto popup = popups.take(now_ms)) out.push_back(*popup);
    return out;
}

bool one(const std::vector<Popup>& got, Kind kind, const char* name) {
    return got.size() == 1u && got[0].kind == kind && got[0].name == name;
}

void joins_and_leaves() {
    Popups popups;
    // Joining a room: whoever was there already is no news.
    popups.update({player(1u, "Me", "AstroGalaxy", 1, true), player(2u, "Luigi", "EggStarGalaxy", 1)}, 100u);
    expect(drain(popups, 100u).empty(), "the room as found makes no pop-ups");

    popups.update({player(1u, "Me", "AstroGalaxy", 1, true), player(2u, "Luigi", "EggStarGalaxy", 1),
                   player(3u, "Toad")},
                  200u);
    std::vector<Popup> got = drain(popups, 200u);
    expect(one(got, Kind::Joined, "Toad"), "a new player joined");

    popups.update({player(1u, "Me", "AstroGalaxy", 1, true), player(3u, "Toad")}, 300u);
    got = drain(popups, 300u);
    expect(one(got, Kind::Left, "Luigi") && got[0].character == galaxy::CoopCharacter::Luigi,
           "a player left, in their character's colour");

    // The room ends: everyone is forgotten without a word, and the next room is found as it is.
    popups.update({}, 400u);
    popups.update({player(1u, "Me", "", 0, true), player(5u, "Peach", "AstroGalaxy", 1)}, 500u);
    expect(drain(popups, 500u).empty(), "a room ending and the next one found make no pop-ups");
}

void places() {
    Popups popups;
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true)}, 0u);
    // A player joins with no place yet, then reaches the Observatory.
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi")}, 10u);
    expect(one(drain(popups, 10u), Kind::Joined, "Luigi"), "joined");
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi", "AstroGalaxy", 1)}, 20u);
    expect(one(drain(popups, 20u), Kind::Observatory, "Luigi"), "their first place shows: the Observatory");
    // Into a dome and back is still the Observatory.
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi", "AstroDome", 3)}, 30u);
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi", "AstroGalaxy", 2)}, 40u);
    expect(drain(popups, 40u).empty(), "going about the Observatory makes no pop-ups");
    // To another galaxy's star.
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi", "HoneyBeeKingdomGalaxy", 2)}, 50u);
    std::vector<Popup> got = drain(popups, 50u);
    expect(one(got, Kind::Galaxy, "Luigi") && got[0].stage == "HoneyBeeKingdomGalaxy" && got[0].scenario == 2,
           "a galaxy and its star");
    // To this player's galaxy and star, then away.
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi", "EggStarGalaxy", 1)}, 60u);
    expect(one(drain(popups, 60u), Kind::EnteredHere, "Luigi"), "came to this player's galaxy");
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi", "AstroGalaxy", 1)}, 70u);
    expect(one(drain(popups, 70u), Kind::LeftHere, "Luigi"), "left this player's galaxy");
    // The same galaxy, another star: elsewhere.
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi", "EggStarGalaxy", 2)}, 80u);
    expect(one(drain(popups, 80u), Kind::Galaxy, "Luigi"), "the same galaxy's other star is elsewhere");
    // A place lost (between scenes) changes nothing.
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi")}, 90u);
    popups.update({player(1u, "Me", "EggStarGalaxy", 1, true), player(2u, "Luigi", "EggStarGalaxy", 2)}, 95u);
    expect(drain(popups, 95u).empty(), "no place for a moment is no move");
    // In the Observatory with this player: entering it says the Observatory, not "here".
    popups.update({player(1u, "Me", "AstroGalaxy", 1, true), player(2u, "Luigi", "EggStarGalaxy", 2)}, 100u);
    popups.update({player(1u, "Me", "AstroGalaxy", 1, true), player(2u, "Luigi", "AstroGalaxy", 1)}, 110u);
    expect(one(drain(popups, 110u), Kind::Observatory, "Luigi"), "the Observatory is named even when shared");
}

void stars_and_deaths() {
    Popups popups;
    SessionPlayer me = player(1u, "Me", "EggStarGalaxy", 1, true);
    SessionPlayer luigi = player(2u, "Luigi", "EggStarGalaxy", 1);
    luigi.stars = 3u;
    popups.update({me, luigi}, 0u);
    luigi.stars = 4u;
    me.stars = 1u;
    popups.update({me, luigi}, 10u);
    std::vector<Popup> got = drain(popups, 10u);
    expect(one(got, Kind::Star, "Luigi") && got[0].stage == "EggStarGalaxy", "another's star, not this player's own");

    const InstancePlayer alive{2u, "Luigi", galaxy::CoopCharacter::Luigi, false};
    const InstancePlayer dead{2u, "Luigi", galaxy::CoopCharacter::Luigi, true};
    popups.update_instance(std::vector<InstancePlayer>{dead}, 20u);
    expect(drain(popups, 20u).empty(), "one who comes into the instance dead isn't news");
    popups.update_instance(std::vector<InstancePlayer>{alive}, 30u);
    popups.update_instance(std::vector<InstancePlayer>{dead}, 40u);
    popups.update_instance(std::vector<InstancePlayer>{dead}, 50u);
    got = drain(popups, 50u);
    expect(one(got, Kind::Died, "Luigi"), "a death, once");
    popups.update_instance(std::vector<InstancePlayer>{}, 60u);
    popups.update_instance(std::vector<InstancePlayer>{dead}, 70u);
    expect(drain(popups, 70u).empty(), "gone from the instance and back dead isn't news");
}

void options_and_waiting() {
    Popups popups;
    galaxy::RuntimeCoopPopups options;
    options.joins = false;
    options.galaxies = false;
    popups.set_options(options);
    popups.update({player(1u, "Me", "AstroGalaxy", 1, true)}, 0u);
    popups.update({player(1u, "Me", "AstroGalaxy", 1, true), player(2u, "Luigi")}, 10u);
    popups.update({player(1u, "Me", "AstroGalaxy", 1, true), player(2u, "Luigi", "EggStarGalaxy", 1)}, 20u);
    popups.update({player(1u, "Me", "AstroGalaxy", 1, true), player(2u, "Luigi", "AstroGalaxy", 1)}, 30u);
    std::vector<Popup> got = drain(popups, 30u);
    expect(one(got, Kind::Observatory, "Luigi"), "joins and galaxies off: only the Observatory shows");

    expect(popup_wanted(Kind::LeftHere, options) == false && popup_wanted(Kind::Left, options),
           "leaving this player's galaxy goes with the galaxies; leaving the room with the leaves");

    // Waiting too long: dropped.
    popups.set_options({});
    popups.update({player(1u, "Me", "AstroGalaxy", 1, true)}, 1000u);
    expect(!popups.take(1000u + kPopupWaitMs + 1u).has_value(), "nothing waits");
    popups.update({player(1u, "Me", "AstroGalaxy", 1, true), player(3u, "Toad")}, 2000u);
    expect(!popups.take(2000u + kPopupWaitMs + 1u).has_value(), "a pop-up not shown in time is dropped");
    for (unsigned i = 0; i < kPopupWaitLimit + 4u; ++i) {
        std::vector<SessionPlayer> room{player(1u, "Me", "AstroGalaxy", 1, true)};
        if (i % 2u == 0u) room.push_back(player(9u, "Yoshi"));
        popups.update(room, 3000u);
    }
    expect(drain(popups, 3000u).size() == kPopupWaitLimit, "at most kPopupWaitLimit wait");
    popups.clear();
    expect(!popups.take(3000u).has_value(), "clear drops them");
}
}  // namespace

int main() {
    joins_and_leaves();
    places();
    stars_and_deaths();
    options_and_waiting();
    if (s_failures != 0) return 1;
    std::puts("coop popups tests passed");
    return 0;
}
