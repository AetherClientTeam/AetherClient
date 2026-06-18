# Aether Custom Aspect Ratio

## Requirement Source

User-authored behavior request on 2026-06-13.

## Behavior

- The Aether settings page exposes an enable checkbox plus custom width and height inputs.
- When enabled, the values define the logical game aspect ratio.
- The window mode, fullscreen mode, and physical display resolution are not changed.
- Values are clamped to a conservative desktop range:
  - Width: `640-7680`
  - Height: `360-4320`
- The feature uses DDNet's existing screen aspect path.

## Clean-Room Notes

No external client source, UI text, assets, or algorithms were used. The implementation is based on the user's requested behavior and DDNet graphics APIs.
