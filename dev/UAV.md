# The UAV killstreak and the minimap

MW2's UAV, with no models: the trigger is placeholder art and the UAV itself is
never drawn, only its radar.

## MW2 sources

- `mp/killstreakTable.csv`: `uav`, 3 kills, weapon `killstreak_uav_mp`.
- `maps/mp/killstreaks/_killstreaks.gsc`:
  - Kills in one life earn it, once a life (`checkKillstreakReward`'s
    `lastEarnedStreak`).
  - An earned streak is kept through death (`self.pers["killstreaks"]`).
  - It is used on the `weapon_change` to its weapon, followed by a switch back
    to the last weapon (`killstreakUseWaiter`).
- `maps/mp/killstreaks/_uav.gsc`:
  - `level.radarViewTime = 30`.
  - The team's radar (`setTeamRadar`); with two or more UAVs up it is
    `fast_radar`.
  - In free-for-all it is the owner's alone.
- `weapons/mp/killstreak_uav_mp`: raise 1.2 s, drop 0.899 s.
- `iw4x.exe`:
  - `weapon_change` fires when `ps.weapon` stops matching the client's last
    weapon (5e49b0), which is in `PM_FinishWeaponChange`.
  - The compass dvars' registered defaults:
    - `compassRadarUpdateTime` 4.
    - `compassFastRadarUpdateTime` 2.
    - `compassRadarPingFadeTime` 4.
    - `compassMaxRange` 2500.
    - `compassClampIcons` 1.
    - `compassSoundPingFadeTime` 2 (0x4e9bc2): "The time in seconds for the
      sound overlay on the compass to fade".
  - The sound ping, which needs no UAV:
    - `CG_FireWeapon` (0x59d8d9-0x59d915) records it for a player entity
      whose gun is neither `silenced` (WeaponDef +0x670) nor of
      `weapType` grenade (+0x2c). The offhands are grenade type in the dump;
      the RPG is a projectile and the guns are bullets.
    - The hold is the fire sound's playing length, looked up at 0x410330,
      or 1500 ms when it is not found (0x59d901).
    - The recorder (0x57fe60) skips the local player and spectators, stamps
      `cg.time` plus the hold, copies where they fired, and marks them an
      enemy unless both are on one team (free-for-all: always).
    - The compass draws it at full alpha until the stamp, then fades it over
      `compassSoundPingFadeTime` (0x4fe381-0x4fe3d4).

## How it plays

- **3 enemy kills in one life** earn a UAV (`G_KillstreakKill` in
  `g_killstreak.c`). Suicides, team kills and kills after your own death don't
  count. The count is `ammo[WP_KILLSTREAK_UAV]`, mirrored in
  `pers.killstreakUAVs` so it survives death.
- **D-pad right** (`killstreak`, keyboard `4`) asks for the trigger. The
  request rides in the weapon command as a toggle (`BG_SLOT_CMD_KILLSTREAK`).
  It is wanted while it differs from `PWF_KILLSTREAK_SERVED`, and the same
  command still carries the selection to go back to.
  1. The gun goes down in its own drop.
  2. The trigger is in hand: that command is MW2's `weapon_change`. One UAV
     is spent, the request is served, and `EV_USE_KILLSTREAK` goes out.
  3. From the next command the selection is wanted again. The trigger goes
     away in 0.899 s, and the gun it came from (the underbarrel included)
     comes back on its ordinary raise.
- **Y before the trigger is out** takes the request back. The drop returns to
  the gun in hand, ready on that command, and the UAV is kept. The gun is the
  one it was pulled from, so from the underbarrel that is the underbarrel
  (owner's rule). MW2 itself would select its `weaponLatestPrimary`, the base
  rifle.
- A spawn counts the toggle the client is sending as served, so no life starts
  by pulling the trigger.
- **The radar** (`G_UpdateUAVs`) covers the owner's team, or the owner in
  free-for-all, for 30 s.
  - Every 4 s (2 s with two or more up) the server sends each covered
    player the "uav" command with every living enemy's position.
  - cgame (`cg_radar.c`) shows each ping for 4 s, fading.
  - When the last UAV leaves, "uav 0" clears the radar.
  - S&D's round restart and a map restart take every UAV down.
- **The minimap** sits top left and turns with your view (you face up). Its
  edge is 2500 units from you, and anything farther is held at the edge.
  - Teammates the snapshot carries are drawn on it always.
  - An enemy who fires shows where they fired: full for 1.5 s, then fading
    over 2 s (MW2's sound ping). Every Peace gun pings, since none is
    silenced; the knife and the equipment never do. Peace holds it 1.5 s
    always (MW2's fallback; cgame cannot read a sound's length), and only
    the players its snapshot carries can ping.
  - "UAV" (or "UAV x2") shows while a radar is up.
  - An earned UAV shows its key below it ("UAV x1 [DPAD RIGHT]").

## Tests

- `dev/tests/run-masterkey.sh`: the trigger through the real `PM_Weapon`.
  - The request and its drop.
  - Y taking it back, with the UAV kept and the knife lost meanwhile.
  - Use on the command it is in hand, spent once.
  - The trigger's drop and the gun's return.
  - Nothing asked for with none left.
- `dev/tests/run-masterkey.sh` also builds `radar_test.c`, the real
  `cg_radar.c`: the UAV's pings and their fade, and the sound pings (full
  through the hold, the fade, gone after, held at the edge, turned with the
  view, a stale stamp dropped).
- `dev/tests/run-bullets.sh`: the sound ping through the real
  `CG_FireWeapon`. Every opponent's real shots ping them where they fired;
  the shooter never pings on their own minimap; the knife and every offhand
  never ping; a teammate and a spectator never do; an enemy always does.
- `dev/tests/run-snd.sh`: every round start clears the UAVs.
- `dev/tests/action_wire_test.c`: `weapFlags`' new bit round-trips.
