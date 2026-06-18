# Aether Ninja Tee Preview

## Status

Implemented as a clean Aether Phase 2 visual feature.

## Behavior

- Adds a compact `Ninja Tee Preview` entry to the Aether settings catalog.
- Renders the built-in DDNet ninja tee skin in the Aether menu.
- Provides independent body and feet color controls using `ae_*` settings.
- The preview is menu-only. It is not a gameplay overlay and therefore is not handled by the HUD editor.

## Provenance

- Requirement source: user-authored Aether feature list and follow-up direction.
- Implementation references: DDNet skin, tee render and menu color picker APIs.
- No third-party client branding, assets, setting names, or copied implementation are used.

## Acceptance

- The feature appears under `Visuals`.
- Enabling the row expands the feature and scrolls it into view like other Aether entries.
- Body and feet colors update the preview immediately.
- Disabling the feature stops showing it as active but keeps saved color choices.
