# MW2's stance button and prone

MW2's stance is client state: the client builds every command with the stance as a level
(prone, crouch, or neither), and pmove decides what the body can do. Pmove says when it
refused or forced a stance through the `EV_STANCE_*` events, and the player's own cgame then
resets the client's stance (MW2's `CL_SetStance`). The eye follows MW2's curve in pmove.

The sections below cover the owner's decisions, the wire and shared definitions, pmove and the
server, and the input, cgame and binds.

## The owner's decisions

Code and docs name them by number.

- **D1** (approved): cgame resets the client's stance as MW2's `CL_SetStance` does, through the
  hidden console command `_peaceSetStance` (the channel `leaveads` uses), on the player's own
  `EV_STANCE_FORCE_*` and after every prediction pass while respawned; own view only. It is the
  second exception to "cgame is picture only" (CLAUDE.md).
- **D6**: an ET_PLAYER entity carries its player's eye (`ps.viewheight`) in `origin2[2]`, so
  others' bullets are drawn from the shooter's real eye.
- **D7** ("Approximate MW2"): bullets and pellets also hit two volumes past the move box, from one
  bg function both sides call (`BG_PlayerHitVolumes`): over the box while the eye is above it,
  and the prone body behind the origin. Everything else keeps the box. Peace's approximation of
  MW2's skeleton, a placeholder.
- **D13**: MW2's binds: PC SPACE `+gostand`, CTRL `toggleprone`, `c` `togglecrouch`; pad RS
  `+stance`, A `+gostand`.
- **D21** (approved): prone is the one exception to "the player picture fills the box": the
  picture lies along the body's yaw and overhangs the box behind the origin; origin, box, traces
  and lighting never move.
- Defaults the owner accepted: prone now (D2); the crouch on MW2's command bits, protocol 88
  (D3); a float eye and an elapsed leg clock (D4); MW2's `oldcmd` as `ps.prevCmd` (D5); Peace's
  dead and spectator boxes (D8); the talk strip keeps the stance (D9); MW2's `PMF_RESPAWNED`
  clear (D10); the prone slowdown only while `jumpLand` is 0 and no knockback timer runs (D11);
  water sampled from the standing eye (D12); prone spread as crouched (D14); the unpredicted eye
  lerped (D15); `+movedown` still sends `upmove < 0` with no jump held (D16); the forced stand
  in `PM_CheckSprint` (D17); MW2's invalid-command hint, no stance icon (D18); MW2's viewmodel
  prone offsets optional (D19, not ported); the Quake 3 model laid flat for third person (D20);
  the stance events share MW2's four-slot ring with no special case (D22); a prone landing's
  fall damage is not doubled (D23).

## Wire and shared definitions (protocol 88, GAME_VERSION -17)

**The command.** `usercmd_t.buttons` carries the stance in bits 22-24, MW2's 0x100, 0x200 and
0x1000:
- `BUTTON_PRONE`;
- `BUTTON_CROUCH`;
- `BUTTON_TEMP_STANCE`, meaning the stance comes from a held key (`+prone`, `+movedown`), so
  pmove raises no refusal event for it.

Bits 25-31 are spare.

**The playerstate.**
- `pm_flags` is 18 bits on the wire: `PMF_PRONE` (bit 16) and `PMF_PRONEMOVE_OVERRIDDEN`
  (bit 17), MW2's 0x1 and 0x200.
- `viewheight` is a float: the eye above Peace's origin.
- MW2's eye leg:
  - `viewHeightTarget`, in `BG_EYE_*`: MW2 units above the feet, 60/40/11/8;
  - `viewHeightLerpTime`, the elapsed ms into the running leg (MW2 keeps an absolute time);
  - `viewHeightLerpTarget`, where 0 means no leg is running;
  - `viewHeightLerpDown`.
- The prone body: `proneDirection`, `proneDirectionPitch` and `proneTorsoPitch`.
- `prevCmd`: MW2's pmove `oldcmd`, packed by `BG_PrevCmdPack`.

None of these is a clock, so the killcam replay rebases none of them.

**The entity.** An ET_PLAYER entity, through `BG_PlayerStateToEntityState` and
`…ExtraPolate`, carries:
- MW2's `EF_CROUCHING` and `EF_PRONE`, mirrored from the flags (the entity's `eFlags` is 22
  bits). They are never in `playerState_t.eFlags`, which is 16 bits.
- the eye, unsnapped, in `origin2[2]`;
- while prone, the body's yaw and pitch in `origin2[0..1]`.

Others' drawn bullets start from that eye, and the bullet-only hit volumes read all of it.

