# TClient Integration Baseline

## Provenance

- Target base: DDNet 19.8.2, commit
  `cbb22df93df2d97c5b6630b1d84e80d95b3568a4`
- Local clean baseline: `26c939a62f2efd3b824f752f6c17f10d36cba2c3`
- TClient revision: `4e4269396b97d06879c11ae3b9696c3dc1a06084`
- TClient version: `10.8.7`
- TClient archive SHA-256:
  `883901CA0949E21493B18F0264CAC750F314EF3998B10215CD4DD3BCB9EAB622`
- Common merge ancestor:
  `47c7f4ca7cab28235ca0c2d6862850a8a01be6e9`
- `ddnet-libs` revision:
  `592dad190992b76a40c76c615043061222507dd2`

The TClient change set was merged into DDNet 19.8.2 with a three-way Git
merge. The merge completed without source conflicts. TClient license notices
and data licenses are retained.

## Product Boundary

- Client executable: `Aether.exe`
- Server executable: `DDNet-Server.exe`
- DDNet protocol/base version: 19.8.2
- TClient settings: `tc_*`
- TClient config: `settings_tclient.cfg`
- Profiles config: `tclient_profiles.cfg`
- Mumble component, external bridge, build entries, and console command:
  excluded
- New Aether features and `ae_*` settings: not implemented in this stage
- Vera, Via, Vex, and product matrices: not implemented in this stage

## Build

```powershell
$env:VULKAN_SDK = "C:\VulkanSDK\1.4.341.1"
cmake -S . -B Release -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DVULKAN=ON `
  -DDISCORD=OFF `
  -DSTEAM=OFF `
  -DTOOLS=OFF `
  -DDOWNLOAD_GTEST=ON `
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=. `
  -DCLIENT_EXECUTABLE=Aether

cmake --build Release --config Release `
  --target game-client game-server --parallel
cmake --build Release --config Release --target run_tests --parallel
```

## Automated Acceptance

- Clean-room denylist: passed
- Mumble source/build reference scan: passed, zero references
- `game-client`: passed
- `game-server`: passed
- C++ tests: 344 passed, 0 failed
- Rust doc tests: 18 passed, 0 failed
- Client `quit` smoke test: exit code 0
- Server `shutdown` smoke test: exit code 0

Release artifacts:

```text
Aether.exe
SHA-256 1D8FB2F407067B3573896778AD97972578429A666214BD8972237661B8EB31A2

DDNet-Server.exe
SHA-256 F703B6F793C16F6071A4752F6F26A8F16F0E8794E4F811CA359CB073C867E830

testrunner.exe
SHA-256 F8DB6D64F6129199C057D83B0AE446E405235BC51CD76B890AA158669368A2BD
```

## Manual Acceptance Remaining

- Server list loads in the interactive client.
- Connect to `151.242.52.120:8304`.
- TClient settings tabs render and save.
- Profiles render, save, reload, and apply player/dummy data.
- No Mumble setting or `mumble_reconnect` command is exposed.

