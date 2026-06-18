# Aether Gores Mode

## Status

Implemented on 2026-06-15.

## Behavior

Gores Mode turns the user's working `weapons.txt`/`switch.txt` cfg flow into native Aether wrapper commands:

- `+ae_gores_fire`
- `+ae_gores_hook`

The feature can install these wrappers onto `mouse1` and `mouse2` while preserving the previous binds for restore.

When enabled, `mouse2` hooks, switches to gun, and arms the next `mouse1`. The next `mouse1` follows the cfg-style gores hammer/fire flow. If `Only from gun` is enabled, this conversion is restricted to the gun/pistol. Hammer and ninja are never converted.

When disabled, both wrapper commands behave like vanilla `+fire` and `+hook`.

## Acceptance

- Existing mouse binds are saved before wrapper install and can be restored.
- `Only from gun` keeps shotgun, grenade and laser on normal fire behavior.
- Mouse wrapper commands still count as Sub-Tick aiming actions.
- Chat/menu/spectator input guards remain consistent with vanilla controls.
