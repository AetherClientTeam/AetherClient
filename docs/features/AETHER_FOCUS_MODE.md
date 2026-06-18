# Aether Focus Mode

## Behavior

When `ae_focus_mode` is enabled, Aether hides nonessential HUD overlays so the
gameplay view stays quiet. The world, menus, player rendering, and inputs are
not changed.

Hidden while focused:

- DDNet HUD widgets.
- Passive chat lines.
- Server broadcasts.
- Kill and finish info messages.
- TClient status bar.

If the player intentionally opens chat, the chat input remains visible so the
player is not typing blind.

## Acceptance

- Disabled by default.
- Does not modify existing DDNet HUD config values.
- Can be toggled from Aether Settings > Gameplay.
- Leaves gameplay simulation and network input unchanged.
