# Aether Gameplay Sounds

## Status

Implemented as clean Aether gameplay sound controls. Keyboard typing sound is a separate Aether feature row.

## Behavior

- Top-level `ae_sound` enables Aether gameplay sound filtering.
- Other-player hook, other-player hammer, local hammer, local weapon switch, other-player weapon switch and double-jump sounds can be toggled separately.
- With `ae_sound` disabled, gameplay sounds keep vanilla behavior.

## Keyboard Sounds

- `Keyboard Sounds` appears as its own Aether accordion feature.
- It has its own enable flag, volume and file dropdown.
- Files are scanned from `assets/keyboard/` through DDNet storage paths, so both user data and packaged data folders can provide files.
- The menu can open the user `assets/keyboard/` folder for adding custom files.
- Supported runtime formats in this clean client are `.mp3`, `.opus` and `.wv`.
- With keyboard sound disabled, no keyboard sample is loaded or played.

## Provenance

- Requirement source: user-authored Aether feature list.
- Implementation references: DDNet world/predicted sound events, chat input rendering and existing sound component APIs.
- No third-party client branding, assets, setting names, or copied implementation are used.

## Acceptance

- Toggling gameplay sound rows suppresses only the corresponding sounds.
- Keyboard typing plays the selected sample only when chat input grows.
- Changing the selected keyboard file reloads the sample automatically.
- The feature appears under `Gameplay` in the Aether accordion menu.
