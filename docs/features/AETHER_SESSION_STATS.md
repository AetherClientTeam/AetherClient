# Aether Session Stats

## Behavior

Tracks lightweight personal session stats in a compact HUD panel. The panel
shows optional session time and local deaths. Map name, finishes and team stats
are intentionally excluded to keep the overlay simple.

## Acceptance

- Disabled by default.
- Uses DDNet kill messages for local death counting.
- Shows personal session time and deaths only.
- Does not show map name, finishes, or team stats.
- Uses a subtle translucent background without an accent bar.
- Can be dragged and resized in the Aether HUD editor.
- Performs no message accounting while disabled.
- Does not affect vanilla statboard, scoreboard, demo, or race recording.

## Clean-Room Notes

The feature behavior is user-authored legacy Aether behavior ported at the
user's request, with team stats and map name removed for the clean UI. No
third-party branding or assets are used.
