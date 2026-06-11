# Aether Ecosystem Baseline

## Source

- Baseline: DDNet 19.8.2
- Official DDNet commit: `cbb22df93df2d97c5b6630b1d84e80d95b3568a4`
- Source archive: `DDNet-19.8.2.tar.xz`
- Source archive SHA-256: `F950E3310F5DF8F85EAC5610D4103D24AEC7A2FECCF441CBE406CE3B4B8D9FD2`
- Official `ddnet-libs` revision: `592dad190992b76a40c76c615043061222507dd2`

The baseline is an unmodified DDNet source tree. No TClient, legacy AetherClient,
branding, product profiles, or product assets are included at this stage.

## Toolchain

- Host: Windows 10.0.26100.8328, x64
- Generator: Visual Studio 18 2026, x64
- MSBuild: 18.5.4
- MSVC compiler: 19.50.35730
- MSVC toolset: 14.50.35717
- Windows SDK: 10.0.26100.0
- CMake: 4.3.1
- Vulkan SDK: 1.4.341.1
- Rust: 1.95.0

## Build

Configure:

```powershell
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.341.1"
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DVULKAN=ON `
  -DDISCORD=OFF `
  -DTOOLS=OFF `
  -DDOWNLOAD_GTEST=ON `
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=.
```

Build the vanilla client and server:

```powershell
cmake --build build --config Release --target game-client game-server --parallel
```

Run the available test suites:

```powershell
cmake --build build --config Release --target run_tests --parallel
```

## Verification

- `DDNet.exe`: built successfully, version 19.8.2
- `DDNet-Server.exe`: built successfully, version 19.8.2
- C++ tests: 344 passed, 0 failed
- Rust documentation tests: 18 passed, 0 failed
- Client smoke test: launched with `quit` and left no running process
- Server smoke test: launched with `shutdown` and exited with code 0

Baseline binary SHA-256 values:

```text
DDNet.exe        E420B017ECAC0DBEABCC479F2B0EFE9C9E7F8B44F3F0981EA73D1FA5A653BA18
DDNet-Server.exe 21580B6A62A34FE31B9A50860038F4BE22205E4BD592917A6CC348A14121B72D
```

Build outputs are intentionally excluded from Git.
