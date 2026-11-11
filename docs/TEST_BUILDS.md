# Test builds

Every pull request and every push to `main` builds Nebula for Windows, Linux and Android, runs its tests, and keeps what
it built as a download for 14 days (the Build workflow, `.github/workflows/build.yml`). These are test builds, not
releases: they have no installer, no launcher, no update check and no ray-tracing material tables, the Windows programs
are built without cross-file optimization (as the tests are), and the Android app is signed with a debug key. macOS and
iOS have no build yet.

A branch without a pull request is not built on its own; run **Actions** > **Build** > **Run workflow** on it to get
one. To download one, open the pull request's **Checks** tab (or the run under **Actions** > **Build**) and pick an
artifact under **Artifacts**. You need to be signed in to GitHub.

| Artifact | Contents |
|---|---|
| `nebula-windows-x64` | `NebulaNative.exe` (Direct3D 12), `NebulaNativeVk.exe` (Vulkan), `smg_game.dll`, `nebula-import.exe` |
| `nebula-linux-x64` | `NebulaNative` (Vulkan, SDL3), `libsmg_game.so`, `libSDL3.so.0`, `nebula-import` |
| `nebula-android` | `Nebula.apk` (arm64 and x86_64) |

## Windows and Linux

1. Pack your own disc image of the North American Super Mario Galaxy (`RMGE01`) once. It stays on your PC:

   ```
   nebula-import identify SMG.rvz
   nebula-import import SMG.rvz --output content
   ```

   Run this in the folder that holds `NebulaNative`, so `content` lands next to it.

2. Run the game from a terminal, so its log shows:

   ```
   NebulaNative
   ```

   With no arguments, Nebula looks for `content/game.pak` (or `game.pak`) next to the program, then in the current
   folder, then in an installed Nebula's folder. Saves go in a `saves` folder next to the program or in the current
   folder when there is one, otherwise in Nebula's data folder (the installed Nebula's saves, with the settings). The
   log's `paths:` line names both. `--pak <game.pak>` and `--nand <folder>` choose others, as before. On Linux the
   folder must also hold `libSDL3.so.0` and `libsmg_game.so` (they're found next to the program), and you need a
   Vulkan driver. Nebula's in-game settings menu opens with F1 (or View and Menu, Minus and Plus on a controller) and
   the main menu's Options, as on Windows; it writes `runtime_settings.ini` and `config.ini`. Windows-only settings
   (DLSS, Joy-Con pairing) aren't offered.

The main menu's Host opens the Host page: pick a save with left and right (or New save), set the room, then press
Host. Saves are folders under `saves/coop_saves/`, each with its own achievements; on the first start the files of
your old save become saves named `FILE 1`, `FILE 2` and so on, and the old save is left as it was.

Online co-op lines in the log start with `[coop]`. `--host [port]` hosts the last played save as the main menu opens
(port 7777 when none is given) and `--join <address[:port]>` joins, so two copies on one PC are:

```
NebulaNative --host
NebulaNative --join 127.0.0.1
```

On Windows up to four games run at once. The first plays as always; each one started while it runs is game 2, 3 or 4
(its window says "Nebula (2)") and keeps its own settings, graphics settings, logs and saves in
`%LOCALAPPDATA%\NebulaCoop\instances\<n>\`. The first time, that folder starts as a copy of the first game's (windowed,
so the two don't cover each other), and later starts keep what it has; delete the folder to copy again. All of them
share the shader cache and the installed mods. The keyboard and Xbox controllers only reach the focused window.

### Windows Defender

Test builds aren't code-signed, and every build is a new file Microsoft has never seen, so Defender's machine-learning
checks sometimes flag `NebulaNative.exe` or `NebulaNativeVk.exe` (names ending in `!ml`, such as
`Program:Win32/Contebrew.A!ml`). To report one:

1. Open <https://www.microsoft.com/en-us/wdsi/filesubmission>, choose **Home customer**, and upload the flagged `.exe`
   from the extracted folder (not the archive).
2. Pick **Incorrectly detected as malware/malicious**, give the detection name from **Protection history**, and say
   it's an open-source game built by GitHub Actions in this repository.
3. When Microsoft replies that the file is clean, update Defender (`MpCmdRun.exe -RemoveDefinitions -DynamicSignatures`
   then `MpCmdRun.exe -SignatureUpdate`, from `%ProgramFiles%\Windows Defender`, as administrator) and restore the file
   from **Protection history**.

Until then, **Protection history** > the detection > **Actions** > **Allow** allows that one detection only. Don't turn
off real-time protection or exclude the whole folder.

## Android

Install `Nebula.apk` (allow installs from unknown sources), open it, and pick your disc image in its setup. A test
build only installs over another test build: uninstall a release first, or the other way round.
