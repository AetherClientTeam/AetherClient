# Aether Player Skin/Color Copy

## Behavior

Adds two player action buttons to the existing scoreboard/ESC player popup:
`Copy skin` and `Copy colors`.

`Copy skin` copies the selected player's skin name to the currently active local
profile. `Copy colors` copies the selected player's custom-color enabled state,
body color, and feet color to the currently active local profile.

## Acceptance

- Works from the scoreboard player popup.
- Works from the ESC player list because it uses the same popup.
- Updates the main player when dummy mode is inactive.
- Updates the dummy player when dummy mode is active and connected.
- Sends the updated player info immediately after copying.
- Does not copy names, clans, country flags, binds, or any skin files.

## Clean-Room Notes

The feature is based on the user's behavior description and DDNet client data,
scoreboard popup, and player-info APIs. No forbidden client source, text, or
assets were used.
