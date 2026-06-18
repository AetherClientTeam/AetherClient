# Aether Fast Input

Status: Implemented first test pass on 2026-06-14.

## Behavior

Aether Fast Input is managed from one input hub with TClient, Aether Adaptive and Saiko+ modes. It does not delay real input and does not switch to full server-side rendering.

Adaptive keeps movement and hook/fire horizons fixed while applying visual correction filtering for small and medium prediction corrections. Saiko+ preserves the old user-authored Aether behavior with tick-style amount units such as `1.10`.

## Settings

- `ae_fast_input`
- `ae_fast_input_movement_amount`
- `ae_fast_input_action_amount`
- `ae_saiko_plus_amount`
- `ae_saiko_plus_others`
- `ae_fast_input_smooth_corrections`
- `ae_fast_input_ping_assist`
- `ae_fast_input_interaction_assist`
- `ae_fast_input_interaction_strength`
- `ae_fast_input_auto_margin`
- `ae_fast_input_dummy`
- `ae_fast_input_debug`

## Acceptance

- TClient fast input is preserved and can be selected from the Aether input hub.
- Aether Fast Input uses local tee and optional dummy only; other players are not fast-input rendered.
- Movement and hook/fire horizons are independently configurable from `0-50ms`.
- Correction sharpness uses the user's tested feel: low values are softer/heavier, high values are sharper.
- Ping assist only adjusts render correction follow slightly from prediction time; movement and hook/fire amounts stay user controlled.
- Interaction assist is render-only. It keeps movement/action amounts fixed, but softens local render corrections while hooking another player, being dragged, or being hammer-saved out of freeze. `ae_fast_input_interaction_strength` controls how strongly these player-interaction corrections are smoothed. Freeze-save gets a longer smoothing window for readability; teleport, death, respawn and very large corrections still snap immediately.
- Saiko+ uses `ae_saiko_plus_amount / 100.0` ticks for render offset and `(amount + 2) / 5` milliseconds as the optional auto-margin floor.
