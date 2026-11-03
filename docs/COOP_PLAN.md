# Online co-op plan

Goal: several players in the same galaxy over the internet, each with their own Mario (or Luigi), in the spirit of
[sm64coopdx](https://github.com/Mackery6969/sm64coopdx). Co-op is online only: each player runs Nebula on their own
PC, and games are self-hosted (see [Hosting](#hosting)), with no central service run by the project. This document
records what the codebase gives us today, the approach chosen, and the order of work. It is a plan, not a description
of shipped features.

## What we start from

| Area | Today | Consequence for co-op |
|---|---|---|
| Player | One `MarioActor`, held by `MarioHolder`, reached through `MarioAccess::getPlayerActor()` and `MR::getPlayer*` | Every enemy, object, camera and area check assumes one player |
| Player call sites (`native/petari/src`) | 616 `MR::getPlayer*` (308 of them `getPlayerPos`), 504 `isPlayer`, 208 `isNearPlayer`, 187 `MarioAccess`, 42 files using `MarioActor*` | A second *real* `MarioActor` is a long refactor; remote players must start as something lighter |
| Input | Each Nebula instance drives one Wii Remote, channel 0 (`native/host/host_main.cpp`, `gModule.wpad_report(0, ...)`) | Nothing to change: every player controls the local Mario on their own PC with their own controls |
| Co-Star | Removed. The native WPAD layer (`native/sdk/wpad/wpad_native.c`) ignores the host on channels 1-3, so a second player's star pointer never connects | The game's 2P code (pointer assists, 2P jumps, `Manual2P`, 2P guidance) stays in Petari but can't run. Online co-op replaces it |
| Simulation | The game's threads run on one host thread under the SDK's scheduler (`docs/ARCHITECTURE.md`) | Same-frame state is easy to read and write from one place; a network tick fits next to `nebula_game_vi_retrace` |
| Host/game boundary | Plain C interface, `native/include/nebula_game_api.h` (version 33: `coop_player` added in 32, `coop_menu` in 33) | Netcode lives in the host; the game module gets a small set of new `nebula_game_*` / `NebulaHostServices` entries |
| Networking | None | Sockets, the protocol, the lobby and the UI are all new |

## Approach: state sync with remote player actors (sm64coopdx's model)

sm64coopdx doesn't run lockstep. Each client simulates its own Mario fully, sends its state (position, rotation,
animation, action, health) several times a second, and draws the other players as remote Marios driven by that state.
Shared things (coins, stars, enemies, switches) are synced with per-object packets and a host that settles conflicts.

We use the same model, for these reasons:

- Lockstep or rollback needs a bit-identical simulation on every machine. Nebula can't promise that across
  compilers, CPUs, frame interpolation and asynchronous disc reads.
- The local Mario stays exactly as Petari wrote it. Nothing in `Game/Player` changes to get the first version working.
- sm64coopdx shows the model holds up in a 3D Mario game with dozens of players.

**The server runs everything but the visuals.** The host (later a dedicated server) decides all of the game's state:
the save, collectibles, objects, enemies, timers, who is where. A client moves its own player and draws; everything
else it shows is what the server sent. So nothing a player opens stops the world: the pause menu, the settings and
the other menus show over a game that keeps running, as sm64coopdx's do, and the pause menu no longer pauses.

## Hosting

Games are self-hosted. There is no lobby server, relay or account system run by the project.

- **Listen server (first).** One player chooses Host in Nebula, which opens a UDP port (configurable, default to be
  picked). The other players choose Join and enter `address:port`. The host runs the session (who's in, the
  save); each instance's world is settled by a player inside it (see
  [Instances](#instances-one-per-galaxy-and-mission)). This is how sm64coopdx's direct connections work, without its
  CoopNet relay.
- **Reaching the host.** The host forwards the UDP port on their router, or the group uses a VPN overlay such as
  Tailscale, ZeroTier or Radmin VPN and joins by its address. The docs explain both; Nebula itself does no NAT
  traversal.
- **A relay (maybe, much later).** sm64coopdx's CoopNet lets players join by a code without forwarding ports. Galaxy
  could have the same, but it needs a server someone runs; it's a side idea, only worth it if the project grows.
- **Dedicated server (later).** The world simulation *is* the game, so a server with nobody playing on it needs a
  headless Nebula: the game module running with no window, renderer or audio output, and no local Mario (or a hidden
  one). That's a real but separate project after Phase 2. The protocol is designed so the authority doesn't have to be
  a player, so a dedicated server can slot in later without changing clients.
- **Server settings.** The `coop_*` keys in `runtime_settings.ini`, so a dedicated server can be configured without a
  menu: `coop_port` (default 7777), `coop_max_players` (2-16, default 8), `coop_player_name`, `coop_password`, and the
  [player collision](#player-collisions) options `coop_player_interaction`, `coop_knockback_strength` and
  `coop_pvp_mode`, and `coop_cheats`. They're written only once they differ from the defaults. The host's collision options and save are
  the session's, as in sm64coopdx.

## Instances: one per galaxy and mission

As in sm64coopdx, players only share a world when they're in the same place *and* on the same mission. An instance is
keyed by the stage name and the scenario (`MR::getCurrentStageName()`, `MR::getCurrentScenarioNo()`): two players in
Good Egg Galaxy on "Dino Piranha" share an instance; one on "Dino Piranha" and one on "A Snack of Cosmic Proportions"
don't. The Comet Observatory (and the other hub/demo stages) is an instance too, keyed the same way.

- **Visibility.** A client draws remote players, and receives their snapshots, only for its own instance. A joiner
  sends its snapshot to the host alone, which passes it on (the session's message 33) only to the players whose place
  is the same instance; the host sends its own the same way. So a room's traffic grows with how many players share
  each instance, not with the room's size, and a joiner uploads one snapshot however many are with it. Players in
  other instances don't appear, can't be touched and don't touch anything. They still show in the player list with
  the galaxy and star they're on.
- **Authority.** The server (the host, later a dedicated server) keeps every instance's record and settles it, as
  well as the session (joins, kicks, the save): what a player's game reports is news only once the server takes it
  (first come wins), and the server tells it to who it concerns. This replaces sm64coopdx's way of the level's
  lowest-index player settling it (`get_network_player_smallest_global()`). The server keeps the record of an
  instance only while someone is in it; running an instance nobody plays in is the dedicated server's later work.
- **Joining an instance.** As a player's scene is placed it tells the server its instance and gets the instance's
  record (what is gone from it) while the scene still loads, like sm64coopdx's `packet_level_area_request` /
  `packet_level_area_inform`. An instance nobody else is in starts fresh from the stage files, as sm64coopdx's level
  does once everyone left; so a player alone whose scene starts over (a miss) finds its coins back, as in the game
  alone, while one with company finds them as the others left them.
- **Free movement.** Nobody is pulled along to another galaxy. Each player picks galaxies and stars from their own
  dome and star select, and leaves an instance by finishing, dying out or exiting.
- **Who is where.** The dome's galaxy select names the other players in each galaxy under its miniature, and the star
  select names those on each star under it (`NebulaSelectPlayers`, from `coop_list`), as sm64coopdx's act select puts
  a player's head over the act they play. Up to three names in their character's colour, then how many more.
- **Zones.** A galaxy's zones are loaded into the same scene, so the whole galaxy is one instance, with no
  sub-instance per zone or planet.

## Main menu

The file select goes. In its place is a co-op main menu like sm64coopdx's (its `djui_panel_main.c`,
`djui_panel_host.c`, `djui_panel_join_direct.c`), over the file select's space backdrop. The title screen stays in
front of it, as its own screen.

- **Where.** The `FileSelect` stage stays for its backdrop and music, but its `FileSelector` skips the six file
  bubbles and shows the menu instead. Boot goes from the logos to the game's title (`exeTitle`: its logo, music and
  Press A and B), then to the menu, without the Mii windows. The title shows once, as Nebula starts (game API 43's
  `NEBULA_COOP_MENU_TITLE`): back from a room the menu opens at once, and so does a menu that starts a room by itself
  (`--host`, `--join`, a restart for the room's mods).
- **The game's look.** It keeps Super Mario Galaxy's feel: the file select's space backdrop and music, and the
  game's own windows, buttons and font. The menu's window fills the screen as the settings menu's does (the title
  logo above it took too much room).
- **How it's drawn.** With Nebula's settings menu machinery: pages and rows kept by the host
  (`NebulaSettingsView`), drawn by the game in its own windows and font (`NebulaSettingsMenu`). The main menu is a new
  set of pages. Text rows (address, name, password) are new: typed on a keyboard, or picked letter by letter on a
  controller.
- **Pages:**

  | Page | Rows |
  |---|---|
  | Main | Host, Join, Options, Quit; Nebula's version in a corner |
  | Host | Save (any number, each with its name and Power Stars; New, Rename and Erase), port, password, max players, Player Interaction, Knockback Strength, PvP Mode, Cheats, then Host and Back |
  | Join | Address (`host[:port]`), password, then Join; the last address is remembered. No public or private lobbies: there's no relay |
  | Options | Nebula's settings pages, with PLAYER in the MODS page's old place: name, character (Mario or Luigi, as sm64coopdx's player options pick a model). MODS is the last page |
  | Quit | "Quit to desktop?" yes or no |

- **Saves: any number, no characters.** The game's own format (`GameData.bin`: six user files, each with a Mario
  and a Luigi half) stays inside, but the player never sees it. Each Nebula save is a folder of its own
  (`coop_saves/<id>/` in the NAND folder, `galaxy::coop::SaveStore`) holding its own copy of the game's save folder
  and a `save.ini` (its name, the user file it plays, when it was last played). Host points the game's save folder
  (`title/00010000/524d4745/data`) at the chosen save's; the game reads it again from a new console's data
  (`SaveDataHandleSequence::startReload`, so a new save keeps nothing of the last one), then plays its user file,
  made first when new. A new save plays user file 1. On the first start, the old `GameData.bin`'s used files
  become saves of their own (`FILE 1`, `FILE 3`, ...), each playing its old file; the old folder stays as it was.
- **Achievements: the save's.** Each save keeps its own `achievements.ini`; an achievement unlocked in a save is
  every player's who plays in it ([Shared world state](#shared-world-state)). The old shared `achievements.ini` is
  copied into each converted save.
- **Character: the player's, not the save's.** `MR::isPlayerLuigi()` (today `!isDataMario()`, the save's half)
  follows Options' PLAYER page's character (`coop_character`, `mario` or `luigi`) instead, so the same save plays as
  either. Remote players show each player's own character (`NEBULA_COOP_PLAYER_LUIGI`).
- **Starting.** Host loads the chosen save through the file select's own path (`FileSelector::exeDemo`) and opens
  the session. With nobody joining it's single player, as in sm64coopdx. Join needs Phase 1: a joining player plays
  the host's save, sent at join (sm64coopdx's `packet_save_file`), so their own saves stay untouched.
- **Dropped with the file select:** its six files, the Mario and Luigi halves, copying files, and Miis, which only
  ever named and pictured files: the file icons, Nebula's Mii import (Settings > GENERAL > IMPORT MIIS, the `miis`
  folder) and the wait for the Mii data before a file starts. The player's name is Options' PLAYER page's.
- **Portable.** The menu's model (pages, rows, saves, keys) is host code shared by every host
  (`runtime/src/coop_menu.cpp`), not the Windows window's, as the settings menu's model is now too
  (`runtime/src/gx/native_settings_*.inl`, shared by the Windows window and the SDL host). The game draws it with
  the settings menu's screen and turns the remote's buttons into its input, so it works with whatever controls the
  player has on every platform.

Order: first the menu shell (Main, Host with a save file, Options, Quit) replacing the file select, so the
game plays as before through it; then text rows, the Player page and the Host page's server rows; then Join with
Phase 1.

## Mods: the room's, not the player's

Mods work as in sm64coopdx (`djui_panel_host_mods.c`, `packet_mod_list.c`, `packet_download.c`): they're a room
setting, not each player's own.

- **The host picks them.** The Host page gets a Mods row listing the mods in the `mods` folder (Riivolution mods,
  disc files, texture packs, movies) to turn on for this room, in order. The room plays with exactly those.
- **Joiners get the host's.** On joining, a player receives the room's mod list. Mods they already have (same name
  and hash) are used as they are; the rest are downloaded from the host into a cache before the game starts, and
  turned off again when they leave. A player's own enabled mods don't apply in someone else's room.
- **Only what changes the game is the room's.** Riivolution mods, game files and loose files are shared; texture
  packs and HD movies stay each player's own (sm64coopdx keeps its DynOS model and texture packs per player too), so
  nobody downloads gigabytes of textures to join. A mod with both is shared whole.
- **No mods page outside a room.** The settings menu's MODS page and the launcher's Mods tab stop turning mods on
  for every game: importing and removing mods stays, choosing them moves to the Host page.
- **Mods apply at the start.** Nebula applies mods when the game starts (the virtual disc), so a room's mods are
  set before the joiner's game loads the galaxy, and changing them means restarting the room.

How a join goes (`galaxy/coop_mods.h`, `galaxy/coop_session.h`), as sm64coopdx's mod list and downloads do:

1. The joiner asks to be let in with its game version, its disc (boot.bin's game id and revision, and a hash of
   main.dol and the file table) and the mods it plays now (each one's id, a hash of its files and its choices).
2. Another version or another disc is refused with the host's (`THE HOST PLAYS ANOTHER DISC (RMGE01 REVISION 0)`;
   the same release with another hash is a changed or bad dump).
3. Other mods: the host sends the room's list with each mod's files. The joiner uses its own copy of a mod with the
   same files, else one already in its cache (`coop_mods/<hash>/` beside the mods folder), and downloads the rest from
   the host a piece at a time (60 KiB, four ahead, on the ordered channel), checked against the mod's hash before it
   is kept. It isn't a player while it downloads, so nobody sees it; the Join page shows how far it is.
4. With every mod here, Nebula starts again with exactly the room's mods (`mods::set_room_mods`, for one start; the
   player's own texture packs stay) and joins again by itself. This time the mods match and the player is let in.
   Leaving the room and hosting later starts Nebula again with the player's own mods.

## The Online tab

Nebula's in-game settings menu (F1, the pause menu's Settings) keeps its pages, but ADVANCED becomes **ONLINE**:

- **Room.** The session's settings: Player Interaction, Knockback Strength, PvP Mode, Cheats and the player list.
  The host changes them for everyone, like sm64coopdx's in-game host settings; other players see them.
- **No Leave game.** The way out of an online game is the game's own: the pause menu's Quit (in the Observatory, or
  before it opens) returns to the main menu, and back there the session ends (the host closes the room, a joiner
  disconnects). Only the host's Quit writes the save first: a joiner plays the host's save, the room's, which only
  the host's game writes. A joiner whose host quits is brought back to the main menu with a notice. Closing the
  window also leaves.
- **Cheats.** Today's ADVANCED rows (Galaxy Select: travel to any galaxy and star; Grand Stars: give the save Grand
  Stars 1-6) move here under a Cheats heading. They work only while the room has cheats on (`coop_cheats`, the
  host's choice, off by default), and are shown greyed out otherwise. A host playing alone turns cheats on for
  their own room.
- **From the main menu** (Options, before any game) the whole tab shows greyed out: the room is set on the Host
  page.

The settings menu's model is shared by every host (the Windows window's and the SDL host's on Linux and Android), so
the Online tab is on every platform.

## Remote players

A `RemotePlayer` is a `LiveActor` that loads Mario's or Luigi's model and animations (`MarioAnimator`'s resources) and
plays whatever animation the network names, at the position, rotation and gravity it received, interpolated between
packets. It has no physics or state machine of its own. It collides with the local Mario as the room's
[player collision](#player-collisions) options say; collision with enemies comes later.

- **Snapshots** (`galaxy/coop_snapshot.h`). Each player sends its own (the game's `coop_player` report) 30 times a
  second on the unreliable channel, as the session's message 32, to the players in its instance (a joiner's through
  the host, which passes it on as message 33). It holds the instance, the position, the model's facing and up (its base matrix, the true turn on any planet),
  gravity, velocity, and the animation: its group as Mario's `XanimePlayer` names it, the bck, the frame, the speed
  and the group's track weights (walking into running). An older snapshot than the last is dropped.
- **Drawn 100 ms behind.** The host keeps the last second of each player's snapshots and gives the game
  (`coop_remote`, each scene frame) the others in its own instance as they were 100 ms ago, between the two snapshots
  either side: one lost snapshot doesn't show. A player not heard from for a second isn't drawn.
- **The copies** (`NebulaRemotePlayer`, `native/petari/src/Game/Player`). As a scene starts in a room, it makes one
  copy of Mario and one of Luigi for each other player the room may have (15 of each at most). Each is Mario's or
  Luigi's own model (the other character's loads into the scene) with Mario's animations, played as `GhostPlayer`
  plays the cosmic ghost's. Each player keeps their copy while they stay; unused copies are hidden.
- **Not yet:** the power-up forms (Bee, Boo, Fire...: the copy is the plain model), the upper-body animations
  (holding a shell), sounds and effects. The copies share the model with a local player of the same character, so a
  form's colours on the local player can show on them.

- **Nametags.** Each remote player's name over their head, in the game's font, hidden when they're behind a wall or
  far away (sm64coopdx's nametags). `NebulaNametags`, drawn with the HUD: the name in the character's colour on a
  small see-through window 200 units up the copy's up; it shrinks past 1200 units (to 0.6 of its size), and fades out
  past 6000, off the screen, behind the map (a line from the camera to the head) and in the game's cutscenes.
- **Player list.** Held Tab on a keyboard, or the controller's Share (Back/Select/View) button: every player in the
  room, their character, and the galaxy and star they're on, as sm64coopdx's player list. It shows over play and
  doesn't stop it. The controller's button is the Wii Remote's Minus (Share's default binding): in a room Minus holds
  the list instead of pausing, and Plus (Start) pauses. Each player tells the room where it plays as that changes
  (the session's own message, sent with the room to who joins later), so the list has every player's place before
  remote players exist; the game names the galaxy and star in the played language (`NebulaPlayerList`,
  `coop_list`). The round trip to each player shows beside them, and the Power Stars they got in the room before it
  ([Personal Star Counter](#personal-star-counter)).

## Chat

A text chat as sm64coopdx's, keyboard only: T opens a line at the screen's bottom left (not Enter, which is the
keyboard's A), Enter sends it to everyone in the room, Escape drops it. The last few messages show over play and fade
after ten seconds, each with the sender's name in their character's colour; joins and leaves show there too. It
doesn't stop the game, and while the line is open the keyboard types instead of playing. A controller has no way to
type, so it only reads the chat.

The host keeps it (`runtime/src/coop_chat.cpp`): a line is a session message holding only its text (printable ASCII,
at most 100 characters), so its sender's name is the session's and can't be faked. A player's lines past five in three
seconds are dropped. The window layers send T to it while the host allows it (a room's game with no menu over it), and
the game draws what `coop_chat` gives it (`NebulaCoopChat`, the game's window and message font).

## Personal Star Counter

sm64coopdx's Personal Star Counter mod (`personal-starcount-ex.lua`), built in. Galaxy's HUD shows the save's Power
Stars when the player stands still (`StarCounter`, with the coins and Star Bits); two more counters come in under it
with it, the same layout with the star tinted as the mod's (`NebulaStarCounter`, from `CounterLayoutController`):

- **Red: this room's.** The Power Stars this player got since they hosted or joined. Only in a room.
- **Green: every one.** All the Power Stars this player ever got, kept across saves with Nebula's other all-time
  counts (`power_stars` in `stats.ini`); the achievement board's stats page lists it too.

As in the mod, a star counts only when it's new to the save (the mod counts when the save's star count goes up), so a
star the room already has counts for nobody again; a Grand Star counts too. The host counts them, as it decides the
save ([Shared world state](#shared-world-state)): a Power Star a player's game puts in its save comes to the host's
`galaxy::coop::World`, and one that is news to the session's save counts for the player who sent it
(`Session::credit_star`). The host tells everyone the new count (a session message of ten bytes, only as a count
changes) and sends everyone's with the room to who joins later; the game reads its own with `coop_star_count`. The
player list shows each player's beside their round trip, as the mod puts it in sm64coopdx's player list. The green
count is this player's game's own (`NebulaAchievements::onStarGet`, as a star new to its save starts its get sequence).

## Cutscenes and transitions

As sm64coopdx has them: a cutscene is the player's own, the save's progress reaches everyone without its cutscene,
and only what ends a chapter takes the others along. Nobody gets stuck, because whatever moves a player on still
reaches each one.

| What | Galaxy | As sm64coopdx |
|---|---|---|
| A demo, a talk, a star's appearing | Plays for the player who started it. Nobody else is stopped or moved, and their games keep running | Cutscenes and dialogs are the local player's |
| The opening (the Star Festival, Bowser's attack, waking in Gateway) | Plays for each player on a save that hasn't passed it. A joiner plays the host's save, so it starts where that save does | The intro plays on a new save |
| A Power Star taken | Shows for the others in the instance as a star already had, as a finished mission's star does. It's theirs in the save; they play on, and can still take it for its get and return | `packet_collect_star`: the star is the save's for everyone, who keep playing |
| A Grand Star taken | Takes everyone in the instance along: each gets its get sequence and its return to the Observatory (a player dead or in a demo goes once play allows, in the same mission). The last Bowser's sends the instance to the ending together | The Bowser 3 Grand Star puts everyone in the level into its cutscene |
| A star the room gave, taken later by the player itself | Returns as its first: the story's demos (Rosalina's talks, the domes, the first trip to the Observatory) play for that player, not the short return for a star already had | |

- **Messages.** A star taken is the session's message 39 (`kStarGotMessage`): the taker tells the host, which tells
  the others in the same instance, each for its current scene. Nothing is kept, so a player arriving later still
  finds the star and can take it, as in sm64coopdx. Game API 42's `NEBULA_COOP_WORLD_STAR_GOT`.
- **The game.** `NebulaCoopDemos` (native/petari): `GameScene::stageClear` tells of the local player's star,
  `GameScene::update` shows the others' Power Stars as had (`PowerStar::showAsHad`) and plays a Grand Star's get once
  play allows.
- **First returns.** The result's demos read the save as the scene started (`getSceneStartGameDataHolder`): a star
  already had there plays the short return. Stars the room gave this session are noted as they come, and when the
  player takes one, it is left out of that save just before the result (`GameSequenceProgress::requestGalaxyMove`),
  so the story's demos for it play. Stars already in the save when the player joined are the save's, as in a game
  continued alone.
- **Left as the room's.** Demos tied to the save's star count (the comet lecture at 13 stars, a galaxy appearing)
  play for the player whose star reaches the count; the others get what they open without the demo. Each player's
  story progress (`GameDataPlayerStatus`) is its own and isn't synced, so the lectures it gates (the dome, the star
  bits) still play for each player the first time they get there.

## Player collisions

The same handler and options as sm64coopdx (`interact_player` / `interact_player_pvp` in its `src/game/interaction.c`).
The host's choice is the session's and is sent to each player as they join, as sm64coopdx's `packet_join` does.

| Option (`runtime_settings.ini`) | Choices | Default |
|---|---|---|
| Player Interaction (`coop_player_interaction`) | `nonsolid`: players pass through each other. `solid`: they push apart, bounce on each other's heads and knock one another back with attacks, with no damage. `friendly_fire`: solid, and attacks cost health too | `solid` |
| Knockback Strength (`coop_knockback_strength`) | 0-255; the menu offers Weak 10, Normal 25, Too much 60 | 25 |
| PvP Mode (`coop_pvp_mode`) | `classic` or `revamped`: the rules for which attack beats which, and how far each one throws | `classic` |

The rules carried over, with SMG's equivalents (`NebulaRemotePlayer`, game API 42's `coop_rules`):

- **No contact** in a player's first 60 frames in an instance, or while either player is intangible: dead, in a demo,
  bound to something (a Launch Star, a bubble...), a Boo, hidden or out of the player's control
  (`NEBULA_COOP_PLAYER_INTANGIBLE` in the snapshot). Non-solid turns the copies' body sensors off, so they cost nothing.
- **Solid.** Each copy has a body sensor, as the cosmic ghost has, that pushes the local Mario out of it. His stomp on
  a copy bounces him off, as off an enemy. A player landing on the local Mario's head squashes him a moment (the
  no-damage flinch, sm64coopdx's `bounceSquishTimer`), with no damage.
- **Attacks (solid and friendly fire).** A spin or a ground pound that reaches another player throws them away from
  the attacker by the knockback strength times the attack's scale (sm64coopdx's `scaler`: a ground pound 2 as its
  kick, a spin 1 as its punch; a Rainbow Star's player throws whoever they touch at 2). Galaxy runs at 60 frames a
  second, so the strength is halved into Galaxy's speeds. Solid throws without harm (Galaxy's no-damage flip); friendly
  fire is an enemy's hit and costs health. A player already being knocked back, or invincible, isn't hit again until
  it ends. Revamped: a spin staggers a player on the ground on the spot (its punch), two spins meeting cancel out, a
  ground pound throws at 1.9 and its landing reaches 1.3 times as far round.
- **Who decides.** As sm64coopdx: the victim's game. Each snapshot says whether its player spins or ground pounds; the
  victim's game checks that against where its own Mario was when the attacker saw him (`rewind_ms`: the round trip
  between the two and both ways' drawing delay, sm64coopdx's `lag_compensation_get_local_state`) and moves only its
  own Mario. No message is sent for a hit, so collisions add nothing to a room's traffic.
- Not yet: a Fire Mario's fireballs and other thrown things, which other players' games don't draw.

## Shared world state

The server decides everything but the visuals (`galaxy/coop_world.h`, the game's `NebulaCoopWorld`, game API 40's
`coop_world_report` and `coop_world_take`). Each player's game reports what its own play did; the server settles it
and tells the players it concerns. It follows sm64coopdx's rules, with Galaxy's things in place of SM64's:

| What | Who it reaches | As sm64coopdx |
|---|---|---|
| A coin taken | Gone for everyone in the instance, and counted for each of them (without the life or 1-Up it gives its taker) | `packet_collect_coin` |
| A purple coin taken | Gone for everyone in the instance, and counted toward the 100 for each of them, so the star appears for all | `packet_collect_coin` |
| A star bit, a 1-Up or Life Mushroom, a Rainbow Star taken | Gone for everyone in the instance; what it gives is its taker's | `packet_collect_item` |
| A plain enemy defeated | Gone for everyone in the instance, its dead switch turned on in each game (a star waiting on it appears for all), without its drops | an object's death in `packet_object.c`, kept by `packet_level_respawn_info` |
| A Power Star got, an event flag on | The session's save, for every player | `packet_collect_star`, `packet_save_set_flag` |
| An event value set (comets, the like) | The host's game's are the session's, for every player; each joiner's stay its own | |
| An achievement unlocked | The save's: kept in the host's `achievements.ini` on the day it was first unlocked, and every player's, each with its pop-up | |

- **What is followed.** The things placed from the stage's files that these name: coins and purple coins, alone or in
  rail and circle groups; star bits alone or in groups; 1-Up and Life Mushrooms and Rainbow Stars; the plain enemies
  (`cEnemyNames` in `NebulaCoopWorld.cpp`: not launchers or generators, bosses, or enemies that come back by
  themselves). Each has a key from its placement (zone, name, link id and place), the same in every player's game;
  things spawned while playing (an enemy's coins) aren't followed.
- **Gone stays gone.** A thing the server says is gone is made dead quietly in each game and kept dead if the game
  brings it back, while anyone is in the instance (sm64coopdx's respawn info). A scene's first half second isn't
  reported: the stage's own setup kills things then.
- **The save.** The save each joiner plays from (sent at the join, galaxy/coop_session.h) is brought up to date with
  the session's changes, which the server keeps; each scene start gives them all to the game again, which applies
  what it lacks (a scene started over may have dropped what came during it). The host's save on disk is written as its
  game saves.
- **Achievements.** A joiner gets the save's `achievements.ini` with the rest of the save as it joins, so the board
  shows what the save has and nothing pops up for it. While the room plays, an achievement any player's game unlocks
  goes to the server as a save change (`SaveKind::Achievement`, with when); the server keeps the first and tells every
  player. Each player's host unlocks it in its copy of the save's achievements on that day, and the game shows its
  pop-up (game API 43's `coop_achievement_take`); the goal met later in that player's own game shows nothing again.
  Achievements are the host process's, not the game's: a scene start doesn't give them again. The headless host keeps
  none.
- **Later (Phase 3).** Objects' movement and state (enemies walking, platforms, switches) and which player each enemy
  targets: sync objects as sm64coopdx's `sync_object.c`, each owned by the server.

## Phases

Each phase ships on its own and leaves the game playable.

### Phase 0: groundwork (no networking)

- [x] The `coop_*` settings keys, the player collision options among them (`runtime/src/runtime_settings.cpp`, with
      `parse_coop_endpoint` for addresses), and
      the windowed hosts' `--host <port>` / `--join <address[:port]>` options. Both are checked, then the game starts
      alone with a `[coop]` log line until Phase 1.
- [x] Co-Star removed: the native WPAD layer takes channel 0 only.
- [x] `native/tools/coop_inventory.py` lists every single-player assumption (`MR::getPlayer*`, `MR::isPlayer*`,
      near-player checks, `MarioAccess`, `MarioActor*`, the demo actor) by subsystem and kind; `--sites [kind]` lists
      each one, so later phases can tick them off. Today: 1,453 sites, 428 of them in `Game/Enemy`.
- [x] The local player's snapshot, once per game scene frame: `NebulaCoopPlayer` through the `coop_player` host
      service (API version 32), filled in `GameScene::update` with the instance (stage and scenario), position,
      rotation, gravity, velocity, animation name and frame, health, power-up form, Luigi and a miss in progress. The
      host keeps the latest one for the netcode and logs each change of instance (`[coop] instance ...`).
- [ ] Remote player actors: spawn, move and remove copies of other players from their snapshots.

### Phase 0.5: main menu

- [x] The [main menu](#main-menu) shell replaces the file select: Main, Host, Options, Quit, over the file
      select's space backdrop, with the pointer, D-pad, stick, A and B.
- [x] The title screen before the main menu, as its own screen, once as Nebula starts.
- [x] Saves: any number, each a folder of its own, with its own achievements; New and Erase on the Host page; the
      existing `GameData.bin` converted; Rename on the Host page.
- [x] The Host page: the save, max players, Player Interaction, Knockback Strength, PvP Mode and Cheats
      (`coop_cheats`), Host and Back.
- [x] The pause menu and Nebula's menus (settings, achievement board) leave the game running, without the
      player's input; a miss or a demo closes the pause menu.
- [x] ~~The game's logo above the menu, as on the title screen.~~ Dropped: it took too much room.
- [x] The Player page: name and character per player (`coop_character`), not per save; in Options (its PLAYER
      page, where MODS was), not on the main menu.
- [x] Text rows (name, address, password; the keyboard, or letter by letter on a controller); the Host page's port
      and password; the Join page's address (`coop_join_address`); renaming a save.
- [x] Mods as a room setting: the Host page picks them (a restart when the disc's mods change); the MODS page only
      imports and removes.
- [x] Joiners download the host's mods (with online play): the ones that change the game, checked by hash, then
      Nebula starts again with them and joins.
- [ ] The launcher's Mods tab only imports and removes.
- [x] Settings: ADVANCED becomes ONLINE (room settings, Cheats); Galaxy Select and Grand Stars work
      only with the room's cheats on (`coop_cheats`). On every host, as the in-game settings menu is.

### Phase 1: ghosts

- [x] Socket layer in `runtime/src/net/` (UDP, Windows and POSIX, the SDL host included) with reliable/ordered
      channels on top. sm64coopdx's `packet_reliable.c` / `packet_ordered.c` are a reference for the design. See
      [The network layer](#the-network-layer).
- [x] Host and Join (`address:port`) on the main menu and on the command line (`--host <port>`, `--join
      <address[:port]>`); an optional password. The pause menu's Quit leaves the room, or closes it for everyone as
      its host (only the host's Quit saves).
- [x] Each client sends its player snapshot at 30 Hz. Others draw it as a `RemotePlayer` with no collision
      (a "ghost"): `NebulaRemotePlayer`, from `coop_remote` (API version 38).
- [x] Each player's name over their head (nametags); the player list held on Tab or the controller's Share button.
- [x] Version, disc and mods check as a player joins: another version or disc is refused; other mods get the room's
      (see [Mods](#mods-the-rooms-not-the-players)).
- [x] [Chat](#chat), keyboard only; controllers read it.
- [x] The [Personal Star Counter](#personal-star-counter): each player's stars this room and all time, on the HUD and
      in the player list (API version 41).

### Phase 2: same world

- [x] [Cutscenes and transitions](#cutscenes-and-transitions) as sm64coopdx's: each player's own, a Grand Star takes
      the instance along, a Power Star taken shows as had for the others, and a star the room gave returns as its first.
- [x] Instances: each client reports its stage and scenario, sees only players in its own instance (snapshots go
      only there), and a player entering an occupied instance gets its state from the server.
- [x] The galaxy select and the star select show who plays in each galaxy and star ([Instances](#instances-one-per-galaxy-and-mission)).
- [x] Collectibles, defeated enemies, Power Stars and save flags sync, with the host authoritative
      ([Shared world state](#shared-world-state)).
- [x] [Player collisions](#player-collisions): non-solid, solid and friendly fire, knockback strength and PvP mode,
      with lag compensation.
- [ ] Remote players stomp, spin and touch objects. Hits are sent as events to the client that owns the object.

### Phase 3: shared enemies and objects

- [ ] Per-object packets on top of Phase 2's placement keys (`NebulaCoopWorld`), each object owned by the server.
      Begin with simple enemies (Goomba/Kuribo, Octoomba), then platforms and switches.
- [ ] Each enemy targets the *nearest* player instead of `MR::getPlayerPos`. This is where the call-site inventory
      gets used: route `MR::getPlayer*` through a "player relevant to this actor" helper, one subsystem at a time.

### Phase 4: bosses, demos, polish

- [ ] Boss and story demos shared by everyone in the instance (the boss is the server's, Phase 3); until then each
      player's are its own (see [Cutscenes and transitions](#cutscenes-and-transitions)).
- [ ] Camera and HUD per player; spectate on death.
- [ ] Discord presence for the lobby (`discord_presence.cpp` already exists), player colours.

## The network layer

Shaped like sm64coopdx's CoopNet, over direct UDP (no relay: see [Hosting](#hosting)). Four layers, each with its
tests in `runtime/src/net/net_tests.cpp`:

- **Sockets** (`galaxy/net/udp.h`): one non-blocking UDP socket, dual-stack IPv6 where the system allows, Winsock on
  Windows and POSIX sockets on Linux, Android (the app has the `INTERNET` permission) and macOS.
- **Channels** (`galaxy/net/link.h`): unreliable (snapshots), reliable (acknowledged and sent again, delivered once)
  and ordered (reliable and in order; long messages such as a save or a mod are cut into datagrams and joined again,
  with 256 datagrams in flight at most). The resend timeout follows the measured round trip; a message unacknowledged
  for 15 seconds breaks the link.
- **Transport** (`galaxy/net/transport.h`, `galaxy/net/direct_transport.h`): CoopNet's model. The host creates a
  lobby; members join it, each gets a peer id, and everyone hears of members joining and leaving through callbacks
  (`on_lobby_joined`, `on_peer_joined`, `on_peer_left`, `on_receive`, `on_error` with CoopNet's errors: lobby full,
  wrong password, another protocol, a peer that stopped answering). The direct transport admits members on the host's
  port and passes their messages on, so only the host's port needs forwarding. A relay that joins by a code would be
  another `Transport`, with nothing above it changing.
- **Session** (`galaxy/coop_session.h`): the room as the game sees it, on a thread of its own. A joiner asks to be let
  in with the game's version, its disc and mods, their name and character (a joiner with other mods gets the room's
  first: see [Mods](#mods-the-rooms-not-the-players)); the host answers with the player's number, the room's
  settings, its save (sm64coopdx's `packet_join` and `packet_save_file`) and who is playing, and tells everyone else.
  The joiner plays the host's save from `coop_joined/` under the NAND folder, so their own saves stay untouched.
  A player who renames themselves on Options' PLAYER page while in a room tells it at once (a joiner's new name goes
  to the host as the session's message 11, and the host passes it on to everyone), so the player list, nametags and
  new chat lines show it, as sm64coopdx's player settings do. The character stays the one they joined with.
  Later features send their own message types with `Session::send` and `Session::on_message`.

## Licensing

Nebula is GPLv3 and Petari is CC0. The sm64coopdx repository has no license file, so its code can't be copied in.
Use it as a reference for protocol and design ideas only, and write Nebula's netcode from scratch.

## Open questions

- Transport: raw UDP with our own reliability layer, or an existing library (ENet, GameNetworkingSockets, both
  permissive) vendored under `third_party/`?
- Saves: the host's save is the session's (as in sm64coopdx). Do joining players' own saves stay untouched, or do
  they get a copy of what they collected?
- Android: should the SDL host get co-op from Phase 1, or later?
