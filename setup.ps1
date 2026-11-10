# Nebula Co-Op Setup Script
# Sets up the build environment, builds all components, and creates instance directories
# for testing co-op with multiple instances.
#
# Usage (PowerShell, run as Administrator not required):
#   .\setup.ps1                          # Full setup: check tools, init submodules, build everything
#   .\setup.ps1 -SkipTools               # Skip tool detection, assume everything is installed
#   .\setup.ps1 -SkipBuild               # Just check tools and init submodules, don't build
#   .\setup.ps1 -SkipTests               # Build everything but skip running tests
#   .\setup.ps1 -UseSccache              # Enable sccache for host and game module builds
#   .\setup.ps1 -BuildHostOnly           # Only build the host (CMake), skip game module and import tool
#   .\setup.ps1 -BuildGameModuleOnly     # Only build the game module, skip host and import tool
#   .\setup.ps1 -CreateInstances 2       # Create N instance directories after building (default: 2)

param(
    [switch]$SkipTools,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$UseSccache,
    [switch]$BuildHostOnly,
    [switch]$BuildGameModuleOnly,
    [int]$CreateInstances = 2
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

    # Visual Studio 2022 C++ tools
    $VsWhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    $VsRoot = $null
    if (Test-Path $VsWhere) {
        $VsRoot = & $VsWhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
    }
    if (-not $VsRoot) {
        @("C:\Program Files\Microsoft Visual Studio\2022\Community",
          "C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
          "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community",
          "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools") | ForEach-Object {
            if (Test-Path (Join-Path $_ "Common7\Tools\VsDevCmd.bat")) {
                $VsRoot = $_
            }
        }
    }
    if (-not $VsRoot) {
        Write-ErrorCustom "Visual Studio 2022 with C++ tools not found. Install from Visual Studio Installer with 'Desktop development with C++' workload."
    }
    Write-Ok "Visual Studio 2022 found at: $VsRoot"

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
    Write-Ok "Windows SDK bin path: $($WindowsSdkVerBin ?: 'not in PATH (will search default locations)')"
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

# ── 7. Create Instance Directories for Co-Op Testing ────────────────────────

if ($CreateInstances -gt 0) {
    Write-Step "Creating $CreateInstances instance directory(ies) for co-op testing"

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
        # Determine content folder path
        $ContentPath = "$PSScriptRoot\content"
        if (-not (Test-Path (Join-Path $ContentPath "game.pak"))) {
            Write-Warn "content\game.pak not found. Run the import tool first:"
            Write-Host "  .\target\release\nebula-import.exe --key common.key import <disc-image> --output content" -ForegroundColor Yellow
            $ContentPath = $null
        }

        for ($i = 0; $i -lt $CreateInstances; $i++) {
            $InstanceNum = $i + 1
            $InstanceDir = "$PSScriptRoot\instances\instance-$InstanceNum"
            New-Item -ItemType Directory -Force -Path $InstanceDir | Out-Null

            # Copy binaries
            Copy-Item $HostBin (Join-Path $InstanceDir "NebulaNative.exe") -Force
            Copy-Item $GameDll (Join-Path $InstanceDir "smg_game.dll") -Force

            # Each instance gets a unique port: instance 1 hosts on 7777,
            # instance 2 joins to it, etc. Alternate instances can host too.
            $PortBase = 7777 + ($i * 100)  # Each instance gets a unique port range
            $IsHost = ($i % 2 -eq 0)  # Even instances host, odd ones join

            # Create a short README for each instance
            $InstanceReadme = @"
# Nebula Co-Op - Instance $InstanceNum

## Quick Launch
# Host mode:
#   .\launch-instance-$InstanceNum.ps1

## Arguments
# --pak       Path to game.pak (auto-found if in the same folder)
# --saves     Path to this instance's saves folder
# --module    Path to smg_game.dll (auto-found if in the same folder)
# --host <N>  Start as host on port N (co-op host)
# --join <X>  Join a co-op session (room code, address, or address:port)

## Networking
# Port: $PortBase
# Mode: $(if ($IsHost) { "HOST" } else { "JOIN" })
"@
            Set-Content (Join-Path $InstanceDir "README.md") $InstanceReadme

            # Create saves directory
            $SavesDir = Join-Path $InstanceDir "saves"
            New-Item -ItemType Directory -Force -Path $SavesDir | Out-Null

            # Copy content if available
            if ($ContentPath) {
                Copy-Item (Join-Path $ContentPath "*") $InstanceDir -Recurse -Force -Exclude "*.git*"
            }

            Write-Ok "Instance $InstanceNum created at: $InstanceDir (port: $PortBase)"
        }

        # Create launch script for all instances
        $LaunchScript = @"
# Launch all co-op instances for local testing
# Generated by setup.ps1
#
# Instance 1 hosts on port 7777
# Instance 2 joins to 127.0.0.1:7777
# Instance 3 hosts on port 7877
# Instance 4 joins to 127.0.0.1:7877
# ...and so on (even = host, odd = join)
#
# Usage: .\launch-instances.ps1

`$ErrorActionPreference = "Continue"
`$root = "$PSScriptRoot"

Write-Host "`nLaunching $CreateInstances Nebula Co-Op instance(s) for local testing..." -ForegroundColor Cyan
Write-Host "Each instance runs with its own saves folder and a unique port.`n" -ForegroundColor Gray

$hosts = @()
for (`$i = 1; `$i -le `$CreateInstances; `$i++) {
    `$dir = Join-Path `$root "instances\instance-`$i"
    `$port = 7777 + ((`$i - 1) * 100)
    `$isHost = ((`$i - 1) % 2 -eq 0)

    `$args = @()
    `$args += "--saves"
    `$args += "`"`$(Join-Path `$dir 'saves')`""
    `$args += "--module"
    `$args += "`"`$(Join-Path `$dir 'smg_game.dll')`""

    if (`$isHost) {
        `$args += "--host"
        `$args += "`$$($port)"
        `$hosts += @{ Port = `$port }
        Write-Host "  Instance `$i -> HOST on port `$port" -ForegroundColor Green
    } else {
        # Join the previous (hosting) instance
        `$prevHost = `$hosts[(`$hosts.Count - 1)]
        `$args += "--join"
        `$args += "127.0.0.1:`$$($prevHost.Port)"
        Write-Host "  Instance `$i -> JOIN 127.0.0.1:`$$($prevHost.Port)" -ForegroundColor Yellow
    }

    Write-Host "`n--- Starting Instance `$i ---" -ForegroundColor Cyan
    Start-Process -WorkingDirectory `$dir -FilePath (Join-Path `$dir "NebulaNative.exe") `
        -ArgumentList `$args -WindowStyle Normal
    Start-Sleep -Milliseconds 500
}

