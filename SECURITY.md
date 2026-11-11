# Security policy

Nebula Co-Op runs on players' PCs, talks to other players' games over the internet, and updates itself. A bug in any
of those can hurt the people who play it, so please tell us about it privately first.

## Reporting a vulnerability

**Please don't open a public issue, pull request, Discord message or post for a security problem.**

Report it privately through GitHub instead:

1. Go to this repository's [Security tab](https://github.com/NebulaCoop/nebula-coop/security) and choose
   **Report a vulnerability** ([direct link](https://github.com/NebulaCoop/nebula-coop/security/advisories/new)).
2. Say what is affected (the game, Setup and the updater, co-op, NebulaNet or LumaLink), which version, and how to
   make it happen. A proof of concept, a crash dump or a packet capture helps a lot.
3. Say how you would like to be credited, if at all.

A problem in [NebulaNet](https://github.com/NebulaCoop/NebulaNet) or
[LumaLink](https://github.com/NebulaCoop/lumalink-coop) can be reported here too.

If you can't use GitHub, ask a maintainer on the [Discord](https://discord.gg/hNbgFtaRHv) for a private way to send it,
without putting any details in the server.

## What happens next

Nebula Co-Op is made by volunteers in their free time, so these are aims, not promises:

- We say we've received your report within **14 days**.
- We tell you whether we think it's a vulnerability, and how serious, within **14 days**.
- We aim to release a fix for a serious problem within **30 days**, and keep you told of progress until then.
- Once a fix is released, we publish a GitHub security advisory crediting you (unless you'd rather not be named).
  Please keep the problem to yourself until then, or until 90 days have passed, whichever comes first. If we need
  longer we'll ask, and say why.

If a report turns out to be [Nebula's](#upstream-nebula) problem, we'll tell you and pass it on with your permission.

## Supported versions

Only the **latest release** gets security fixes, and only the latest beta for beta players. Installed copies offer each
new release when they start, so updating is how a fix reaches you.

| Version | Supported |
| --- | --- |
| Latest release ([releases](https://github.com/NebulaCoop/nebula-coop/releases/latest)) | Yes |
| Latest `-beta.` pre-release | Yes |
| Older releases | No: please update |
| Builds from a branch, a pull request or CI test builds | No |

## What's in scope

- **The game and its runtime** (`runtime/`, `native/`): anything that lets a file, a mod or a network message run code,
  read or write files it shouldn't, or crash the game.
- **Co-op** (`galaxy/coop_*`, `galaxy/net/`): another player's game, or anyone who can send packets to yours, being able
  to
  - run code on your PC, or write outside the game's `coop_mods/` and `coop_joined/` folders (for example with a mod
    download or a save the host sends);
  - crash or freeze your game, or the host's room, with a message;
  - pretend to be another player, the host or a moderator, get past a room's password, kick or ban, or undo a ban.
- **Setup, the launcher and the updater** (`installer/`): getting a Setup that isn't signed with the release key to
  run as an update, getting around the signature check, or Setup writing outside its own folders. Import from Nebula
  must only ever read Nebula's folders.
- **Releases and CI** (`.github/workflows/`): anything that would let someone outside the project change what goes
  into a release, or get at the release signing key or other secrets.
- **NebulaNet** (`nebulanet.mackery.com`, [its repository](https://github.com/NebulaCoop/NebulaNet)): joining a room
  without its password or code, finding a private room without its password, seeing a room's addresses without
  joining it, taking over or closing someone else's room, or getting at its secrets.
- **The Discord Join button**: a crafted invite or `discord-…` link doing more than joining a room.
- **LumaLink**: a mod or archive that makes LumaLink write outside its output, or makes a `.luma` that does so in the
  game.

## What isn't a vulnerability

Some things work the way they do on purpose. Please don't report these, though ideas for doing better are welcome as
ordinary issues:

- **The host is trusted.** As in sm64coopdx, the world runs on the host's PC: the host decides the save, the mods, who
  stays and what happens in the game. A host doing what a host can do (kicking, changing the room's rules, cheats in a
  room with cheats on, sending the room's mods) isn't a vulnerability. A host running code on a joiner's PC, or
  writing outside the folders above, is.
- **Players see each other's IP addresses.** Play goes straight between the host and each player, not through a
  server, so the host sees every player's address and each player sees the host's. Use a VPN if that matters to you.
- **Co-op traffic isn't encrypted.** Messages between players, including chat and a room's password, travel as plain
  UDP. Don't use a password you use anywhere else. (NebulaNet itself is HTTPS.)
- **Antivirus warnings.** Nebula Co-Op's programs aren't code-signed yet, so Windows Defender sometimes flags a new
  build by its reputation (names ending in `!ml`). See [docs/TEST_BUILDS.md](docs/TEST_BUILDS.md). Real evidence of
  something harmful in a release is in scope, of course.
- **Cheating** in a room the host made with cheats on, or memory editing of your own game.
- **Problems that need someone to already control your PC**, such as editing your settings files or replacing the
  game's files.
- **Reports from automated scanners** without a way to actually exploit what they found.
- **Load tests or denial of service against NebulaNet.** Please don't attack the live service; run your own copy
  (`coop_nebulanet=<url>`, or the Host page's NEBULANET SERVER) to test against.

## Upstream Nebula

Nebula Co-Op is a fork of [Nebula](https://github.com/keepvaibin/nebula) by keepvaibin, which made everything that runs
the game on a PC: the host, the renderers, Setup and the launcher, mods and settings. A problem that's also in upstream
Nebula, rather than in what Nebula Co-Op added (co-op, NebulaNet, Discord invites, the separate install and import),
should go to Nebula as well, through its own repository. Tell us too, and we'll fix it here, keeping in step with
upstream's fix where there is one.

The game code itself comes from the [Petari](https://github.com/SMGCommunity/Petari) decompilation. Other third-party
code is listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md); a problem in one of those libraries should go to
its own project first.

## Testing safely

- Test on your own PCs, your own rooms and your own copy of NebulaNet, never on other people's games or rooms.
- Don't read, keep or share other players' data, saves or addresses beyond what you need to show the problem.
- Don't publish an exploit that works against released versions before a fix is out.

If you follow this policy in good faith, we won't take any action against you over your research, and we'll work with
you to understand and fix the problem.
