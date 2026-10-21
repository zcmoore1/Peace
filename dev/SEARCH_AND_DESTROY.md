# Search and Destroy

Branch: `dev-gpt`. Run in **x64 Native Tools Command Prompt for VS**:

```bat
cd /d C:\Users\ZackI\Desktop\OneManIW\ProjectPeace\dev2\Peace
git pull origin dev-gpt
cmake --build out\build\x64-Debug --clean-first
```

## Play

In the base-game menu: **Multiplayer → Create → Game Type: Search and Destroy**,
select **peace_ladder**, then set the round target and duration and start.
Join Red or Blue. Normal play needs at least one player on each team.
Alternatively set `g_gametype 8`, `g_sndPractice 0`, then `map peace_ladder`.
Everyone must rebuild the client, server and game modules together (protocol 73).

**Site A is on the upper ladder platform**, marked by a gold outline and an A.
Attackers spawn on the floor opposite the ladder; defenders spawn on the platform.
The room is deliberately a mechanics test lab, not a balanced competitive map.
It has four starts per side and no retail map geometry.

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
- The HUD shows roles, round scores, living players, objective markers, the active
  timer and hold progress. The first team to four round wins takes the match.
- A decisive enemy elimination plays **ROUND WINNING KILLCAM** for the entire
  lobby, including the victim, surviving players and spectators. It shows up to
  five seconds before the shot and 750 ms afterwards from the killer's first-person
  view, including weapon actions and ADS. Everyone returns on the server's shared
  deadline; individual input cannot steer or skip the replay. The match-winning
  round finishes its replay before the scoreboard.
- Bomb, timer and draw results, suicides and disconnect eliminations have no
  unrelated kill replay. If there is too little history, the normal pause applies.

Default round time is 2:30; after planting, a separate 45-second bomb timer runs.
There is a five-second deployment countdown and five-second round-result pause.
An elimination replay extends the result pause as needed, with one second after
playback before the next deployment. Detonation currently
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

`dev/tools/build_ladder_map.py` generates the committed BSP, markings and arena.
`dev/tools/build_snd_assets.py` generates the original bomb-case MD3 and shader.
Both support `--check`; no external map compiler or art download is needed.
Between rounds, players/classes/ammo reset; missiles, dropped items and corpses
are cleared. Arbitrary map-specific movers or destroyed props are not reset by
this first pass; the supported ladder map has none.

The engine records complete entity states once per sample (at most 40 Hz) and
each connected player's visibility and player state into a bounded 262-frame
history. It streams the selected killer's snapshots through the ordinary delta
channel to every recipient. This works outside a viewer's original PVS and for
clients arriving during playback; no client replay buffer or bulk reliable
transfer is required. Storage is allocated outside the fixed engine zone and
released on shutdown. Recording is pinned during playback and reset each round.
Only snapshot copies are rebased to the live transport clock. Game simulation,
ammo, reload notes and weapon readiness are never rewound or changed by playback.
Client presentation clears old effects/animation ownership at each replay cut.

## Checks

`dev/tests/run-snd.cmd` (Windows) or `sh dev/tests/run-snd.sh` (Linux) drives the
actual server implementation, HUD site parser and host menu. Linux can repeat
with `SND_TEST_DEFINES=-DMISSIONPACK`. Tests cover deadlines, holds, interrupted
actions, carrier loss/recovery, late joins, teammate spectating, side changes,
round limits, empty teams, cheat-only practice and menu launch settings.

`sh dev/tests/run-replay.sh` also drives the actual engine recorder and snapshot
builder: disjoint visibility areas, all lobby recipients, early kills, different
server frame rates, ADS/weapon clocks, projectile trajectories, late arrival,
camera retention, immutable live state and history reset. Repeat with
`REPLAY_TEST_FLAGS=-DMISSIONPACK`; sanitizers can be supplied through the same
variable. The Windows S&D runner includes this engine test.

`peacedump` includes replay activity, viewer/camera IDs, snapshot time,
position/grounding, S&D configstrings and use progress.
For a network playtest, check plant/defuse interruption, carrier disconnect,
death after a plant, teammate camera, class changes taking effect next round,
and the ladder exit into Site A. Artificial lag and packet-loss feel still need
hands-on testing; native/QVM builds and deterministic tests do not prove that.
