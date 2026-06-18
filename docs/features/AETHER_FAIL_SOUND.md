# Aether Fail Sounds

## Status

Implemented as a clean Aether gameplay feature.

## Behavior

- Plays when a tee transitions from not frozen to frozen in DDRace snapshots.
- Local tee/dummy, same-DDTeam players and team-last warning are controlled separately.
- Each mode has its own file name and volume.
- Files are selected from a dropdown and scanned from `assets/failsound/` through DDNet storage paths.
- The menu can open the user `assets/failsound/` folder for adding custom files.
- Supported runtime formats in this clean client are `.mp3`, `.opus` and `.wv`.
- Disabled state performs no per-frame work after the top-level config check.

## Provenance

- Requirement source: user-authored Aether feature list.
- Implementation references: DDNet extended character freeze state, team APIs and existing client sound component APIs.
- No third-party client branding, assets, setting names, or copied implementation are used.

## Acceptance

- A new local freeze plays the local sample when enabled.
- A new same-DDTeam freeze plays the team sample when enabled.
- The last-unfrozen warning plays only on the rising edge of that state.
- Volume and file name changes apply after the sample cache reloads automatically.
- The feature appears under `Gameplay` in the Aether accordion menu.
