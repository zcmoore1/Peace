# The silent shot

MW2 behaviour, emergent from MW2's own event rings, client-side bullets and
view kick. **No code names it.** There is no silent-shot flag, branch, state,
event or test hook in the game, the engine or the cgame; never add one. If it
stops happening, one of the pieces below has stopped being MW2's.

## What the player sees

Shoot, then press the throwing knife a moment later (the next command at the
earliest: pressed on the same command, the knife wins and there is no shot).

- **The shooter** sees and hears the shot, its tracer and impacts, and feels
  its view kick. The kick runs to its 10-degree clamp and stays there while
  the knife is in hand: the view is knocked over (the tilt).
- **Live opponents** hear the shot and see its flash, tracer and impacts,
  with the gun that fired, though the shooter already holds the knife.
- **The victim dies.** The shot is the server's and is never lost.
- **The round-winning killcam**, when the shot and the press ran in one
  server frame, shows no shot at all: no flash, no sound, no tracer, no
  impact. It still shows the view knocked over.

## The pieces, each MW2's

All addresses are MW2's `iw4x.exe`; the kick's decode is in
`dev/VIEW_KICK.md`, the events' in CLAUDE.md ("Events are MW2's rings").

1. **The playerstate's ring of four predictable events.** `MAX_PS_EVENTS` 4;
   MW2's add (0x4bd150) writes `events[seq & 3]` and steps an 11-bit
   sequence. The shot think adds one event, `EV_FIRE_WEAPON` (MW2's
   `EV_SV_FIRE_WEAPON`, 0x5771c0).
2. **The knife's press is four events on one think,** in MW2's order and with
   MW2's parms (`PM_UpdateWeaponAction`'s latch, `PM_FinishWeaponChange`):
   CheckForOffHand's `EV_SWITCH_OFFHAND` (579168), OffHandInit's
   `EV_WEAPON_SWITCH_STARTED` (578e10) and `EV_RESET_ADS` (578e5e), and,
   because the throwing knife is weapClass 9 (578eec), OffHandPrepare's
   `EV_PREP_OFFHAND` (5778e6) on the same think. A frag, flash, Semtex or
   Tactical Insertion press is three: its prepare waits for the gun's quick
   drop. Straight out of an empty hand or `OFFHAND_END` (5791ce, the sea lion)
   it is two. Each event has MW2's function (see CLAUDE.md, "Events are MW2's
   rings").
3. **The killcam records one playerstate a server frame.** `SV_Frame` calls
   `SV_RecordReplayFrame` after every `GAME_RUN_FRAME` (`sv_snapshot.c`), so
   at the default `sv_fps 20` a sample is MW2's 50ms (MW2's archive, 451d0a;
   `SV_GetCachedSnapshot` 0x4dc220). The replay plays the archived states in
   order through `CG_TransitionPlayerState`, whose
   `CG_CheckPlayerstateEvents` walks the last four slots, as MW2's 0x58f070
   does. The killer's own entity is never in the replay (its viewpoint skips
   it, as a live snapshot skips the viewer's own), so nothing else of the
   shot reaches the viewer.
4. **Bullets are drawn by the clients** (MW2's `sv_clientSideBullets`,
   0x2098da8, on by default; the server's impact temp entity is skipped at
   5d5816; `CG_FireWeapon` draws them at 4fb2df). The tracer and impacts come
   from the fire event alone, so they go with it.
5. **The view kick is in the command's aim** (`BG_WeaponFireRecoil` 0x4a5fe0,
   `CG_KickAngles` 0x596790, `CL_FinishMove` 5a6740). The server aims with
   the command's angles and the killcam records them. The throwing knife's
   centre speed is 0 (`throwingknife_mp`), so with the knife in hand nothing
   brakes the kick the shot left.
6. **Every entity carries MW2's own ring of four** (`es.eventSequence`,
   `es.events[4]`, filled from the playerstate's by 56eab0 minus MW2's
   serverOnly and singleClient lists, 0x739b38/0x739af0, and played by
   `CG_CheckEvents`, 4c33f0). The press gives the entity only
   `EV_PREP_OFFHAND`, so the shot always reaches opponents; its parm names
   the gun that fired, as MW2's `es.weapon` stays the gun.

**The arithmetic.** The shot is at sequence s; the press adds s+1 to s+4, and
s+4 is written into slot (s+4) & 3, which is s's. An archived frame that holds
both has ring slots s+1..s+4 and nothing of s; the replay's loop starts at
s+1. Live, nothing is lost: the shooter's client checks its events every frame
and makes one command a frame, the server keeps the last four per command
(ClientEvents), and the entity ring holds two of them (the shot and the
pullback). A frag's press is three, so the shot survives it in the same frame
unless one more predictable event (a footstep, a jump, a landing) lands there
too.

