# Aether Clean-Room Policy

## Scope

This repository is the clean implementation of Aether. It starts from DDNet
19.8.2 and may integrate TClient under the license shipped with the approved
TClient source archive.

The older AetherClient, BestClient, BestProject, and other unlicensed client
implementations are quarantined references. Their source code must not be
opened, copied, translated, mechanically transformed, or used to derive an
implementation in this repository.

This policy documents the engineering process. It is not legal advice.

## Allowed Inputs

- DDNet source and documentation under their respective licenses.
- TClient source from `TClient-master.zip`, including its retained license and
  data licenses.
- Official platform and library documentation.
- Public protocol specifications and DDNet APIs.
- User-written behavioral requirements that do not reproduce protected source,
  text, assets, or a pixel-identical interface.
- User-authored legacy Aether feature implementations when the user explicitly
  identifies them as their own work and requests that they be ported. These
  ports must still use `ae_*` settings and the clean Aether UI/HUD systems.
- Independently created tests, mockups, algorithms, text, and assets.

## Forbidden Inputs

- Source from the old AetherClient or BestClient/BestProject trees, except for
  user-authored Aether features explicitly identified and requested by the user.
- `ac_*`, `bc_*`, or other implementation details learned from forbidden code.
- Old AetherClient/BestClient component files, copied functions, translated
  algorithms, UI strings, assets, or exact file duplicates.
- Binary decompilation or behavior tracing intended to reconstruct protected
  implementation details.

## Naming

- TClient settings keep their original `tc_*` names.
- New Aether settings use the `ae_*` prefix.
- The first clean product is `Aether.exe`.
- Vera, Via, Vex, and product capability matrices are outside the first clean
  release.

## Approved Provenance

| Source | Revision / digest | Use |
|---|---|---|
| DDNet | 19.8.2, `cbb22df93df2d97c5b6630b1d84e80d95b3568a4` | Clean upstream base |
| Baseline commit | `26c939a62f2efd3b824f752f6c17f10d36cba2c3` | Reproducible local baseline |
| TClient archive | SHA-256 `883901CA0949E21493B18F0264CAC750F314EF3998B10215CD4DD3BCB9EAB622` | Licensed TClient integration |
| TClient Git revision | `4e4269396b97d06879c11ae3b9696c3dc1a06084` | Archive content verification |

TClient Mumble integration is intentionally excluded. Neither its component,
external bridge, config, command, nor build entries may be present.

## Feature Provenance

Every new Aether feature must add a row before implementation starts.

