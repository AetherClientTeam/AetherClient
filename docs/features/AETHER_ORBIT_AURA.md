# Aether Orbit Aura

## Status

Implemented on 2026-06-14.

## Requirement Source

User-authored visual feature request: draw configurable orbit, energy ring and
flame effects around the main local tee.

## Behavior

- Draws only around the local/main player tee.
- Can be restricted to idle mode with configurable delay and fade duration.
- Provides three styles: orbit particles, energy ring and upward flame ribbons.
- Supports configurable radius, particle count, alpha, speed, particle size,
  primary color, and accent color.
- Uses lightweight quad/freeform rendering and no external assets.
- Does not affect gameplay, prediction, collision, networking, or other players.

## Acceptance

- Feature disabled state adds no aura rendering.
- Aura renders behind the local tee.
- Idle-only mode hides the aura while moving.
- Idle entry and exit animate smoothly.
- All styles are selectable from the Aether settings accordion.
