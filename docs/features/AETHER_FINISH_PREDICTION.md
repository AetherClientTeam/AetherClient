# Aether Finish Prediction

## Status

Initial version implemented on 2026-06-14.

## Requirement Source

User-authored behavior request: when starting a KoG or DDRace map, show a HUD
estimate for remaining finish time and percentage progress.

## Behavior

- Shows a compact HUD panel with predicted time and optional percentage.
- Supports `Time left` and `Finish time` display modes.
- Supports centiseconds, percentage, and show-always toggles.
- Uses the current race start tick, local player position and map travel
  heuristic for the first estimate.
- Can be moved and resized through the Aether HUD editor.
- Does not affect gameplay, prediction, collision, input, networking or server
  communication.

## Acceptance

- Disabled state renders no finish prediction HUD.
- Race start resets the estimator.
- Early/uncertain progress hides the panel unless `Show always` is enabled.
- HUD editor can drag, resize and reset the panel.
- The estimate is clearly a first local heuristic and can be improved later
  with route-aware map data.