| Feature | Requirement source | Implementation references | Status |
|---|---|---|---|
| Aether settings catalog | User-authored menu behavior in the clean-room plan | DDNet UI APIs | Mockups approved on 2026-06-13 |
| Silent typing | Pending written behavior contract | DDNet chat/input APIs | Not started |
| Draft preservation | `docs/features/AETHER_SAVE_UNSENT_MESSAGES.md`, user-authored behavior | DDNet chat/input APIs | Implemented on 2026-06-13 |
| Snap tap | `docs/features/AETHER_SNAP_TAP.md`, user-authored behavior | DDNet input/control APIs | Implemented and verified on 2026-06-13 |
| Gores Mode | `docs/features/AETHER_GORES_MODE.md`, user-provided `weapons.txt` and `switch.txt` cfg behavior | DDNet bind, console and input-control APIs | Wrapper bind implementation added on 2026-06-15 |
| Aether Fast Input | `docs/features/AETHER_FAST_INPUT.md`, user-authored fast input requests including Saiko+ from the user's old Aether client and Lewn+ from user-provided player feedback | DDNet prediction, input and render-position APIs; existing TClient fast input integration points kept as licensed project context; user-owned old Aether Saiko+ behavior used for equivalence | TClient, Adaptive and Saiko+ input hub updated on 2026-06-15; Lewn+ experimental mode added on 2026-06-27 |
| Fail sound | `docs/features/AETHER_FAIL_SOUND.md`, user-authored Aether feature request | DDNet extended character freeze state, team APIs and sound component APIs | Implemented on 2026-06-13 |
| Sound controls | `docs/features/AETHER_SOUND.md`, user-authored Aether feature request | DDNet world sound events and predicted sound event APIs | Implemented on 2026-06-13 |
| Keyboard sound | `docs/features/AETHER_SOUND.md`, user-authored Aether feature request | DDNet chat input and sound component APIs | Implemented as a separate Aether feature on 2026-06-13 |
| Auto team lock | `docs/features/AETHER_AUTO_TEAM_LOCK.md`, user-authored behavior | DDNet team snapshot and chat APIs | Implemented and verified on 2026-06-13 |
| Focus mode | `docs/features/AETHER_FOCUS_MODE.md`, user-authored behavior | DDNet HUD/chat/render component APIs | Implemented and verified on 2026-06-13 |
| Real hitbox | `docs/features/AETHER_REAL_HITBOX.md`, user-authored behavior | DDNet collision/render APIs | Implemented on 2026-06-13 |
| Ninja tee preview | `docs/features/AETHER_NINJA_TEE_PREVIEW.md`, user-authored Aether feature request | DDNet skin, tee render and menu color picker APIs | Implemented on 2026-06-13 |
| Sweat Weapon | `docs/features/AETHER_SWEAT_WEAPON.md`, user-authored visual feature request | DDNet rifle/shotgun laser render APIs and Aether menu/config APIs | Implemented on 2026-06-14 |
| Orbit Aura | `docs/features/AETHER_ORBIT_AURA.md`, user-authored visual feature request | DDNet player render position and Aether menu/config APIs | Implemented on 2026-06-14 |
| Jelly Tee | `docs/features/AETHER_JELLY_TEE.md`, user-authored visual feature request | DDNet tee render scale and player velocity APIs | Implemented on 2026-06-14 |
| Finish Prediction | `docs/features/AETHER_FINISH_PREDICTION.md`, user-authored behavior | DDNet race timer, local render position, collision map bounds and HUD editor APIs | Initial heuristic implemented on 2026-06-14 |
| 3D Particles | `docs/features/AETHER_3D_PARTICLES.md`, user-authored visual feature request | DDNet world render coordinates, camera center and quad rendering APIs | Implemented on 2026-06-14 |
| Loading Theme Background | `docs/features/AETHER_LOADING_THEME_BACKGROUND.md`, user-authored behavior | DDNet menu background and loading screen render APIs | Implemented on 2026-06-15 |
| Chat Bubbles | `docs/features/AETHER_CHAT_BUBBLES.md`, user-authored behavior | DDNet chat message, player render position and text render APIs | Implemented on 2026-06-15 |
| Silent Typing | `docs/features/AETHER_SILENT_TYPING.md`, user-authored typing-indicator request | DDNet player input flag APIs | Implemented on 2026-06-15 |
| Keystrokes | `docs/features/AETHER_KEYSTROKES.md`, user-authored behavior | DDNet control/render APIs | Implemented on 2026-06-13 |
| Session stats | `docs/features/AETHER_SESSION_STATS.md`, user-authored behavior | DDNet kill/race-finish messages and render APIs | Implemented on 2026-06-13 |
| Player skin/color copy | `docs/features/AETHER_PLAYER_COPY.md`, user-authored behavior | DDNet scoreboard popup and player-info APIs | Implemented on 2026-06-13 |
| Input visualizer | `docs/features/AETHER_INPUT_VISUALIZER.md`, user-authored legacy Aether behavior and overlay requirements | DDNet control/render APIs; user-authored legacy Aether input visualizer behavior | Reworked with flow history and HUD editor support on 2026-06-13 |
| Stability trainer | `docs/features/AETHER_STABILITY_TRAINER.md`, user-authored legacy Aether behavior and overlay requirements | DDNet prediction velocity, snapshot and render APIs; user-authored legacy Aether stability behavior | Implemented on 2026-06-13 |
| Browser tools | `docs/features/AETHER_BROWSER_UTILS.md`, user-authored behavior | DDNet server browser APIs | Implemented on 2026-06-13 |
| Gores Maps | `docs/features/AETHER_GORES_MAPS.md`, user-authored legacy Aether behavior and user-owned `AllOfGores.map` training fallback | DDNet HTTP, JSON, storage and menu APIs; user-authored legacy Aether Gores Maps behavior | Implemented on 2026-06-14 |
| Custom aspect ratio | `docs/features/AETHER_CUSTOM_RESOLUTION.md`, user-authored behavior | DDNet screen aspect APIs | Implemented on 2026-06-13 |
| Rollback demo | `docs/features/AETHER_ROLLBACK_DEMO.md`, user-authored behavior | DDNet replay recorder and console APIs | Implemented on 2026-06-13 |
| Aether UI scale | `docs/features/AETHER_UI_SCALE.md`, user-authored behavior | DDNet UI APIs | Implemented on 2026-06-13 |
| Gradient team colors | `docs/features/AETHER_GRADIENT_TEAM_COLORS.md`, user-authored behavior | DDNet scoreboard/DDTeam color APIs | Implemented on 2026-06-13 |
| Optimizer | `docs/features/AETHER_OPTIMIZER.md`, user-authored behavior | DDNet component/render APIs; Windows process priority API | First safe pass implemented on 2026-06-13 |
| Cinematic camera | Pending written behavior contract | DDNet camera/render APIs | Not started |
| Music player | `docs/features/AETHER_MUSIC_PLAYER.md`, user-authored v1 behavior | DDNet UI/render/config APIs; official Microsoft GSMTC and process-loopback WASAPI APIs | Implemented and verified on 2026-06-13 |
| Assets editor | `docs/features/AETHER_ASSETS_EDITOR.md`, user-authored behavior request | DDNet asset loading, PNG image and storage APIs | Initial clean-room mixer implemented on 2026-06-15 |
| Audio asset packs | `docs/features/AETHER_ASSETS_EDITOR.md`, user-authored audio-pack request | DDNet sound loader/storage APIs and Aether audio decoder for PCM/float WAV fallback | Implemented on 2026-06-15 |
| Aether family logos | User-provided `aether-family-v2.html` inline SVG artwork | SVG shapes exported to `data/core/logos/` without external font dependency | Exported on 2026-06-15 |

## Review Checklist

1. Record the feature behavior, edge cases, and acceptance criteria.
2. Record all implementation references in the provenance table.
3. Run `scripts/audit_clean_room.ps1`.
4. Build `game-client`, `game-server`, and `testrunner`.
5. Verify disabled features perform no per-frame work.
6. Verify threads and platform resources stop cleanly.
