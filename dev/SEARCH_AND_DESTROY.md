# Search and Destroy

Branch: `dev-gpt`. Run in **x64 Native Tools Command Prompt for VS**:

```bat
cd /d C:\Users\ZackI\Desktop\OneManIW\ProjectPeace\dev2\Peace
git pull origin dev-gpt
cmake --build out\build\x64-Debug --clean-first
```

## Play

In the base-game menu: **Multiplayer → Create → Game Type: Search and Destroy**,
select **peace_ladder** or **peace_sd**, then set the round target and duration
and start. Join Red or Blue. Normal play needs at least one player on each team.
Alternatively set `g_gametype 8`, `g_sndPractice 0`, then `map peace_ladder`.
Everyone must rebuild the client, server and game modules together (the current
`PROTOCOL_VERSION`; see CLAUDE.md's capacity limits).

**Site A is on the upper ladder platform**, marked by a gold outline and an A.
Attackers spawn on the floor opposite the ladder; defenders spawn on the platform.
The room is deliberately a mechanics test lab, not a balanced competitive map.
It has four starts per side and no retail map geometry.

**peace_sd** is a long hall with both sites on the defenders' half: A (orange
pillar) and B (cyan pillar), each with a pad and a crate to plant behind, cover
in the middle and a stair up to a perch. Attackers start at the -X end, five per
side.

- One life per round. Eliminated players can watch living teammates; Attack
  cycles teammates. Joining or changing team mid-round waits until the next round.
- Red attacks first. Sides switch after every three rounds; team scores stay put.
- One attacker carries the bomb, separately from their two-weapon loadout.
  Death, disconnect or leaving the team drops it. A living attacker picks it up
  by approaching it; defenders cannot take it. The initial carrier rotates.
- At A, the carrier holds **Use** (default **F**, gamepad **D-pad Up**) or
  use/reload (gamepad **X**, as on console MW2) for five seconds to plant. Stand
  still with the weapon ready, off the ladder. At a site or the planted case an
  X press is a use, never a reload.
- Defenders hold the same button near the planted case for five seconds to defuse.
  Releasing, moving, attacking, reloading, swapping, using equipment, ADS or
  sprinting interrupts progress. Progress is not saved or shared between players.
- Defenders win by preventing a plant until time expires, eliminating attackers
  before the plant, or defusing. Attackers win by eliminating defenders or by
  detonation. Killing all attackers **after** a plant does not cancel the bomb.
- As in MW2's `sd.gsc`, the bomb's own wins wait: a detonation ends the round
  3 s later (`SND_DETONATION_DELAY`), a defuse 1.5 s later (`SND_DEFUSE_DELAY`).
  Play goes on meanwhile, damage and all, but no elimination ends the round or
  makes a killcam (`onDeadEvent` returns). The bomb is stopped and can no longer
  be defused; the HUD reads BOMB DETONATED or BOMB DEFUSED.
- The HUD shows roles, round scores, living players, objective markers, the active
  timer and hold progress. The first team to four round wins takes the match.
- **The round end is MW2's (`_gamelogic.gsc` endGame).** The moment a round
  ends (the last death of an elimination; the frame time runs out; the end of a
  detonation's or defuse's delay) it is over: no damage, no plant or defuse. But
  every survivor keeps full control - moving, shooting, reloading, a knife or
  grenade thrown - for endGame's waitframe and `freezePlayerForRoundEnd( 1.0 )`:
  1.05 s (`ROUNDEND_FREEZE_DELAY`). Then they freeze. So a throwing knife pressed
  right after the round-winning shot comes out (and hurts nobody). Peace's
  freeze stops the player's Pmove whole; MW2's `PMF_FROZEN` keeps only stance
  and still runs the weapon, so a knife or grenade held at the freeze is still
  thrown there and here stays in hand (not modelled yet).
- **An elimination is decided in the death itself**, as MW2's is
  (`PlayerKilled` -> `removeFromAliveCount` -> `onDeadEvent` -> endGame,
  postgame before anything else). So nothing later in the same frame - a
  knife or rocket still in the air, a grenade the dying defender dropped, the
  rest of the same blast - can hurt anyone or change the result: the first
  decisive death wins the round, and its killer gets the killcam. A draw comes
  only from both teams emptying without a deciding death (disconnects, team
  changes), which the frame's own check catches.
