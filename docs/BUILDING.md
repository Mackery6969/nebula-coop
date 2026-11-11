# Building Nebula from source

`Nebula-Setup.exe` installs a prebuilt Nebula. Build by hand to develop Nebula or to check a release.

## Tools

- Visual Studio 2022 C++ tools and the Windows SDK, for the host.
- LLVM/clang 19 or newer (`C:\Program Files\LLVM`), for the game module.
- CMake 3.24+ and Ninja.
- Rust stable (MSVC target), for the disc import tool.
- The .NET 8 SDK, for Setup and the launcher.

In PowerShell, dot-source `tools\enter_msvc_environment.ps1` first. CMake downloads the remaining dependencies at
configure time from pinned archives checked by SHA-256: the FidelityFX SDK (FSR 3), and for the Vulkan renderer
Vulkan-Headers, volk, VulkanMemoryAllocator, glslang, SPIRV-Tools and SPIRV-Headers, and the DirectX Shader Compiler's
release (a build tool: it compiles the ray tracing shaders to SPIR-V; `GALAXY_DXC_SPIRV` names another dxc that can).

## 1. Game files

```powershell
cargo build --release --locked -p nebula-import
.\target\release\nebula-import.exe --key common.key identify D:\Games\SMG.rvz      # must report RMGE01, RMGP01 or RMGJ01
.\target\release\nebula-import.exe --key common.key import D:\Games\SMG.rvz --output D:\nebula\content
```

An extracted folder works in place of the disc image; `--features folders-only` builds an importer that takes folders only. The content folder holds `game.pak`.

## 2. Host

```powershell
cmake -S . -B build\host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build\host --target NebulaNative
```

`-DNEBULA_VULKAN=ON` also builds `NebulaNativeVk.exe`; `-DNEBULA_FSR3=OFF` leaves out FSR 3. DLSS uses NVIDIA's
signed Streamline libraries, which the host loads at run time from the vendor folder.

## 3. Game module

```powershell
.\native\build.ps1 -Petari native\petari -Build build\game
```

Builds `build\game\smg_game.dll` with clang and Ninja.

A build for players adds `-Pgo`: ThinLTO with the profile in `native/pgo` (`tools/verify/pgo_profile.sh` makes it). It takes longer to link and spends about 17 % less CPU on the game.

## 4. Run

Put `NebulaNative.exe` and `smg_game.dll` in one folder with the `content` folder from step 1, then:

```powershell
.\NebulaNative.exe
```

With no arguments it finds `content\game.pak` next to it (or in the current folder) and uses the launcher's saves,
`%LOCALAPPDATA%\NebulaCoop\saves`. `--pak <game.pak>` and `--nand <folder>` name others:

```powershell
.\NebulaNative.exe --pak D:\nebula\content\game.pak --saves "$env:LOCALAPPDATA\Nebula\saves"
```

The other arguments are in COMMAND_LINE.md.

## Tests

```powershell
ctest --test-dir build\host --output-on-failure
cargo test -p nebula-import
sh native/tests/run_tests.sh
.\tools\audit_repo.ps1
.\installer\tests\run-launcher-tests.ps1
.\installer\tests\run-beta-updater-tests.ps1
.\installer\tests\run-update-flow-tests.ps1
```

The Build workflow runs only what a change can affect: a change under `installer/` runs the installer tests (the last
three above) and not the game builds, and any other change to the code runs the game builds and their tests and not
the installer's. `installer/assets/`, `runtime/include/galaxy/store_version.inc` and `build.yml` itself run both;
documentation, `tools/`, `nebulanet/` and the other workflows run neither. The **Build result** check reports either
way, so it is the one to require before merging.

## Setup

```powershell
.\installer\build-native-setup.ps1 -Version <version> -Dxc <unpacked DirectX Shader Compiler release> -Bin <folder with NebulaNative.exe and smg_game.dll>
```

Builds the disc import tool, the launcher and Setup, and writes `Nebula-Setup.exe` with the host and game module
inside it.

## Releases

Pushing a version tag publishes a release (`.github/workflows/release.yml`):

```powershell
git tag -a v2.0.0 -m "Nebula 2.0.0"
git push origin v2.0.0
```

The Release workflow builds the host and the game module (the host with cross-file optimization), makes Setup with the
script above, runs `tools/release_check.ps1` on it (without Defender), and publishes `Nebula-Setup.exe` and
`Nebula-Setup.exe.sig` as the GitHub release `Nebula 2.0.0`, with install steps and the pull requests since the last
release as its notes. A tag with `-beta.` in it (`v2.0.0-beta.1`) makes `Nebula-Beta-Setup.exe`, which only betas update
to; any tag with a `-` part is a pre-release. When a release for the tag already exists (made by hand on GitHub), its
notes are kept and only the files are added. Setup's update check reads this repository's releases. Its ray-tracing
tables have a header and no rows (Setup refuses an empty file): they describe the game's own data and are kept outside
the repository. `NebulaNative.pdb` is kept as the run's `release-symbols` download for 90 days.

Setup installs NebulaCoop beside an installed upstream Nebula, never over it: `%LOCALAPPDATA%\Programs\NebulaCoop`,
data in `%LOCALAPPDATA%\NebulaCoop`, its own Start menu folder, `HKCU\Software\NebulaCoop` and Add/Remove Programs entry
(`installer/src/Common/NebulaPaths.cs`, `ProductName`; the game's own `galaxy::kDataFolderName`). A first install asks
whether to import from Nebula: the player picks Nebula's install folder (or its data folder), and Setup copies its saves
and settings, and its packed game when this version can read it, so the disc is not asked for. Nebula's folders are only
read. `--unattended --import-from <folder>` does the same without the questions. Updates never ask.

Updates run only a Setup signed with the release key, the private half of `installer/release-public-key.xml`. Put
that private key (RSA XML) in the repository secret `NEBULA_RELEASE_KEY`; without it Setup is published unsigned and
installed copies don't update to it. To make a new key pair (keep `nebula-release-private.xml` out of the repository):

```powershell
$rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider 4096
$rsa.PersistKeyInCsp = $false
New-Item -ItemType Directory -Force "$env:USERPROFILE\.nebula-release-key" | Out-Null
[IO.File]::WriteAllText("$env:USERPROFILE\.nebula-release-key\nebula-release-private.xml", $rsa.ToXmlString($true))
[IO.File]::WriteAllText("$PWD\installer\release-public-key.xml", $rsa.ToXmlString($false))
```

Run it from the repository's root. A pull request that changes the Release workflow, the installer,
`native/build.ps1` or `tools/release_check.ps1` builds Setup the same way without publishing it (the `nebula-setup`
download), and so does **Actions** > **Release** > **Run workflow**.