**Events.** `EV_STANCE_FORCE_STAND`, `EV_STANCE_FORCE_CROUCH`, `EV_STANCE_FORCE_PRONE` and
`EV_STANCE_INVALID` (MW2's 6-9) are on MW2's single-client list. Their parm is
`BG_STANCE_PARM_*`: 0 forced, 1 stand blocked, 2 crouch blocked, 3 prone refused or blocked.

**Legs anims.** `LEGS_IDLEPR`, `LEGS_CRAWL` and `LEGS_CRAWLB` are appended past
`MAX_ANIMATIONS`.

**The box.** `BG_PlayerBox` is the one box rule: dead, prone (`PRONE_HEIGHT`, MW2's 30),
crouched or standing; a spectator stands.

**Hit volumes (owner's decision D7, Peace's approximation of MW2's skeleton).**
`BG_PlayerHitVolumes` gives the volumes a bullet or pellet can hit a player in. The server's
damage traces and every client's drawn bullets both use it, fed from the target's
`entityState_t` and the move box their own trace tests. The volumes are:
- the move box, always;
- while the eye is above the box top (on a downward leg), the box's footprint from its top up
  to the eye plus `BG_HIT_HEAD_ALLOWANCE`;
- while prone, the body MW2's prone room check traces: `BG_PRONE_FEET_DIST` behind the origin,
  `BG_PRONE_TRACE_HALF` each way across, feet to prone top, turned to the body's yaw and pitch.

`BG_HitVolumeTrace` is the ray test. A shot that starts inside a volume and leaves it is no hit
there, as the engine's own box passes one (`CM_TraceThroughBrush`: startsolid, the fraction
kept), so a muzzle over another prone player's legs fires over them or away from them freely;
only a segment that never leaves is a hit, at 0. Only bullets and pellets test the volumes: the
railgun, the lightning gun, knives, melee, the throwing knife and explosions keep the move box.
This is Peace's approximation of MW2's skeleton, a placeholder (`dev/MW2_DIFFERENCES.md`, "Prone
and leg hit volumes"). The full contract is in `bg_public.h`.

Tests: `action_wire_test.c` and `stance_shared_test.c`, in `run-loadouts`.

## Pmove and the server

Everything here is MW2's code ported to Peace's pmove (`bg_pmove.c`, `bg_slidemove.c`), with
the exe addresses in the comments. Nothing names a glitch: drop shots, crouch spam, a queued
prone and a sprint out of prone come out of these rules.

**The order of a think** (MW2's PmoveSingle 0x57448B-0x574B53). `pml.forwardmove` and
`pml.rightmove` are this think's copies of the command's moves; the command is never written.
Then:
1. the crawl rules (`PM_ProneMoveOverride`) and the prone view (`PM_ProneViewAngles`), on the
   stance the last think left; a stopped crawl zeroes the copies;
2. `PM_CheckDuck`, before the ground trace, so prone asked for in the air comes on the think
   after the one whose move lands;
3. after the ground trace, the prone body against the ground (`PM_UpdatePronePitch`);
4. the move; `PM_AirMove` crouches a prone body with no room in the air;
5. sprint and aim (after the move, Peace's documented "one think late"), then the weapon.

`Pmove` keeps MW2's `oldcmd` in `ps.prevCmd`: written once a command, and only if a step ran,
so every chopped step compares with the command before.

**The stance** (`PM_CheckDuck`, MW2's 0x570A90). The command's bits only ask:
- prone: with room to lie down (`PM_ProneAllowed`: MW2's room check along the view), else
  nothing in the air, else `EV_STANCE_FORCE_STAND/CROUCH 3` unless the stance is from a held key;
- crouch: never refused; out of prone only if the crouch box fits, else `FORCE_PRONE 2`;
- neither: stand if the standing box fits, else crouch (silently, from prone), else
  `FORCE_PRONE 1` or `FORCE_CROUCH 1`.

Room is the box at the origin with the trace mask less `CONTENTS_BODY`, so another player never
keeps you down. The eye's target follows the flags only between legs, and the flags and the box
(`BG_PlayerBox`) follow the target: a request mid-leg waits, stand to prone goes through a
crouch, and the first leg asks for room again every think. A ladder strips the crouch and prone
bits with `FORCE_STAND 0` every think (no held-key test). `PMF_RESPAWNED` ignores the bits and
holds while the trigger or prone is down (MW2's 0x574617). Spectators stand; the dead keep their
flags while the eye falls at 180 a second to `BG_EYE_DEAD`.

**The eye** (`PM_ViewHeightAdjust`, 0x570270). The eye runs MW2's four waypoint tables, a leg at
a time: stand-crouch legs 200ms, into and out of prone 400ms. A leg's start think does not move
the eye; a running leg shows its table at the whole percent gone; a target the other way
reverses the leg from the mirrored percent. The box drops at a leg's start while the eye follows
the curve, so on the way down the eye is above the new box for 110ms of stand to crouch and
106ms of crouch to prone (at 8ms thinks: whole thinks at other rates).

**Prone.**
- *Entry* (0x570830): a moving player drops a held aim (`EV_RESET_ADS`); the box settles up 10
  and back; the body takes the view's yaw and the ground's pitch. Lying down starts a landing's
  slowdown in `jumpLand` (MW2's Jump_ActivateSlowdown), never `pm_time`, unless knockback's
  timer runs.
- *Room* (`BG_CheckProne`, MW2's BG_CheckProneValid 0x420EB0): the legs' reach behind the body
  (retried 22 higher when close), the waist and feet on walkable ground (the trace's own
  walkable, `BG_TraceWalkable`), no bend past 70 with the legs hanging down (raised legs are
  held at the torso's + 10, so they always pass), two clear lines. It is
  asked to lie down, after every ground move (a crawl into a wall, over a ledge or onto a bend
  is undone, origin and velocity: `PM_VerifyPronePosition`), in the air, and by every turn.
- *Moving*: 0.15 of the speed, ramping from and to the crouch's 0.65 over the 400ms legs
  (`BG_StanceScale`, MW2's PM_CmdScaleForStance, which the walk and the bob's most read); walk
  accel 19; the step 10 (`STEPSIZE_PRONE`), the push down 9 as standing.
- *The view* (0x571ED0): the body turns toward the view at 55 a second whenever the command asks
  for prone (always, lying down), each step checked; a turn into a wall stops and backs off, and
  a body still off the view pushes the view toward it within the think. The view stays within
  85 of the body and 45 of `proneTorsoPitch` (delta_angles takes the excess); a blocked turn
  raises `EV_STANCE_INVALID 3` every think and undoes 98% of a small turn the same way. The
  pitches follow the ground at 70 a second. The server's `SetClientViewAngle` keeps the same
  limits.
- *The crawl and the gun*: a shot, a pump or bolt, the knife and every offhand stop the crawl
  (`PMF_PRONEMOVE_OVERRIDDEN`) until the stick is pushed further than on the command before or
  the weapon is quiet again; a magazine reload and a swap never stop it. An unscoped aim stops it;
  an aim held since the command before does not start while crawling; a scoped gun crawls aimed.
- *Water* is sampled from the standing eye in every stance (`BG_WATER_SAMPLE_EYE`), so neither a
  crouch nor prone swims or drowns where standing wades.

**Interplay.**
- Sprint: a crouch or prone bit ends a running sprint (MW2's end mask) but does not keep one from
  starting; one started out of a crouch or prone needs room to stand and stands the player up on
  its first think with `FORCE_STAND 0`, which resets the client's stance before its next command.
  A crouch and a sprint pressed on one command from standing therefore stand and sprint (MW2
  crouches: it decides the sprint before the move).
- No jump unless standing (the press is spent); no ladder prone; a prone landing's fall damage
  is not doubled (only Q3's crouch doubling remains).
- Bots crouch with the crouch bit and the held-key bit; S&D's use refuses a jump (`upmove > 0`)
  but no stance.

**The server.** `ClientSpawn` sets MW2's spawn eye (`BG_SpawnStance`: standing, no leg) before
the spawn's angles. The entity box follows pmove, and the entity carries the stance and eye for
everyone else (above).

**Hit volumes on the server.** `G_ShotTrace` (`g_weapon.c`) is the damage trace of every bullet
(`Bullet_Fire`: the machinegun, the M300, the chaingun) and every SPAS pellet (`ShotgunPellet`),
the shots every client draws through `CG_ShotTrace` with the volumes. It takes the engine's trace
of the boxes and the world, then each linked player's extra volumes from `BG_PlayerHitVolumes` on
that player's entity and box (`r.currentOrigin`, `r.mins`, `r.maxs`); the nearest entry wins. The
railgun, the lightning gun, knives, melee and missiles keep the box.

**The event ring.** Stance events can come every think, so a command Pmove chops into several
steps can push its shot out of the last four events the server reads, as MW2's four slots would.
Nothing special-cases it; `stance_test.c` prints the cases.

Tests: `dev/tests/run-stance.sh` (and `.cmd` for the pmove half): `stance_test.c` drives the
real Pmove through brushes traced as the engine traces them, every think counted from the
tables and constants; `stance_server_test.c` runs the real `ClientThink`, `G_ShotTrace`, the
railgun and the knife (which keep the box) and `SetClientViewAngle`.

## Input, cgame and binds

**The client's stance** (`cl_input.c`, `client.h`: MW2's `clientActive_t` fields `stanceHeld`,
`stance`, `stancePosition`, `stanceTime`, 0xB2F86C-0xB2F878). The stance is the client's,
`BG_STANCE_STAND/CROUCH/PRONE`, and every command carries it as a level (MW2's `CL_KeyMove`
0x5A5F4B and `CL_AddCurrentStanceToCmd` 0x5A5EF0): `BUTTON_CROUCH`, `BUTTON_PRONE`, or neither.
`CL_ClearState`'s memset clears it, as MW2's does.

- **The held keys.** `+prone` (`in_buttons[22]`, MW2's PRONE kbutton) and `+movedown`
  (`in_down`, MW2's DOWN) send their own stance with `BUTTON_TEMP_STANCE`, prone winning, and
  while either is down every stance command, toggle and reset below is ignored. A `+prone` tap
  inside one frame is lost (`CL_CmdButtons` sets bit 22, the stance level overwrites it).
- **`+stance` / `-stance`** (0x5A5A30, 0x5A5A80): the press crouches at once and arms a hold at
  the frame's `com_frameTime`; the first command at least `cl_stanceHoldTime` later (MW2's int
  300, 0..1000, not archived; exactly 300 fires) goes prone, or stands if the press came from
  prone. A release before that is a tap: from a crouch it stands, from anything else it stays
  crouched. No kbutton and no argument is read, so every press re-arms, and either of two keys
  resolves it. A held key freezes a pending hold, which fires on the first command after it
  comes up. The hold is measured in frames, so it fires at 304 ms with 8 or 16 ms frames and
  330 ms with 33 ms frames.
- **The rest of MW2's commands:** `togglecrouch`, `toggleprone`, `goprone`, `gocrouch` (no
  connection test), `lowerstance`, `raisestance`, `+gostand`/`-gostand` (UP, and MOVEUP, the
  jump, only from standing; a crouch or prone stands up instead), `+moveup`/`-moveup` (one
  stance up, the jump only from standing). Below `CA_CONNECTED` the ones MW2 gates do nothing.
- **The jump is MW2's MOVEUP** (`in_jump`) alone, so a walker's `upmove > 0` is only ever a jump.
  UP (`in_up`) flies noclip and spectators (MW2's `CL_CmdButtons` 0x5A6697). The jump is MW2's
  own bit (0x400, 0x5A6635), which DOWN (MW2's kb[11]) never touches, so `in_down` goes into
  `upmove` only while nothing goes up: it is still Quake's fly and swim down (`upmove < 0`,
  D16), and a held `+movedown` never cancels a jump, as a held `+prone` does not. With
  `+movedown` held, `+gostand` from standing sends the jump; letting go of the crouch under
  that held jump is no fresh press (MW2's `Jump_Check` 0x4E9889 refuses a jump already down on
  the command before), and on a ladder the held crouch is stripped and the jump pushes off.
- **`_peaceSetStance <0-2>`** is MW2's `CL_SetStance` (0x4630A0), hidden: cgame's channel, as
  `leaveads` is. It writes the stance only (never the hold) and a held stance key refuses it.
- `cl_debugButtons 1` prints where a hold fired and every change of a command's stance bits.

**cgame** (the owner's second exception to "cgame is picture only", CLAUDE.md).
- `EV_STANCE_FORCE_STAND/CROUCH/PRONE` (`cg_event.c`): the player's own, live, sends
  `_peaceSetStance 0/1/2`; never in a demo, the killcam or a followed view. MW2's FORCE_PRONE
  hint needs a weapon that blocks prone while prone (0x408410, 0x4CA460,
  `BG_WeaponBlocksProne`), which Peace has none of.
- **Respawn** (`cg_predict.c`, MW2 0x5901AF): after every prediction pass, moved or not, while
  the predicted playerstate has `PMF_RESPAWNED`, cgame sends `_peaceSetStance 0`. Pmove keeps
  the flag while a command carries `BUTTON_PRONE`, so a player who died prone spawns standing.
- **MW2's invalid-command hint** (`CG_SetInvalidCmdHint` in `cg_event.c`, MW2's 0x587230;
  `CG_DrawInvalidCmdHint` in `cg_draw.c`, 0x58DB10). `EV_STANCE_INVALID` arms "Prone Blocked"
  (parm 3 is type 5; 1 and 2, "Can't Stand Here" and "Can't Crouch Here", are drawn too but no
  Peace event arms them). The weapon is stored every time; a same-type event never restamps;
  the draw clears it `CG_INVALID_HINT_MS` (1800) after its stamp and draws white with alpha
  `((time - stamp) % 600) / 600` (`CG_INVALID_HINT_BLINK_MS`), small and shadowed at MW2's
  `rect 0 -125` about the centre. So a block that raises the event every think blinks for as
  long as it lasts. Hidden while a menu is up or a client is spectated (the draw does not run,
  so it neither clears nor restamps); the scoreboard does not hide it.
- **The eye.** Pmove moves `ps.viewheight` along MW2's curve every think, so cgame's duck ease
  is gone (`DUCK_TIME`, `cg.duckChange`, `cg.duckTime`). An unpredicted view (a demo, a
  followed player, the killcam) lerps `viewheight` between snapshots, as the origin.
- **Others' eye and stance.** Others' muzzles (`CG_CalcMuzzlePoint`, the lightning bolt) and
  their drawn shots (`CG_EntityShot`) start from the eye their entity carries (`origin2[2]`,
  the shooter's `ps.viewheight`, the server's muzzle eye), so a drop shot is drawn from the
  server's eye on every think of both legs. Their spread's stance is MW2's `EF_CROUCHING` and
  `EF_PRONE` (exact: no legs-anim lag). `CG_WaterLevel` samples at `BG_WATER_SAMPLE_EYE`. The
  viewed player's box is `BG_PlayerBox`.
- **Drawn bullets hit the hit volumes** (owner's decision D7). `CG_ShotTrace` tests, for a
  bullet or a pellet (not the lightning gun's beam), every snapshot player's volumes past the
  box (`CG_ShotHitVolumes`, `cg_predict.c`: the solid list's players at the origin and decoded
  box `CG_ClipMoveToEntities` traces them at) and the viewed player's own (from
  `BG_PlayerStateToEntityState` of the predicted state, its `BG_PlayerBox`), through
  `BG_PlayerHitVolumes` and `BG_HitVolumeTrace`: the nearest volume entered before what the
  engine trace hit is the hit, at its own fraction and face. Blood and impacts therefore land
  where the server's damage did, under the existing rule (exact when the entity shows the
  shot's own think).
- **Third person** (`cg_players.c`, a placeholder; the owner's prone line in CLAUDE.md).
  `LEGS_IDLEPR`, `LEGS_CRAWL` and `LEGS_CRAWLB` are the standing idle, the crouch walk and the
  crouch walk reversed. A live player with `EF_PRONE` lies face down along `origin2[0..1]`
  (MW2's `proneDirection` and pitch): the legs pitched 90 degrees about the yaw's right axis,
  no lean, the torso held to the legs and the head looking ahead with the view's pitch; the
  model's eye sits on the prone eye over the origin and the body runs back behind it, pivoted on
  the rotated feet (`CG_PlaceProneModel`, then `CG_ScalePlayerModel`). The gun hangs on the
  torso and points down. Origin, box, lighting origins and shadow trace are untouched. A
  corpse plays its death standing. The CTF flag stands while crouched or prone.
- **peacedump** prints prone, the crawl stop (`PMF_PRONEMOVE_OVERRIDDEN`), the eye and its
  leg, the prone body and the last command's stance bits.
- Not ported (optional, last in the spec): MW2's viewmodel prone offsets; the gun bob stays
  Quake 3's.

**Binds** (owner's decision): PC SPACE `+gostand`, CTRL `toggleprone`, `c` `togglecrouch`
(`dev/peace-autoexec.cfg`, MW2's `default_mp_controls.cfg`); pad RS `+stance`, A `+gostand`
(`dev/peace-gamepad.cfg`). The q3_ui controls menu has MW2's PC rows ("stand / jump",
"crouch", "prone", "hold crouch", "hold prone", "change stance"; the preview shows the crouch,
as it has no prone animation) and the missionpack UI the same defaults; attack's default is
MOUSE1.

**Tests.** `run-stanceinput.sh` (the real client), `run-stancecgame.sh` (respawn reset, the
unpredicted eye, the hint), `run-stanceflow.sh` (client, Pmove and cgame end to end),
`run-bullets.sh` (drawn
bullets against the server's on the hit volumes, drop shots), `event_ring_test.c` (in
`run-loadouts`), `view_test.c` and `player_model_test.c` (in `run-view`).
