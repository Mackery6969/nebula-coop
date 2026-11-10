# Nebula Co-Op Setup Script
# Sets up the build environment, builds all components, and creates a shared
# instance directory for testing co-op (run multiple copies from the same dir).
#
# Usage (PowerShell):
#   .\setup.ps1                          # Full setup: check tools, init submodules, build everything
#   .\setup.ps1 -SkipTools               # Skip tool detection, assume everything is installed
#   .\setup.ps1 -SkipBuild               # Just check tools and init submodules, don't build
#   .\setup.ps1 -SkipTests               # Build everything but skip running tests
#   .\setup.ps1 -UseSccache              # Enable sccache for host and game module builds
#   .\setup.ps1 -BuildHostOnly           # Only build the host (CMake), skip game module and import tool
#   .\setup.ps1 -BuildGameModuleOnly     # Only build the game module, skip host and import tool
#   .\setup.ps1 -CreateInstances 1       # Create the shared instance directory (default: 1)

param(
    [switch]$SkipTools,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$UseSccache,
    [switch]$BuildHostOnly,
    [switch]$BuildGameModuleOnly,
    [int]$CreateInstances = 1
)

$ErrorActionPreference = "Stop"
$VerbosePreference = "Continue"

# ── Helpers ──────────────────────────────────────────────────────────────────

function Write-Step($msg) {
    Write-Host "`n=== $msg ===" -ForegroundColor Cyan
}

function Write-Ok($msg) {
    Write-Host "[OK] $msg" -ForegroundColor Green
}

function Write-Warn($msg) {
    Write-Host "[WARN] $msg" -ForegroundColor Yellow
}

function Write-ErrorCustom($msg) {
    Write-Host "[FAIL] $msg" -ForegroundColor Red
    exit 1
}

function Test-Command($name) {
    return $null -ne (Get-Command $name -ErrorAction SilentlyContinue)
}

# ── 1. Tool Detection ───────────────────────────────────────────────────────

