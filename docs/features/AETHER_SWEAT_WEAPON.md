# Aether Sweat Weapon

## Status

Implemented on 2026-06-14.

## Requirement Source

User-authored visual feature request: rifle and shotgun laser beams should be
client-side, glow-rich, animated, and configurable without changing gameplay.

## Behavior

- Adds a `Sweat Weapon` visual feature under Aether settings.
- Applies only to rifle/crystal laser and shotgun/sand shotgun beam rendering.
- Keeps hitboxes, weapon logic, prediction, and server-visible state unchanged.
- Provides separate glow/core colors for rifle and shotgun beams.
- Provides glow strength, sparkle intensity, and shine speed controls.
- If custom colors are disabled, the effect uses the normal DDNet rifle and
  shotgun laser colors.
- Disabled state adds no extra beam layers.

## Acceptance

- Feature is client-side only.
- Rifle and shotgun lasers show extra glow and animated shine while enabled.
- Custom colors can be toggled without affecting vanilla laser settings.
- Performance cost is bounded to a few extra quads per rifle/shotgun beam.
- Existing DDNet laser previews and actual in-game lasers render without assert.
