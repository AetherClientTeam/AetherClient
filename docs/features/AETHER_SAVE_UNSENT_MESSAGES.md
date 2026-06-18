# Aether Save Unsent Messages

## Requirement Source

User-authored behavior request on 2026-06-13.

## Behavior

- When enabled, all chat and team chat keep separate single drafts.
- The all-chat draft is stored in `m_aDraftAll`.
- The team-chat draft is stored in `m_aDraftTeam`.
- Typing updates the active mode's draft immediately.
- Empty input clears the active mode's draft.
- Closing chat with `Escape`, closing the mode, map changes, and reconnects preserve the active draft.
- Reopening the same chat mode restores its draft and moves the cursor to the end.
- Sending a message with `Enter` clears only the active mode's draft.
- Chat history navigation does not permanently overwrite the draft; returning from history restores the current unsent text.

## Acceptance

- All chat and team chat drafts do not overwrite each other.
- A draft survives `Escape`, map changes, and reconnect-triggered chat reset.
- A sent message clears the relevant draft.
- A blank chat input clears the relevant draft.
- History up/down can inspect old messages without deleting the unsent draft.

## Clean-Room Notes

No external client source, UI text, assets, or algorithms were used. The implementation is based on the user's requested behavior and DDNet chat/input APIs.