if (-not $SkipTools) {
    Write-Step "Detecting build tools"

    # Visual Studio 2022/2026 C++ tools
    $VsWhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    $VsRoot = $null
    if (Test-Path $VsWhere) {
        $VsRoot = & $VsWhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
    }
    if (-not $VsRoot) {
        # Check VS 2026 and VS 2022 locations (including D:/Programs)
        @("D:\Programs\Microsoft Visual Studio\2026\Community",
          "D:\Programs\Microsoft Visual Studio\2026\BuildTools",
          "C:\Program Files\Microsoft Visual Studio\2026\Community",
          "C:\Program Files\Microsoft Visual Studio\2026\BuildTools",
          "D:\Programs\Microsoft Visual Studio\2022\Community",
          "D:\Programs\Microsoft Visual Studio\2022\BuildTools",
          "C:\Program Files\Microsoft Visual Studio\2022\Community",
          "C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
          "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community",
          "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools") | ForEach-Object {
            if (Test-Path (Join-Path $_ "Common7\Tools\VsDevCmd.bat")) {
                $VsRoot = $_
            }
        }
    }
    if (-not $VsRoot) {
        Write-ErrorCustom "Visual Studio 2022/2026 with C++ tools not found. Install from Visual Studio Installer with 'Desktop development with C++' workload."
    }
    Write-Ok "Visual Studio found at: $VsRoot"

    # MSVC environment (for CMake host build)
    Write-Step "Entering MSVC environment"
    $VsDevCmd = Join-Path $VsRoot "Common7\Tools\VsDevCmd.bat"
    $VsWhereDir = "C:\Program Files (x86)\Microsoft Visual Studio\Installer"
    if ((Test-Path (Join-Path $VsWhereDir "vswhere.exe")) -and
        ($env:Path -split ';') -notcontains $VsWhereDir) {
        $env:Path = "$VsWhereDir;$env:Path"
    }
    $EnvLines = & $env:ComSpec /d /s /c "`"$VsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        Write-ErrorCustom "Failed to initialize Visual Studio developer environment"
    }
    foreach ($Line in $EnvLines) {
        $Sep = $Line.IndexOf("=")
        if ($Sep -gt 0) {
            $Name = $Line.Substring(0, $Sep)
            $Value = $Line.Substring($Sep + 1)
            Set-Item -Path "Env:$Name" -Value $Value
        }
    }
    $VsCMakeBin = Join-Path $VsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    $VsNinjaBin = Join-Path $VsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
    @($VsCMakeBin, $VsNinjaBin) | Where-Object { Test-Path $_ } | ForEach-Object {
        if ($env:Path -split ';' -notcontains $_) { $env:Path = "$_;$env:Path" }
    }
    Write-Ok "MSVC environment loaded"

    # CMake
    if (-not (Test-Command cmake)) {
        Write-ErrorCustom "CMake not found. Install CMake 3.24+ (or use the one bundled with VS)."
    }
    $CMakeVer = cmake --version | Select-String -Pattern '(\d+\.\d+\.\d+)' | ForEach-Object { $_.Matches[0].Value }
    Write-Ok "CMake $CMakeVer found"

    # Ninja
    if (-not (Test-Command ninja)) {
        Write-ErrorCustom "Ninja not found. Install from https://github.com/ninja-build/ninja/releases or use VS's bundled copy."
    }
    Write-Ok "Ninja found at: $(Get-Command ninja).Source"

    # LLVM/clang 19+
    $LlvmBin = "C:\Program Files\LLVM\bin"
    if (-not (Test-Path (Join-Path $LlvmBin "clang++.exe"))) {
        Write-ErrorCustom "LLVM/clang not found at $LlvmBin. Install LLVM 19+ from https://github.com/llvm/llvm-project/releases"
    }
    Write-Ok "LLVM/clang found at: $LlvmBin"

    # Rust
    if (-not (Test-Command cargo)) {
        Write-ErrorCustom "Rust/cargo not found. Install from https://rustup.rs (stable channel, MSVC target)"
    }
    $RustVer = cargo --version
    Write-Ok "Rust $RustVer found"
    cargo --version | Out-Null  # ensure MSVC target
    if (-not (rustup target list --installed | Select-String "msvc")) {
        Write-Step "Installing Rust MSVC target"
        rustup target add x86_64-pc-windows-msvc
    }

    # sccache (optional, enabled with -UseSccache)
    if ($UseSccache) {
        if (-not (Test-Command sccache)) {
            Write-Warn "sccache not found. Install from https://github.com/mozilla/sccache or skip with -UseSccache`n"
            $UseSccache = $false
        } else {
            $SccacheVer = sccache --version | Select-String -Pattern 'sccache (\d+\.\d+\.\d+)'
            Write-Ok "sccache $($SccacheVer.Matches[0].Groups[1].Value) found"
        }
    }

    # .NET 8 SDK (for Setup/launcher - optional for dev)
    if (Test-Command dotnet) {
        $DotNetVer = dotnet --version
        if ($DotNetVer -match "8\.") {
            Write-Ok ".NET SDK $DotNetVer found"
        } else {
            Write-Warn ".NET 8 SDK not found (found $DotNetVer). Setup project won't build, but host and game module will."
        }
    } else {
        Write-Warn ".NET SDK not found. Setup project won't build, but host and game module will."
    }

    # Windows SDK (for fxc/dxc shader compilers)
    $WindowsSdkVerBin = $env:WindowsSdkVerBinPath
    if (-not $WindowsSdkVerBin) {
        # Try to find from registry
        $SdkKey = "HKLM:\SOFTWARE\Microsoft\Windows Kit\Installed Roots"
        if (Test-Path $SdkKey) {
            $SdkRoot = (Get-ItemProperty $SdkKey).KitRoot
            if ($SdkRoot) {
                $WindowsSdkVerBin = Join-Path $SdkRoot "bin"
            }
        }
    }
    if ($WindowsSdkVerBin) {
        Write-Ok "Windows SDK bin path: $WindowsSdkVerBin"
    } else {
        Write-Ok "Windows SDK bin path: not in PATH (will search default locations)"
    }
}

# ── 2. Git Submodules ────────────────────────────────────────────────────────

Write-Step "Initializing git submodules"
if (Test-Path ".gitmodules") {
    if (-not (Test-Path "nebulanet")) {
        git submodule update --init --recursive nebulanet
        Write-Ok "Initialized nebulanet submodule"
    } else {
        Write-Ok "nebulanet submodule already present"
    }
}

# ── 3. Start sccache Server ─────────────────────────────────────────────────

if ($UseSccache) {
    Write-Step "Starting sccache"
    sccache --start-server
    Write-Ok "sccache server started"
}

# ── 3. Build nebula-import (Rust disc importer) ─────────────────────────────

