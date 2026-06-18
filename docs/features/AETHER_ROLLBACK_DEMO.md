# Aether Rollback Demo

## Behavior

Keeps DDNet's existing replay recorder ready while `ae_rollback_demo` is enabled
and provides `ae_save_rollback_demo ?seconds` to save the last seconds as a demo
clip.

The default clip length is 30 seconds and can be configured with
`ae_rollback_demo_seconds`.

## Acceptance

- Disabled by default.
- Uses DDNet's built-in replay recorder instead of a second recorder.
- Enables `cl_replays` and raises `cl_replay_length` when needed.
- Saves through DDNet's existing `save_replay` console command.
- Provides a key binding row in Aether settings for `ae_save_rollback_demo`.
- Accepts clip lengths from 10 to 600 seconds.
- If replay recording was not active yet, enables it and asks the user to try
  again after the buffer has had time to fill.

## Clean-Room Notes

The feature is based on the user's rollback-demo requirement and DDNet's replay
recorder/console APIs. No forbidden client source, text, or assets were used.