- A decisive enemy elimination plays **ROUND WINNING KILLCAM** for the entire
  lobby, including the victim, surviving players and spectators. It is MW2's
  final killcam: 4 s before the death to 1.95 s after it (`KILLCAM_REWIND`,
  `KILLCAM_LINGER`; `_killcam.gsc`'s camtime 4.0 and postdelay 2 less its 0.05 s
  settle frame), so whatever the killer did in the 1.05 s of control is in it.
  It plays when MW2's round-end wait is over: 5.05 s after the death
  (`KILLCAM_ROUND_DELAY`: the waitframe and `roundEndWait( 4, true )`), 4.55 s on
  the match-winning round (`KILLCAM_MATCH_DELAY`: `roundEndWait( 4, false )`).
  It shows the killer's first-person view, including weapon actions and ADS, at
  game speed (MW2 also slows the last second down; Peace does not yet). Everyone
  returns on the server's shared deadline; individual input cannot steer or skip
  the replay. The next round follows 0.1 s after it (doFinalKillcam's wait 0.1,
  then `map_restart`); the match-winning round finishes its replay before the
  scoreboard.
- Bomb, timer and draw results, suicides and disconnect eliminations have no
  unrelated kill replay. If there is too little history, the normal pause applies.

Default round time is 2:30; after planting, a separate 45-second bomb timer runs.
There is a five-second deployment countdown and five-second round-result pause
(MW2's 0.05 + `roundEndWait( 4, true )`) when there is no killcam.
Detonation currently
uses an existing impact effect; dedicated planting/defusing hand animations,
announcer audio and a cinematic explosion are not included.

## Solo objective practice

Console: `exec peace-snd-practice`

This loads the ladder map with cheats and a clearly labeled **SOLO PRACTICE**
mode. It bypasses the missing-opponent/elimination wins so you can climb, plant
and wait for detonation alone. It does not change normal multiplayer rules.
After the side switch, join the attacking team and deploy next round, or execute
the practice config again to restart. A real second player is needed to test
defusing against an attacker. Stock Quake bots do **not** understand bomb
objectives; the S&D host menu defaults to open human slots rather than bots.

## Server settings

| Cvar | Default | Meaning |
| --- | --- | --- |
| `g_sndRoundTime` | 150 | Pre-plant seconds (10–1800) |
| `g_sndBombTime` | 45 | Planted timer, seconds (5–300) |
| `g_sndPlantTime` | 5 | Continuous plant hold, seconds (1–30) |
| `g_sndDefuseTime` | 5 | Continuous defuse hold, seconds (1–30) |
| `g_sndWinLimit` | 4 | Round wins needed (1–99) |
| `g_sndSwitchRounds` | 3 | Rounds between side changes (1–99) |
| `g_sndCountdown` | 5 | Deployment countdown, seconds (1–30) |
| `g_sndRoundPause` | 5 | Result pause, seconds (1–30) |
| `g_sndPractice` | 0 | Cheat-only solo mode; not archived |

Settings are snapshotted at round start. Ordinary frag/capture/map time limits
do not decide this mode. The host menu resets those limits and solo practice.
Friendly fire still uses the existing `g_friendlyFire` setting.

## Implementation and map authoring

`g_snd.c` owns round membership, bomb ownership, timers and use validation on the
server. HUD state uses previously unused configstrings 28/29. Existing signed
`STAT_USE_TARGET` values -2/-3 identify plant/defuse; `STAT_USE_PROGRESS` is 0–1000
for objectives. Ordinary weapon pickup retains its existing timing convention.
There are no new playerState fields. A snapshot flag identifies replay scenes.
Reload, swap, sprint,
ladder holster and notetrack code are unchanged; no named glitches are simulated.

Use progress requires continuous valid commands and uses server time, never the
client's claimed command timestamp. A command gap longer than 200ms resets the
hold. Range, line of sight, grounded state and weapon readiness are server checks.
At the exact deadline, expiration wins. Dead/late clients retain their own score
and team while following teammates; copied spectator health grants no extra life.

Supported maps need `peace_bombsite` point entities: `site` is 0 (A) or 1 (B),
`radius` defaults to 72 units. Put `origin` slightly above a reachable floor,
with clear line of sight. Add `snd` to the arena's type list. Add four or more
`team_CTF_redplayer` starts for attackers and `team_CTF_blueplayer` starts for
defenders; roles remap those starts after a side switch. Maps without sites
stay in waiting with a HUD warning instead of silently playing team deathmatch.

`dev/tools/build_ladder_map.py` generates the committed BSP, markings and arena;
`dev/tools/build_sd_map.py` (with `dev/tools/q3bsp.py`) generates `peace_sd`, and
`dev/tests/sd_map_test.c` checks it against the real engine collision and player
movement: spawns land, both sites can be stood on, the stair climbs, the hall is
sealed.
`dev/tools/build_snd_assets.py` generates the original bomb-case MD3 and shader.
Both support `--check`; no external map compiler or art download is needed.
Between rounds, players/classes/ammo reset; missiles, dropped items and corpses
are cleared. Arbitrary map-specific movers or destroyed props are not reset by
this first pass; the supported ladder map has none.

The engine records complete entity states once per sample (at most 40 Hz) and
each connected player's visibility and player state into a bounded 262-frame
history. `trap_ReplayStart( killer, from, to, begin )` keeps recording to `to`,
pins the clip, and from `begin` streams the selected killer's snapshots, at a
constant offset and game speed (MW2's archive playback), through the ordinary delta
channel to every recipient. It refuses a window longer than the history, one
ending more than `KILLCAM_LINGER` ahead, and a `begin` before the last sample.
This works outside a viewer's original PVS and for clients arriving during
playback; no client replay buffer or bulk reliable transfer is required. Storage
is allocated outside the fixed engine zone and released on shutdown. Recording
is pinned from `to` through playback and reset each round.
Only snapshot copies are rebased to the live transport clock. Game simulation,
ammo, reload notes and weapon readiness are never rewound or changed by playback.
Client presentation clears old effects/animation ownership at each replay cut.

## Checks

`dev/tests/run-snd.cmd` (Windows) or `sh dev/tests/run-snd.sh` (Linux) drives the
actual server implementation, HUD site parser and host menu. Linux can repeat
with `SND_TEST_DEFINES=-DMISSIONPACK`. Tests cover deadlines, holds, interrupted
actions, carrier loss/recovery, late joins, teammate spectating, side changes,
round limits, empty teams, cheat-only practice and menu launch settings, the
round end's 1.05 s of control, the detonation and defuse delays, and the
killcam's window and start.

`snd_roundend_test.c` (in `run-snd.sh` only: it links whole game files, as
`offhand_server_test.c` does) plays the owner's case through the real server:
the real `g_snd.c`, `ClientSpawn`, `ClientThink`, `Pmove`, `FireWeapon`,
`G_Damage`, `player_die` and the knife's flight, in `G_RunFrame`'s order with a
command every 8 ms. The shot wins the round; the throwing knife tapped at the
shooter's teammate (friendly fire on) any time up to level time D + 1000 is
pulled and thrown and hurts nobody, a press from D + 1050 does nothing, the
killcam is asked for with MW2's window and start, and the next round comes 0.1 s
after it. A control shows the same throw in live play does hurt. The last
defender's own knife, still in the air when the shooter kills him, lands on the
next frame and does nothing: the attackers win (deciding at the end of that
frame let the knife kill the shooter first, a draw). A team game ends the same
way: the kill that reaches the score limit gets MW2's final killcam 6.05 s
later, a time limit's end gets none, and both freeze everyone 1.05 s after the
end.

`sh dev/tests/run-replay.sh` also drives the actual engine recorder and snapshot
builder: disjoint visibility areas, all lobby recipients, early kills, different
server frame rates (25 ms, the sampling floor, included), ADS/weapon clocks,
projectile trajectories, late arrival, camera retention, immutable live state and
history reset, and the S&D killcam's timeline: live snapshots and recording on to
the window's end, then the pinned clip from its first frame at `begin`. Repeat with
`REPLAY_TEST_FLAGS=-DMISSIONPACK`; sanitizers can be supplied through the same
variable. The Windows S&D runner includes this engine test.

`peacedump` includes replay activity, viewer/camera IDs, snapshot time,
position/grounding, S&D configstrings and use progress.
For a network playtest, check plant/defuse interruption, carrier disconnect,
death after a plant, teammate camera, class changes taking effect next round,
and the ladder exit into Site A. Artificial lag and packet-loss feel still need
hands-on testing; native/QVM builds and deterministic tests do not prove that.

A usable site or the bomb makes the use hint `ENTITYNUM_WORLD` (live, never
freed), as a usable trigger is MW2's hint, so standing at one ends a sea lion
(CLAUDE.md).
