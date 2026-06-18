# Aether Gores Maps

Status: Implemented

## Behavior

- Adds a compact `Gores Maps` tool to the Aether settings catalog.
- Fetches the KoG map manifest from `https://raw.githubusercontent.com/wtfseanscool/kog-maps/main/manifest.json`.
- Reads manifest entries with `name`, `category`, and `stars`.
- Saves downloaded maps under `maps/GoresMaps/<category>/<map>.map`.
- Supports refresh, category filtering, selected/category/all/training downloads, selected-map deletion, and opening the target folder.
- Installs `AllOfGores.map` from the bundled clean data package when the training map is not available online.

## Notes

- This feature is based on user-authored legacy Aether behavior and is adapted to the clean Aether UI and `ae_*` ecosystem.
- `data/maps/AllOfGores.map` is a user-owned legacy Aether asset copied as a training-map fallback.
- Old AetherClient tab layout, legacy config naming, and branding are not carried over.
