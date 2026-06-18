# Aether Optimizer

## Requirement Source

User-authored behavior request on 2026-06-13.

## First Safe Pass

- Optional Windows high process priority for `Aether.exe`.
- Optional Discord process priority reduction to Below Normal.
- Access to DDNet's existing High Detail map-render toggle from the Aether optimizer panel.
- Optional in-game particle suppression.
- Optional disabling of non-music menu and Aether settings animations.
- Optional FPS fog tile and remote-player culling around the camera, with one manual radius.
- Optional FPS fog rectangle overlay for tuning the culling radius.

## Deferred

- Quad-layer and object-layer culling are intentionally deferred. The current FPS fog limits tile-layer draw ranges and hides remote players outside the selected radius while keeping the local player visible.

## Clean-Room Notes

No forbidden source was opened or used. The feature is based on the user's behavior list and DDNet component/render APIs.
