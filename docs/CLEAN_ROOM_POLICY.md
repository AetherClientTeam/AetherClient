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
- Independently created tests, mockups, algorithms, text, and assets.

## Forbidden Inputs

- Source from the old AetherClient or BestClient/BestProject trees.
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
| Draft preservation | Pending written behavior contract | DDNet chat/input APIs | Not started |
| Snap tap | Pending written behavior contract | DDNet controls APIs | Not started |
| Auto team lock | Pending written behavior contract | DDNet console/network APIs | Not started |
| Real hitbox | Pending written behavior contract | DDNet collision/render APIs | Not started |
| Input visualizer | Pending written behavior contract | DDNet input/render APIs | Not started |
| Browser tools | Pending written behavior contract | DDNet server browser APIs | Not started |
| Cinematic camera | Pending written behavior contract | DDNet camera/render APIs | Not started |
| Music player | Pending written behavior contract | Windows Media Control and WASAPI | Not started |
| Assets editor | Pending written behavior contract | DDNet asset loading APIs | Not started |

## Review Checklist

1. Record the feature behavior, edge cases, and acceptance criteria.
2. Record all implementation references in the provenance table.
3. Run `scripts/audit_clean_room.ps1`.
4. Build `game-client`, `game-server`, and `testrunner`.
5. Verify disabled features perform no per-frame work.
6. Verify threads and platform resources stop cleanly.
