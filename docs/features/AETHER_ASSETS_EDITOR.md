# Aether Assets Editor

## Status

Initial clean-room v1 implemented on 2026-06-15.

## Behavior

- Adds an `Assets Editor` tool to the Aether Tools/Editors catalog.
- The tool opens a dedicated draggable popup instead of filling the accordion body.
- The popup layout is split into `Source` on the left and `Mixed asset` on the right.
- The editor mixes only known DDNet atlas regions. Manual rectangle editing is intentionally out of scope for v1.
- Categories:
  - Game
  - HUD
  - Particles
  - Emoticons
  - Extras
  - Skins
- The atlas categories read source packs from `assets/<category>` across DDNet storage paths.
- The skin mixer reads source skins from `skins`.
- The editor shows a source preview for the selected pack/skin and a live mixed preview for the output asset.
- Source packs/skins can be clicked or dragged onto mixed parts to choose where that part is copied from.
- Mixed parts can be selected from the preview or the part list, then tinted and assigned opacity.
- Export always writes a new PNG into the save directory and refuses to overwrite an existing file.
- Atlas exports are applied to the matching DDNet asset config after a successful export.
- Skin exports can be applied to player or dummy after export.

## Audio Asset Packs

- Adds an `Audio` tab to the existing DDNet `Settings > Assets` page.
- New config: `cl_assets_audio`, default `default`.
- Supported pack layouts:
  - `assets/audio/<pack>/audio/*.wv`
  - `assets/audio/<pack>/*.wv`
  - `assets/audio/<pack>/audio/*.wav`
  - `assets/audio/<pack>/*.wav`
- `.wv` uses the DDNet sound loader directly.
- `.wav` is decoded through the Aether audio decoder to 16-bit interleaved PCM before being submitted to the sound engine.
- Missing or unsupported pack files fall back to the default DDNet sound.
- Applying a pack stops current sounds and reloads game samples.

## Safety

- Original assets, skins and audio files are never overwritten.
- Export names are sanitized and `default` is reserved.
- The editor uses DDNet sprite metadata for atlas regions instead of user-provided rectangles.
- Large pack folders are scanned by name; individual audio files are not listed in the UI.

## Known V1 Limits

- No manual atlas rectangle editor.
- No project/preset save file for in-progress mixes.
- Part placement is fixed to known DDNet atlas regions; freeform moving/scaling inside the atlas is intentionally out of scope for v1.
