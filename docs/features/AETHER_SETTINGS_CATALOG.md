# Aether Settings Catalog Contract

## User Behavior

- DDNet's existing top-level settings navigation remains unchanged.
- Aether is one top-level settings page containing a single scrollable catalog.
- The header contains `Aether Settings` and a search field.
- `Visuals`, `Gameplay`, `Tools`, and `Editors` are section headings, not tabs.
- A normal feature row contains an enable checkbox, feature name, and expansion
  arrow.
- Checkbox and expansion actions are independent.
- A disabled feature can still be expanded to inspect its settings.
- Only one feature can be expanded at a time.
- Editor rows have an `Open` button and no enable checkbox.

## Search

- Search is case-insensitive.
- Feature names and child setting labels are searchable.
- A section heading is hidden when it has no matching feature.
- A result is not automatically expanded.
- Clearing search restores the complete catalog.

## State

- Feature enable values are persisted in `ae_*` config variables.
- Expansion and search are transient UI state.
- Leaving the Aether page or closing settings clears expansion and search.
- Re-entering the page starts with all rows collapsed and an empty search.

## Layout

- No feature occupies a full page.
- No internal tab bar is used.
- No popup is used as a normal settings page.
- No irregular two-column card flow is used.
- Rows and controls follow DDNet spacing and UI-scale behavior.
- The catalog must remain usable at 1280x720 with 150% UI scale.

## Architecture

- `EAetherFeatureId` identifies features without depending on display text.
- One metadata table defines feature id, section, label, enable config, editor
  action, and searchable child labels.
- One accordion controller owns the transient expanded feature id.
- Each feature has a separate body renderer.
- Disabled features perform no per-frame work outside the settings renderer.

## Acceptance

1. Checkbox clicks never expand or collapse a row.
2. Arrow/row expansion never changes the enable config.
3. Expanding a second feature collapses the first.
4. Search finds both feature and child-setting labels.
5. Search does not auto-expand results.
6. Sections with no results disappear.
7. Leaving and returning clears search and expansion.
8. Enable values survive leaving settings and restarting the client.
9. Editor rows expose only their `Open` action.
10. Layout is legible in all four approved mockup dimensions.

