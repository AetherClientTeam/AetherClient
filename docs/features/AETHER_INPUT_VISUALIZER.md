# Aether Input Visualizer

## Behavior

Shows a compact HUD timeline of recent input states. It uses a flowing segment
history rather than per-frame boxes, so long holds stay smooth and rounded
corners remain visible while the bars move. It samples the active dummy's
resolved DDNet input, or the followed/demo player's character state when that
option is enabled. It renders lanes for left, right, optional jump, optional
M1, and optional M2.

The overlay supports flow speed, horizontal or vertical layout, configurable
panel length, lane thickness, opacity, background visibility, corner style,
hook markers, local/spectated input selection, and per-lane colors. Labels can
be hidden for a cleaner overlay.

## Acceptance

- Disabled by default.
- Can be dragged and resized in the Aether HUD editor.
- Uses the active dummy input state without changing controls or prediction.
- Can show followed player inputs in spectate/demo playback from snapshot
  character state.
- Performs no sampling or rendering work while disabled.
- Keeps jump, M1 and mouse-button lanes optional to stay compact.
- Draws flowing active ranges instead of per-sample rectangles to reduce render
  cost and keep rounded edges visible on moving bars.
- Visual customization changes apply immediately from the Aether settings page.

## Clean-Room Notes

The feature behavior is user-authored legacy Aether behavior ported at the
user's request and adapted to the clean Aether `ae_*` config and HUD Editor
systems. No third-party branding or assets are used.
