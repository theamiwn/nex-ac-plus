# Changelog

All notable changes to Nex-AC Plus are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Planned
- Optional logging of violations to a file in `scriptfiles`
- Per-weapon tunable fire-rate and damage tables via `#define`
- Packet-loss scoring
- Statistics summary printed on server shutdown

## [2.0.0] - 2026-10-18

First public release.

### Added
- **Violation score system**: per-player score with configurable decay (`ACP_DECAY_PER_SEC`), warn threshold (`ACP_SCORE_WARN`) and punish threshold (`ACP_SCORE_KICK`).
- **Movement analysis**: time-aware detection of on-foot speed, vehicle speed, teleport and vertical fly, with automatic set-back to the last safe position for on-foot teleports (`ACP_SETBACK`).
- **Shot validation**: fake bullet origin, hits beyond weapon range, silent-aim hit offsets, targets out of range, invalid hit player or vehicle, rapid fire, and shooting a weapon the player does not own.
- **Damage validation**: damage above a weapon's maximum, invalid body part, damage to an invalid player.
- **Server-authoritative state** (via native hooks): health, armour, money, weapons and jetpack are tracked from script calls; client values above them are flagged and reverted.
- **Vehicle checks**: remote enter, teleport into a vehicle, unoccupied-vehicle teleport or speed, vehicle health regeneration.
- **Fake kill and fake spawn handling**: invalid killer, killer out of weapon range, invalid death reason, double death, and spawn without a request. The killer ID is sanitised before the gamemode sees it.
- **Flood and network protection**: callback rate limiting (text, commands, dialogs, vehicle enter/exit, spawn and class requests, pickups), connection-flood IP blocking, RCON brute-force IP blocking, and high-ping scoring.
- **Dialog input hardening**: `%` is neutralised in dialog input text. Optional dialog-ID validation with `ACP_DIALOG_CHECK`.
- **Callbacks**: `OnACPlusFlag` for alerts and `OnACPlusPunish` for custom punishment (return `0` to cancel the default kick).
- **Public API**: `ACP_EnableCheck`, `ACP_IsCheckEnabled`, `ACP_SetPlayerExempt`, `ACP_IsPlayerExempt`, `ACP_GetScore`, `ACP_ResetScore`, `ACP_AllowTeleport`, `ACP_AllowJetpack`, `ACP_ResyncState`.
- **Configuration** through `#define` before the include (speed limits, thresholds, kick delay, ping limit).
- `ACP_NO_NATIVE_HOOKS` option for running next to other anti-cheats or filterscripts that change player state.
- `example.pwn` demonstrating usage.
- `pawn.json` for sampctl package installation.

### Notes
- Verified to compile without warnings on Pawn compiler 3.10.10 with the standard SA-MP stdlib, as a gamemode, as a filterscript, with `ACP_NO_NATIVE_HOOKS`, and with `ACP_DIALOG_CHECK`.
- Fire-rate and damage tables are deliberately conservative. Watch your logs before tightening them.

[Unreleased]: https://github.com/theamiwn/nex-ac-plus/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/theamiwn/nex-ac-plus/releases/tag/v2.0.0
