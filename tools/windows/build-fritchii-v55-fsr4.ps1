# SPDX-License-Identifier: GPL-3.0-or-later

param(
    [string]$BuildDirectory = 'build-v55-fsr4-release',
    [string]$LlvmDirectory = '.cache/toolchains/llvm-22.1.8',
    [string]$QtDirectory = '.cache/toolchains/Qt/6.8.3/msvc2022_64',
    [string]$PgoProfile = '',
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $projectRoot
try {
    $llvmRoot = (Resolve-Path -LiteralPath $LlvmDirectory).Path.Replace('\', '/')
    $qtRoot = (Resolve-Path -LiteralPath $QtDirectory).Path.Replace('\', '/')
    $env:PROCESSOR_ARCHITECTURE = 'AMD64'
    . "$PSScriptRoot/load-msvc-env.ps1"
    $env:VULKAN_SDK = 'C:\VulkanSDK\1.4.357.0'
    $env:Path = "$llvmRoot/bin;$qtRoot/bin;$env:VULKAN_SDK/Bin;$env:Path"
    $releaseFlags = '/O2 /Ob2 /DNDEBUG /Zi -O3 -flto=thin'
    if ($PgoProfile) {
        $profile = (Resolve-Path -LiteralPath $PgoProfile).Path.Replace('\', '/')
        & "$llvmRoot/bin/llvm-profdata.exe" show $profile
        if ($LASTEXITCODE -ne 0) { throw 'The PGO profile is incompatible with this LLVM version' }
        $releaseFlags += " -fprofile-instr-use=$profile -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date -Wno-backend-plugin"
    }
    $arguments = @(
        '-S', '.', '-B', $BuildDirectory, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_C_COMPILER=$llvmRoot/bin/clang-cl.exe",
        "-DCMAKE_CXX_COMPILER=$llvmRoot/bin/clang-cl.exe",
        "-DCMAKE_LINKER=$llvmRoot/bin/lld-link.exe",
        "-DCMAKE_C_FLAGS_RELEASE=$releaseFlags",
        "-DCMAKE_CXX_FLAGS_RELEASE=$releaseFlags",
        '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG /INCREMENTAL:NO /OPT:REF,NOICF /opt:lldltojobs=12',
        "-DCMAKE_PREFIX_PATH=$qtRoot",
        '-DYUZU_BUILD_PRESET=custom', '-DENABLE_LTO=OFF',
        '-DBUILD_TESTING=ON', '-DYUZU_TESTS=ON', '-DYUZU_CMD=OFF',
        '-DYUZU_ROOM=OFF', '-DYUZU_ROOM_STANDALONE=OFF',
        '-DYUZU_USE_BUNDLED_QT=OFF', '-DYUZU_USE_BUNDLED_SDL3=ON',
        '-DYUZU_USE_BUNDLED_FFMPEG=ON', '-DYUZU_USE_BUNDLED_SIRIT=ON',
        '-DENABLE_QT_TRANSLATION=ON', '-DENABLE_UPDATE_CHECKER=ON',
        '-DUSE_DISCORD_PRESENCE=OFF', '-DUSE_CCACHE=OFF', '-DENABLE_WERROR=OFF',
        '-DENABLE_NVIDIA_DLSS=ON', '-DENABLE_AMD_FSR4=ON', '-DFSR4_BUNDLE_RUNTIME=ON',
        '-DLICHII_BUILD_TAG=local', '-DLICHII_BUILD_PLATFORM=windows-x86_64',
        "-DCPM_SOURCE_CACHE=$projectRoot/.cache/cpm",
        "-DOpenSSL_CUSTOM_DIR=$projectRoot/.cache/cpm/openssl/4.0.1-1788234794-b64f68a94e-windows-amd64",
        "-DSDL3_CUSTOM_DIR=$projectRoot/.cache/cpm/sdl3/3.4.14-1788231389-147a8ee32d-windows-amd64",
        "-DFFmpeg_CUSTOM_DIR=$projectRoot/.cache/cpm/ffmpeg/9.0.2-av1-1-windows-amd64",
        "-Dsirit_CUSTOM_DIR=$projectRoot/.cache/cpm/sirit/v1.0.7-windows-amd64",
        "-Dllvm-mingw_CUSTOM_DIR=$projectRoot/.cache/toolchains/clang-rt-builtins"
    )
    & cmake @arguments
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    if (-not $ConfigureOnly) {
        & cmake --build $BuildDirectory --target yuzu tests --parallel 12
        if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
    }
} finally {
    Pop-Location
}
