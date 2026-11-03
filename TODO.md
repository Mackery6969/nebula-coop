# Co-op to-do

What's left to turn Nebula into an online co-op mod like sm64coopdx. The reasoning behind each item is in
[docs/COOP_PLAN.md](docs/COOP_PLAN.md); this list is what's done and what's next. Tick an item when it's merged.

## Main menu and saves (Phase 0.5)

- [x] Main menu in place of the file select, over the file select's space backdrop
- [x] The title screen before the main menu, once as Nebula starts (not when coming back from a room)
- [x] Pointer, D-pad, stick, A and B work on the menu
- [x] Host page: pick a save (left and right, or New save), Erase, room settings, Host and Back
- [x] Unlimited saves, each a folder under `coop_saves/`; the old save's files converted once
- [x] Achievements belong to the save: each save keeps its own
- [x] Settings: ADVANCED renamed ONLINE, with room settings and cheats that need the room's cheats on
- [x] No Leave game on the Online tab: the pause menu's Quit leaves the room, and saves only for the host
- [x] The Online tab greyed out when opened from the main menu
- [x] Tab (a controller's Back) resets the selected Host page setting to its default; Escape or B goes back
- [x] No page tabs or page hint on the main menu's one-page screens
- [x] ~~The game's logo above the main menu, as on the title screen~~ (dropped: it took too much room)
- [x] Player page: name and character (Mario or Luigi), the character per player instead of per save; in Options
      in the MODS page's place (MODS moved to the end)
- [x] Text entry (keyboard, or letter by letter on a controller), then: renaming a save, the Host page's port and
      password, the Join page's address
- [x] Mods as a room setting: the Host page picks them; the MODS page only imports and removes
- [x] Pause menu and Nebula's menus no longer pause the game (the server keeps running)
- [x] Options on Linux and Android (the in-game settings menu is every host's now)

## Playing together (Phase 1)

- [x] Network layer (UDP, reliable and ordered channels) on every platform
- [x] Host opens the room; Join connects to `address:port`; the pause menu's Quit closes or leaves it
- [x] Other players drawn as remote Marios and Luigis from their snapshots
- [x] Nametags over each player's head
- [x] Player list while Tab (keyboard) or Share (controller) is held
- [x] Version, mods and disc check when a player joins
- [x] Mod sharing as sm64coopdx's: joiners download the room's mods they don't have from the host
- [x] Chat (keyboard only; controllers read it): T opens a line, Enter sends it, Escape drops it
- [x] Personal Star Counter (sm64coopdx's mod, built in): each player's stars this room and all time under the HUD's
      star count, and each player's in the player list

## Same world (Phase 2)

- [x] Instances per galaxy and mission: players only see others on the same mission
- [x] The server decides everything except the visuals: save, collectibles, objects, enemies
- [x] Cutscenes and transitions as sm64coopdx has them: each player's own, a Grand Star takes everyone in its
      mission along, and a star the room gave returns as its first when the player takes it, so nobody gets stuck
- [x] An achievement unlocked in a save is every player's who plays in it
- [x] Player collisions with sm64coopdx's options (non-solid, solid, friendly fire, knockback, PvP mode)

## Later (Phases 3 and 4)

- [ ] Shared enemies and objects
- [ ] Bosses and story demos with several players
- [ ] Dedicated server (the game with no window)

## Maybe later

- [ ] A relay like sm64coopdx's CoopNet (join by code, no port forwarding), only if the project grows
