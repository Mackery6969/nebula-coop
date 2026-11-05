#include "galaxy/coop_teleport.h"

#include "galaxy/coop_chat.h"
#include "galaxy/coop_commands.h"
#include "galaxy/net/bytes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

namespace galaxy::coop {

namespace {

// Farther than any galaxy reaches: a coordinate past it is a typo.
constexpr float kFarthest = 10000000.0f;

std::vector<std::string_view> split_words(std::string_view words) {
    std::vector<std::string_view> out;
    std::size_t at = 0u;
    while (at < words.size()) {
        if (words[at] == ' ') {
            ++at;
            continue;
        }
        const std::size_t end = std::min(words.find(' ', at), words.size());
        out.push_back(words.substr(at, end - at));
        at = end;
    }
    return out;
}

std::optional<float> parse_number(std::string_view word) {
    if (word.empty() || word.size() > 32u) return std::nullopt;
    const std::string text(word);
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end != text.c_str() + text.size() || !std::isfinite(value) || std::fabs(value) > kFarthest) return std::nullopt;
    return value;
}

void write_coordinate(net::ByteWriter& out, const Coordinate& coordinate) {
    out.f32(coordinate.value).u8(coordinate.relative ? 1u : 0u);
}

Coordinate read_coordinate(net::ByteReader& in) {
    Coordinate coordinate;
    coordinate.value = in.f32();
    coordinate.relative = in.u8() != 0u;
    return coordinate;
}

std::optional<SessionPlayer> local_player(const std::vector<SessionPlayer>& players) {
    for (const SessionPlayer& player : players) {
        if (player.local) return player;
    }
    return std::nullopt;
}

bool in_room(const Session& session) {
    const SessionState state = session.state();
    return state == SessionState::Hosting || state == SessionState::Joined;
}

}  // namespace

std::optional<Coordinate> parse_coordinate(std::string_view word) {
    Coordinate coordinate;
    if (!word.empty() && word.front() == '~') {
        coordinate.relative = true;
        word.remove_prefix(1u);
        if (word.empty()) return coordinate;
    }
    const std::optional<float> value = parse_number(word);
    if (!value) return std::nullopt;
    coordinate.value = *value;
    return coordinate;
}

std::optional<TeleportWords> parse_teleport(std::string_view words) {
    const std::vector<std::string_view> list = split_words(words);
    TeleportWords out;
    const auto place_from = [&](std::size_t first) {
        for (std::size_t i = 0; i < 3u; ++i) {
            const std::optional<Coordinate> coordinate = parse_coordinate(list[first + i]);
            if (!coordinate) return false;
            out.place[i] = *coordinate;
        }
        return true;
    };
    switch (list.size()) {
    case 1u:
        out.player = std::string(list[0]);
        return out;
    case 2u:
        out.target = std::string(list[0]);
        out.player = std::string(list[1]);
        return out;
    case 3u:
        if (!place_from(0u)) return std::nullopt;
        return out;
    case 4u:
        out.target = std::string(list[0]);
        if (!place_from(1u)) return std::nullopt;
        return out;
    default:
        return std::nullopt;
    }
}

Position resolve_place(const Place& place, const Position& from) {
    Position out{};
    for (std::size_t i = 0; i < 3u; ++i) out[i] = place[i].relative ? from[i] + place[i].value : place[i].value;
    return out;
}

std::string position_text(const Position& position) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.0f %.0f %.0f", static_cast<double>(position[0]),
                  static_cast<double>(position[1]), static_cast<double>(position[2]));
    return text;
}

std::vector<std::uint8_t> encode_teleport(const TeleportMessage& message) {
    net::ByteWriter out;
    out.u64(message.by).u64(message.target).u64(message.to.player);
    for (const Coordinate& coordinate : message.to.place) write_coordinate(out, coordinate);
    return out.bytes();
}

std::optional<TeleportMessage> decode_teleport(std::span<const std::uint8_t> data) {
    net::ByteReader in(data);
    TeleportMessage message;
    message.by = in.u64();
    message.target = in.u64();
    message.to.player = in.u64();
    for (Coordinate& coordinate : message.to.place) coordinate = read_coordinate(in);
    if (!in.ok()) return std::nullopt;
    for (const Coordinate& coordinate : message.to.place) {
        if (!std::isfinite(coordinate.value) || std::fabs(coordinate.value) > kFarthest) return std::nullopt;
    }
    return message;
}