Write-Host "`nAll instances launched. Close all game windows to stop.`n" -ForegroundColor Green
"@
        Set-Content "$PSScriptRoot\launch-instances.ps1" $LaunchScript
        Write-Ok "Launch script created: launch-instances.ps1"

        # Create individual launch scripts per instance
        for ($i = 1; $i -le $CreateInstances; $i++) {
            $PortBase = 7777 + (($i - 1) * 100)
            $IsHost = (($i - 1) % 2 -eq 0)

            $IndivLaunch = @"
# Launch Nebula Co-Op Instance $i
# Port: $PortBase
# Mode: $(if ($IsHost) { "HOST" } else { "JOIN" })

`$ErrorActionPreference = "Stop"
`$dir = "$PSScriptRoot\instances\instance-$i"
`$port = $PortBase

`$args = @()
`$args += "--saves"
`$args += "`"`$(Join-Path `$dir 'saves')`""
`$args += "--module"
`$args += "`"`$(Join-Path `$dir 'smg_game.dll')`""

$(if ($IsHost) {
    @"
# Host mode: start a co-op session on this port
`$args += "--host"
`$args += "`$$($port)"
"@
} else {
    @"
# Join mode: connect to the previous instance (host on port 7777 + $((($i - 2) * 100)))
`$args += "--join"
`$args += "127.0.0.1:`$(7777 + $((($i - 2) * 100)))"
"@
})

Write-Host "Starting Nebula Co-Op Instance $i ($('HOST' if $IsHost else 'JOIN'))..." -ForegroundColor Cyan
Write-Host "  Directory: `$dir" -ForegroundColor Gray
Write-Host "  Port: `$port" -ForegroundColor Gray
Write-Host "  Saves: `$dir\saves" -ForegroundColor Gray

Start-Process -WorkingDirectory `$dir -FilePath (Join-Path `$dir "NebulaNative.exe") `
    -ArgumentList `$args

Write-Host "Instance $i launched.`n" -ForegroundColor Green
"@
            Set-Content "$PSScriptRoot\instances\launch-instance-$i.ps1" $IndivLaunch
        }
        Write-Ok "Individual launch scripts created in instances\"
    }
}

# ── Done ─────────────────────────────────────────────────────────────────────

Write-Step "Setup complete!"
Write-Host @"

Build artifacts:
  - Host:     $HostBin
  - Game DLL: $GameDll
  - Importer: .\target\release\nebula-import.exe

To import game content (if not already done):
  .\target\release\nebula-import.exe --key common.key import <disc-path> --output content

To launch all co-op instances together:
  .\launch-instances.ps1

To launch a single instance:
  .\instances\launch-instance-1.ps1
  .\instances\launch-instance-2.ps1

Manual launch examples:
  # Host a co-op session on port 7777:
  .\build\host\Release\NebulaNative.exe --host 7777

  # Join the host on port 7777:
  .\build\host\Release\NebulaNative.exe --join 127.0.0.1:7777

  # With explicit saves and module paths:
  .\build\host\Release\NebulaNative.exe --pak content\game.pak --saves my-saves --module build\game\smg_game.dll --host 7777

CLI options:
  --pak <path>       Path to game.pak (auto-found if content\game.pak exists)
  --saves <path>     Saves directory
  --module <path>    Game module DLL (auto-found as smg_game.dll beside executable)
  --host <port>      Start as co-op host on port
  --join <addr>      Join a co-op session (room code, address, or address:port)
"@ -ForegroundColor White
