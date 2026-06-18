# Aether Snap Tap

## Behavior

When `ae_snap_tap` is enabled and both left and right movement keys are held,
the last pressed direction is sent as the active movement direction.

Examples:

- Hold left, then press right: movement becomes right.
- Keep holding left, release right: movement returns to left.
- Hold right, then press left: movement becomes left.

Only left/right movement conflict resolution is changed. Jump, hook, fire,
weapon input, chat, and menu input are not changed.

## Acceptance

- Disabled by default.
- Works for the active tee and respects DDNet's existing dummy input handling.
- Uses the same resolved direction for normal input and fast-input checks.
