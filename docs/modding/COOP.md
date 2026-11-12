# Co-op mods

NebulaCoop's multiplayer calls for mods, modeled on sm64coopdx's Lua API. A co-op mod says `"requires": ["coop"]` in
its luma.json and is packed as a `.lumacoop` (LumaLink does both), so upstream Nebula refuses it.

This is the first part: who is in the room, where they play, and when that changes. Packets, synced variables,
chat and commands come next (the plan: the "Co-op LumaLink API" proposal).

## Players

A room's players go by index: the host is 0, then 1, 2... as they joined. A player keeps their index while they stay,
and a later joiner takes the lowest free one (sm64coopdx's global index). Outside a room the game is a room of one:
this player is index 0 and the host, with no name.

| C (NebulaModApi) | LumaScript | Gives |
|---|---|---|
| `coop_in_room()` | `coop.in_room()` | 1 in a room, 0 outside one |
| `coop_is_host()` | `coop.is_host()` | 1 for the host (and outside a room) |
| `coop_me()` | `coop.me()` | this player's index |
| `coop_count()` | `coop.count()` | how many players the room has now |
| `coop_max_players()` | `coop.max_players()` | how many it takes |
| `coop_connected(i)` | `coop.connected(i)` | 1 when someone has index `i` |
| `coop_name(i)` | `coop.name(i)` | their name, as the player list shows it |
| `coop_character(i)` | `coop.character(i)` | 0 Mario, 1 Luigi, -1 nobody |
| `coop_stage(i)` | `coop.stage(i)` | their stage's name ("" before they reach one) |
| `coop_mission(i)` | `coop.mission(i)` | their mission (0 where none was chosen) |
| `coop_same_place(i)` | `coop.same_place(i)` | 1 when they play in this player's galaxy and mission |
| `coop_stars(i)` | `coop.stars(i)` | Power Stars they got in this room (with the Personal Star Counter on) |
| `coop_ping(i)` | `coop.ping(i)` | round trip in milliseconds (0 for this player or unknown) |

The values are read once a frame, on the game's thread. A C plugin checks a call with
`NEBULA_MOD_API_HAS(api, coop_count)` before using it.

## Events

| Event | LumaScript handler | When |
|---|---|---|
| `NEBULA_MOD_EVENT_COOP_ROOM_JOINED` | `room_joined` | this player is in a room (opened one, or was let in). value: its index |
| `NEBULA_MOD_EVENT_COOP_ROOM_CLOSED` | `room_closed` | this player's room ended |
| `NEBULA_MOD_EVENT_COOP_PLAYER_JOIN` | `player_join` | another player came in. value: index, name: name |
| `NEBULA_MOD_EVENT_COOP_PLAYER_LEAVE` | `player_leave` | another player left. value: the index they had, name: name |
| `NEBULA_MOD_EVENT_COOP_PLAYER_PLACE` | `player_place` | a player (this one too) reached another galaxy or mission. value: index, name: stage |

On joining, the players already in the room don't each get a `player_join`: `room_joined` comes, then a
`player_place` for each player already in a stage. These events only run while a mod with code is loaded.

```
var others: int

on room_joined(me) {
    others = 0
    let i = 0
    while i < coop.max_players() {
        if coop.connected(i) && i != me { others = others + 1 }
        i = i + 1
    }
}

on player_join(index, name) { others = others + 1 }
on player_leave(index, name) { others = others - 1 }
```

The `coop.*` names in LumaScript need LumaLink-coop's compiler and the LumaCore toolkit regenerated with these rows
(tools/lumacore/gen_lumacore.py names a `coop_<name>` field's row `coop.<name>`).
