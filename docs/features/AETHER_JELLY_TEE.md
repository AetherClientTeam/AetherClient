# Aether Jelly Tee

## Status

Implemented on 2026-06-14.

## Requirement Source

User-authored visual feature request: make the tee squash and stretch like slime
while moving left and right.

## Behavior

- Applies client-side X/Y render scaling to tee body, eyes and feet.
- Uses horizontal movement speed, vertical movement and sudden downward impact
  changes as stretch/squash sources.
- Uses a small spring model to add visible jiggle before returning to normal.
- Defaults to local tee only, with an option to apply to other players.
- Does not affect physics, collision, input, prediction, or networking.

## Acceptance

- Disabled state renders vanilla tee scale.
- Moving left/right stretches horizontally and compresses vertically.
- Falling, rising and abrupt downward velocity changes affect the Y axis.
- Stopping movement returns with a subtle jiggle instead of a linear fade.
- Stopping movement smoothly returns to normal scale.
- `Jelly others` extends the effect to other rendered players.
