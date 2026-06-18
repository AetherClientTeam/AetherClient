# Aether Loading Theme Background

Status: Implemented on 2026-06-15.

## Behavior

Shows the current DDNet menu theme background during loading screens when enabled. When disabled, loading uses the plain fallback background. If the menu theme itself is still loading, the existing loading-safety skip remains in place.

## Settings

- `ae_loading_theme_background`

## Acceptance

- Default is enabled.
- Disabling the option prevents the menu theme from rendering in loading screens.
- Partially loaded menu themes are not rendered.