if (-not $BuildHostOnly) {
    Write-Step "Building nebula-import (Rust disc import tool)"
    cargo build --release --locked -p nebula-import
    if ($LASTEXITCODE -ne 0) {
        Write-ErrorCustom "nebula-import build failed"
    }
    Write-Ok "nebula-import built at: .\target\release\nebula-import.exe"
} else {
    Write-Warn "Skipping nebula-import build (-BuildHostOnly)"
}

# ── 4. Build Host (CMake / D3D12) ───────────────────────────────────────────

if (-not $BuildGameModuleOnly) {
    Write-Step "Building host (NebulaNative - D3D12)"
    $sccache_flags = if ($UseSccache) {
        " -DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache"
    } else { "" }
    cmake -S . -B build\host -G Ninja -DCMAKE_BUILD_TYPE=Release$sccache_flags
    cmake --build build\host --target NebulaNative --parallel
    if ($LASTEXITCODE -ne 0) {
        Write-ErrorCustom "Host build failed"
    }
    Write-Ok "Host built at: build\host\NebulaNative.exe"
} else {
    Write-Warn "Skipping host build (-BuildGameModuleOnly)"
}

# ── 5. Build Game Module (Petari decompilation) ─────────────────────────────

if (-not $BuildHostOnly) {
    Write-Step "Building game module (smg_game.dll)"
    $sccache_arg = if ($UseSccache) { "-UseSccache" } else { "" }
    & "$PSScriptRoot\native\build.ps1" -Petari "$PSScriptRoot\native\petari" -Build "$PSScriptRoot\build\game" @sccache_arg
    if ($LASTEXITCODE -ne 0) {
        Write-ErrorCustom "Game module build failed"
    }
    Write-Ok "Game module built at: build\game\smg_game.dll"
} else {
    Write-Warn "Skipping game module build (-BuildHostOnly)"
}

# ── 6. Run Tests ─────────────────────────────────────────────────────────────

if (-not $SkipTests) {
    Write-Step "Running tests"

    if (-not $BuildHostOnly) {
        Write-Host "CMake tests..."
        ctest --test-dir build\host --output-on-failure
        if ($LASTEXITCODE -ne 0) { Write-Warn "Some CMake tests failed" }
    }

    if (-not $BuildGameModuleOnly) {
        Write-Host "Cargo tests..."
        cargo test -p nebula-import
        if ($LASTEXITCODE -ne 0) { Write-Warn "Some cargo tests failed" }
    }
}

# ── 7. Create Shared Instance Directory for Co-Op Testing ────────────────────

