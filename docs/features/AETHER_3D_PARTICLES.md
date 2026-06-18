# Aether 3D Particles

## Status

Implemented on 2026-06-14.

## Requirement Source

User-authored visual feature request: draw ambient 3D-like particles with three
depth layers. The far layer should move the least, the middle layer should move
normally, and the near layer should move faster.

## Behavior

- Draws lightweight procedural particles in the game world behind players.
- Uses three parallax layers with different apparent camera movement, size,
  opacity and drift speed.
- Supports cube, heart and mixed particle types.
- Supports custom color or lightweight random color mode.
- Supports particle count, size, speed, alpha and optional glow.
- Uses only quads/circles generated at runtime; no external assets.
- Does not affect gameplay, prediction, collision, input, networking or server
  communication.

## Acceptance

- Disabled state renders no particles.
- Far particles move slower than the map, middle particles move moderately, and
  near particles move faster.
- Particle count, type, color mode, custom color, alpha, size, speed and glow
  update from the Aether settings accordion.
- The effect remains visual-only and does not create per-particle heap churn.
