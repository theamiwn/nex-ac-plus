# Nex-AC Plus

**A behavioural anti-cheat layer for SA-MP 0.3.7+, built around a violation-score system.**

Most server-side anti-cheats kick on a single detection, so lag causes false kicks and cheaters learn the exact thresholds. Nex-AC Plus adds **points** instead. Each suspicious event adds to a per-player score that decays every second. Lag spikes fade away; real cheats pile up points within seconds and get punished.

It is a single `.inc` file with no plugins required. It can run **standalone** or **next to** [Nex-AC](https://github.com/NexiusTailer/Nex-AC).

![version](https://img.shields.io/badge/version-2.0.0-blue) ![license](https://img.shields.io/badge/license-LGPL--3.0-green) ![samp](https://img.shields.io/badge/SA--MP-0.3.7%2B-orange)

---

## Features

| Area | What it does |
|---|---|
| **Scoring** | Decaying per-player violation score, configurable warn/kick thresholds |
| **Movement** | Time-aware speed, teleport and vertical-fly detection (on foot and in vehicles), with automatic **set-back** to the last safe position |
| **Shooting** | Fake bullet origin, impossible range, silent-aim hit offsets, damage above weapon maximum, rapid fire, shooting a weapon the player does not own |
| **Server-authoritative state** | Health, armour, money, weapons and jetpack tracked from your script calls; client values above them are flagged **and reverted** |
| **Vehicles** | Remote enter, teleport into vehicle, unoccupied-vehicle teleport/speed, vehicle health regeneration |
| **Fake kill** | Invalid killer, killer out of weapon range, invalid reason, double death — sanitised before your gamemode sees them |
| **Flood / network** | Callback flood limiter, connection-flood IP blocking, RCON brute-force blocking, high-ping scoring |
| **Extensibility** | `OnACPlusFlag` (alerts) and `OnACPlusPunish` (custom ban/kick logic), per-check enable/disable, per-player exemption |

### Checks

`SpeedHack(foot)` · `SpeedHack(vehicle)` · `Teleport` · `FlyHack` · `RapidFire` · `FakeShot` · `SilentAim` · `DamageHack` · `HealthHack` · `ArmourHack` · `MoneyHack` · `WeaponHack` · `Jetpack` · `VehicleTeleport` · `VehicleHealth` · `RemoteEnterVehicle` · `FakeSpawn` · `FakeKill` · `DialogHack` · `CallbackFlood` · `HighPing`

---

## Installation

1. Download `nex-ac-plus.inc` and put it in `pawno/include/`.
2. In your **gamemode**, include it right after `a_samp`:

```pawn
#include <a_samp>
#include <nex-ac-plus>
```

3. Recompile.

With [sampctl](https://github.com/Southclaws/sampctl):

```bash
sampctl package install theamiwn/nex-ac-plus
```
---

## Quick start

```pawn
#include <a_samp>
#include <nex-ac-plus>

// Alert admins on each violation (warn = 1 once the score reaches ACP_SCORE_WARN)
forward OnACPlusFlag(playerid, check, points, score, warn);
public OnACPlusFlag(playerid, check, points, score, warn)
{
    if(warn) SendClientMessageToAll(0xFFAA00FF, "[AC] A player is being flagged.");
    return 1;
}

// Handle the punishment yourself. Return 0 to skip the default delayed kick.
forward OnACPlusPunish(playerid, check, score);
public OnACPlusPunish(playerid, check, score)
{
    // write to your ban database here
    return 1; // 1 = default kick
}
```

**Tip: run it in log-only mode first.** Return `0` from `OnACPlusPunish` and just print. This shows how your players actually behave before anyone gets kicked, so you can tune thresholds safely.

---

## Configuration

Override any of these with `#define` **before** the include:

| Define | Default | Meaning |
|---|---|---|
| `ACP_SCORE_KICK` | `100` | Score that triggers punishment |
| `ACP_SCORE_WARN` | `60` | Score at which `warn=1` is passed to `OnACPlusFlag` |
| `ACP_DECAY_PER_SEC` | `2` | Points removed each second |
| `ACP_KICK_DELAY` | `700` | ms between punishment and `Kick()` |
| `ACP_MAX_PING` | `800` | Ping above this adds score every second |
| `ACP_FOOT_SPEED` | `14.0` | Max horizontal speed on foot (m/s) |
| `ACP_JETPACK_SPEED` | `45.0` | Max speed with jetpack (m/s) |
| `ACP_VEHICLE_SPEED` | `120.0` | Max vehicle speed (m/s) |
| `ACP_TELEPORT_MIN_DIST` | `35.0` | Jumps shorter than this are never "teleports" |
| `ACP_SETBACK` | `true` | Move teleporters back to the last safe position |
| `ACP_NO_NATIVE_HOOKS` | off | Disable native hooks (see notes below) |
| `ACP_DIALOG_CHECK` | off | Enable dialog-ID validation |

```pawn
#define ACP_SCORE_KICK 150
#define ACP_FOOT_SPEED 16.0
#include <nex-ac-plus>
```

---

## API

```pawn
ACP_EnableCheck(check, bool:enable);       // enable/disable one check globally
ACP_IsCheckEnabled(check);
ACP_SetPlayerExempt(playerid, bool:exempt); // e.g. for admins
ACP_IsPlayerExempt(playerid);
ACP_GetScore(playerid);
ACP_ResetScore(playerid);
ACP_AllowTeleport(playerid, ms = 2000);    // grace period after a server-side teleport
ACP_AllowJetpack(playerid, bool:allow);
ACP_ResyncState(playerid);                 // re-learn health/armour/weapons/money
```

Callbacks you can implement:

```pawn
forward OnACPlusFlag(playerid, check, points, score, warn);
forward OnACPlusPunish(playerid, check, score);   // return 0 to cancel the default kick
```

Check IDs are the `ACP_*` constants (for example `ACP_TELEPORT`, `ACP_RAPIDFIRE`, `ACP_WEAPON`).

---

## Important notes

- **Include it in the gamemode.** State checks (health, armour, money, weapons, jetpack, fake spawn) hook natives such as `SetPlayerHealth` and `GivePlayerWeapon`, so they assume one script owns those calls.
- **Filterscripts that change player state:** call `ACP_ResyncState(playerid)` / `ACP_AllowTeleport(playerid)` from them, or define `ACP_NO_NATIVE_HOOKS` to keep only the purely behavioural checks.
- **Using it together with Nex-AC:** both hook some of the same natives. If you see conflicts, define `ACP_NO_NATIVE_HOOKS` or disable the overlapping checks in one of them.
- **Dialog validation is opt-in** because dialogs shown by filterscripts would otherwise be flagged.
- **Tune before enforcing.** The fire-rate and damage tables are deliberately conservative; watch your logs for a few days before tightening.
- **Pay'n'Spray** repairs add a few points to the vehicle-health check. They decay, but tune it if it is noisy for you.

## Limitations

Server-side checks raise the cost of cheating a lot, but they **cannot** stop aimbots, wallhacks or modified clients on their own. For that you need a client-side or plugin-based solution. Treat this as one layer of a defence, not the whole defence.

---

## Compatibility

- SA-MP 0.3.7 (R2+ recommended) and compatible servers
- Needs `GetPlayerPoolSize`, `BlockIpAddress`, `OnIncomingConnection` and `OnPlayerGiveDamage` (all 0.3.7+)
- Compiles warning-free with the standard `a_samp` stdlib on Pawn compiler 3.10.10

## Contributing

Issues and pull requests are welcome. If you report a false positive, please include the server-log line (it contains the check name and reason) and the player's approximate ping.

## Credits

Inspired by [Nex-AC](https://github.com/NexiusTailer/Nex-AC) by NexiusTailer. Nex-AC Plus is an independent implementation, not a copy of its code.

## License

[LGPL-3.0](LICENSE)
