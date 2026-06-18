# Aether Stability Trainer

## Behavior

Shows a compact velocity stability overlay for local play, spectating, and demo
playback. The overlay displays a horizontal velocity bar. The moving block and
quality color follow horizontal velocity so the visual stays simple and direct.

The trainer is movable and resizable through the Aether HUD Editor. Its visual
settings include quality colorization, solid color fallback, bar glide,
average ticks, minimum speed, velocity scale, corner style, bar thickness,
track width, and moving block width.

## Acceptance

- Disabled by default.
- Uses `ae_*` config variables only.
- Performs no stability update or render work while disabled.
- Supports the followed player in spectate and demo playback when enabled.
- Local players update on predicted ticks for stable smoothing.
- HUD Editor can drag, resize, reset with `R`, and close with `Esc`.
- If both left and right are held locally, the trainer warns and centers the
  bar instead of presenting misleading precision.

## Clean-Room Notes

The feature behavior is user-authored legacy Aether behavior ported at the
user's request and adapted to the clean Aether component, menu, and HUD Editor
systems. No third-party branding or assets are used.
