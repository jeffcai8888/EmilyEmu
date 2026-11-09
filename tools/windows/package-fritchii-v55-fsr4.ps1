# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$BuildDirectory = 'build-v55-fsr4-release',
    [string]$QtDirectory = '.cache/toolchains/Qt/6.8.3/msvc2022_64',
    [string]$RecipeDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $projectRoot
try {
    $buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
    $qtRoot = (Resolve-Path -LiteralPath $QtDirectory).Path
    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the source commit' }
    $packageName = "EmilyEmu-v5.5-FSR4.1.2-$($commit.Substring(0, 10))-win64"
    $packageRoot = Join-Path $buildRoot "package/$packageName"
    if (Test-Path -LiteralPath $packageRoot) {
        throw "Package directory already exists: $packageRoot"
    }
    New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
    Copy-Item -LiteralPath "$buildRoot/bin/lichii.exe" -Destination $packageRoot
    $env:PROCESSOR_ARCHITECTURE = 'AMD64'
    . "$PSScriptRoot/load-msvc-env.ps1"
    $env:VULKAN_SDK = 'C:\VulkanSDK\1.4.357.0'
    $env:Path = "$qtRoot/bin;$env:VULKAN_SDK/Bin;$env:Path"
    & "$qtRoot/bin/windeployqt.exe" --release --no-translations --compiler-runtime `
        --dir $packageRoot "$packageRoot/lichii.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
    Copy-Item -LiteralPath "$buildRoot/bin/fsr4" -Destination $packageRoot -Recurse
    Copy-Item -LiteralPath 'LICENSES' -Destination $packageRoot -Recurse
    Copy-Item -LiteralPath 'LICENSE.txt', 'docs/FSR4.md', 'docs/ReleaseComparison-v5.5.md', 'dist/nx2_switches.txt' -Destination $packageRoot
    Copy-Item -LiteralPath '.cache/cpm/streamline/v2.14.16/license.txt' `
        -Destination "$packageRoot/LICENSES/Streamline-MIT.txt"
    if ($RecipeDirectory) {
        $recipe = Join-Path $RecipeDirectory 'OoT v131072 (cache v54).nx2recipe'
        New-Item -ItemType Directory -Path "$packageRoot/recipes" -Force | Out-Null
        Copy-Item -LiteralPath $recipe -Destination "$packageRoot/recipes"
    }
    $cache = Get-Content -LiteralPath "$buildRoot/CMakeCache.txt"
    $flags = ($cache | Where-Object { $_.StartsWith('CMAKE_CXX_FLAGS_RELEASE:') }) -replace '^[^=]*=', ''
    $profileText = if ($flags -match '-fprofile-instr-use=([^ ]+)') { $Matches[1] } else { 'Disabled; upstream training data unavailable' }
    @"
EmilyEmu / Fritchii v5.5 + optional AMD FSR 4.1.2
Source: https://github.com/jeffcai8888/EmilyEmu
Branch: codex/fritchii-v5.5-fsr4.1.2
Commit: $commit
Upstream baseline: fritchii-v5.5 / 4012e5271e278b21cf8f56b97481dac2b777cd0d
Toolchain: Clang 22.1.8, lld-link, Qt 6.8.3, local MSVC SDK
Release flags: $flags
PGO: $profileText
Run lichii.exe; retain the accompanying DLLs and subdirectories.
FSR 4.1.2 defaults to off; its validated path currently covers Fire Emblem: Fortune's Weave.
Ocarina of Time retains the upstream path; this build does not add FSR 4 for that game.
FSR 4.1.2 requires a compatible provider/GPU/driver. RTX 4060 Ti did not expose it in prior tests.
NVIDIA proprietary DLSS DLLs are obtained separately through the Graphics panel.
This matches the release source/UI baseline, not the author's unpublished PGO training results.
The upstream updater can replace this custom build with an upstream build without the FSR port.
No games, keys, user configuration or debug symbols are included.
See FSR4.md and ReleaseComparison-v5.5.md for validation and reproduction limits.
"@ | Set-Content -LiteralPath "$packageRoot/BUILD-INFO.txt" -Encoding utf8
    $archive = Join-Path $buildRoot "$packageName.zip"
    & 'C:\Program Files\7-Zip\7z.exe' a -tzip -mx=7 $archive $packageRoot -bso0 -bsp0
    if ($LASTEXITCODE -ne 0) { throw 'ZIP creation failed' }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
    "$hash  $packageName.zip" | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
    [pscustomobject]@{Directory=$packageRoot; Archive=$archive; Sha256=$hash}
} finally {
    Pop-Location
}
