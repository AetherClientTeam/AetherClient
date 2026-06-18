# Aether Real Hitbox

## Behavior

- `ae_show_real_hitbox` draws only the local tee hitbox marker.
- The marker is centered on the rendered local player position and includes a small full-opacity center dot.
- `ae_real_hitbox_color` controls the marker color.
- The feature is a world overlay, so it is not moved in the HUD editor.

## Edge Cases

- Disabled by default and does no render work while disabled.
- Only runs while online or in demo playback.
- Skips inactive local clients and clients absent from the current snapshot.

## Acceptance

- Enabling the checkbox shows a marker on the local tee only.
- Color picker changes the outline color immediately.
- Disabling the feature removes the overlay without affecting vanilla player rendering.
