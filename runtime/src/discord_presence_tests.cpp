// Discord Rich Presence: the IPC frames and JSON the worker sends (galaxy/discord_presence.h).
#include "galaxy/discord_presence.h"

#include <cstdio>
#include <optional>
#include <string>

namespace {
int s_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++s_failures;
    }
}
}  // namespace

int main() {
    using namespace galaxy::discord;
    // Frame: opcode and length little-endian, then the JSON.
    const std::string f = frame(1u, "{}");
    expect(f.size() == 10u, "frame size");
    expect(f[0] == 1 && f[1] == 0 && f[2] == 0 && f[3] == 0, "frame opcode");
    expect(f[4] == 2 && f[5] == 0 && f[6] == 0 && f[7] == 0, "frame length");
    expect(f.substr(8) == "{}", "frame body");

    expect(handshake_json("123") == "{\"v\":1,\"client_id\":\"123\"}", "handshake");

    // Escaping: quotes, backslashes, control characters; UTF-8 (the game's accented names) passes through.
    expect(json_string("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"", "escape");
    expect(json_string("\x01") == "\"\\u0001\"", "control escape");
    expect(json_string("Galaxie \xC3\xA9toil\xC3\xA9" "e") == "\"Galaxie \xC3\xA9toil\xC3\xA9" "e\"", "utf-8 kept");

    const Activity activity{"Good Egg Galaxy", "Dino Piranha", {}};
    const std::string set = set_activity_json(42u, activity, 1000, 7u, false);
    expect(set.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos, "cmd");
    expect(set.find("\"pid\":42") != std::string::npos, "pid");
    expect(set.find("\"details\":\"Good Egg Galaxy\"") != std::string::npos, "details");
    expect(set.find("\"state\":\"Dino Piranha\"") != std::string::npos, "state");
    expect(set.find("\"start\":1000") != std::string::npos, "timestamp");
    expect(set.find("\"nonce\":\"7\"") != std::string::npos, "nonce");

    // An empty line is left out; clearing sends a null activity.
    const std::string menu = set_activity_json(1u, Activity{"Choosing a file", "", {}}, 5, 1u, false);
    expect(menu.find("\"state\"") == std::string::npos, "empty state omitted");
    expect(set_activity_json(1u, {}, 0, 2u, true).find("\"activity\":null") != std::string::npos, "clear");

    expect(set.find("\"party\"") == std::string::npos && set.find("\"secrets\"") == std::string::npos,
           "no party outside a room");

    // A room: its party with the count, and the Join button's secret only with an id.
    Activity room{"Good Egg Galaxy", "Dino Piranha", {party_id("K7QX2M"), 2u, 8u, join_secret("K7QX2M", "")}};
    const std::string in_room = set_activity_json(1u, room, 5, 3u, false);
    expect(in_room.find("\"party\":{\"id\":\"" + party_id("K7QX2M") + "\",\"size\":[2,8]}") != std::string::npos,
           "party");
    expect(in_room.find("\"secrets\":{\"join\":\"nebulanet:K7QX2M:\"}") != std::string::npos, "join secret");
    expect(in_room.find("\"supported_platforms\":[\"desktop\"]") != std::string::npos, "platforms with a secret");
    expect(set_activity_json(1u, room, 5, 3u, false, false).find("supported_platforms") == std::string::npos,
           "platforms left out when Discord refused them");
    room.party = {"", 3u, 4u, ""};
    const std::string direct = set_activity_json(1u, room, 5, 4u, false);
    expect(direct.find("\"party\":{\"size\":[3,4]}") != std::string::npos, "party count alone");
    expect(direct.find("\"secrets\"") == std::string::npos, "no secret without a party id");
    expect(direct.find("supported_platforms") == std::string::npos, "no platforms without a secret");
    room.party = {"x", 9u, 4u, "s"};
    expect(set_activity_json(1u, room, 5, 4u, false).find("\"size\"") == std::string::npos, "a count over the most");

    // The party id is the room's for everyone, and not its code.
    expect(party_id("K7QX2M") == party_id("K7QX2M") && party_id("K7QX2M") != party_id("K7QX2N"), "party id");
    expect(party_id("K7QX2M").find("K7QX2M") == std::string::npos, "party id hides the code");

    // Join secrets: the code and the password (which may hold a colon); anything else is not a room.
    const auto joined = parse_join_secret(join_secret("K7QX2M", "pass:word"));
    expect(joined && joined->code == "K7QX2M" && joined->password == "pass:word", "secret round trip");
    const auto open_room = parse_join_secret("nebulanet:ABCDEF:");
    expect(open_room && open_room->code == "ABCDEF" && open_room->password.empty(), "secret without a password");
    expect(!parse_join_secret("coopnet:123:") && !parse_join_secret("nebulanet::x") && !parse_join_secret(""),
           "not a room");

    expect(subscribe_json("ACTIVITY_JOIN", 9u) ==
               "{\"cmd\":\"SUBSCRIBE\",\"args\":{},\"evt\":\"ACTIVITY_JOIN\",\"nonce\":\"9\"}",
           "subscribe");

    // Frames Discord sends, whole or in pieces.
    std::string buffer = frame(1u, "{\"a\":1}") + frame(3u, "{}").substr(0u, 5u);
    std::uint32_t opcode = 0u;
    std::string json;
    expect(take_frame(buffer, &opcode, &json) && opcode == 1u && json == "{\"a\":1}", "first frame");
    expect(!take_frame(buffer, &opcode, &json), "a part frame waits");
    buffer += frame(3u, "{}").substr(5u);
    expect(take_frame(buffer, &opcode, &json) && opcode == 3u && json == "{}" && buffer.empty(), "frame completed");

    // A Join's dispatch, and the fields read from it.
    const std::string dispatch =
        "{\"cmd\":\"DISPATCH\",\"data\":{\"secret\":\"nebulanet:K7QX2M:p\\\"w\\u00e9\"},\"evt\":\"ACTIVITY_JOIN\","
        "\"nonce\":null}";
    expect(json_field(dispatch, "evt") == std::optional<std::string>("ACTIVITY_JOIN"), "evt");
    expect(json_field(dispatch, "secret") == std::optional<std::string>("nebulanet:K7QX2M:p\"w\xC3\xA9"), "secret");
    expect(!json_field(dispatch, "nonce"), "null is no string");
    expect(!json_field(dispatch, "missing"), "missing field");
    expect(json_field("{\"e\": \"\\ud83c\\udf1f\"}", "e") == std::optional<std::string>("\xF0\x9F\x8C\x9F"),
           "surrogate pair");
    expect(json_field("{\"k\":\"evt\",\"evt\":\"ERROR\"}", "evt") == std::optional<std::string>("ERROR"),
           "a value with the key's text");

    if (s_failures == 0) std::printf("discord presence: all passed\n");
    return s_failures == 0 ? 0 : 1;
}
