# Aether Browser Utils

## Behavior

- `ae_browser_utils` enables server browser utilities.
- `ae_browser_auto_refresh` refreshes the active server browser tab while menus are open.
- `ae_browser_refresh_seconds` is clamped to 15-120 seconds.
- `ae_browser_short_kog_names` shortens KoG server names only for display in the server list.
- Short KoG names use `<REGION> - <State>`, for example `GER4 - Insane`.
- Test servers show `Test`; maintenance or misspelled maintaince servers show `Maintenance`.

## Edge Cases

- Disabled by default and does no timer work while disabled.
- Does not refresh while the server browser is already refreshing or downloading a list.
- Does not mutate server browser data, favorites, addresses, filters, or connect behavior.
- If a KoG name cannot be confidently parsed, the original server name is shown.

## Acceptance

- Auto-refresh respects the configured interval.
- KoG server display names are shortened without breaking joining or favorites.
- Non-KoG server names remain unchanged.
