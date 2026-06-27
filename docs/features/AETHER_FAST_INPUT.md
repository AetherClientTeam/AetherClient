# Aether Fast Input

Status: Implemented first test pass on 2026-06-14.

## Behavior

Aether Fast Input is managed from one input hub with TClient, Aether Adaptive, Saiko+, Control and Lewn+ modes. It does not delay real input and does not switch to full server-side rendering.

Adaptive keeps movement and hook/fire horizons fixed while applying visual correction filtering for small and medium prediction corrections. Saiko+ preserves the old user-authored Aether behavior with tick-style amount units such as `1.10`. Lewn+ is an experimental direct-response mode for extreme play: movement and action use the same immediate fast input horizon, while local render correction follows sharply enough to avoid a delayed feel.

Lag guard is a separate opt-in stability layer from auto margin. When enabled with any Aether or legacy TClient fast input mode, it ignores a few isolated input-timing spikes before applying normal prediction-time correction and temporarily falls other tees back to stable render positions when fast-input history becomes unreliable after network spikes.

## Settings

- `ae_fast_input`
- `ae_fast_input_movement_amount`
- `ae_fast_input_action_amount`
- `ae_saiko_plus_amount`
- `ae_saiko_plus_others`
- `ae_lewn_plus_amount`
- `ae_lewn_plus_correction`
- `ae_lewn_plus_others`
- `ae_fast_input_control_ping_assist`
- `ae_fast_input_control_interaction_assist`
- `ae_fast_input_control_interaction_strength`
- `ae_fast_input_smooth_corrections`
- `ae_fast_input_ping_assist`
- `ae_fast_input_interaction_assist`
- `ae_fast_input_interaction_strength`
- `ae_fast_input_auto_margin`
- `ae_input_lag_guard`
- `ae_fast_input_dummy`
- `ae_fast_input_debug`

## Acceptance

- TClient fast input is preserved and can be selected from the Aether input hub.
- Mode-specific tuning is isolated: Adaptive, Saiko+, Control and Lewn+ do not read each other's amount, correction, smoothing or assist settings.
- Aether Fast Input always prioritizes the local tee. Dummy and other-tee prediction are mode-controlled options.
- Movement and hook/fire horizons are independently configurable from `0-50ms`.
- Correction sharpness uses the user's tested feel: low values are softer/heavier, high values are sharper.
- Adaptive ping assist adjusts render correction follow slightly from prediction time; movement and hook/fire amounts stay user controlled. Control has its own `ae_fast_input_control_ping_assist` toggle so the old Control feel can be restored without changing Adaptive, Saiko+, Lewn+ or TClient.
- Adaptive interaction assist is render-only and keeps movement/action amounts fixed while softening local render corrections during close player interactions. Control has separate interaction assist and strength settings. Freeze-save gets a longer smoothing window for readability; teleport, death, respawn and very large corrections still snap immediately.
- Saiko+ uses `ae_saiko_plus_amount / 100.0` ticks for render offset and `(amount + 2) / 5` milliseconds as the optional auto-margin floor.
- Lewn+ uses `ae_lewn_plus_amount / 100.0` ticks for render offset and `(amount + 2) / 5` milliseconds as the optional auto-margin floor. The first Lewn test tuning uses `1.40` amount as the minimum with `75%` correction so it stays direct without the low-amount glide, while the render follow is slightly softened at the edge. Its optional other-tee offset is slightly softer than the local offset for smoother hooks and drags. Lewn+ does not read Adaptive ping assist or interaction strength; its interaction correction uses a fixed internal profile to preserve the tested feel.
- Lag guard defaults off. When off, input timing and other-tee rendering stay on the existing path. When on, it does not change configured fast input amounts, auto margin, or local tee render correction; it only reduces isolated spike recovery and other-tee jitter from unreliable prediction history.
