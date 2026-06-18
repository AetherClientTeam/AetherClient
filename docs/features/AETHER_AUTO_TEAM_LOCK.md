# Aether Auto Team Lock

## Behavior

When `ae_auto_team_lock` is enabled, Aether watches the local player's DDNet
team membership. After the player joins a normal DDNet team, Aether waits
`ae_auto_team_lock_delay` seconds and sends `/lock` once in all chat.

The feature does not trigger for spectators, team `0`, or super team. Leaving a
team clears the pending lock. Joining another team schedules a new lock.

## Settings

- `ae_auto_team_lock`: enable automatic locking.
- `ae_auto_team_lock_delay`: delay in seconds after joining a team.

## Acceptance

- Disabled by default.
- Does not spam `/lock` while staying in the same team.
- Resets when disconnecting, spectating, or returning to team `0`.
- Uses DDNet client team snapshots and the existing chat send path.
