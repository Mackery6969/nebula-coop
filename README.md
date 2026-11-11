# Nebula Co-Op

**Online co-op for Super Mario Galaxy on PC.** Nebula Co-Op is a fork of
[**Nebula**](https://github.com/keepvaibin/nebula) by [**keepvaibin**](https://github.com/keepvaibin), the native PC
port of Super Mario Galaxy, that adds online multiplayer in the spirit of
[sm64coopdx](https://github.com/coop-deluxe/sm64coopdx): several players in the same galaxy, each with their own Mario
or Luigi, over the internet.

You need your own copy of the game. Not affiliated with or endorsed by Nintendo.

[Download](https://github.com/NebulaCoop/nebula-coop/releases/latest) ·
[Playing online](#playing-online) ·
[Building from source](docs/BUILDING.md) ·
[Co-op design](docs/COOP_PLAN.md) ·
[Controls](docs/KEYBOARD_CONTROLS.md) ·
[Upstream Nebula](https://github.com/keepvaibin/nebula)

## Credits

- **[Nebula](https://github.com/keepvaibin/nebula), by [keepvaibin](https://github.com/keepvaibin).** Everything that makes the game run on a PC is Nebula's: the host, the renderers, Setup and the launcher, controller and Wii Remote support, mods, achievements and the settings menu. Nebula Co-Op follows it closely and merges its changes in regularly. ([Discord](https://discord.gg/2fJEdhAw2f))
- **[sm64coopdx](https://github.com/coop-deluxe/sm64coopdx)** and its [CoopNet](https://github.com/coop-deluxe/coopnet) are the model for how co-op plays here: rooms, instances, player interaction, the bubble on death, chat commands, pop-ups and the Personal Star Counter. Its code is not used; only its design.
- **[Petari](https://github.com/SMGCommunity/Petari)**, the Super Mario Galaxy decompilation by SMGCommunity, is the
  game code Nebula builds natively.
- Other third-party code and its licenses are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and
  [docs/PROVENANCE.md](docs/PROVENANCE.md).

## Features

### Playing together

- **Rooms of 2 to 16 players** (8 by default). One player hosts and the world runs on their PC; everyone else joins.
- **Join by room code through [NebulaNet](https://github.com/NebulaCoop/NebulaNet)**, Nebula Co-Op's own
  CoopNet-style matchmaker, so nobody has to forward a port. Rooms are private: a password lists them on the Join page's ROOMS, and a room code joins one directly.
- **Or connect directly** to `address:port` (UDP 7777 by default), with a forwarded port or a VPN such as Tailscale,
  ZeroTier or Radmin VPN.
- **Mario or Luigi**, picked per player and switchable live, each with their own animations.
- **Per-mission instances**, as in sm64coopdx: you see and play with the players on the same galaxy *and* star. The
  galaxy and star selects show who is where.
- **Mods are the room's**: the host picks them, and joiners download any they don't have from the host. The game
  version, disc and mods are checked as a player joins.

### A shared world

- The host's game decides the save, collectibles, enemies and objects; the pause and settings menus no longer pause
  the game.
- **Shared enemies and objects**: hits and squashes play for everyone, enemies follow the nearest player, and
  switches and blocks are shared.
- **Coins and star bits** are each player's own, but gone for everyone once taken. Star Chips, the Observatory's star
  bit bank and high scores are shared.
- **Cutscenes and stars**: each player's cutscenes are their own, and a Grand Star takes everyone in its mission along.
- **Boss fights are the room's**: a boss's phases are shared, its opening plays once, and players arriving late join
  the fight in progress.
- **Races** are each player's own race against the rival, with the other players see-through.
- **A mission resets** only once everyone in it has missed or left. A game over sends you back to the Observatory and
  keeps you in the room.
- **Achievements** unlocked in a save are every player's who plays in it.

### Room options

Set on the Host page, and changed by the host from the in-game ONLINE tab:

- **Player Interaction**: non-solid, solid or friendly fire, with **Knockback Strength** and a **PvP mode**.
- **Bubble on Death** (on by default): instead of missing, float in a bubble and fly to another player to get back in.
- **Cheats**, which allow the `/tp`, `/teleport` and `/pos` commands.
- **Personal Star Counter**, sm64coopdx's mod built in: each player's stars this room and all time.

### In the room

- **Nametags** over each player's head, and a **player list** while Tab (keyboard) or Share (controller) is held.
- **Chat**: T opens a line, Enter sends it.
- **Chat commands**, as sm64coopdx's: `/help`, `/players`, `/kick`, `/ban`, `/permban`, `/moderator` and `/confirm`, with Tab completion. See [Chat commands](docs/COOP_PLAN.md#chat-commands).
- **Pop-ups** when players join, leave, change galaxy, die or get a star, each toggled on the PLAYER page.
- **Any number of saves**, each with its own name, Power Stars and achievements. Joiners play the host's save; their own saves are untouched.

### Still to come

From [TODO.md](TODO.md): a relay fallback through TURN when a direct path can't be made, a Discord Join button
carrying the room code, bosses' movement and thrown-object hits shared, story demos with several players, and a
dedicated server.

Everything Nebula itself offers is here too: keyboard and mouse, Xbox, PlayStation, Switch and Steam controllers and
a real Wii Remote; Direct3D 12 and Vulkan; upscalers and anti-aliasing; `.luma` mods; and more. See
[Nebula's README](https://github.com/keepvaibin/nebula#readme) for the full list.

## Requirements

- Your own copy of **Super Mario Galaxy**, revision 0, as an ISO, RVZ or extracted folder: North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01). Everyone in a room needs the same game build and a supported disc.
- Windows 10 (version 1803 or newer) or Windows 11, 64-bit
- A GPU with Direct3D 12 support and a CPU with AVX2
- 8 GB of RAM
- An internet connection to play online

## Install

1. Download `Nebula-Setup.exe` from the [latest release](https://github.com/NebulaCoop/nebula-coop/releases/latest). 
It isn't code-signed, so Windows SmartScreen shows "Unknown publisher": choose **More info**, then **Run anyway**.
2. Select your copy of the game and choose **Install**.
3. Start **Nebula** from the Start menu.

Run Setup again to update, repair or uninstall. Updates come from this repository's releases and are signed; Setup checks the signature before installing.

## Playing online

1. Set your name and character in **Options > PLAYER**.
2. **To host:** choose **Host**, pick or create a save, and leave NETWORK on **NebulaNet** (or pick **Direct** to share your address and port instead). Set a password to make the room findable by it, then choose **Host**. The room code shows in your chat and on the ONLINE tab.
3. **To join:** choose **Join**, then enter the room code, or the host's `address:port`, or type the room's password to find it under ROOMS.

To leave, use the pause menu's **Quit**. Only the host's Quit saves the room's game; if the host leaves, everyone goes back to the main menu.

## Building from source

[docs/BUILDING.md](docs/BUILDING.md) covers the tools, the game module, the host, Setup and the tests. On Windows,
[`setup.ps1`](setup.ps1) checks the tools and builds everything in one go:

```powershell
.\setup.ps1
```

You still need your own copy of the game to run what you build. The co-op design and its progress are in
[docs/COOP_PLAN.md](docs/COOP_PLAN.md) and [TODO.md](TODO.md).

## Related repositories

- [keepvaibin/nebula](https://github.com/keepvaibin/nebula): upstream Nebula, which this repository follows
- [NebulaCoop/NebulaNet](https://github.com/NebulaCoop/NebulaNet): the room-code matchmaker (a Cloudflare Worker)
- [NebulaCoop/lumalink-coop](https://github.com/NebulaCoop/lumalink-coop): LumaLink, which converts mods and texture
  packs to `.luma` files

## Reporting problems

Report co-op problems on this repository's [issue tracker](https://github.com/NebulaCoop/nebula-coop/issues). If the
game closed unexpectedly, attach its crash report from `%LOCALAPPDATA%\Nebula\crashes`. Please don't report co-op
problems to upstream Nebula.

## How it was made

Nebula's host, renderer, installer, tests and documentation were written with AI coding agents (Anthropic's Claude),
directed, reviewed and tested by its author. Much of Nebula Co-Op's co-op code was written the same way. The game's own
logic is the Petari decompilation, compiled for the PC.

## License

Nebula Co-Op, like Nebula, is licensed under the GNU General Public License v3.0 only ([LICENSE](LICENSE)).

"Super Mario Galaxy", "Wii" and "Nintendo" are trademarks of Nintendo.
