# Aether Keystrokes

## Behavior

Shows a compact overlay for the active tee's left/right movement, jump, fire,
and hook input. The layout is `A/D`, `SPACE`, then `M1/M2`, using simple flat
rectangles and highlighting each key while its corresponding input is active.

## Acceptance

- Disabled by default.
- Uses DDNet's resolved active dummy input state.
- Can be dragged and resized in the Aether HUD editor.
- Does not affect input, prediction, networking, chat, or menus.
- Performs no rendering work while disabled.

## Clean-Room Notes

The feature is based on the user's description and DDNet control/render APIs.
No forbidden client source or assets were used.
