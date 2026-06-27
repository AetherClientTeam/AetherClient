# Aether Music Player v1 Contract

## Scope

Aether Music Player v1 adds a compact game HUD timer container and Windows
media artwork visualization. It does not provide playback controls, playlists,
a system-wide audio meter, or audio persistence.

## Settings

- The feature is named `Music Player` under `Visuals` in the Aether settings
  catalog.
- Its enable checkbox and expansion arrow are independent.
- It is disabled by default and all new persistent settings use `ae_*` names.
- Expanded settings expose:
  - dynamic cover-derived background color, enabled by default;
  - a static background color;
  - panel opacity from 10 through 100 percent;
  - an optional freeze counter inside the panel, disabled by default;
  - an optional media title row, enabled by default;
  - process-audio visualizer, enabled by default;
  - Bars and Mountain visualizer styles;
  - visualizer sensitivity from 50 through 1500;
  - visualizer glow from 0 through 100 percent;
  - top-center anchored horizontal and vertical offsets.
- The `HUD Editor` row appears under `Editors` and exposes only `Open`.
- The target panel size is approximately 108 by 30 HUD units at 100 percent
  scale.

## Panel

- While enabled, the panel remains visible as the game timer container.
- The left region shows the active media thumbnail when available. Otherwise
  it shows a neutral, code-drawn `A` monogram.
- The upper center region contains the existing DDNet game timer presentation.
- The lower center region shows the active media title and artist. Long names
  scroll inside the panel with alpha-faded edges; short names remain static.
  Styled mathematical/fullwidth media letters are normalized to readable ASCII
  so unsupported glyphs do not render as square boxes.
- When enabled, the freeze counter shows frozen tees over total tees for the
  local or spectated DDNet team inside the upper row.
- The right region renders the selected Bars or Mountain visualization.
- Bars uses five centered rounded bars. Mountain uses a centered, edge-faded
  filled spectrum silhouette. Both styles use the analyzed bass/RMS energy with
  fast attack and smoother release so low-frequency hits visibly rise and fall.
- All visualizer styles use the configured glow amount.
- The panel contains no playback controls.
- Enabling the feature transfers game-timer rendering ownership to the panel so
  the vanilla timer is not rendered a second time.
- `cl_showhud_timer 0` hides only the timer text. It does not hide the panel.

## Media State

- Playing media renders the panel and artwork at full opacity.
- Paused or stopped media keeps the last valid artwork dimmed and sets all
  five bars to zero. If the active media session changes to a track without
  artwork, the old thumbnail is cleared instead of sticking to the previous
  track.
- After five minutes without active playback, the artwork changes to the
  fallback monogram.
- A session that has never received usable media artwork uses the fallback
  monogram immediately.
- Background color is either the configured static color or an independently
  computed dominant thumbnail color darkened to preserve foreground
  readability.

## HUD Editing

- A small reusable HUD layout/editor core owns top-center anchoring, offset
  persistence, screen clamping, dragging, and reset-to-default behavior.
- The whole music panel is one draggable item.
- A visible bottom-right handle resizes the selected panel from 50 through 200
  percent while preserving its aspect ratio.
- The mouse wheel remains a secondary resize input while the editor is open.
- Escape closes the HUD editor.
- Opening the editor is supported while connected or viewing a demo.
- When no game or demo view is available, the Open action provides concise
  feedback and does not enter the editor.
- The editor performs no per-frame work while closed.

## Windows Media Integration

- Windows x64 is the first supported media platform.
- Metadata and thumbnails come from
  `GlobalSystemMediaTransportControlsSessionManager`.
- Audio levels come only from process-specific WASAPI loopback for the active
  media session process and its process tree.
- Product identity in the media session may be matched to the executable's
  installation path when Chromium-based applications use a generic executable
  name.
- Process capture may verify playback even when the media session incorrectly
  reports `Paused`; recent process audio wins for 500 milliseconds.
- The implementation never falls back to whole-system loopback and never
  substitutes fake animation.
- If process identification, process-loopback activation, or capture fails, the
  visualizer is hidden while metadata, artwork, panel, and timer remain
  available.
- Captured samples are analyzed in memory only. Audio is never recorded,
  stored, or transmitted.
- Thumbnail bytes may be acquired off the render thread. Texture creation,
  replacement, and destruction happen only on the render thread.
- Disabling the feature and shutting down the client stop workers and release
  COM apartments, WinRT objects, process handles, capture clients, events, and
  audio resources.

## Other Platforms

- Non-Windows builds compile without Windows media dependencies.
- Media is reported unavailable, the fallback monogram is used, and the panel
  continues to provide timer-container behavior.

## Acceptance

1. The feature is disabled by default and disabled state performs no media
   polling, capture, or panel rendering.
2. The panel owns timer rendering without duplicating the vanilla timer.
3. Hiding the timer text leaves the panel visible.
4. Every visualizer style uses the same real five-band process audio and never
   shows fake levels.
5. Paused and stopped states retain dimmed art and zero the bars.
6. Five inactive minutes replace retained art with the fallback monogram.
7. Dynamic color is cover-derived and darkened; static color remains selectable.
8. Failed or unsupported process loopback hides only the visualizer.
9. Dragging and handle resizing are clamped, reset restores defaults, and
   Escape closes the editor.
10. Leaving Aether settings resets search and accordion state.
11. Disable and shutdown release all feature-owned platform resources.
12. Pure timer, metadata display, marquee, band mapping, color, inactivity,
    clamping, search, and accordion behavior is covered by focused tests.