if ($CreateInstances -gt 0) {
    Write-Step "Creating shared instance directory for co-op testing"

    $HostBin = if (Test-Path "build\host\NebulaNative.exe") {
        "build\host\NebulaNative.exe"
    } else {
        $null
    }
    $GameDll = if (Test-Path "build\game\smg_game.dll") {
        "build\game\smg_game.dll"
    } else {
        $null
    }

    if (-not $HostBin) {
        Write-Warn "Host executable not found; skipping instance creation"
    } elseif (-not $GameDll) {
        Write-Warn "Game DLL not found; skipping instance creation"
    } else {
        $InstanceDir = "$PSScriptRoot\instances\instance-1"
        New-Item -ItemType Directory -Force -Path $InstanceDir | Out-Null

        # Copy binaries into the shared instance directory
        Copy-Item $HostBin (Join-Path $InstanceDir "NebulaNative.exe") -Force
        Copy-Item $GameDll (Join-Path $InstanceDir "smg_game.dll") -Force

        # content/ and saves/ live at repo root (both gitignored) -- launch scripts point to them
        $ContentPath = "$PSScriptRoot\content"
        $SavesPath = "$PSScriptRoot\saves"

        if (-not (Test-Path (Join-Path $ContentPath "game.pak"))) {
            Write-Warn "content\game.pak not found. Run the import tool first:"
            Write-Host "  .\target\release\n ebula-import.exe --key common.key import <disc-image> --output content" -ForegroundColor Yellow
        }

        # Create saves directory if it doesn't exist
        New-Item -ItemType Directory -Force -Path $SavesPath | Out-Null

        # Create a README
        $Readme = @"
# Nebula Co-Op - Shared Instance

This is a shared instance directory. Run several copies of NebulaNative.exe
from here with different --host/--join flags and different --saves paths.

## Quick Launch

    # Launch a co-op pair (host + join):
    .\launch-coop.ps1

    # Host a co-op session on port 7777:
    .\instances\instance-1\NebulaNative.exe --host 7777

    # Join the host on port 7777:
    .\instances\instance-1\NebulaNative.exe --join 127.0.0.1:7777

    # With explicit content and saves (repo-root paths):
    .\instances\instance-1\NebulaNative.exe --pak content\game.pak --saves saves --host 7777

## Multiple simultaneous instances

Run several copies of NebulaNative.exe from this same directory:

    # Terminal 1 (host, port 7777):
    Start-Process .\instances\instance-1\NebulaNative.exe -ArgumentList @("--host", "7777", "--saves", "saves\coop-1")

    # Terminal 2 (join host on 7777):
    Start-Process .\instances\instance-1\NebulaNative.exe -ArgumentList @("--join", "127.0.0.1:7777", "--saves", "saves\coop-2")

## Arguments

    --pak <path>       Path to game.pak (auto-found if content\game.pak exists)
    --saves <path>     Saves directory (repo-root saves/ by default)
    --module <path>    Game module DLL (auto-found as smg_game.dll beside executable)
    --host <port>      Start as co-op host on port
    --join <addr>      Join a co-op session (room code, address, or address:port)
"@
        Set-Content (Join-Path $InstanceDir "README.md") $Readme

        Write-Ok "Shared instance directory created: $InstanceDir"
        Write-Ok "Content: $($ContentPath)\game.pak  Saves: $SavesPath"

        # Create a convenience launch script that opens two windows (host + join)
        $LaunchScript = @"
# Launch a co-op pair: one host + one join instance
# Generated by setup.ps1

`$ErrorActionPreference = "Continue"
`$root = "$PSScriptRoot"
`$exe = Join-Path `$root "instances\instance-1\NebulaNative.exe"
`$saves = Join-Path `$root "saves"

Write-Host "Starting co-op pair (host + join)..." -ForegroundColor Cyan

# Host on port 7777
Start-Process -WorkingDirectory `$root -FilePath `$exe `
    -ArgumentList "--host", "7777", "--saves", (Join-Path `$saves "coop-1")
Write-Host "  Host started on port 7777" -ForegroundColor Green

Start-Sleep -Milliseconds 500

# Join the host
Start-Process -WorkingDirectory `$root -FilePath `$exe `
    -ArgumentList "--join", "127.0.0.1:7777", "--saves", (Join-Path `$saves "coop-2")
Write-Host "  Join started (connected to 127.0.0.1:7777)" -ForegroundColor Yellow

Write-Host "`nBoth windows open. Close them to stop." -ForegroundColor White
"@
        Set-Content "$PSScriptRoot\launch-coop.ps1" $LaunchScript
        Write-Ok "Launch script created: launch-coop.ps1"
    }
}

# ── Done ─────────────────────────────────────────────────────────────────────

Write-Step "Setup complete!"
$ContentPath = "$PSScriptRoot\content"
$SavesPath = "$PSScriptRoot\saves"
Write-Host @"

Build artifacts:
  - Host:     build\host\NebulaNative.exe
  - Game DLL: build\game\smg_game.dll
  - Importer: .\target\release\n ebula-import.exe

To import game content (if not already done):
  .\target\release\n ebula-import.exe --key common.key import <disc-image> --output content

To launch a co-op pair (host + join):
  .\launch-coop.ps1

Manual launch examples:
  # Host a co-op session on port 7777:
  .\instances\instance-1\NebulaNative.exe --host 7777

  # Join the host on port 7777:
  .\instances\instance-1\NebulaNative.exe --join 127.0.0.1:7777

  # With explicit saves paths:
  Start-Process .\instances\instance-1\NebulaNative.exe -ArgumentList @("--host", "7777", "--saves", "saves\coop-1")
  Start-Process .\instances\instance-1\NebulaNative.exe -ArgumentList @("--join", "127.0.0.1:7777", "--saves", "saves\coop-2")

CLI options:
  --pak <path>       Path to game.pak (auto-found if content\game.pak exists)
  --saves <path>     Saves directory
  --module <path>    Game module DLL (auto-found as smg_game.dll beside executable)
  --host <port>      Start as co-op host on port
  --join <addr>      Join a co-op session (room code, address, or address:port)
"@ -ForegroundColor White
