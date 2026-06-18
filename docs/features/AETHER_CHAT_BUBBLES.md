# Aether Chat Bubbles

Status: Implemented on 2026-06-15.

## Behavior

Displays normal and team player chat messages as compact bubbles above the sender tee. Server, client, system and whisper messages do not create bubbles.

## Settings

- `ae_chat_bubbles`
- `ae_chat_bubbles_duration`
- `ae_chat_bubbles_opacity`
- `ae_chat_bubbles_width`
- `ae_chat_bubbles_visible_only`
- `ae_chat_bubbles_colored_messages`
- `ae_chat_bubbles_stack_count`
- `ae_chat_bubbles_show_own_live`
- `ae_chat_bubbles_show_own_sent`

## Acceptance

- Default is disabled.
- New messages from the same player stack with the newest message at the bottom, up to four bubbles.
- Long messages wrap to at most two lines and then truncate.
- Optional visible-only mode hides bubbles for tees outside the current screen.
- Optional colored mode tints team messages green and direct local name mentions red.
- Optional local draft mode shows the current unsent chat input above the local tee.
- Focus Mode hides bubbles.
- Online and demo playback render from the sender client id when that tee exists.