## Measured (dev/tests/run-silentshot.sh)

125 fps client (a command every 8ms), `sv_fps 20`, a tap of the knife N ms
after a one-command shot, swept over every 1ms arrival phase against the
server frame (`cl_maxpackets 30`: every 5ms, times the shot's place in its
packet). SPAS and M300 give the same loss windows; ping 50, 100 and 200 move
nothing (checked).

Killcam loses the shot (the shooter, live opponents and the server never do):

| knife after the shot     | 8 ms | 16 ms | 24 ms | 32 ms | 40 ms | 48 ms | 56 ms | 64 ms | 72 ms | 80 ms+ |
|--------------------------|------|-------|-------|-------|-------|-------|-------|-------|-------|--------|
| a packet every frame     | 84%  | 68%   | 52%   | 36%   | 20%   | 4%    | 0     | 0     | 0     | 0      |
| `cl_maxpackets 30`       | 86%  | 68%   | 52%   | 36%   | 20%   | 18%   | 12%   | 8%    | 4%    | 0      |

- **A packet every frame (loopback, LAN):** lost exactly when the press runs in
  the shot's server frame, (50 - N)/50 of the phases, so only under 50ms.
- **`cl_maxpackets 30` (MW2's default):** commands go out every 40ms at
  125 fps. A press in the shot's own packet arrives with it and is always
  lost with it; one a packet later arrives 40ms later and is lost a fifth of
  the time; later, never. So the window stretches to under 80ms.
- **The frag control:** a frag pressed 8ms after the shot ran in the shot's
  frame in 42 of 50 phases, and the killcam kept the shot every time.

The tilt. With the knife in hand the killcam's recorded aim, sampled a
server frame at a time, is the shooter's own screen; both are checked equal.
Both runs use the first shot of a fresh client: MW2's kick seed 0x89abcdef
draws r = 0.436 for pitch and 0.247 for yaw, so the M300 kicks (-88.7, -51.2,
+25.6) and the SPAS (-89.4, -22.8, +11.4) degrees a second, pitch, yaw and
roll. The view stays at its clamp, with the knife in hand, in every phase, for
a knife up to (a packet every frame; batched by `cl_maxpackets 30`, one case
in 50 already misses the yaw at the last of these, 80ms on the M300 and 8ms on
the SPAS):

| gun  | pitch  | yaw   | roll  |
|------|--------|-------|-------|
| M300 | 160 ms | 80 ms | 16 ms |
| SPAS | 80 ms  | 8 ms  | never (5.8 degrees at 8 ms) |

With no knife the gun brakes its own kick: it peaks at (-8.1, -2.8, +0.7) on
the M300 and (-4.3, -0.3, +0.1) on the SPAS and runs out. Once an axis reaches
the clamp with the knife in hand it never leaves it while the knife stays
(checked), and a knife later than an axis's window still lets that axis keep
the velocity the gun had not yet braked (the M300's roll is still 3.6 degrees
with the knife at 48 ms). Another draw moves these edges (the research's
windows for the roll to grow at all: about 50ms on the Intervention with a
mid yaw draw, 22ms on the SPAS at its extreme yaw); the loss windows do not
depend on the draw.

## The test

`dev/tests/run-silentshot.sh` (`SILENTSHOT_TEST_FLAGS=-DMISSIONPACK` for the
other numbering) models the network as `fake_reload_test.c` does (server
frames, the client's MW2 clock through the real `CL_DriftTimeDelta`, ping,
`cl_maxpackets`) and runs only real code on both sides:

- **Server:** the game module's `ClientThink` (Pmove,
  `BG_PlayerStateToEntityState`, `ClientEvents`, `FireWeapon`, `G_Damage`,
  the thrown knife), `G_AddEvent`, `G_AgeEntityEvents` and `ClientEndFrame`
  (`silent_shot_server.c`; `G_RunFrame`'s entity pass is copied, as
  `g_main.c` is the module's entry point); `sv_snapshot.c`'s
  `SV_RecordReplayFrame`, `SV_ReplayStart` and `SV_BuildClientSnapshot`, and
  the codec (`msg.c`) for every snapshot (`silent_shot_engine.c`).
- **Client:** the real `CL_FinishMove` and `CL_SetUserCmdKickAngles`
  (`kick_client.c`), and cgame's `CG_ProcessSnapshots`,
  `CG_TransitionSnapshot`, the killcam's cut, `CG_PredictPlayerState`, the
  playerstate and entity events, `CG_FireWeapon` with its client-side
  bullets, `CG_KickAngles`, and `CG_AddPlayerWeapon` for every other
  player's third-person weapon (`silent_shot_client.c`).

One cgame runs at a time, so each case runs twice: as the shooter (whose
commands, kick and all, are kept with the time each reached the server), then
as an opponent watching from the side, live and then through the round's
killcam, started as S&D starts it (`SV_ReplayStart` from the death minus
`KILLCAM_REWIND` to the death plus `KILLCAM_LINGER`, MW2's 4 s and 1.95 s,
played from the death plus `KILLCAM_ROUND_DELAY`, 5.05 s, so the opponent
watches live until then). The server runs the
same both times, and that is checked. Every case checks: the shot is real
and hits; the server sends no impact or tracer; the shooter's client plays
the shot once with its sound, flash and every impact where the server's
damage went, and kicks; the opponent hears and sees it once with the gun,
whose flash the real `CG_AddPlayerWeapon` draws on the shooter's model with
the gun's flash model, light and muzzle tag (until another weapon's fire with
a flash of its own plays: the frag control's throw, now live as well), and
the snapshot that brings the
shot already shows the knife exactly when the press ran in the shot's frame;
the killcam loses its fire event, sound, flash and every drawn bullet exactly
when the server ran the shot and the press in one frame, and shows them
otherwise; the killer's entity plays nothing there; every recorded aim is one
of the shooter's commands; and an axis at the clamp stays there.

Two other views of the same shot are checked too. A spectator follows the
shooter through the real `SpectatorClientEndFrame`, then switches to the
victim 50 to 600 ms after the shot. They play the shot exactly once, from the
followed playerstate: the shooter's entity comes back with the shot still in
its ring, and MW2's `CG_ResetEntity` counts a player's ring as played. And a
viewer out of the S&D round stands in the line of fire, unlinked with no
health, as `G_SNDClientEndFrame` leaves one. Their client draws every bullet
through the absent body, exactly as it draws them for the same viewer off the
line, on into the victim or past it (another player's shot is drawn from what
the entity carries, exact only when it shows the shot's own think, so at
other `SILENTSHOT_SVFPS` rates a pellet can land beside the victim). The same
viewer alive in that spot takes them, as the server's bullets do.

`SILENTSHOT_TRACE="spas|m300 knife-ms phase [rtt maxpackets shift]"` prints
one case (knife -1 for none; `SILENTSHOT_FRAG=1` presses the frag),
`SILENTSHOT_PROBE=1` every case, `SILENTSHOT_FRAMES=1` the shooter's frames
around the shot, `SILENTSHOT_SVFPS` another server rate.

Two simplifications, neither of which the windows depend on: nobody dies
(the victim has 30000 health, so the replay is started directly, as S&D
would start it at the death), and the client's clock starts 600ms before the
shot, still drifting toward its target as it does all through play.

That the knife can come out at all after a round-winning shot is MW2's round
end: everyone keeps full control for 1.05 s after the deciding death (endGame's
waitframe and `freezePlayerForRoundEnd( 1.0 )`), and the killcam's window runs
1.95 s past it. Peace used to freeze the survivors on the frame after the kill,
so a press after that never ran. `snd_roundend_test.c` (`run-snd.sh`) plays it
through the real `g_snd.c`, `ClientThink` and `Pmove`; see
`dev/SEARCH_AND_DESTROY.md`.

## Where Peace and MW2 still differ

- MW2's `ps.weapon` stays the gun during the press and its viewmodel weapon is
  `offHandIndex`; Peace's `ps.weapon` is the knife. The fire event's parm
  names the gun instead, so opponents still hear it and see its flash
  (`CG_AddPlayerWeapon` draws the impulse flash, its light and its muzzle
  tag from the event's gun, `muzzleFlashGun`, not from `es.weapon`), and the
  kick reads the knife's centre speed either way.
- MW2's `sv_clientSideBullets` can be turned off; Peace's bullets are always
  the clients'.
- MW2's server frame, and so its archive, is a fixed 50ms. Peace's is
  `sv_fps`: at `sv_fps 40` it records every 25ms (`REPLAY_SAMPLE_MSEC` is the
  floor), which halves the window (`SILENTSHOT_SVFPS=40`: 68%, 36% and 4% at
  8, 16 and 24ms with a packet every frame, none from 32ms).
