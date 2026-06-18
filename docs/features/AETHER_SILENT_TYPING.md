# Aether Silent Typing

Status: Implemented on 2026-06-15.

## Behavior

Suppresses the local `PLAYERFLAG_CHATTING` flag while the chat input is open, so other clients should not see the normal typing indicator above the local tee.

The local chat input and draft behavior are unchanged.

## Settings

- `ae_silent_typing`

## Acceptance

- Default is disabled.
- When enabled, opening chat does not send the typing flag to the server.
- Message sending still works normally.