void add_cheat_commands(Chat& chat, Session& session, CheatHooks given) {
    const auto hooks = std::make_shared<const CheatHooks>(std::move(given));
    const auto cheats = [&session] { return in_room(session) && session.room().cheats; };
    // This player to `to`, as its own game sees it: the text id of why not, or none once moved.
    const auto move_here = [hooks](const TeleportTo& to, Position& moved) -> const char* {
        const std::optional<Position> here = hooks->here ? hooks->here() : std::nullopt;
        if (!here) return "CoopCmdTpNoStage";
        if (to.player != net::kNoPeer) {
            const std::optional<Position> there = hooks->where ? hooks->where(to.player) : std::nullopt;
            if (!there) return "CoopCmdTpElsewhere";
            moved = *there;
        } else {
            moved = resolve_place(to.place, *here);
        }
        if (hooks->move) hooks->move(moved);
        return nullptr;
    };

    const ChatCommand::Handler teleport = [&chat, &session, cheats, move_here](std::string_view words) {
        if (!cheats()) {
            chat.say("CoopCmdCheatsOff");
            return true;
        }
        const std::optional<TeleportWords> parsed = parse_teleport(words);
        if (!parsed) {
            chat.say("CoopCmdTpHelp");
            return true;
        }
        const std::vector<SessionPlayer> players = session.players();
        const std::optional<SessionPlayer> self = local_player(players);
        if (!self) return true;
        std::vector<SessionPlayer> targets;
        if (parsed->target.empty() || same_words(parsed->target, "@s")) {
            targets.push_back(*self);
        } else if (same_words(parsed->target, "@a")) {
            targets = players;
        } else if (const std::optional<SessionPlayer> found = find_player(players, parsed->target)) {
            targets.push_back(*found);
        } else {
            chat.say("CoopCmdNotFound");
            return true;
        }
        std::optional<SessionPlayer> destination;
        if (parsed->player) {
            destination = same_words(*parsed->player, "@s") ? self : find_player(players, *parsed->player);
            if (!destination) {
                chat.say("CoopCmdNotFound");
                return true;
            }
        }
        const bool others = std::any_of(targets.begin(), targets.end(), [](const SessionPlayer& p) { return !p.local; });
        if (others && !session.is_host() && !session.is_moderator()) {
            chat.say("CoopCmdNoPerms");
            return true;
        }
        TeleportTo to;
        to.player = destination ? destination->id : net::kNoPeer;
        to.place = parsed->place;
        unsigned moved = 0u;
        std::string last;  // the one moved: who, or where this player went
        for (const SessionPlayer& target : targets) {
            if (destination && destination->id == target.id) continue;  // already there
            if (target.local) {
                Position at{};
                if (const char* why = move_here(to, at)) {
                    const bool elsewhere = std::string_view(why) == "CoopCmdTpElsewhere";
                    chat.say(why, elsewhere && destination ? destination->name : target.name);
                    continue;
                }
                last = destination ? destination->name : position_text(at);
            } else {
                if (target.stage.empty()) {
                    chat.say("CoopCmdTpNoStage", target.name);
                    continue;
                }
                if (destination && (destination->stage != target.stage || destination->scenario != target.scenario)) {
                    chat.say("CoopCmdTpElsewhere", destination->name);
                    continue;
                }
                TeleportMessage message;
                message.by = self->id;
                message.target = target.id;
                message.to = to;
                // The host's straight to them; a moderator's by the host, which checks it.
                const net::PeerId via = session.is_host() ? target.id : net::kEveryone;
                if (!session.send(kTeleportMessage, net::Channel::Reliable, encode_teleport(message), via)) continue;
                last = target.name;
            }
            ++moved;
        }
        if (targets.size() > 1u) {
            if (moved > 0u) chat.say("CoopCmdTpEveryone", std::to_string(moved));
        } else if (moved == 1u) {
            chat.say(targets.front().local ? "CoopCmdTpTo" : "CoopCmdTpOther", last);
        }
        return true;
    };

    for (const char* name : {"tp", "teleport"}) {
        ChatCommand command;
        command.name = name;
        command.help = name[1] == 'p' ? "CoopCmdTpHelp" : "CoopCmdTeleportHelp";
        command.options = {"@a", "@s"};
        command.players = true;
        command.shown = cheats;
        command.run = teleport;
        (void)chat.commands().add(std::move(command));
    }

    ChatCommand pos;
    pos.name = "pos";
    pos.help = "CoopCmdPosHelp";
    pos.shown = cheats;
    pos.run = [&chat, &session, cheats, hooks](std::string_view words) {
        if (!words.empty()) return false;
        if (!cheats()) {
            chat.say("CoopCmdCheatsOff");
            return true;
        }
        const std::optional<Position> here = hooks->here ? hooks->here() : std::nullopt;
        if (here) {
            chat.say("CoopCmdPos", position_text(*here));
        } else if (const std::optional<SessionPlayer> self = local_player(session.players())) {
            chat.say("CoopCmdTpNoStage", self->name);
        }
        return true;
    };
    (void)chat.commands().add(std::move(pos));

    // Another player moving this one (on the session's thread).
    session.on_message(kTeleportMessage, [&chat, &session, move_here](const SessionPlayer& from,
                                                                     std::span<const std::uint8_t> data) {
        std::optional<TeleportMessage> message = decode_teleport(data);
        if (!message || !session.room().cheats) return;
        const std::vector<SessionPlayer> players = session.players();
        const std::optional<SessionPlayer> self = local_player(players);
        if (!self) return;
        std::string by;
        if (session.is_host()) {
            // A moderator's: theirs to ask, and the host says who.
            if (!from.moderator) return;
            message->by = from.id;
            by = from.name;
            if (message->target != self->id) {
                const auto target = std::find_if(players.begin(), players.end(), [&](const SessionPlayer& player) {
                    return player.id == message->target;
                });
                if (target != players.end() && target->id != from.id) {
                    (void)session.send(kTeleportMessage, net::Channel::Reliable, encode_teleport(*message), target->id);
                }
                return;
            }
        } else {
            // Only the host's, for this player.
            if (!from.host || message->target != self->id) return;
            const auto mover = std::find_if(players.begin(), players.end(), [&](const SessionPlayer& player) {
                return player.id == message->by;
            });
            by = mover != players.end() ? mover->name : from.name;
        }
        Position at{};
        if (move_here(message->to, at) == nullptr) chat.say("CoopCmdTpBy", by);
    });
}

}  // namespace galaxy::coop
