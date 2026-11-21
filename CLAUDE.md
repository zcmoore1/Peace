# Peace

CoD-style total conversion on ioquake3.

## Build command — give the user EXACTLY this, never reworded, never varied

```
cd /d C:\Users\ZackI\Desktop\OneManIW\ProjectPeace\dev2\Peace
cmake --build out\build\x64-Debug --clean-first
```

Run it in the **x64 Native Tools Command Prompt for VS**. Always include the
`cd`. Do not substitute paths, drop `/d`, or make `--clean-first` conditional —
the user asked for this block verbatim.

When a pull is also needed, put `git pull origin <branch>` between the two lines.

The owner builds with MSVC; Linux GCC builds and tests do not see its
preprocessor. **Define every negative constant in parentheses**
(`#define MINS_Z (-24)`): MSVC pastes a sign written against a bare negative
macro inside a macro argument into one token, so `VectorMA( a, -MINS_Z, b, c )`
with `-24` became `--24` (error C2105, '--' needs l-value) where GCC builds.
`gcc -E -traditional-cpp` substitutes text the same way and shows it.

## Branches

- `dev` — **the development branch.** Work here and pull from here. It grew
  out of `dev-gpt-usereload` (MW2 use/reload, movement, quick actions, throwing
  knife and sea lion, GPT's class menus, perks, attachments and M300, S&D,
  ladders, Semtex) and took in the standalone boot check and the generated maps
  from the older branches.
- Merged into `dev` and retired, safe to delete (their history is all in `dev`): `dev1`,
  `dev2`, `dev3`, `dev3b`, `gptbranch`, `dev-3b-gpt`, `dev-gpt`,
  `dev-gpt-usereload`, `feature/nested-class-menu`,
  `integration/usereload-class-menu`.
- Kept as history, superseded: `dev2-standalone` (its `fs_checkPak0` skip is in
  `dev`), `dev-claude` (an earlier S&D; its `peace_sd` map was regenerated for
  `g_snd.c` in `dev`) and the early `claude/*` prototypes (the `dm_outpost`
  source and map tools from `claude/quake3-mod-maps-67wm8k` are in `dev`).
  `main` is still the initial commit.

## Config files

Both go in `out\build\x64-Debug\Debug\baseq3\`:
- `dev/peace-autoexec.cfg` → rename to `autoexec.cfg`
- `dev/peace-gamepad.cfg` → copy as-is, keep the name (autoexec execs it by name)

The stance binds are MW2's (owner's decision). PC (MW2's
`default_mp_controls.cfg`): SPACE `+gostand` (stands up; jumps only when
standing), CTRL `toggleprone`, `c` `togglecrouch` (a toggle now, not a held
crouch). Pad: RS `+stance` (MW2's Tactical stance button: tap to crouch or
stand, hold `cl_stanceHoldTime`, 300ms, to go prone or up from prone), A
`+gostand`. `+movedown` and `+prone` are MW2's held crouch and prone, unbound;
`+moveup` raises one stance, then jumps. The q3_ui controls menu has MW2's PC
rows and defaults (attack on MOUSE1). `cl_debugButtons 1` also prints the
command a `+stance` hold fired in and each change of a command's stance bits.
See `dev/STANCE.md`.

## Diagnostics

- `qconsole.log` and `peacedump.txt` land in the **homepath**, not the build dir:
  `%APPDATA%\Quake3\baseq3\`
- Logging defaults to `logfile 2` (unbuffered, survives a hard crash) and
  appends, with a `===== session started` banner per run.
- `peacedump` prints the full weapon/loadout state. `F11` = `peacedump; condump`.
- In-game: `cg_weaponDebug 1`, `g_debugMove 1`, `devmap <map>` (not `map`).
- `cl_debugButtons 1` prints which command each X (use/reload) and RT press
  went out in. X must go out a command before RT for a fake bullet.
- `spawndummy [red|blue]` (server console) adds a stationary training dummy on
  any map. It needs no bot AI and no `.aas` file — Peace's maps have none, and
  botlib refuses a map without one. The mark is the `peace_dummy` userinfo key,
  so it survives `map_restart`. In team games it joins the named or smaller
  team, which makes S&D testable alone: `team red`, then `spawndummy blue`.
  `pers.isDummy` survives `ClientSpawn`'s clear/restore; putting the flag in the
  transient part of `gclient_t` loses it on the very first spawn and every round.

## MW2 evidence (owner's rule)

"Do what MW2 does" means MW2's own code. Read it in the MW2 multiplayer
executable (IW4x's `iw4x.exe`, unpacked; IW4x's source gives the addresses,
struct layouts and names). CoD4's decompile is only a labeled fallback, never
proof of what MW2 does. **Permanent rule: absolutely no copying iw4L unless
the owner explicitly instructs it.** Do not read it, cite it or use it as a
source for MW2 behaviour; the owner rejects it.

## Design invariants — do not violate

- **The NAC is not code.** It emerges from ordering in `PM_Weapon`: a holster
  ALWAYS stamps a real time from `BG_HolsterTime`, `WNOTE_MAG_IN` zeroes `weaponTime`, and a
  transition whose lock is already spent finishes that think and `return`s —
  before the queued clip fill. Never add `if (nac)`, a NAC-named function, a
  special state, or a zero drop time. (One gun changing firing mode is not a
  holster: its times are MW2's `altDropTime` and `altRaiseTime`, and MW2's
  data makes that drop 0. See the M16 entry under "Adding a weapon".)
- **Two clocks, kept separate.** `weaponAnimTime` counts UP and is what notes
  fire off; `weaponTime` counts DOWN and is only a busy lock. Never derive note
  times from `weaponTime`.
- **Reload never resumes.** It always restarts from 0. No partial-reload state.
- **Animation ownership is generic.** Sprint preserves a running non-reload
  action; a holster keeps that source through its whole duration, including a
  ladder holster. One-shots finish and hold their endpoint; loops keep playing.
  An ordinary sprint entry still reverses into its exit within its own layer;
  a sprint on again during the exit lets the exit finish (MW2's sprint check
  does nothing in SPRINT_DROP, 0x5780F4-0x578107), and the entry, on the
  think after the exit's last, starts at its ready pose, where the exit left it.
  Returning the requested slot releases the retained picture through the same
  cancellation used for any drop. No named-glitch branch, weapon-specific
  retention list, zero lock, or separate glide state. Reloads and quick actions
  cannot own a retained layer. Cgame must never confer gameplay readiness.
- **YY flows; it never snaps (owner's rule).** Every raise and drop starts from
  the pose on screen. A holster that interrupts an unfinished draw plays that
  same draw back down, fitted to `BG_HolsterTime`; a gun at rest fades into its
  drop. When YY cancels a transition, gameplay is `WEAPON_READY` at once — only
  the picture eases back along the clip it was showing (smoothstep over that
  stretch's natural time, capped at `MOVE_RETURN_MS`), then hands to idle with
  the 40ms crossfade a draw ends on. A shot cuts the return immediately, and a
  returning cancel is never a retained sprint source. It holds no lock and is
  not a gameplay state. `TestFlow` drives it through the real `PM_Weapon`.
- **Sprint cancellation returns along the visible transition.** Sprint, Y,
  end the sprint (it latches: a fresh press, aim, letting go of forward), Y
  transfers the retained sprint entry, carry or exit into the existing
  move-layer return. The picture is the ordinary sprint exit's, minus its
  lock: a carry (entry finished, or the loop) plays the exit over the whole
  `BG_WeaponSprintOutTime` (250ms SPAS / 260ms M300), an exit already under
  way keeps its own pace, and a partial entry runs back over the whole
  sprint-out time, as a sprint tap's reversal does. No `MOVE_RETURN_MS` cap
  and no second easing: the clock is linear and the authored clip eases the
  pose, so it leaves the carry at the exit's pace and settles like it. A loop
  drops its bob onto the exit's first frame exactly as an ordinary stop in the
  loop does. The return does not keep advancing away while
  fading to idle. The ready endpoint stays fixed throughout that fade. A new
  sprint may inherit only a posture that is still visible (including a
  move-layer return), never a hidden layer's old clock. A sprint still running
  through the swap still requests sprint: YY readies the gun on its think and
  the next think lowers it again (MW2's double-tap cancel comes after its
  sprint check, 0x44C4D7), its entry starting from the pose the return shows;
  a pump or bolt still owed is worked first, from READY on the YY think. In
  the air the picture uses the same
  sprint weapon states; whether a sprint starts, goes on or ends there is the
  MW2 sprint rule below (holding jump ends one).
- **Base-action releases preserve the last picture.** Fire/bolt/reload completion
  and interruption into a holster use a 100ms pose release in native and
  frame-only rendering. It never overlays the move layer's YY return with a
  second transition; a new shot cancels it. Stereo eyes share one cosmetic clock.
- **Bolting is a state, not a phase of firing.** `WEAPON_FIRING` is the shot;
  `WEAPON_BOLTING` is the action being worked. "Bolting" covers bolt, pump,
  cock and lever — they differ only in cycle length and note placement, never
  in code. It cannot fire, it cannot be sprinted out of, and it CAN be
  holstered out of. Sprint is MW2's `PM_Weapon_CheckForSprint` (iw4x
  0x578060), which does nothing while the gun is FIRING or RECHAMBERING;
  the sprint flag, speed and meter run as usual (MW2's sprint gates never
  test those states). With the trigger let go, MW2's shot ends READY owing its
  action at the end of its last think (`ShouldBeFiring`, 0x576D99-0x576DA3),
  and on the next think
  every check, the sprint's included, comes before `CheckForRechamber`
  (0x575a30, which starts the action only from READY, 0x575B77), so a sprint
  on by then takes the READY gun and the action stays owed: step 4d works all
  of it once the gun is back up (a deferral, never a cancel). The shot's last
  think is the one its clock reaches `WNOTE_BOLT_OPEN` (`PM_ShotOver`): still
  FIRING, it does not lower, and step 4f leaves it READY at that note
  (`BG_ShotAwaitsAction`, which cgame reads to keep the one fire clip on
  screen); the next think lowers before step 4d can start the action. Once
  the action is under way step 1b never lowers `WEAPON_BOLTING`: you run
  while it finishes and the gun lowers the think after step 4c ends it,
  chambered. A knife waits for `WNOTE_BOLT_CLOSED` (MW2's knife refuses the
  rechamber's pending bolt-close delay): a press before it is lost, one
  after it cuts the tail short; the shot and its READY think take one. It
  ends on the anim clock, never on the lock. A reload is taken at once out
  of the shot, its READY think or the action, as MW2's
  `PM_BeginWeaponReload` accepts READY, FIRING and RECHAMBERING (owner's
  rule: shoot and reload with no delay); the fire cycle is simply abandoned,
  its debt still owed. An empty gun reloads itself once the shot is over, on
  its READY think (MW2's auto reload refuses FIRING), never out of the shot,
  unless the trigger is still down from the shot (below); a gun with no fire
  clip, whose shot ends as its lock runs out, the think after that one, or on
  it with the trigger held (MW2's FireWeapon ammo check, 0x5770C0).
- **A single-shot gun fires once a pull of the trigger (owner's rule: do what
  MW2 does).** MW2's fire is a level test of the trigger (`ShouldBeFiring`,
  0x576CC0); what stops a held trigger is the lock `PM_WeaponTimeAdjust` puts
  on the think a state's time runs out (MW2's `PM_Weapon_WeaponTimeAdjust`,
  0x5789DD-0x578B71), before anything else of the think: with the trigger
  down, the pull's shots all fired (`BG_ShotLimitReached`, MW2's
  `ShotLimitReached` 0x5769E0, on the gun's fireType from `BG_WeaponFireType`,
  and `ps.weaponShotCount`, MW2's counter, which every shot but a full-auto one
  adds to, capped at 4, 0x576F93-0x576FC5, and a reload zeroes, 0x575EFE), the
  command still naming the gun and a round in its magazine, the gun's time
  becomes 1 (0x578AE6): the shot's end, the action's end and the knife's end
  leave the gun READY, any other state (a raise, the sprint-out) keeps its
  state, and the 1 runs out on the next think, which asks again. A think that
  asks with the trigger up zeroes the count. So the SPAS, the M300 (as the
  Intervention) and the Masterkey, Single Shot in MW2's files, are READY
  under the lock on the shot's last think with the trigger held, and step 4d
  starts the action from that READY on that same think (MW2's
  `CheckForRechamber` takes READY with no weaponDelay, 0x575B07-0x575BA9):
  the bolt and the end come a think sooner
  than with the trigger let go, the READY think after the shot exists only
  with the trigger let go, and the gun fires nothing until the trigger has
  been let go: let go on the action's last command and pulled on the next, it
  fires on the command a let-go trigger would have, so holding gains nothing
  (`dev/tests/trigger_test.c`). Full auto (the machinegun's `m4_mp`, the rocket launcher's
  `rpg_mp`, the Quake 3 guns, the M16 as Peace's placeholder automatic) is
  never locked, and each of its shots is given its whole time on the think the
  last one's ran out (MW2's `StartFiring` assigns `iFireTime`, 0x576E9C-0x576E9F,
  after the crossing dropped the overshoot, 0x578B71): held, it fires every
  ceil(fireTime / msec) thinks. (The knife's time is MW2's one meleeTime from
  the press, so its draw's overshoot carries into it.) The fire type is data (`bg_weaponFireType`, and the generated
  guns' `fire_type` in their `weapon.json`); never test a weapon id for it.
  The lock is only the time, so whatever zeroes it lets a held trigger fire:
  YY's return, the gun's return after equipment (MW2's offhand states are
  never locked, 0x578A94), a reload; the count is the hand's, so a
  single-shot gun swapped to with the trigger still down from another's shot
  waits in its raise. With the trigger down on the last think of a last
  round's shot (let go and pulled again in the shot is the same: a level
  test), nothing is locked (no round): with a reserve the reload starts out of
  the shot on that think, the action owed (MW2's `CheckFiringAmmo`,
  0x5770C0), and with nothing anywhere and nowhere to go it dry-fires there
  (below). Never add a
  semi-auto flag, a trigger-release latch or a weapon-named branch: the
  count, the fire type and the time are the whole rule.
- **The bolt cancel is the reload cancel.** `WNOTE_BOLT_CLOSED` on the fire
  animation carries no rounds. Like `WNOTE_MAG_IN` it clears the lock on its
  think and only queues its completion: the chambering commits in step 4, the
  same commit point as a reload's fill and delayed action. A swap or ladder
  grab that completes on that exact think is still instant, but it returns
  before step 4, so the chamber debt stays owed (owner's rule: a successful
  NAC does not complete the pump). A sprint is not one of them: it never
  lowers a gun in the action, so the note has no sprint lock to take and
  there is no "bolt NAC into a sprint" (MW2 has none). One think later — the
  late cancel — the chambering has committed and is kept. It is the same note,
  in the same place, that opens the reload's window. Never write a second
  cancel.
- **An open action is a debt, and it is never resumed.** Firing sets the
  weapon's bit in `STAT_UNCHAMBERED`; only the close note's step-4 commit, the
  reload's delayed action, or MW2's dry fire clears it. The dry fire is MW2's
  `PM_Weapon_CheckFiringAmmo` (0x576FE0), which clears the rechamber
  (`BG_ClearRechamber`, 0x577094) whenever it dry-fires a gun with no round in
  its magazine and none in reserve; it only ever finds a debt on the last
  think of that gun's last round's shot (`PM_ShotOver`), when the trigger is
  down on that think and no change begins on it (no gun to go to). MW2's fire
  is a level test of the trigger (`ShouldBeFiring`, 0x576CC0), so a trigger
  let go during the shot and pulled again by that think dry-fires as one held
  throughout: the gun dry-fires on that think, out of the shot, and never
  works the action. With the trigger up on that think the shot ends READY and
  step 4d starts the action on the next, before any pull can dry-fire. (Out of
  READY it never has one: step 4d starts the action first.) A gun that reaches `WEAPON_READY`
  with its bit set re-enters `WEAPON_BOLTING` (step 4d) on the next think
  that finds it READY, from the action's own start, the open note: the whole
  action, never from where it was and never the shot again, exactly as a
  reload restarts from 0. That is MW2's `CheckForRechamber` from READY
  (0x575B77): RECHAMBERING (0x575BA9) for `rechamberTime` (0x575BD2), the fire
  clip after its open note, with the bolt `rechamberBoltTime` in, or 1 when
  that is 0 (0x575BD8-0x575C03), the close note's place after the open note.
  The shot itself ends READY with its bit set (step 4f, or with the trigger
  held the lock on its last think), so the same rule starts the action after
  every shot: the bolt comes a think after the action begins at the earliest,
  and the gun is ready ceil(rechamberTime / msec) thinks after. A cancel before
  the close note, or on its very think, costs the full pump or bolt when you
  come back; a cancel a think later keeps the chambering. Never add a
  partial-bolt state, and never clear the bit anywhere but those three places
  (spawn is the one exception).
- **The reload's delayed action is MW2's (owner's rule: do what MW2 does).**
  When a reload starts on a gun that still owes its pump or bolt, the action is
  settled after the gun's OWN action length (fire-cycle open to close, MW2's
  `rechamberBoltTime`) on the reload clock, capped by the reload start's first
  round or its end (`PM_ReloadSettleCrossed`; derived from the tables, never a
  written time). It lands like a note: step 2b releases the lock, step 4 settles
  the debt alongside the queued fill. So Y on that think is the fastest NAC —
  the holster is spent and finishes in step 3, skipping step 4, and the gun
  comes back owing its whole pump, as in MW2. YY a think or more later leaves a
  ready gun with no pump — the Fayde; YY before it pumps again; an ordinary
  reload gains nothing. Never add a state test or a placed time to this. (The M
  NAC, a return with the pump still workable, is future work.)
  A gun whose bolt is 1ms into its action (MW2's `rechamberBoltTime` 0, which
  MW2 never arms as 0: the Masterkey, and CoD4's W1200) settles on the reload's
  first think. A swap already waiting when that reload starts therefore always
  meets it, on any timing: shoot, Y, Y, X on the Masterkey is MW2's Moni, and
  the SPAS (449 of its 467 in MW2, 283 here) cannot do it. Nothing names it;
  `dev/tests/moni_test.c` only presses the buttons.
- **A swap asked for during the shot begins on the shot's last think (owner's
  rule: do what MW2 does).** MW2's change check refuses FIRING only while the
  shot has time left (0x578C0A-0x578C31), and its rechamber starts only from
  READY, on a later think (0x575B77): the change takes the gun first and the
  action stays owed. Here the shot ends on the think its clock reaches
  `WNOTE_BOLT_OPEN`: `PM_WeaponChangeOpen` opens there (`PM_ShotOver`), the
  shot's clock stops at that note, and step 4f leaves the gun READY there.
  MW2's change check runs before its rechamber check (0x44C4D7), so a change
  first seen on the READY think after is a whole drop with the action owed
  too, and only one first seen on the bolt's own think (the next one on the
  Masterkey) is instant with it owed: the bolt's delayed action finishes the
  drop begun on its think (MW2's path A, 0x579474, 0x57928C; model trace
  `mk_rech_frame` in wf20). A think later the bolt is paid. With the trigger
  held from the shot the action starts on the shot's last think (the lock's
  READY), so the Masterkey's bolt think, the instant swap's, is the one after
  the shot's last; a change first seen on the shot's last think names another
  gun, so it locks nothing and is the whole drop with the action owed, as
  with the trigger let go (`moni_test.c`'s `TestHeldRechamber`). The Moni
  (shoot, Y, Y, X inside the shot) is the same either way. Never let the
  fire clock run from the shot into the action: a gun whose bolt is 1ms in
  would pay its pump on the very think a waiting swap begins, and swap free.
- **Clip frame ranges are data.** They live in the `.cfg` beside the model and
  are read by `CG_WeapAnim_RegisterClips`. Re-exporting a model must never mean
  editing C, and no weapon may be named in that loader.
- **cgame is picture only.** It never touches ammo or weapon state. The owner
  has approved two exceptions. The first is MW2's view kick, and only the
  kick: cgame authors it and the client adds it into the command's aim. The
  second is MW2's `CL_SetStance`, and only it: on the player's own
  `EV_STANCE_FORCE_STAND/CROUCH/PRONE` (MW2's 6-8, 0x4DD48F-0x4DD4EB), and
  after every prediction pass while the predicted playerstate has
  `PMF_RESPAWNED` (0x5901AF), cgame puts the client's stance where pmove put
  the body, through the hidden console command `_peaceSetStance` (leaveads's
  channel: the next frame's `Cbuf_Execute` runs it before that frame's command
  is built, the command a direct call reaches). Client input only, the
  player's own view only (never a demo, a killcam or a followed player), and a
  held stance key refuses it as MW2's does; it never touches the `+stance`
  hold, and pmove alone decides the body. Never let cgame write the stance
  anywhere else.
- **The view kick is MW2's, and the tilt emerges (owner's rule).** The
  predicted player's own `EV_FIRE_WEAPON` assigns the kick's angular velocity
  (`BG_WeaponFireRecoil`'s view part, MW2's 0x4a5fe0, ADS data only at
  `cg.adsFrac` exactly 1, the client-only `cg_kickSeed`). `CG_KickAngles`
  (MW2's 0x596790) runs it out once a frame in 5ms steps, at the centre speed
  of `BG_ViewmodelWeapon` (MW2's viewmodel weapon: `ps.weapon`, but the gun
  under the melee knife, which MW2 writes over the gun's state), 2400 with no
  MW2 def, clamped at 10 degrees, a step back cut to 0.06. It is zeroed
  unless the state is the player's own (no demo, killcam or follow: MW2's
  `POF_PLAYER`). `trap_SetUserCmdKickAngles` hands it to
  `cl.cgameKickAngles`, and `CL_FinishMove` adds it into `cmd.angles`, all
  three axes, never into `cl.viewangles`. The view shows it only through the
  predicted `ps.viewangles`; the server aims with them and the killcam
  replays them, so pmove is unchanged and the replay adds no kick. The
  throwing knife's centre speed is 0, so a knife pulled just after a shot lets
  the kick run to the clamp: the tilt. Never add a knife, tilt or killcam
  branch to it, never draw the kick into the refdef, and never let it touch
  `cl.viewangles`. A quick action's fire kicks nothing (MW2 fires no gun event
  for it), and a gun with no MW2 counterpart has no row. See
  `dev/VIEW_KICK.md`.
- **Bullets are drawn by the clients (MW2's sv_clientSideBullets, owner's
  decision).** The server traces every hitscan shot for its damage only and
  sends no impact, tracer, blast or beam for it (`g_weapon.c`: `Bullet_Fire`
  for the machinegun, M300 and chaingun, the SPAS's pellets, the railgun, the
  lightning gun). Every client traces the shot again from its fire event and
  draws them (`CG_FireBullets`, cg_weapons.c). Both build it with the same
  `bg_misc.c` code: `BG_ShotMuzzle`, MW2's seed (the shot command's time,
  Bullet_Fire's gameTime, through BG_srand 0x410720 and BG_random 0x44c590:
  `BG_BulletSeed`, `BG_BulletRandom`) and Peace's own shapes (`BG_BulletEnd`
  scaled by `BG_WeaponSpread`, `BG_ShotgunAxes`/`BG_ShotgunPelletEnd`), so a
  shot hurts what it always did. The player's own shot comes from its
  playerstate event, from the state its command left (`CG_RecordShots`, in
  the prediction loop): it lands exactly where the server's damage traces
  went. With nothing predicted (a demo, a followed player, the killcam) it is
  drawn from the state the event came in, MW2's predictedPlayerState.
  Everyone else's comes from the shooter's entity event and what the entity
  carries (its last command's snapped origin, its eye unsnapped in
  `origin2[2]`, whole-degree aim, `pos.trTime` and MW2's `EF_CROUCHING`/
  `EF_PRONE` for the spread's stance; no aim state, so the hip's spread):
  exact only when the entity shows the shot's own think (MW2 seeds others'
  from the clock). The viewed player is never in their own snapshot, so
  `CG_ShotTrace` tests their box (`BG_PlayerBox`), but only while they have
  one: with no health and no `PM_DEAD` corpse (S&D's
  player out of the round, unlinked on the server) the bullet goes through.
  Damage, ammo and weapon state stay the server's and pmove's; the fire event's
  parm names the gun that fired, never a seed or spread. Projectiles, both
  knives and Team Arena's invulnerability bounces keep their server effects.
  The fake bullet now shows its tracer and impacts on the shooter's screen,
  with no damage, as MW2's does; no window moved. Run
  `dev/tests/run-bullets.sh`.
- **What a bullet hits is Peace's approximation of MW2's skeleton (owner's
  decision; a placeholder, not a match).** MW2 hits the posed skeleton,
  never its move box. Peace's bullets and pellets hit the move box plus two
  bullet-only volumes from `BG_PlayerHitVolumes`, the one function the
  server's damage trace (`G_ShotTrace`, for `Bullet_Fire` and
  `ShotgunPellet`) and cgame's drawn bullets and pellets (`CG_ShotTrace`)
  both call, on the target's entity and the box their own trace tests: over
  the box while the eye is above it (a downward stance leg, 110ms of stand to
  crouch and 106ms of crouch to prone, in whole thinks: whoever can fire over
  cover can be shot over it), up to the eye plus `BG_HIT_HEAD_ALLOWANCE`; and, prone, the
  body MW2's prone room check traces (`BG_PRONE_FEET_DIST` behind the
  origin, `BG_PRONE_TRACE_HALF` either side, feet to the prone top, turned
  by the body's yaw and pitch on the entity). The nearest entry wins; a
  shot that starts inside a volume and leaves it passes, as the engine's
  own box passes one (startsolid, the fraction kept); a corpse is its box.
  Everything else keeps the move box: the railgun, the lightning gun,
  knives, melee, the throwing knife, explosions, collision.
  Everything the volumes read is on the entity (`EF_DEAD`, `EF_PRONE`,
  `origin2`), so both sides agree whenever they see the same entity. Never
  let one side test a volume the other does not, and never add a volume
  that reads anything but the entity and the box.
- **Aim follows simulation permission, never a scheduled scope flash.** Aim
  progress belongs to the player and survives weapon/slot/attachment changes.
  `BG_CanAimDownSights` is the shared physical rule (no empty hand, no quick
  action, ladder, sprint, reload, raise or drop); `PM_CheckADS` adds the
  buttons (aim held, and no sprint held with an unscoped gun) and runs before
  `PM_Weapon`. An instant transition, such as a NAC from an aimed SPAS to the
  M300, can therefore select a scoped gun while the outgoing aim flag is still
  live, and that frame shows the scope. The next movement update sees the
  raise and revokes permission; cgame clears aim when `PMF_ADS` is off and
  `BG_CanAimDownSights` refuses. Letting go of aim, or holding sprint, blends
  out normally. No handoff event, frame counter, NAC detector or weapon
  exception may manufacture a visible flash. Multiple movement updates before
  a render may pass through this overlap unseen. It reads only fields that
  already travel: no protocol or game version change.
- **Movement modifiers are per-command values.** `PM_WalkScale`,
  `PM_AirScale` and `PM_CmdScale` must not write sprint/ADS/crouch/class
  multipliers into `ps.speed`, and nothing writes the command: prediction
  replay and pmove substeps would apply them repeatedly, making speed depend
  on latency or command count. MW2 writes its sprint strafe into the command;
  Peace scales this think's copy, `pml.rightmove`, set from the command at the
  top of `PmoveSingle`. MW2's prone crawl stops zero the command too; Peace
  zeroes this think's copies (`pml.forwardmove`, `pml.rightmove`), and every
  mover, the bob, the legs and the sprint read the copies. The stance's speed
  (`BG_StanceScale`) is read from the eye's state every think, and MW2's
  `oldcmd` (`ps.prevCmd`) is written once a command, never per step (MW2's
  `Pmove` copies it after every step, 0x4CFF61, which differs only on a
  command chopped into steps; the trigger's record, `WA_ATTACK_HELD`, is
  written every step as MW2's).
- **The walk is MW2's (owner's rule: MW2's numbers, at IW4x's exe defaults).**
  `g_speed` is 190 (`BG_PLAYER_SPEED`; the world was already MW2's, so nothing
  else is scaled, never by 0.8). `PM_WalkScale` is MW2's `PM_CmdScale_Walk`
  (0x572DB0), for `PM_WalkMove` only: forward and right only (the crouch's
  upmove never counts); a backward forwardmove worth 0.7 of itself and a
  rightmove 0.8 (`BG_BACK_SPEED_SCALE`, `BG_STRAFE_SPEED_SCALE`; the retail
  playlist's 1 and 1 are one line each), the larger setting the magnitude,
  never the direction; then aim x0.4, the sprint x1.5 and the stance
  (`BG_StanceScale`, MW2's PM_CmdScaleForStance: the crouch x0.65, prone
  x0.15, and the ramps between them over the 400ms prone legs), each a float
  multiply; the aimed gun's `adsMoveSpeedScale`
  (`BG_WeaponAdsMoveScale`: SPAS 2, the rest 1); and the class speed last
  (`BG_MoveClassScale`). While sprinting the walk truncates `pml.rightmove` to
  0.667 of itself before the jump check, so a take-off's air move reads it too.
  The air is the base speed alone, x0.4 aimed: no class, sprint, crouch or gun.
  Friction 5.5, walk accel 9 standing, 12 crouched and 19 prone, 1.0 on slick
  ground or under knockback; noclip keeps Quake 3's 10 and 9 under their own names. The
  class speed is MW2's script (`updateMoveSpeedScale`): `ps.moveClass` is set
  once, at spawn, by `BG_ApplyClass` from the class's primary through the
  catalog's move-class column, and a swap or a pickup never changes it. Guns
  with no MW2 twin take the script's default 1.0 (owner's choice). Lightweight
  (MW2's perk; Peace's Expeditious slot, same ID) multiplies the class speed by
  1.07, so it is the ground walk's alone. The bob's most (`PM_BobMaxSpeed`,
  MW2's PM_GetMaxSpeed 0x573850) mixes the strafe and back scales MW2's own way
  and leaves the class speed out, so a rifle class bobs at 95% of the rate.
  Pitch is clamped at 85 up and 85 down (`BG_VIEW_PITCH_UP/DOWN`, constants
  so prediction always agrees; the playlist's 70 is one line each). MW2's flash
  `moveSpeedScale` 1.2 is not ported: MW2 keeps the gun in `ps->weapon` through
  an offhand and Peace does not. Sprint and aim are still decided after the
  move (MW2 decides them before it): one think late. Tests read every number
  from the code (`BG_PLAYER_SPEED` and the scale constants), never a literal.
- **The body is MW2's.** 70 tall standing with the eye at 60, 50 crouched with
  the eye at 40, 30 prone with the eye at 11, 30 wide, above the feet. Peace
  keeps its origin `MINS_Z` (24) above the feet, so `DEFAULT_HEIGHT` is 46,
  `CROUCH_HEIGHT` 26, `PRONE_HEIGHT` 6, `DEFAULT_VIEWHEIGHT` 36,
  `CROUCH_VIEWHEIGHT` 16 and `PRONE_VIEWHEIGHT` -13: no map, spawn, death drop
  or TI ground point moves. `BG_PlayerBox` is the one box rule (pmove, the
  server's entity, cgame's viewed player); every eye-based reach (use,
  throwback, knife, muzzle, the PVS) reads `ps.viewheight`, a float. Never
  hard-code a box or an eye in code or tests. The stance is MW2's: the command
  carries it as a level (`BUTTON_PRONE`, `BUTTON_CROUCH`, `BUTTON_TEMP_STANCE`
  for a held key) and `PM_CheckDuck` decides what the body can do, asking for
  room (other players never block it) and saying what it refused or forced
  through the single-client `EV_STANCE_*` events. The eye runs MW2's curve in
  pmove (`PM_ViewHeightAdjust`, its four waypoint tables), a leg at a time,
  the elapsed ms in `viewHeightLerpTime` (never a clock); the flags and the
  box follow the eye's target, so the box drops at a leg's start and a request
  mid-leg waits. Prone is MW2's room check (`BG_CheckProne`), view, crawl and
  weapon rules. Drop shots, crouch spam, a queued prone and a sprint out of
  prone come out of these rules: never name one in code, and never write the
  command (a stopped crawl zeroes this think's copies). See `dev/STANCE.md`.
  MW2's landing stun, mantle and dive are not ported.
- **Selection is MW2's cycle (owner's rule).** `weapnext` and `weapprev` run
  MW2's `CycleWeapPrimary` (0x59ED40) from the latest requested selection, not
  the server's last acknowledged gun: from an underbarrel they select the gun
  that carries it, from any other gun the next slot, and the cycle brings a gun
  back in its own mode, never the underbarrel (MW2's `altModeSameWeapon` is 0
  in every multiplayer weapon file). So the Masterkey is two presses from the
  next gun. `weapalt` is MW2's alt toggle (+actionslot 3): refused while a gun
  goes down and during a mode change's own raise, and from a gun with no other
  mode it selects the next gun's underbarrel as one ordinary change. If an
  unfinished ordinary holster's destination returns to the gun still held,
  shared pmove cancels the drop and reaches the normal fire gates that think.
  There is no YY timing window or extra delay. Initial drops still stamp their
  real times; chamber debt, ammo, sprint and ladder ownership still apply.
  A gun run completely dry (nothing in the magazine, nothing in reserve) asks
  for another itself, as MW2's: pmove raises `EV_NOAMMO` after its last round
  (MW2's `BG_SwitchWeaponsIfEmpty`, 0x576320) and on a dry pull, which starts
  no shot and puts `BG_DRY_FIRE_TIME` (500) on the lock, so a held trigger
  clicks again each time it runs out (`PM_Weapon_CheckFiringAmmo`, 0x576FE0).
  With the trigger down on the last round's shot's last think (held, or let go
  and pulled again in the shot) and nowhere to go, the click comes on that
  think, out of the shot: the gun stays FIRING (no change until
  the 500 has run out, 0x578C31), shows its idle (`BG_DryFiring`) and its
  action is forgiven (An open action is a debt); each click re-arms the 750ms
  Y lockout below, so Y waits for the trigger to be let go.
  The player's own live view then runs `CG_OutOfAmmoChange` (MW2's 0x5A07C0):
  an underbarrel selects its own gun in its own mode, the alt switch, if that
  gun has ammo; otherwise, and for any other gun, the cycle runs from the
  latest selection skipping empty guns (MW2's ignoreEmpty): from an
  underbarrel still selected its own gun, from any other selection the next
  slot's gun in its own mode, only if that mode has rounds in its magazine or
  reserve; if that finds nothing an underbarrel still goes to its own gun and
  any other gun selects nothing. It is a selection like Y's, with no swap
  press, so every change gate, the freeze and the times are the ordinary
  ones. From a gun that is not an underbarrel it also holds Y and `weapprev`
  off for `CG_EMPTY_SWITCH_LOCKOUT` (750, MW2's `weaponForcedSelectTime`,
  0x5A084C), selection or not: a Y then is lost and the change goes on; the
  alt toggle is not held off. A gun given rounds during the dry pull's 500ms
  reloads at once (MW2's reload check comes before the busy gate, step 4e).
  The empty-fire sound is not played. Nothing names a gun.
- **Inventory instances own attachments and ammunition.** `BG_GetSlot` /
  `BG_PutSlot` / `BG_SelectSlot` bridge the two instances to the existing
  `ammo[]`, `ammoReserve[]` and `STAT_UNCHAMBERED` working registers. Stowed
  variants of the same base gun must keep separate magazines and chamber debt.
  Do not replace a floor pickup with a full-mag grant. Death drops the held
  slot, not the requested destination. Exact weapon+attachment matches scavenge
  to reserve on touch (capacity-capped) and consume the item; other guns, a
  resting throwing knife and a Tactical Insertion take MW2's use hold
  (`BG_UseHoldTime`, one hold entity: `useHold`): `+activate`
  at once (g_useholdtime 0), use/reload (and, for guns, the owner's plain
  reload) held 250ms (IW4x's gpad_use_hold_time), nothing in the first 500ms
  after the PLAYER spawns, never an item's age. A short tap still reloads: a
  reload tap in pmove, a use/reload tap through the server's bit (`pickupHeld`
  3 marks a use/reload hold so pmove does not also reload). S&D use is separate.
- **Pickup does not own selection.** The reload hold latches its replacement
  slot, while `cmd.weapon = BG_SlotCommand(ps, slot)` (`BG_SLOT_COMMAND` + slot,
  + `SLOT_COUNT` for the slot's generation bit) continues carrying the latest
  selection. A pickup that replaces the held slot flips that slot's
  `BG_GUN_GENERATION` and empties the hand (`WP_NONE`, MW2's
  BG_TakePlayerWeapon) but leaves a running drop running; a slot command
  still carrying the old generation names the gun just dropped, so it names
  no gun (`BG_SlotCommandCurrent`, MW2's CheckForChangeWeapon): a running drop
  runs on (YY never returns to an empty hand), one that ends meanwhile leaves
  the hand empty and ready (`PM_FinishWeaponChange` to `WP_NONE` spends no
  first draw), and the hand stays empty about a round trip, until the
  client's selection catches up (MW2's select hand-off). Nothing is put down
  from an empty hand and nothing is aimed with it; equipment out of it pays
  `BG_EMPTY_HAND_OFFHAND_TIME` (none on the think another's return to the
  empty hand ends, MW2's one-think OFFHAND_END delay; with no use hint it pays
  nothing, and on that think it is in hand and ready, its prepare unplayed:
  MW2's switch sends OFFHAND_PREPARE to OffHandHold, 0x5792DA), and comes
  back to an empty hand (`WA_LEFT_EMPTY`, MW2's OffHandEnd with weapon 0), a
  Semtex cancel's included, so the gun then comes up on its ordinary raise.
  The knife out of an empty hand is Peace's own (MW2's knife is the gun's
  melee, and its knife check needs the weapon's melee damage, 0x5776E0): it
  goes back to the empty hand as MW2's MeleeEnd goes with no knife model to
  raise, `PM_Weapon_Idle` on the knife's last think (0x578DF3-0x578DF7: READY,
  no time, no delay), so the think after is an ordinary READY think, with no
  OFFHAND_END delay: a knife, a gun asked for and equipment are all taken on
  it, equipment's prepare played. Old plain weapon IDs are supported for bots/tools. Neither
  inventory replacement nor a delayed pickup event may overwrite Y intent.
  First deployment is pending until the instance is drawn, then consumed on
  start; normal holster/YY/sprint retention may interrupt it. No CAN state.
  `first_raise` is optional clip data, with ordinary raise as the art fallback.
  `cg_sight` seeds attachments at spawn; owned/dropped gun state drives optics
  thereafter, including spectators and replays. See `dev/WEAPON_PICKUPS.md`.
- **The sea lion is MW2's, and emerges (owner's rule: do what MW2 does).**
  `ps.cursorHintEntIndex` is MW2's sticky use hint, written only by the
  server: `G_UpdateCursorHint` (end-frame) names the pickup in reach (or
  `ENTITYNUM_WORLD`, live and never freed, while an S&D site or the bomb is
  usable, which ends a sea lion) and
  never clears it; with nothing in reach, sprinting, equipment out
  (`BG_OffhandOut`) or dead it writes nothing; spawn leaves 0, a live entity
  number. Only freeing what it names (`G_ClearCursorHintRefs` in
  `G_FreeEntity`, flagged `FL_CURSOR_HINT`) makes it `ENTITYNUM_NONE`, and a
  pickup frees its dropped gun - or a throwing knife taken back by use, or a
  Tactical Insertion picked up by its owner or smashed by an enemy - at once.
  MW2's use list ranks by score, lowest first: a resting knife outranks
  anything; a TI (-192) and a floor gun compare by 128 * (1 - dot) + eye
  distance (`G_UseScore`). Its type, `ps.cursorHint`, is cleared by every pass
  and set beside a written index; the server's use test and cgame's use
  prompt read it, pmove never does.
  Pmove only reads the index: with no hint at
  all and either no gun in hand or the gun's quick raise after equipment
  (MW2's OFFHAND_END, a Semtex cancel's included), equipment comes straight
  out and the gun is never lowered (MW2's CheckForOffHand 5791b0-5791ce). So
  swapping for a gun, then pulling equipment before the dropped gun (after
  `BG_PICKUP_OWNER_DELAY`) or another becomes the hint, gives instant pulls
  and instant double cancels until a gun in reach takes the hint. Never test
  for the glitch; never write `ENTITYNUM_NONE` into the hint anywhere else
  (doing so at spawn would start every life in it). The knife's return is not
  OFFHAND_END. The throwing knife's own pull never lowers the gun, hint or not
  (MW2's weaponClass 9 prepares in OffHandInit, `BG_OffhandPreparesAtOnce`).
  A quick action pulled from an empty hand goes back to an empty hand
  (`WA_LEFT_EMPTY`), every offhand alike: its own drop goes to no gun, the only
  drop that may, and `WA_RECOVERING` is set only when a Semtex left a gun, so a
  queued Y then draws straight from the empty hand. A gun is never lowered
  toward an unowned selection. This drop that never happens is not a holster.
  The throwing knife's instant pull also comes out of a reload, a shot or a
  pump/bolt (MW2's OffHandInit+Prepare for class 9). It abandons what the gun
  was doing, as any swap does: a reload keeps only rounds whose fill landed
  before the press (a note due on the press think is lost, MW2's same-think
  hand step) and restarts from 0, and an open action stays owed in
  `STAT_UNCHAMBERED` and is worked whole, from its open note, when the gun is
  back. It stamps no
  drop, so the NAC ordering is untouched.
- **The knife is MW2's: no drop, the hit at meleeDelay, over at meleeTime
  (owner's rule: MW2's MeleeInit, MeleeFire and MeleeEnd).** On the press the
  gun is never lowered: the knife is in hand on the press command itself,
  through the same no-drop path as the throwing knife's pull
  (`PM_UpdateWeaponAction`'s prepare, `PM_FinishWeaponChange` on the press).
  No drop happens at all - it is not a zero drop time - as MW2's MeleeInit
  writes the melee state straight over the gun's. The knife's draw is
  `BG_MELEE_DELAY` (79ms, MW2's meleeDelay) and ends in the hit
  (`EV_FIRE_WEAPON`, the server's knife trace) on the think it runs out; the
  knife is over `BG_MELEE_TIME` (800ms, MW2's meleeTime) after the press, and
  on that command the gun's quick raise starts with nothing put down in
  between (`PM_BeginWeaponChange` draws straight from the knife, as from an
  empty hand). With the trigger down there and the gun's pull already fired
  (a knife pressed in a single-shot gun's shot, or out of its lock), MW2's
  lock takes the knife's end (MELEE_FIRE, 0x578B2A-0x578B39): the gun is READY
  in hand at once, under the lock, with no quick raise, and works an owed
  action from there; MW2's MELEE_END, the quick raise's end, is locked the
  same way. Both are MW2 weapon data, identical for every MW2 MP gun in the
  dump; everything else follows from them. The only knife data is
  `BG_QuickActionInPlace` and the knife's table rows: no knife-named branch,
  no glitch branch. Every knife gate stays: equipment pressed on the knife's
  own command wins (MW2 checks its offhands first), and equipment cuts the
  knife at any point (MW2's offhand check takes every melee state): pressed
  before the hit, or on the hit's own think, it never lands. A knife is
  started only from no quick
  action, and never in a raise or drop (MW2's knife check refuses states 1-5),
  so a knife pressed during a swap, a draw or a quick action's return is lost -
  except on the command YY returns the drop to the held gun, as MW2 readies
  the gun before its knife (the knife is out on that command). For the same
  reason Y and the knife on one command is the swap: its holster starts and
  the knife is lost. No knife while a scoped gun is aimed, nor while a pump or
  bolt is still open: a press before `WNOTE_BOLT_CLOSED` is lost. Nor on any
  think a weapon delay runs out (`PM_WeaponDelayEnds`, MW2's delayedAction,
  which its knife check refuses, 0x5776ED): a reload's round or its settle of
  an owed action, the bolt's close, or the think equipment's return to an
  empty hand ends (OFFHAND_END's one-think delay, a state the check refuses
  as well, 0x5776AE-0x5776B6); the press is lost, and the round seats
  or the action is paid. The close note of a reload's end is none (MW2's
  RELOAD_END has no delay, 0x5785EB-0x578606): the knife is taken on it.
  A knife or grenade pressed during a gun's fire lock, or on the READY think
  after the shot, starts at once (MW2's melee and offhand checks take FIRING
  and READY, and run before its rechamber check; only a Y swap waits out the shot,
  `PM_WeaponChangeOpen`), and a pump or bolt still owed is worked whole, from
  its open note, when the gun is back. Whatever the gun was doing is abandoned on the press, as by
  the throwing knife's pull. `BG_HolsterTime` is the single drop rule - gun's
  own drop, quick drop, rapid, or the ladder's; the knife has no line in it,
  since nothing is put down for it - used by pmove to stamp and by cgame to
  fit the drop clip, so the two cannot disagree. Every grenade (frag, flash,
  Semtex) sends the gun, or the knife it cuts, away on the gun's MW2
  `quickDropTime` (`BG_WeaponQuickDropTime`, 250ms), as MW2's OffHandInit
  (owner's rule: MW2's numbers). Out of the knife, in hand or in the gun's
  quick raise after it, that is the whole quick drop from the press: no drop
  is running there to keep or restamp, and the knife's time after its hit
  ends on the press (`PM_WeaponChangeOpen`). The one exception is the think
  the knife's draw ends: that is MW2's meleeDelay running out, a weapon delay,
  so like a note it takes the lock of a drop begun on it (step 2c), and the
  equipment is prepared on that think, with no drop and no hit (MW2's
  OffHandInit, then OFFHAND_INIT's case of its state switch, 0x5792C7). A
  grenade pressed during a
  running holster keeps it, as OffHandInit keeps a drop already going: while
  more of the gun's own drop is left than a quick drop, `BG_HolsterTime`
  answers that drop, so nothing shortens it. cgame re-anchors a drop whose
  lock is restamped or whose `BG_HolsterTime` changes where the old fit has it
  (`CG_WeapAnim_ReanchorDrop`), so the picture never jumps; the fit is still
  `BG_HolsterTime`. Do not add a second timing rule in the renderer.
- **Offhands are MW2's pre-throw machine, from each one's data (owner's rule:
  do what MW2 does).** The table in `bg_loadout.c` and the raise table carry
  the IW4 dump's values; pmove names no offhand. The prepare is the item's
  holdFireTime as its raise (`BG_WeaponRaiseTime` never floors an offhand's):
  frag, flash and Semtex 600ms, the throwing knife 0 (in hand and ready on the
  press think), the TI's 2630 ignite. holdButtonToThrow is 0 for every one
  (`BG_OffhandHeldToThrow`): in hand and ready is MW2's HOLD, which waits while
  an offhand button is down on this command and the one before
  (`PM_OffhandHeld`: lethal OR tactical, MW2's `0xc000`, the two commands asked
  separately, 578f2d-578f3f) and throws on the first command that is not so. A
  release in the drop or the prepare is queued: nothing is thrown early and
  nothing throws itself. Only offhandHoldIsCancelable takes Y
  (`BG_OffhandCancelable`, read from the action's own weapon: Semtex, throwing
  knife, TI); a frag or flash Y is weapnext's toggle, which waits behind the
  recovery. HOLD entry writes fuseTime into `ps.grenadeTimeLeft` (MW2's
  ps+0x34, predicted and networked); only cookOffHold (the frag) counts it
  down, in `PM_UpdateGrenadeThrow` at the top of `PM_Weapon` after the timers
  and before anything else, in HOLD only, so the frag goes off 3500ms after
  HOLD entry whenever it is let go, and a release on the think it runs out
  loses. Run out in hand it is MW2's grenade suicide: -1, the predicted
  `EV_GRENADE_SUICIDE` whose parm is the frag (577c37: offHandIndex), the frag
  spent, that think over; the server kills the holder as a suicide unless in
  god mode (`G_GrenadeSuicide`), firing the frag the event names through the
  death drop at once, and the next think ends the offhand as a cancel does.
  That next think can be a later pmove step of the same command (pmove_fixed,
  or a hitch past 66ms), before the server reads the event: so the event names
  the frag and MW2's OffHandEnd never writes the fuse (577bf7) - the -1 stays
  until the next offhand's start clears it (OffHandInit, 578e00). Never key
  the suicide's drop on what is in hand after pmove. The throw is the release
  command (MW2's fireDelay is not modelled), so the countdown stops at
  `WA_FIRED`; the server reads what is left and zeroes it at the throw
  (`fire_equipment`'s fuse, MW2's 5e9400). IW4x's bg_weaponOffHandFix zeroes
  it after a HOLD cancel only (0x578F52; the INIT and PREPARE cancels have none
  yet). A ladder grab takes nothing: MW2's ladder drop is a weapon change,
  which waits out every melee and offhand state and the quick raise after one
  (`PM_LadderWeapon` holds `LADDER_HOLSTER` until the think after the latch
  clears), so the
  fuse runs on and no new quick action starts on the ladder. The launch is MW2's
  (4c3690): forward times projectileSpeed, plus projectileSpeedUp and the
  flattened forward's projectileSpeedForward (`BG_OffhandLaunchVelocity`),
  snapped, then the thrower's velocity along it, its component only and
  unsnapped (`G_ThrowOffhand`); like every offhand missile and death drop it
  starts at level.time (408490), never with Q3's `MISSILE_PRESTEP_TIME`, which
  only Quake's own `fire_grenade` and the other Quake missiles keep. Death
  fires what has a fuse with that fuse, from 40 above MW2's feet at up to 160
  per axis (`G_DropOffhand`, first in `player_die`; a TI is planted); in the
  drop, the prepare and after the throw there is nothing to fire. An offhand
  let go on the command whose own events kill its holder before its throw (a
  fatal fall's `EV_FALL_FAR` comes first) still has its fuse, MW2's FIRE before
  fireDelay, and is dropped; player_die's `s.weapon = WP_NONE` leaves the
  throw nothing. Ammo is spent only at the throw and at a cook-out. No cook
  timer on the HUD (MW2 has none).
- **Recovering from equipment is ONE thing, in time and function, and the
  knife ends in the same quick raise.** Frag, flash, a released Semtex, the
  throwing knife and the Tactical Insertion (not MW2's flare fireTime 0.6 /
  fireDelay 0.129) are held `BG_QUICK_ACTION_HOLD` (500ms) and go away in
  `BG_RAPID_DROP_TIME` (50ms); the knife has neither, and goes straight from
  `BG_MELEE_TIME` into the raise (or, the trigger held with the gun's pull
  fired, into the READY gun with no raise at all: "The knife is MW2's"). Either way the gun comes back on its MW2
  quick raise (`BG_WeaponQuickRaiseTime`: 750ms for rifles, shotguns and
  launchers, 250ms for the M300, as the Intervention), a Semtex cancel's
  return included (owner's rule: MW2's MeleeEnd and OffHandEnd). A frag that
  cooks out in a hand that lives is no throw: like a cancel it goes straight
  into the quick raise (MW2's OffHandEnd from PM_UpdateGrenadeThrow). Only
  equipment's return is OFFHAND_END, so with no use hint at all, equipment
  pulled during it comes straight out, while the knife's return still
  quick-drops (the sea lion). `BG_WeaponDrawTime` is the single draw rule,
  used by pmove to stamp and by cgame to fit the raise clip.
  **You cannot sprint through it**: the `weaponAction` latch lasts until the
  gun is fully drawn, and the latch is what blocks sprint. No weapon change
  happens during a knife, a grenade or the gun's quick raise after one (owner's
  rule: MW2's CheckForChangeWeapon refuses every melee and offhand state):
  while latched, `PM_DesiredWeapon` wants the knife or equipment, then the gun
  it left. A Y meanwhile updates the requested slot/HUD at once, the gun the
  action left comes back on its quick raise, and the ordinary holster starts on
  the think after the ready boundary: that think is still MW2's MELEE_END or
  OFFHAND_END, which its state switch ends and its change check refuses
  (0x578BC9-0x578BF2), so step 1 and the ladder hold it (`pml.recoveryEnds`);
  equipment pressed on it comes out. (The knife's quick raise ending under the
  held trigger's lock, below, is READY to that check, which takes it
  (0x578C0A-0x578C47): a ladder grabbed then drops the gun on that think; a
  queued swap never meets the lock, whose command names the gun.) An empty
  hand's return from equipment is MW2's one-think
  OFFHAND_END (`OffHandEnd` with weapon 0, 0x577A1A-0x577A2A), so a gun
  selected meanwhile is drawn two thinks after the hand empties; that one
  think's delay is a weapon delay (`PM_WeaponDelayEnds`), so equipment pressed
  on it comes out with no empty-hand drop (step 2c, MW2's path A), and with no
  use hint, straight out, is in hand and ready on it (MW2's switch runs on the
  OFFHAND_PREPARE its offhand check left: OffHandHold, 0x5792DA -> 0x577920),
  and a knife pressed on it is lost. The knife's own return to an empty hand
  has no such think (Pickup does not own selection, above).
  So YY never cancels a recovery draw, and a Semtex cancel can never reset a
  fire lock for free. A grenade pressed during the draw starts the next one,
  as MW2 lets one come out of the gun's return, but a knife pressed while a
  gun is being drawn back, the draw's last command
  included, is lost (owner's rule: MW2's knife check skips every raise).
- **An ordinary raise is cancellable by YY.** That is the owner's rule, not a
  bug: the NAC skips a holster, YY skips a draw, so long swap times on both
  halves reward technique instead of only slowing play. A quick action's
  recovery raise is not an ordinary raise (see above).
- **Sprint latches and ends the MW2 way (owner's rule).** As PC MW2's
  `PM_UpdateSprint`: a press starts a sprint, the sprint goes on with the
  button let go or held, and a fresh press ends it. `PM_CheckSprint` uses
  MW2's sprint-ending conditions, which also keep one from starting: ladder, a
  quick action, the trigger (the gun fires once it is back up if it is still
  held), the aim button, a reload or use/reload button, the knife and equipment
  buttons (grenade or not), holding jump, forward under
  `BG_SPRINT_FORWARD_MINIMUM` (105, MW2's `player_sprintForwardMinimum`),
  a held crouch or prone bit, deep water (MW2 has no swimming). The stance
  bits end a sprint but do not keep one from starting (MW2's two masks): a
  sprint begun from a crouch or prone needs room to stand and stands the
  player up on its first think with `EV_STANCE_FORCE_STAND`, so the client's
  stance is reset before its next command. `PWF_SPRINT_RELEASE` is set while
  the button stays down from the sprint it started or ended, and only letting
  go clears it: MW2's previous-buttons test and `sprintButtonUpRequired` in one
  bit (a quick action sets it in `PM_UpdateWeaponAction`). So a sprint ended
  under its button waits for it to come up - except after a jump: MW2's
  `Jump_Start` clears the wait (`pml.jumped`), so a held button sprints again
  as soon as jump is let go. MW2 has no ground test: a sprint goes on and can
  start in the air (once jump is let go). Its speed (x1.5) applies only on
  the ground (`PM_WalkScale`, as MW2 scales its walk and not its air move), so
  a sprint begun in the air pays off on landing. Prediction carries
  the flag in `weapFlags`. Never make a sprint need its button held. MW2's
  sprint time: `BG_SPRINT_TIME` (4s) in `ps.sprintUsed`, used while sprinting
  and refilled 1:1 while not; spent, the sprint ends like any other, and a
  start needs more than `BG_SPRINT_MIN_TIME` (1s) left. Peace has no Marathon
  or Extreme Conditioning perk yet. In the air after a jump (`jumpLand` -1)
  MW2's start gate skips the quick-action test: that sprint starts and
  `PM_UpdateWeaponAction` ends it the same think, so its button waits. The
  same end is re-applied after `PM_Weapon` for thinks that never reach it
  (a use-item press, a ladder holster), so no sprint flag survives a think.
- **The camera bob is MW2's.** `BG_ViewBobCycle`, `BG_VerticalViewBob` and
  `BG_HorizontalViewBob` are MW2's view bob (BG_GetPlayerViewOrigin): the eye
  moves on z and along the view's right by an amplitude set by stance, sprint
  and aim, times speed, capped at 8 units. `CG_OffsetFirstPersonView` adds it;
  Quake's angle bob and run tilt are gone (MW2 tilts neither at the hip).
  `PM_Footsteps` turns the cycle at MW2's rate (speed over the posture's most,
  `PM_BobMaxSpeed`, times 0.335 standing, 0.34 ducked, 0.36 backpedalling, less aimed, 0.5
  sprinting, 0.25 prone whatever the aim or direction; prone's amplitude is
  MW2's (0.02, 0.005) with no aim blend) and holds it in the air, so a sprint switching in the air moves
  the camera, as in MW2. Picture only: the muzzle, aim and the gun's own bob
  are unchanged.
- **The FOV is MW2's.** `cg_fov` (default 65, held to 65-80 by `CG_ClampFov`)
  is horizontal at 4:3, widened for the view (Hor+): `CG_ProjectFov` fixes the
  height by tan(fovY/2) = 0.75 tan(fov/2) and lets the width follow the aspect.
  `CG_ViewFov` is MW2's `CG_GetViewFov`: intermission 90, then
  lerp(cg_fov, min(cg_fov, zoom), f), where zoom is the sight file's `zoomfov`
  (MW2's `adsZoomFov`; 50 without a file) and f the one aim blend that also
  moves the gun and the hipfire HUD. The mouse follows tan(fov/2)/tan(32.5).
  Never scale the hip FOV for aim, and never let a sight widen the view.
- **The eye is the playerstate's.** The first- and third-person view origins
  add `ps.viewheight` (a float: MW2's curve passes through fractions),
  whatever pmove put there; nothing in cgame writes 26, 50 or any other eye
  height. Another player's eye is their own `ps.viewheight`, which
  `BG_PlayerStateToEntityState` writes unsnapped into their ET_PLAYER entity's
  `origin2[2]` (owner's decision: their drawn bullets start where the server's
  damage trace did, mid-leg too); never guess it from the legs animation.
- **The player picture fills the box.** The player models are Quake 3
  placeholders authored 56 tall. `CG_ScalePlayerModel` scales the legs by
  `CG_PLAYER_MODEL_SCALE` ((DEFAULT_HEIGHT - MINS_Z)/56, x1.25 for MW2's 70)
  about the feet, and the tag functions carry `nonNormalizedAxes` down the
  chain, so the torso, head, gun, barrel, flash and flag scale with it; the
  shadow, sprites and splash follow. Every live player, corpse and podium
  player goes through it. Prone is the owner's one exception to filling the
  box (MW2's prone body overhangs its 30-tall box too): a live player with
  `EF_PRONE` lies face down along the legs' yaw, which is the body's (MW2's
  `proneDirection` and the ground's pitch, `origin2[0..1]`), the model's eye on
  the prone eye over the origin and the body running back behind it, where
  MW2's prone room check and the prone hit volume put it, scaled about the
  rotated feet (`CG_PlaceProneModel`); a placeholder until there is prone art.
  Picture only: no origin, box, trace or lighting
  origin moves. Code that measures directions on a scaled piece (the flag's
  swing) must normalise its axes first.
- **Jumps and landings are MW2's.** A jump leaves at `BG_JumpVelocity`, MW2's
  sqrt(2 g jump_height) for `BG_JUMP_HEIGHT` 39 (249.8 at gravity 800; Q3's
  270 is gone). The engine rounds velocity to nearest every think, as MW2
  does, so 8ms commands jump about 41: MW2's 125fps jump. Test harnesses snap
  the same way (`rintf`), never with q_shared's truncating `SnapVector`.
  `ps.jumpLand` is MW2's jumping flag and
  landing timer in one (0 none, -1 jumped and not landed, >0 ms of slowdown),
  `ps.jumpWait` its 500ms between jumps, `ps.jumpOriginZ` the take-off height.
  A landing from `BG_HARD_LAND_HEIGHT` (12) or more keeps 0.67 of all the
  velocity (`PM_CrashLand`) and raises the landing event (sound and view dip),
  so every jump's landing does; the first walking think after a jump lands keeps
  0.65 more and starts 1800ms of slowdown (0.5 and 1200ms if it landed 18 units
  above the take-off). During it the ground friction and the squared speed of
  a jump are both scaled by `BG_JumpLandFactor` (2.5 falling to 1). A jump
  inside `BG_JUMP_WAIT` is refused and its press spent, and so is a press made
  in the air (MW2's Jump_Check reads the command before): holding it through
  the landing never jumps. So is one crouched under a ceiling too low to
  stand. A jump ends any running pm_time (Jump_Start), so a landing's timer
  never stops it clipping. A ladder jump-off keeps `BG_LADDER_JUMP_SCALE`
  (0.75) of the take-off speed (Jump_PushOffLadder). Another player is never
  ground (MW2's PM_GroundTrace): no landing on heads, no jumping off them. A
  ladder or ground too steep to walk on clears the state; walking off a
  ledge clears a pending slowdown. A jump that comes down in deep water ends
  there (`PM_WaterMove`), but a slowdown already running runs on through
  water: MW2 has no water mover. Stairs and curbs are not ledges:
  `PM_StepSlideMove` is MW2's whole function. Every ground move is pushed
  down up to `STEPSIZE_DOWN` (9) past its step, so walking down them never
  lands (`EV_STEP_DOWN_4/8` smooth the eye). A step is kept only if it got
  farther along the move than the unstepped slide; a walked step costs speed
  by its height (`STEP_SPEED_KEEP` for a full one); one taken while rising
  turns the rise into speed along the ground (`PM_ProjectVelocity`). In the
  air only a jump steps up, and only to `BG_JUMP_HEIGHT` above `jumpOriginZ`
  (Jump_GetStepHeight, Jump_IsPlayerAboveMax, Jump_ClampVelocity): a fall
  never climbs a lip, and a jump never climbs a ledge it cannot clear. A
  water jump and flight leave the ground before the step move. Peace has no
  mantle. All three are durations or positions, never clocks, so a replay
  rebases none.
  MW2's damaging-landing stun is not ported (Peace keeps Q3's fall damage;
  its doubling reads only the crouch, so a prone landing is not doubled, as
  MW2's landing reads no stance). Lying down starts a landing's slowdown in
  `jumpLand` (MW2's Jump_ActivateSlowdown), unless knockback's timer runs:
  friction 2.5x easing off, and a jump soon after is lower. A jump needs the
  standing stance; from a crouch or prone the press is spent.
- **Air control is MW2's.** `PM_AirMove` uses `PM_AirScale`, MW2's air move
  speed: base speed over forward/right only, slowed by `BG_ADS_SPEED_SCALE`
  (0.4) while aiming (MW2's air scale keeps the walking scale aiming sets) - no jump axis
  and no sprint (Quake's held jump would cut it to 71%). `PM_Accelerate` pushes at least as
  hard as at `pm_stopspeed` except on a ladder, as MW2's accelerate does. So
  holding forward never slows a sprint jump, and pulling back always brakes it
  at base speed per second. Sprint itself never touches the air, but a held
  sprint button cancels an unscoped gun's aim every command, airborne too, so
  a sprint press mid-jump while aimed restores full air control: MW2's "a
  sprint trick throws off your jump". It emerges from the two rules; never add
  it as a case.
- **Holding sprint stops aim, except through a scope.** `PM_CheckADS` refuses
  aim while `BUTTON_SPRINT` is held unless `BG_WeaponScopeOverlay`: MW2's sprint
  key is also its hold-breath key, so a scoped gun aims with it held. This
  needs the latch above; with a hold-to-sprint button it would stop aim after
  every shot or reload until sprint was let go. The sprint's weapon states do
  not refuse aim (MW2's `PM_IsAdsAllowed` never tests them): the aim button ends
  the sprint first and aims that same think, while the sprint-out still holds
  the trigger. As MW2's `CL_KeyMove`, `CL_CmdButtons` sends no sprint bit while
  `+back` is held (a tap then waits for back to be let go), so backpedalling
  with sprint held still aims.
- **Never ADS through a reload.** `PM_CheckADS` blocks `WEAPON_RELOADING` on
  purpose. The ZOOMload trick is a client/server desync — the reload animation
  starts client-side from the use/reload bit and the server never agrees one is
  happening — not a rule being relaxed. Do not reproduce it by loosening the gate.
- **No knife while a scoped gun is aimed (MW2).** `BG_WeaponScopeOverlay`
  (MW2's `adsOverlayReticle`, from `weapon.json`'s `scope_overlay`, which also
  writes the `.sights` scope line) refuses a melee press while `PMF_ADS` is set;
  the press is lost. It is the whole wild scope: through a fake reload the
  client is not aiming, so its knife starts, while the server is scoped and
  refuses it. Do not add a wild-scope rule anywhere else. MW2 tests its
  predicted ADS fraction, so its knife also waits for the scope to ease out;
  Peace has no gameplay ADS fraction, so `PMF_ADS` stands in for it.
- **A button held through respawn is not a new press.** When `PMF_RESPAWNED`
  clears, seed the melee/lethal/tactical held mask from the current command.
  Preserve the other action bits; a release and new press still work normally.
  The loadout suite drives full `Pmove` for ordinary/frozen spawns and an
  attack-held spawn gate, including Semtex.
- **Use/reload is MW2's (owner's rule: do what MW2 does).** `+usereload`
  (`BUTTON_USE_RELOAD`, the gamepad's X) is never a reload press in pmove, which
  reads it only to hold for a floor gun and to end a sprint. After each command's `Pmove`, the
  server's `BG_UpdateActivate` (MW2's `Player_UpdateActivate`, run from
  `ClientThink_real`) clears `PWF_USE_RELOAD` on `ps.weapFlags`, then sets it
  again for a press that finds nothing to use, and on every command while that
  press is held. A press over a floor gun is held for the pickup and reloads
  only if released early; a press at a bomb site or the bomb is a use, and the
  bomb comes first (one use per press, as MW2's use list; `G_UseThink`). A press
  made while sprinting, or while equipment is coming out or going (MW2's
  OFFHAND_INIT..DETONATE), is used up as MW2's `Player_ActivateCmd` does: no
  use, no hold, no reload, and none while held. The gun's return after a throw
  (OFFHAND_END) is not refused, which is where the fake reload after equipment
  is tapped. As in MW2 (owner's rule), X and
  R end a sprint and keep one from starting (`PM_CheckSprint`), and the reload
  is taken straight out of the rise (step 4e; on its last think, step 3b); a
  fresh sprint press still
  cancels the reload. A
  released press holds nothing (CoD4 keeps its hold entity until the next
  press). `PM_Weapon` takes the bit as a reload press and
  clears it — except during a quick action, from the press through the
  recovery draw, where it is not read at all (MW2's reload check skips melee
  and offhand) — and a gun that is reloading when the command begins drops it
  (MW2's rule). Peace's own rule: the trigger may fire on the think a reload
  ends, so a held trigger and reload alternate (MW2's reload's last think fires
  nothing, and a held trigger fires on the think after: one pmove step later,
  which is the next command at any rate up to 66ms commands; a longer command
  is split into steps of at most 66ms, MW2's `Pmove` 0x4CFF4A as Peace's, and
  the step after can be in the same command). The trigger stops a
  segmented reload as MW2's does: asked by a fresh press (in the start only
  past 0.4 of it, `PM_Weapon_CheckForReload` 0x578750-0x5787AD) or by a
  trigger down as a segment ends (0x5783DE-0x5783FC, 0x578564-0x578582), the
  segment it is in plays out with its round, then the end; the request is
  `PWF_RELOAD_INTERRUPT` and the press compares with `WA_ATTACK_HELD`, the last
  step's trigger (MW2's `pm->oldcmd`, copied every step, 0x4CFF61). Each
  segment starts afresh on the think the one before it ended, with its whole
  length to run (`PM_AdvanceReloadSegments`: MW2's crossing drops the
  overshoot, 0x578B71, and `PM_SetReloadingState` assigns the next segment's
  time, 0x575E20), so a segment ends ceil(length / msec) thinks after it
  began. The client never sets the bit; it
  gets it from a snapshot and replays its commands over it, so a tap made as
  the gun comes back can reach the client's return after the server has already
  dropped it: the fake reload. A tap just before a shot is taken by the server
  first and reaches the client late: the fake bullet. `+reload` is the
  weapon's own button, predicted exactly, with no such tricks — as in MW2,
  where the scripts do every trick with `+usereload`. A reload request beats the
  trigger on the same command, as MW2 checks reload before it fires. Never
  predict the bit on the client, never hold the gun back after a quick action,
  and never name a trick. How wide the window is comes only from the client's
  clock and the host's frame, both MW2's. The clock (`CL_DriftTimeDelta`,
  0x5a1ad0) sits one snapshot interval plus 16ms behind the newest snapshot,
  and prediction replays from the time-selected `cg.nextSnap`. The host reads
  its own server's snapshot a frame after the server sent it (below). So on
  your own host (sv_fps 20, MW2's fixed frame, 125 fps) a release up to 16ms
  before the gun is up always fakes and up to 64ms sometimes does, and a
  trigger pull up to 24ms after X (three commands at 125 fps, two at 85) is
  always a fake bullet and up to ~75ms sometimes is. Never move that clock
  toward the newest snapshot. `dev/FAKE_RELOAD.md` has the measured windows.
- **A host reads its own snapshots a frame late, as MW2's does (owner's rule:
  do what MW2 does).** MW2 runs its server in a thread of its own: `SV_Frame`
  (0x627610) only signals it (0x627695) and returns, and the thread's game
  frame and snapshots (0x415cd0, 0x4517b0) come after the main thread's second
  event loop (0x47dfc6), so the snapshot is read in the next frame (0x47dd80).
  Peace's `Com_Frame` holds what `SV_Frame` sends the local client
  (`NET_HoldLoopPackets`) and releases it before the next frame's first event
  loop. Nothing else waits: commands, the server's answers to the client's own
  packets, and remote clients keep ioquake3's timing. Over every snapshot
  phase it moves the own-host fake reload and fake bullet exactly one frame
  later (the suite checks the fake bullet gap by gap); the wild scope rides
  on the fake reload. Never read the host's snapshot in the frame that made
  it, and never touch Pmove, the use/reload bit, the clock or the picture rule
  for these windows. `run-fakereload.sh` runs every own-host case through the
  real `Com_Frame` and loopback and checks it against the harness's model.
- **Snapshots come at MW2's rate, not the client's (owner's rule: do what MW2
  does).** MW2's `SV_UserinfoChanged` parses `snaps` and replaces it
  (0x401a8a): a snapshot every 100ms while `onlinegame` is set (default 1;
  IW4x's dedicated server sets it too), else 50ms, and
  `SV_SendMessageToClient` sends a loopback or LAN client one every server
  frame regardless (0x48ff84, 0x490033). Peace's server does the same
  (`sv_onlineGame`, default 1; `SV_ClientSnapshotMsec`,
  `SV_SnapshotEveryFrame`, `SV_SnapshotDue`; a bot or the dummy, sent
  nothing, keeps every frame); `snaps` has no say. Online,
  half the snapshots make the base older and every window wider, which is
  most of why MW2 felt easier there, though X must now be held across one of
  them to be seen at all; your own host and LAN are unchanged.
  `sv_lanForceRate 0` treats a LAN client (127.0.0.1 included) as an internet
  one. Never honour `snaps` again or move this into Pmove, the use/reload bit
  or the clocks; `replay_test.c` checks the rule and the fake reload harness
  models it for every ping row (`dev/FAKE_RELOAD.md`).
- **The gun never un-plays an action it showed (owner's rule).**
  `cg_weappicture.c` draws the viewmodel from its own run of the client's
  commands through the real `Pmove` once prediction is corrected away from a
  reload, shot or pump of the held gun that you were shown. That run only
  finishes what you saw: it withholds whatever in your commands would begin
  something new, never detaches into an action it began itself, and follows
  prediction again when it comes to rest or prediction starts a real action. A
  correction that only removes a swap is followed at once, so a lone knife is
  never a fake knife. The harness checks that the picture never draws a reload
  or knife prediction never had, that a knife the server refuses never cuts a
  fake reload short, that a run which would begin something by itself
  hands back to prediction, and that a shot the server never fired is drawn
  through its READY think and its whole pump or bolt, one fire clip, before
  the picture comes to rest (the shot's READY think, `BG_ShotAwaitsAction`,
  is neither rest nor a new action).
  Aim, camera and HUD always read the predicted state. Run
  `dev/tests/run-fakereload.sh`; `dev/FAKE_RELOAD.md` has the measured windows.
- **The game pad moves and aims the MW2 way (owner's rule: do what MW2 does).**
  `code/client/cl_gamepadaim.c` is MW2's console pad code as IW4x ported it
  (iw4x-client e49e17d6, Gamepad.cpp; the stick mapping is iw4x-rawfiles'
  thumbstick_default.cfg), as pure functions. `CL_StickDeadzone`
  is GPad_ConvertStickToFloat: radial per stick (`gpad_stick_deadzone_min`
  0.2 / `_max` 0.01), applied in `IN_GamepadMove`; the triggers and older
  joysticks keep `joy_threshold` (0.13, IW4x's `gpad_button_deadzone`).
  `CL_StickMove` is CL_GamepadMove's move with the move stick MAP_SQUARED:
  each axis times the stick's length (half a stick, a quarter of the speed),
  stretched from the circle onto the move square by sqrt(1 + (minor/major)^2)
  and floored, so a full diagonal is the corner and a sprint needs forward
  over `BG_SPRINT_FORWARD_MINIMUM` (about 91% of the stick past the
  deadzone). It moves while a menu is up; only the look waits.
  `CL_AimTurn` is AimAssist_CalcAdjustedAxis + AimAssist_ApplyTurnRates: the
  aim graph over the stick's length, the dominant-axis scale, then
  `aim_turnrate_*` (MW2's exe dvars, 260/90 hip, 90/55 ADS) lerped by the aim
  fraction, times `input_viewSensitivity` (IW4x's console presets: 4.0 is
  "Insane", 10), reached at `aim_accel_turnrate_lerp` * sensitivity deg/s^2
  and left at once; the speed is the stick's size, so a flip keeps it. cgame
  sends `cg.adsFrac` (the aim clock's linear base) as `trap_SetUserCmdValue`'s
  third argument: picture state that only the client's look input reads,
  never gameplay. No pad look while anything catches keys
  (CG_ShouldUpdateViewAngles). Not ported: aim assist slowdown;
  `fovTurnRateScale` (aaGlob +288, set by MW2's per-frame aim assist; IW4x
  only reads it) is 1; MW2's graph 3 knots (aaInputGraph 0x7A2FC0) are unread,
  so `aim_input_graph` is empty and the look is linear in the stick until they
  are set there with `aim_input_graph_scale` (GraphFloat's own layout).
  `dev/tools/mw2_aimdump.ps1`, run beside a live IW4x match, reads both out of
  MW2 itself: the graphs, the live aim globals, the code of
  AimAssist_UpdateTweakables/UpdateAdsLerp with every global it names, and
  every instruction naming fovTurnRateScale, fovScaleInv, adsLerp,
  fWeaponPosFrac or the graphs; `dev/tools/mw2_aimdump_disasm.sh` disassembles
  its report. Never
  invent a graph, a FOV scale or a second turn-rate rule. `j_mw2Aim 0` and
  `j_mw2Move 0` hand the look or the move back to Quake's linear `j_*`
  values; with them on, only the signs of `j_yaw`, `j_pitch`, `j_forward`
  and `j_side` count.
- **Events are MW2's rings (owner's rule: do what MW2 does).** The playerstate
  holds MW2's four predictable events (`MAX_PS_EVENTS`); every entity carries
  MW2's own ring of four (`eventSequence`, 11 bits, `events[]`,
  `eventParms[]`). After each command and at each frame's end
  `BG_PlayerStateToEntityState` copies the player's new events onto its entity
  (MW2's 56eab0), skipping MW2's serverOnly and singleClient lists
  (`BG_EventReachesOthers`); `G_AddEvent` puts any entity's on its own ring,
  and a client's there too beside its external event. There is no temp entity
  per event: Q3's SendPendingPredictableEvents is gone (MW2 has none, and the
  ring would play each twice). `CG_CheckEvents` plays every event since the
  last snapshot, up to four, oldest first, each with its own parm. Each slot
  is emptied `EVENT_VALID_MSEC` after it was filled (`G_AgeEntityEvents`, Q3's
  rule per slot), so an entity seen again after that long plays what its ring
  still holds and one seen just before plays only what is new. A player's
  entity is MW2's exception (`CG_ResetEntity`'s ET_PLAYER case, 5951f4):
  whenever it is reset (back in view, teleported, or back after it was
  followed) its ring counts as played, so a follow switch never replays the
  shots its playerstate already played. The fire event names the gun that
  fired (`EV_FIRE_WEAPON`'s parm): the ring can still hold it after an
  offhand has taken the hand, where MW2's `es.weapon` stays the gun. cgame
  takes the whole shot from that parm, never from `es.weapon`: the sound, the
  bullets, and the third-person impulse flash with its light and muzzle tag
  (`muzzleFlashGun`). A fire with no flash model (a throw) leaves the gun's
  flash alone. The offhand press raises MW2's events in MW2's order:
  CheckForOffHand's `EV_SWITCH_OFFHAND`, OffHandInit's
  `EV_WEAPON_SWITCH_STARTED` and `EV_RESET_ADS` unless the offhand comes
  straight out (the sea lion's direct
  prepare, 5791ce), and OffHandPrepare's `EV_PREP_OFFHAND` when its pull
  begins (`PM_FinishWeaponChange`), the throwing knife's on the press think.
  So the knife's press is four events, every other offhand's three and later
  its pullback, straight out two; the offhand's drop raises no
  `EV_CHANGE_WEAPON` (OffHandInit has none). Each has MW2's function: the
  HUD's equipped offhand (whatever player is viewed; when the viewed player
  or their life changes, outside a demo, MW2's snapshot transition takes it
  from the playerstate and drops the view kick, `CG_ViewedPlayerChanged`),
  the server's switch-start notice (a
  `WeaponSwitchStarted:` log line, MW2's script notify), leaving a toggled aim
  (`leaveads`, client input only, the player's own and never in a killcam or
  demo; the client has MW2's `toggleads` and `leaveads` on `cl.usingAds`),
  and the offhand's pullback from its data (MW2's names, under
  `sound/weapons/offhand/`, played only if the file is there). A killcam that
  loses the shot of a shot and a knife press in one recorded frame emerges
  from the four slots: never test for it in game code, never special-case it.
  A new event is appended to `entity_event_t` and `eventnames`, and handled in
  `CG_EntityEvent` (its default is an error).
- **The silent shot emerges; nothing names it (owner's rule).** Shoot, then
  press the throwing knife: the shooter gets the shot (flash, sound, impacts,
  kick), live opponents get it with the gun that fired, the victim dies, and
  the round's killcam shows no shot at all, only the kicked view, exactly when
  the shot and the press ran in one recorded server frame (under 50ms apart
  with a packet a frame, under 80ms with `cl_maxpackets 30`). It is the four
  pieces above working as MW2's: the press's four events overwrite the shot's
  slot in the one archived playerstate a frame, the bullet is drawn from that
  event alone, the kick is in the recorded aim and the knife's centre speed
  of 0 leaves it at the clamp, and the entity ring keeps it for everyone
  live. Never add a flag, branch, event, state or test hook for it, and never
  "fix" it in one piece. `dev/tests/run-silentshot.sh` drives it end to end;
  `dev/SILENT_SHOT.md` has the sources and the measured windows.

## Capacity limits

- `pm_flags` — **full.** All 19 networked bits used: protocol 88 widened the
  field from 16 for MW2's `PMF_PRONE` (bit 16) and `PMF_PRONEMOVE_OVERRIDDEN`
  (bit 17), protocol 91 to 19 for MW2's `PMF_FROZEN` (bit 18). A 20th needs
  `msg.c` widened again (MW2's is 23 bits).
- `STAT_WEAPONS` — full 32-bit wire mask at `PS_STAT_WEAPONS`. `MAX_WEAPONS`
  is 24; the M300 is 15, the throwing knife 16 and the Tactical Insertion 17
  in baseq3 (18, 19 and 20 in missionpack). M16/Masterkey append 18/19
  (21/22 in missionpack; `WP_NUM_WEAPONS` 20 / 23). The
  networked `ps.weapon` is 5 bits, so no weapon may pass 31. Other
  stats remain signed int16: their bit 15 must stay unused.
- `weaponAnimSeq` — **full.** 2 bits in `msg.c`; all four values are taken
  (`RSEQ_START/LOOP/END` + `ASEQ_FIRE`). A fifth animation segment has to widen
  the field.
- `weaponstate` — 4 bits, 10 of 16 values used, including `WEAPON_LADDER`.
- `weaponAction` — a separate 11-bit predicted/networked field: action type (2),
  fired latch (1), previous action buttons (3), cancel return acknowledgement,
  swap edge latch, post-throw recovery, `WA_LEFT_EMPTY`, and `WA_ATTACK_HELD`
  (the last step's trigger, MW2's `pm->oldcmd`, for a reload's fresh press;
  written every step and kept through a freeze). Protocol 92 (GAME_VERSION
  -21) requires client, server and all modules rebuilt together. Older
  builds/demos are incompatible.
- `weaponShotCount` — MW2's `weapState[hand].weaponShotCount` (ps+0x200, its
  netfield's 3 bits, 0x742600): the shots of this pull of the trigger, 0 to
  `BG_SHOT_COUNT_MAX` (4). Predicted, never rebased. Protocol 92.
- `weaponDraw` — 2 bits, three of four values used (`WDRAW_NORMAL`,
  `WDRAW_FIRST`, `WDRAW_ALT`): the kind of draw, and for a mode change the kind
  of drop before it (MW2's RAISING_ALTSWITCH and DROPPING_ALT states).
- Slot caches/attachments/flags, first draw, and pickup target/serial/hold are
  explicit playerstate net fields, not extra `stats[]` or `pm_flags` bits.
  `weapFlags` (MW2's name; 3 bits since protocol 92) carries `PWF_USE_RELOAD`,
  the server's use/reload request, `PWF_SPRINT_RELEASE` and
  `PWF_RELOAD_INTERRUPT` (MW2's RELOAD_START_INTERUPT and RELOADING_INTERUPT,
  set only while reloading). None is a clock, and none is rebased. Protocol 80 adds `jumpLand` (signed 12 bits),
  `jumpWait` (10), `jumpOriginZ` (float) and `sprintUsed` (14): durations and a
  height, none rebased. Protocol 81 adds `cursorHintEntIndex`
  (`GENTITYNUM_BITS`; an entity number, never rebased) and widens
  `slotFlags` to 3 bits for `BG_GUN_GENERATION`, a slot's bit that never
  travels with a dropped instance. Protocol 83 adds `cursorHint`
  (`cursorHint_t`, MW2's ps+0x194): the hint's type, rewritten by every
  end-frame pass where the index is sticky. Server only, never rebased; pmove
  does not read it; cgame's use prompt draws from it. It replaced the
  gclient-only boolean of the same name. Protocol 84 (GAME_VERSION -13) widens
  `PS_CURSOR_HINT_BITS` to 3 for the Tactical Insertion's two types (own:
  pick up; enemy's: smash): six of eight values used, guarded by the static
  assert in `g_items.c`. A seventh and eighth fit; a ninth needs 4 bits.
  Protocol 85 (GAME_VERSION -14) adds `grenadeTimeLeft`, MW2's playerstate
  netfield (0x742550, signed 16 bits): the fuse of the offhand in hand, -1
  after a cook-out. Every offhand's fuseTime fits (the throwing knife's 30000
  the longest). A duration, never rebased by the replay. `EV_GRENADE_SUICIDE`
  (MW2's 0x5d) is appended to `entity_event_t`.
  Protocol 87 (GAME_VERSION -16) adds `moveClass` (`PS_MOVECLASS_BITS` 4,
  beside `perks`): MW2's class speed (`moveClass_t`, nine classes, the riot
  shield's included, guarded by the static assert in `bg_loadout.c`). Spawn
  owned and predicted; not a clock, so the replay never rebases it.
  Protocol 88 (GAME_VERSION -17) is MW2's stance and prone. `viewheight` is a
  float (MW2's eye curve passes through fractions; same offset and size).
  Beside it: `viewHeightTarget` (8, `BG_EYE_*` in MW2 units above the feet,
  0 = never set), `viewHeightLerpTime` (`PS_EYE_LEG_BITS` 10: ms into the
  running leg, always below `BG_PRONE_LERP_MS`), `viewHeightLerpTarget` (8,
  0 = no leg), `viewHeightLerpDown` (1), `proneDirection`,
  `proneDirectionPitch` and `proneTorsoPitch` (full floats, MW2 ps+0x160..)
  and `prevCmd` (17: MW2's oldcmd, `BG_PrevCmdPack`, written per command,
  never per step). None is a clock: the replay rebases none of them. The
  usercmd carries the stance in bits 22-24 (`BUTTON_PRONE`, `BUTTON_CROUCH`,
  `BUTTON_TEMP_STANCE`; 25-31 spare). An ET_PLAYER entity carries MW2's
  `EF_CROUCHING`/`EF_PRONE` (bits 20, 21: the entity's `eFlags` is 22 bits,
  which also lets `EF_TEAMVOTED`, bit 19, through), the eye unsnapped in
  `origin2[2]` and, while prone, the body's yaw and pitch in `origin2[0..1]`
  (`BG_PlayerStateToEntityState`); never in `playerState_t.eFlags`, whose 16
  bits would drop them. The legs anims gain `LEGS_IDLEPR`, `LEGS_CRAWL`,
  `LEGS_CRAWLB` (40 in all; legsAnim stays 8 bits with the toggle at 128).
  The stance events can come every think (`EV_STANCE_INVALID` while a prone
  turn stays blocked, `EV_STANCE_FORCE_STAND` on a ladder with a crouch or
  prone bit, any FORCE event until the client's stance is reset), and they
  share MW2's four predictable slots: a command Pmove chops into five or more
  steps (pmove_fixed with a small pmove_msec, or a long command) can push its
  shot out of the last four that `ClientEvents` reads, as MW2's ring would.
  Never special-case it; `stance_test.c` prints the cases (with 66ms chops
  the shot always stays).
  Dropped ET_ITEM uses `generic1` for attachments, `time2` for packed magazine
  and reserve, `otherEntityNum2` for chamber debt, and `time` as a spawn serial.
- cgame imports are appended after `CG_KEY_GETBINDINGNAME`:
  `CG_SETUSERCMDKICKANGLES` (115, QVM stub -116 in `cg_syscalls.asm`) passes
  the view kick as three floats by value, which the engine reads as
  `VMF(1..3)`. Keep the trap's parameters scalar: a QVM call has no C
  wrapper, so a `vec3_t` parameter would push one word, the pointer, and the
  engine would read garbage. `run-kick.sh` checks it through a q3lcc-built
  QVM. The import numbers above it never change. A new
  cgame on an older client calls an import that client does not have, which
  is one more reason protocol 86 needs the client rebuilt with the modules.
- The pose traps (`CG_R_BUILDMODELPOSE`, `CG_R_BLENDMODELPOSES`) and
  `refEntity_t` with its pose (`cl_vm.c`) take their QVM pointers through
  `VM_ExplicitArgBlock`, which drops the game when the block runs past the
  VM's memory, so a QVM cgame passes `cg.weaponPose` to either renderer as
  native cgame does. The other traps still use `VMA` (`VM_ArgPtr`), which masks
  only the start; give a new trap that hands the engine a whole block
  `VM_ExplicitArgBlock`. Each renderer skins from a pose only when its joint
  count is the model's, and `RE_BlendModelPoses` never writes past
  `MAX_MODEL_JOINTS`.
- Event rings: **protocol 86, GAME_VERSION -15** (one bump with the view
  kick's import and client-side bullets). The playerstate's
  `events[2..3]` and `eventParms[2..3]` (8 bits) sit beside `[0..1]`; the
  entity's `eventSequence` (11 bits), `events[0..3]` and `eventParms[0..3]`
  (8 bits) take Q3's `event`'s place in the netfield order, and `event` moved
  to the end: it is the cgame's working copy, which the server never sets.
  `entityState_t` grew by nine ints, so `sharedEntity_t`/`gentity_t` offsets
  moved: the engine, the server and every native and QVM module are rebuilt
  together, and older demos do not play. Event numbers are 8 bits on both
  rings, so `entity_event_t` stays below 256 (it ends at `EV_STANCE_INVALID`,
  95, well under; `bg_misc.c` asserts it and one name per event); parms are 8
  bits as before (a weapon, a direction byte, a sound index). Client-side bullets add no field: the server simply stops sending
  hitscan impact, tracer, blast and beam temp entities, so a client older
  than protocol 86 would draw none.
- `perks` is a five-bit spawn-owned playerstate field. All three class perk
  slots share the roster in `bg_loadout.c`; duplicate nonzero IDs are invalid.
  Quick Reload retimes authored notes, segment endpoints and presentation via
  `BG_ReloadTime`/`BG_ReloadSegmentTime`, never by changing clock progression or
  note/holster/fill ordering. Fire/bolt, swap and pickup timings stay unchanged.
  Inaudible filters shared footstep events only; falling damage events still
  reach the server, which separately applies 9 Lives (no fall damage).
  Lightweight is MW2's: x1.07 on the class speed (`BG_MoveClassScale`, the
  catalog's `movementPercent` 107), which only the ground walk (and its wading
  clamp) reads; never the air, a ladder, swimming, the jump or the bob, and
  never by mutating `ps.speed`, jump impulse or timers.
  Second Nature raises the per-gun class attachment limit to two. Only Red dot
  is implemented so far; the capacity rule must not invent other attachments.
  Pickup instances keep their own attachments regardless of the looter's perks.
  Class format 2 stores attachment masks; format 1 retains `cg_sight` fallback.
  Run `dev/tests/run-perks.sh` and the loadout suite after perk changes.
- `stats[]` — **15 of 16 used in baseq3; all 16 in missionpack.** Every entry round-trips as a SIGNED
  short (`MSG_WriteShort`) except `STAT_WEAPONS` and `STAT_UNCHAMBERED`.
  Both weapon masks are full-width so guns at IDs 15+ keep chamber debt through
  prediction corrections. Other bitmask stats still have the bit-15 trap.
- `STAT_LADDER` packs attachment, holster destination, jump separation latch,
  and 12-bit inward-facing yaw into bits 0–14. No bit 15, no extra pm_flags.
  Ladder contact stamps `BG_LADDER_DROP_TIME` before the unchanged note pass;
  its destination completes before queued ammo fill, just like a normal holster.
  Like any weapon change it waits for a latched quick action (MW2's
  CheckForChangeWeapon refuses the melee and offhand states). Never add a
  reload/NAC-specific ladder branch. See `dev/LADDERS.md`.

## Weapon animation assets

- Source SMDs in `assets/source/<model>/`; built IQM + clip table in
  `assets/baseq3/models/weapons2/<model>/`. CMake copies `assets/baseq3` into
  the build's `baseq3` on every build.
- `dev/tools/import_spas.py` runs under `blender --background --python` and
  builds the IQM. **`SOURCE_FPS` is 60**, inferred, not stated by the pack: 38
  fire frames are MW2's ~1.7 shots/sec and 26 draw frames its snappy draw only
  at 60. It will not overwrite a hand-edited `view.cfg`.
- Clip lengths and the gameplay timers in `bg_pmove.c` are the SAME numbers.
  When they disagree the clip is time-warped to fit and reads as jittery.
- Material strings are baked into the IQM (`models/weapons2/spas12/...`), so
  moving an asset folder breaks its textures until re-export.
- SPAS normal holster is 829ms and draw is 1075ms, matching the dumped MW2
  multiplayer SPAS weapon's `dropTime`/`raiseTime`. `view.cfg`, the IQM and the
  importer (`CLIP_SECONDS`) carry those same lengths. Its custom first draw
  still includes the existing pump/check, now 1708ms (draw + 633ms fire cycle).
  Ladder holster stays the universal 100ms. Transition-note ordering and YY
  cancellation are unchanged.
- Export IQM triangle indices clockwise for Quake's GL_FRONT culling, while
  retaining outward normals. Blender keeps CCW indices. Reusing Blender order
  in the game renders inside surfaces and hides the front post during ADS.
- `peace_spas.shader` maps the gun material explicitly to `gun_skin.jpg`, an
  original generated UV skin. The explicit map avoids an older loose `gun.tga`
  shadowing it after a build. Re-exporting must preserve authored textures.
- SPAS sprint has separate non-looping entry (200ms), looping carry (600ms),
  and non-looping exit (250ms). `dev/tools/build_spas_sprint.py` bakes the
  original motion from `assets/source/spas12/sprint.json` without Blender;
  the full importer invokes it too. Sprint-in/out follow predicted transition
  locks for presentation only. Sprint/holster retention also covers draw,
  drop, initial pullout and fire/bolt clips through the generic layer owner.
  Both renderers, native and QVM alike, blend skeletal poses; the frame-only
  crossfade of the released source is the fallback for a model without poses. The full authored clip coverage currently belongs to SPAS;
  legacy placeholder weapons still need their own animation assets.
- Known SPAS gaps: the drop is the draw reversed (resampled with ease-out
  progress `f*(2-f)` so a quick YY moves at once), and hands still use a flat placeholder.
  The source mesh references missing `mw2_spas12.bmp`/`v_hands.bmp` atlases;
  other bundled BMPs do not match this model's UV layout. See `dev/SPAS12.md`.
- ADS reset tracks the player `EF_TELEPORT_BIT` edge, not `thisFrameTeleport`:
  prediction owns that flag and never clears it during interpolation/demo play.
  Repeatedly using it would pin the ADS blend at zero.

## Tests

`dev/tests/run-shotgun-reload.cmd` builds and runs `shotgun_reload_test.c`,
which `#include`s `bg_pmove.c` and drives `PM_Weapon` directly — reload
segments, ammo conservation, swap/sprint ordering and the fire cycle, plus
shoot-reload resets (instant reload, fastest NAC, Fayde, YY too early) on every
gun with a worked action, the close note's chamber commit (early, exact and
late cancels by swap and ladder, at every think length), MW2's shot ending READY
on its last think and the action starting from READY at the open note on the
next (`TestPumpIsItsOwnState`; frozen, the shot stays FIRING until the freeze
lifts, and a trigger down as it lifts fires it again with its action owed, as
MW2's, `TestFrozenShotEnd`), a sprint begun in the
shot, on its last think or on the READY think after it lowering the gun on that
READY think with the whole action still owed, and one begun in the action or on
any think around the close note waiting for the whole cycle (`PM_Weapon` alone and
full `Pmove` with a real sprint press, SPAS and M300, at every think length) and the aim a
NAC carries onto the incoming gun through full `Pmove`. Every
timing in it is read from the shipped tables; never write a literal there, or
retiming a weapon breaks the test without telling you anything. Its reload
tests pin MW2's trigger against a segmented reload (a press in the start's
first 0.4 forgotten, the segment the trigger stops always played out with its
round), and `TestPumpCannotBeShortCircuited` MW2's held trigger (no second
shot until a release). `dev/tests/trigger_test.c` (in `run-masterkey`) drives
the held trigger through the real `Pmove` and the client's selection at 8, 16
and 33ms commands: the lock on the SPAS, the M300 and the Masterkey (the
action from the shot's last command, no shot while held, the release), full
auto, what releases the lock (YY, a grenade's return, a swap with the count
at its limit), the shot count's cap at 4 through YY within one pull (each
count through the real codec), the last round held (the reload out of the
shot, the dry fire with nowhere to go), the knife's end (from an empty hand,
nothing to lock), and the reload's stop.

`dev/tests/run-masterkey.sh` (and `.cmd`) also builds `state_end_test.c`:
what the think a weapon state ends does, MW2's `ProcessHand` (0x579450),
through the real `Pmove` at 8, 16 and 33ms commands for the SPAS, the M300 and
the Machinegun. The sprint-out's last think readies the gun, with no time
over, and is over (the
shot, an owed action and an empty magazine's reload come on the next; a reload
press is taken on it); a sprint on again in the sprint-out waits for its end; a
swap queued behind the knife's or equipment's return, or an empty hand's
after equipment, begins the think after it ends, while equipment pressed then comes out; YY
while sprinting is READY on its think, working an owed action first; a gun
with no fire clip (the Machinegun, the M16 rifle) reloads the think after its
last round's lock with the
trigger up; equipment pressed on the knife's hit, or on the think an empty
hand's return from equipment ends, is prepared on that think (with no use
hint, out of the empty hand, it is in hand and ready on it; a think later its
prepare plays), and a knife pressed there is lost; the knife's own return to
an empty hand has no such think (a knife, a gun asked for and equipment are
all taken on the think after it);
a knife pressed on a reload's round or settle is lost while the round seats
or the pump is paid, and one pressed on the SPAS's or M300's reload end's
close note is taken (MW2's RELOAD_END has no delay). The held trigger at
those ends (`TestHeldTriggerAtTheEnds`): with the SPAS's or M300's pull spent
(a frag thrown out of the lock, the trigger let go after the return) the
sprint-out stays lowered under the lock while the trigger is down and ends
READY when it is let go; YY with the trigger pressed on its think fires there,
or works an owed action first; a frag on the knife's hit with the trigger held
is prepared there and the gun's unlocked return fires on the next think; and
after a swap on a reload's settle the other gun's raise, the reload having
begun a new pull, ends unlocked. Only buttons are pressed, and every time is
read from the tables or from the lock pmove stamped. `ladder_test.c`'s
`TestHeldTriggerGrab` grabs a ladder during the knife's quick raise: ended
under the held trigger's lock it is READY, and the ladder drop starts on that
think; let go, on the think after. Grabbed during the knife itself, its own
end under the lock is READY and the drop, on the knife's last think.
`TestHeldTriggerHolster` pins Peace's ladder holster, which takes no lock (a
departure waiting for the owner: MW2's drop hangs under a held trigger).

`dev/tests/run-loadouts.cmd` (or `.sh`) checks class validation/persistence,
modulo selection, same-update YY firing, reload/pull-out cancellation, tap/hold
actions, the sprint latch and its ending conditions, aim with sprint held, MW2
air control (stick-only push, aim scale, stopspeed floor, ladder exempt), MW2's
walk through the real Pmove against MW2's own arithmetic at 8ms (run, rifle
class, sprint, strafe, backpedal, crouch and aim: steady speed, 90%/99% times,
a second's distance, the stops), every class's run, sprint, crouch, aim and
half stick (the secondary and a pickup keeping the spawn class), the sprint
strafe (a 132ms command scales once), the pitch clamp, the 70/50 body (a 70
gap stands, 69 does not), all ten action bits through the actual network codec, and pose release. It
also builds `knife_missile_test.c` (the throwing knife's real launch, impact,
carry and rest code), `knife_corpse_test.c` (the real `CopyToBodyQue`,
`BodySink`, `G_FreeEntity` and use code with a carried knife) and
`missile_draw_test.c` (the real `CG_Missile` and snapshot lerp, a planted
Tactical Insertion's rest pose and glow included). `TestTacticalInsertion`
drives the TI's quick drop, 2630ms ignite, queued tap, hold, Y cancels,
recovery and sea lion chain through the real `PM_Weapon` in both numberings;
`TestHoldThrowsBeforeCancel` checks MW2's HOLD for every offhand: a tap queued
and thrown on HOLD's first command, a re-press held through the prepare, the
throw before Y unless an offhand button (either one) is held across both
commands, and the cancel allowed (Semtex, throwing knife, TI) or refused (frag,
flash, whose Y waits behind the recovery). `TestFragCook` drives the frag's
cook: the fuse at the release, the cook-out at every think length and on every
step of a several-step command (with and without a gun), the event's frag, a
hand that lives keeping the -1 until the next offhand, a death, and a ladder
grabbed while cooking (the fuse runs on, the throw or cook-out, then the ladder
drop on the think after the latch would clear without one). `offhand_server_test.c` (sh only) runs the real `ClientThink`,
`ClientEvents`, `FireWeapon`, missiles and `player_die`: every launch velocity,
start time and fuse, the cook-out's suicide and explosion (also on commands
the real `Pmove` chops, pmove_fixed or not, gun or empty hand), god mode and a
kill in its raise, the death drop of every class, and a release on the
command a fall kills its holder.
Pose checks cover source retention, ladder holsters, loop/endpoint semantics,
reload/action exclusions, ordinary sprint reversal and frame-only crossfades,
plus YY flow (reversed draw, eased return, fire cut-in) and a sprint over the
shot (lowered on the READY think after it, the fire clip still on screen
across that think, the whole action played from its open note once back up)
or the action (fire clip to its end, entry from rest after it), and the one
fire clip played across the shot's READY think with no release eased over it
(`TestShotPictureAcrossReady`), through the real `PM_Weapon`, which is why the
pose test links
`pickup_pmove_driver.c`.
The glide return is compared frame by frame with the ordinary sprint exit
from the same posture (carry, loop, exit under way, partial entry), in both
rendering paths, for SPAS and M300.
`stance_shared_test.c` checks the shared stance definitions and the hit
volumes' contract and geometry (`BG_PlayerHitVolumes`, `BG_HitVolumeTrace`);
`offhand_server_test.c` also checks a prone death drop and a prone throw.
`event_ring_test.c` (with `event_ring_wire.c`) runs MW2's event rings end to
end: the real Pmove and `BG_PlayerStateToEntityState`, the real codec, and
the cgame's real snapshot transition, `CG_CheckEvents`, `CG_FireWeapon`,
`CG_PrepOffhand` and `CG_CheckPlayerstateEvents` - an opponent's shot named
by its gun after a knife pull, the press's routing, up to four a snapshot
across the 11-bit wrap, an entity seen again (a player's ring as played), the
player's own view, and the
killcam losing the shot only when shot and press share a recorded frame.
`offhand_server_test.c`'s `TestEventRing` checks every offhand's press events
and parms through the real `ClientThink`, the entity's ring, the switch-start
log line, no per-event temp entity, `G_AddEvent` and the ring's aging.
The native/QVM pointer-boundary test links `cl_vm.c` and `vm_ptr.c` directly;
keep it independent of the full client and VM lifecycle so MSVC can link it.
It also checks that an entity or pose running past the VM's memory is refused
with ERR_DROP (`VM_ExplicitArgBlock`) before the renderer sees it.
Loadouts UI and class definitions are documented in `dev/LOADOUTS.md`.

`dev/tests/run-stance.sh` (`STANCE_TEST_FLAGS=-DMISSIONPACK`; `.cmd` for the
pmove half) runs MW2's stance and prone. `stance_test.c` drives the real
`Pmove` against brushes traced as the engine traces a brush (floors, walls,
ceilings, steps, slopes, water, ladders, bodies), spawning through
`BG_SpawnStance`, every think counted from the tables and constants (a leg of
L ms started on think t0 ends on t0 + ceil(L / msec)) at 8, 16 and 33ms: the
eye on every think of all four legs, both prone transitions, a request mid-leg,
the reversal, the dead eye, the stance scale and crawl speeds against MW2's
arithmetic, a 132ms command against two 66ms ones, the bob, every room
refusal and its event, `BG_CheckProne` path by path (the two walkables apart,
the bend), steep ground and ledges, the crawl into a wall, the prone step, the
turn, the blocked turn and its event every think, the 0.98 undo, the pitch,
the crawl against every weapon action and aim, the slowdown, sprint, jump,
ladder, talk and respawn, the over-the-box windows with the hit volume that
covers them, the ring budget and the turn's cost (both printed), and water.
`stance_server_test.c` runs the real `ClientThink`, `G_ShotTrace`, railgun,
knife and `SetClientViewAngle`: the prone legs and the leg over the box are
hit, a shot past every volume misses, the railgun and the knife keep the box,
two players prone in a file (the shooter's muzzle inside the other's body:
over them and turned away, a miss; into their box, a hit, through the real
`Bullet_Fire`), and the server agrees with `BG_PlayerHitVolumes` on rays
through every volume, every frame the over-box lasts on a crouch-to-prone leg
reached by its state and as many after (each volume must be hit; every wait
is for a state, never a written time). Test harnesses that
call pmove pieces directly copy the command's moves into `pml.forwardmove`
and `pml.rightmove` first, as `PmoveSingle` does; crouch with `BUTTON_CROUCH`,
never `upmove`.

`dev/tests/run-sights.cmd` builds `sights_test.c` with the shared bg code:
sight configs, the aim clock, and the SPAS-to-M300 NAC through full `Pmove`
showing the scope on that frame only. On Linux, build the same file list with
`cc` (see `.github/workflows/weapon-sights.yml`).

`dev/tests/run-pickups.sh` runs the inventory, pickup, use/reload, take
window (with the client's selection lagging the snapshot), throwing-knife
pickup, Tactical Insertion pickup/smash/ranking, hint type and sea lion checks
(the TI's end to end through the real plant), `cursor_hint_wiring_test.c`,
which links the real `ClientEndFrame` (TI spot sampling included),
`G_FreeEntity` and hint pass, `use_hint_test.c`: the knife and TI prompts
through the real `cg_draw.c`, `cg_usehint.c` and the engine's bind lookup in
`cl_keys.c`, and `insertion_test.c`: the TI's plant through the real
`FireWeapon`, its damage through the real `G_Damage`/`G_RadiusDamage` from real
shots, the spawn on it inside the real `ClientSpawn` (a corpse there, killed
through the real `G_Damage`, is left untouched), and cleanup through the
real `SetTeam` and `ClientDisconnect`. `dev/TACTICAL_INSERTION.md` lists the
TI's checks in every suite.

`dev/tests/run-fakereload.cmd` (or `.sh`) models the client/server clocks
(MW2's client time base through the real `CL_DriftTimeDelta`, snapshot
selection by the client clock, sv_fps frames, ping) with the real `Pmove` on both
sides, the server's use/reload step after each command, and the real `cg_anim`
+ `cg_weappicture.c` on the client. 0 ms is your own host, in `Com_Frame`'s
order with the snapshot held a frame; the `.sh` also runs every own-host case
through the real `Com_Frame` and loopback (`common.c`, `net_chan.c`,
`fake_reload_engine.c`), requires the same outcome, and checks that only
`SV_Frame`'s packets wait. On the SPAS and the M300 it checks the fake
reload after knife/frag/Semtex cancel, tap vs hold, aiming/shooting/real reload
during it, `+reload` never faking, no fake shot, the lone knife, the fake bullet
(per tap, and over every snapshot phase on your own host, one frame later than
ioquake3's host) and the M300 wild scope at 0/50/100/200 ms. It also drives
the picture directly where no modelled lag reaches: a correction whose
snapshot lands exactly on the shot's READY think keeps the shot the picture
began (`TestCorrectionOnTheReadyThink`: `BG_ShotAwaitsAction` is a gun action
to `CG_WeapPicture_PredictionKept`).
`FAKERELOAD_PROBE=1` prints every tap timing; `FAKERELOAD_SVFPS` changes the
server rate; `FAKERELOAD_BULLETS=1` prints the fake bullet over every phase
(`FAKERELOAD_CLFRAME`, `FAKERELOAD_ONLINEGAME=0` for 50ms snapshots online,
`FAKERELOAD_MAXPACKETS`, `FAKERELOAD_HOSTHOLD=0` for ioquake3's host). Every
ping row is an internet client of Peace's server: a snapshot every 100ms.

`dev/tests/run-gamepad-aim.cmd` (or `.sh`) drives the real `cl_gamepadaim.c`:
the radial deadzone, the squared move with its diagonal stretch, floor
rounding and the sprint threshold (read from `BG_SPRINT_FORWARD_MINIMUM`),
full-tilt hip/ADS rates at sensitivity 10, the aim
fraction lerp and clamp, the 217ms ramp at any sensitivity, instant slowdown,
the flip keeping speed, the 281ms 180, the dominant-axis scale, and graph
evaluation and validation. Its expectations are worked out by hand from MW2's
numbers, not by the code's formula, and every rule was mutation-checked.

`dev/tests/run-bullets.sh` (`BULLETS_TEST_FLAGS` for -DMISSIONPACK) runs
client-side bullets end to end: the real `ClientThink`, `Pmove`, `FireWeapon`
and `G_Damage` on the server half (`bullets_server.c`), the real
`CG_PredictPlayerState`, playerstate and entity events and `CG_FireWeapon` on
the client half (`bullets_client.c`), over one world (`bullets_world.c`). For
every hitscan gun it checks that no impact or tracer is sent, the damage per
hit, and that the shooter's client draws every bullet, pellet and beam where
the server's damage traces ended (hip and aimed, still and moving, aim moving,
one or several commands a frame, lag), the unpredicted path, other players'
view from the entity event (the victim's own box included), the sky, a
projectile and the fake bullet. With the stance it checks the hit volumes
(bullets and pellets stopped by a prone target's body behind its box, and
over a target's box while its eye is above it, on the server and every
screen), drop shots drawn from the eye the shooter's entity carries on every
think of both legs, and every bullet's real `G_Damage` (wrapped by the
linker) against its mark.

`dev/tests/run-kick.sh` (`KICK_TEST_FLAGS=-DMISSIONPACK` for the other
numbering) runs MW2's view kick through the real `cg_kick.c`, `cg_event.c`,
`cg_playerstate.c`, cgame's native traps, `CL_FinishMove` and full `Pmove`:
every weapon's data, kicksim.py's published results, the throwing knife's
tilt held in the recorded `ps.viewangles`, the melee knife and frag after a
shot, the demo/killcam/follow gates and the QVM stub's number. It then builds
the real `cg_kick.c` and `cg_syscalls.asm` into a QVM with q3lcc and q3asm,
runs it on the real `vm_interpreted.c`, and checks that the real
`CL_CgameSystemCalls` and `CL_FinishMove` receive exactly the kick cgame holds.
It then does the same for the viewmodel's skeletal pose (`pose_qvm_test.c`):
the real `cg_anim.c`, built into the QVM as a shipped cgame.qvm builds it, loads
the SPAS's clip table through the file traps and hands the engine a viewmodel
pointing at its own `cg.weaponPose` every frame; the two pose traps and the
entity's pose pointer, called directly, reach the renderer as host pointers to
the QVM's own poses, and a pose running past the QVM's memory is dropped at
each of the three places. Both QVM tests share `qvm_engine.c`, the engine
state and loader a real QVM runs in.

`dev/tests/run-iqmpose.sh` builds each renderer's real `tr_model_iqm.c`
(`iqm_pose_test.c`, plain for GL1, `-DIQM_POSE_GL2`) and loads the shipped
SPAS and M40A3 viewmodels with the real `R_LoadIQM`. A pose built from frames
skins exactly as the frames, a blend of two single-frame poses within float
rounding of their lerp, and a pose whose joint count is not the model's is
ignored. Both renderers' pose code is the same code: the runner requires the
digest of GL1's every skin, built pose, blend and blended skin to be GL2's.

`dev/tests/run-silentshot.sh` (`SILENTSHOT_TEST_FLAGS=-DMISSIONPACK`) models
the client/server clocks as the fake reload test does and runs the real game
module (`ClientThink`, `ClientEvents`, `FireWeapon`, `G_AddEvent`,
`G_AgeEntityEvents`, `ClientEndFrame`), `sv_snapshot.c`'s killcam recording and
snapshots through the real codec, the real `CL_FinishMove`, and cgame's real
snapshot pipeline, prediction, events, client-side bullets and kick. It shoots
(SPAS and M300), presses the throwing knife N ms later, and sweeps N against
every arrival phase, a packet a frame and `cl_maxpackets 30`, at 0/50/100/200ms:
the shooter and a live opponent always get the shot (its flash drawn by the
real `CG_AddPlayerWeapon` from the gun, though the entity may hold the knife
already), the killcam loses it exactly when the server ran shot and press in
one frame, the recorded aim is the commands' own, kick and all, held at the
clamp with the knife, and a frag in the same frame never loses it. A spectator
who follows the shooter and switches away sees the shot once, and a viewer
out of an S&D round in the line of fire draws the bullets through their absent
body. `run-loadouts.sh` checks that a body and a podium model start with an
empty ring (`body_ring_test.c`).

`dev/tests/run-view.sh` (`VIEW_TEST_FLAGS=-DMISSIONPACK` for the other
numbering) runs the camera through the real `CG_CalcViewValues` with the real
sights and `Pmove`: MW2's projection at 4:3, 16:10, 16:9 and 21:9, the 65-80
range, intermission, the aimed lerp from the shipped sight files at every hip
FOV, the sensitivity, and the eye at every stance from `ps.viewheight`. Then
the real `CG_Player`, tag chain and `CG_AddPlayerWeapon` draw live players, a
leaning runner, a corpse, a podium player and the viewer's mirror body: the
feet stay, the top reaches the box top, and every attachment sits where the
unscaled assembly would, scaled about the feet.

MW2's stance button and its cgame half (`dev/STANCE.md`):
`dev/tests/run-stanceinput.sh` (`STANCEINPUT_TEST_FLAGS`) drives the real
`cl_input.c` commands and command builder through the real `cmd.c`, `cvar.c`
and `cl_keys.c` key path frame by frame at 8, 16 and 33 ms: the tap/hold
matrix from every stance, exactly `cl_stanceHoldTime`, the held stance keys,
every toggle and gate, `+gostand`/`+moveup` (the jump under a held
`+movedown` or `+prone`: DOWN never cancels it, MW2's 0x400 being its own
bit), `_peaceSetStance`, a hold across death and a console opened mid-hold.
`run-stancecgame.sh` (`STANCECGAME_TEST_FLAGS`) runs the respawn reset in the real
`CG_PredictPlayerState`, the unpredicted eye's lerp and MW2's invalid-command
hint through its real arm and draw. `run-stanceflow.sh`
(`STANCEFLOW_TEST_FLAGS`) runs client, Pmove and cgame end to end: a sprint
from a crouch toggle (and from a held crouch, which ends it and crouches once
the stand leg the sprint started has run out), a blocked stand-up, prone
refused, the respawn, a prone turn blocked into a wall with the hint
blinking, `+stance` while sprinting, and `+gostand` under a held `+movedown`
or `+prone` (spent lowered, no jump when the key is let go under it, a ladder
jump-off with the crouch held). `event_ring_test.c` checks the stance events'
routing and the hint's arming, `view_test.c` the eye over every think of a
stance change, and `player_model_test.c` the prone placeholder's pose.

`run-bullets`, `run-kick`, `run-silentshot`, `run-view`, `run-stanceinput`,
`run-stancecgame`, `run-stanceflow` and `run-iqmpose` have no `.cmd`. They link
whole game, cgame and client files and rely on GNU ld's `--gc-sections` to
drop the functions that reach engine code they do not build, and MSVC's
linker resolves every external of every object first. Run them under WSL or
with any gcc or clang.

Do not statically initialize structs with inline char arrays mixed with ints:
q3asm splits byte literals and word data into different segments. Use word-only
source tables and fill editable structs at runtime, as in `bg_loadout.c`.

## Search and Destroy

- `g_gametype 8` (`GT_SND`) uses server-owned `g_snd.c`, configstrings 28/29,
  and the existing Use stats. Do not use weapon states or playerState bits
  for bomb ownership or round membership. `bg_pmove.c` knows nothing of S&D
  (the freeze below is MW2's general `PMF_FROZEN`, every mode's).
- `peace_ladder` has Site A on the platform. `peace_sd` is a generated
  two-site arena (`dev/tools/build_sd_map.py`, `--check`; its collision test
  is in `run-snd`). Normal rounds need both teams;
  `exec peace-snd-practice` is a separate cheat-only solo test setup.
- Run `dev/tests/run-snd.cmd` or `.sh`; see `dev/SEARCH_AND_DESTROY.md`.
- A Tactical Insertion can be planted, picked up and smashed in S&D but is
  never spawned on; `SND_StartRound` frees it with every missile and
  `SND_WaitForPlayers` frees it too (MW2's round restart frees everything).
- QVM `sscanf` returns zero even after parsing! Do not use its return count
  to validate HUD data. The S&D site reader uses `COM_Parse` instead.
- **The round end is MW2's endGame (owner's rule: do what MW2 does).** A round
  is postgame the moment it ends - no damage, no use - but alive players keep
  full control (`G_SNDClientThink` returns qfalse, so the real Pmove runs) until
  `snd.freezeTime`: the deciding moment plus `ROUNDEND_FREEZE_DELAY` (1050:
  endGame's waitframe and `freezePlayerForRoundEnd( 1.0 )`). The deciding moment
  is the decisive death's time for an elimination, else the frame the result
  comes. That window is why a knife pressed after the round-winning shot comes
  out; never shorten it or freeze on the kill's frame. An elimination is
  decided inside the death, as MW2's onDeadEvent is (`player_die` ->
  `G_SNDRecordKill` -> `SND_CheckElimination`): from that moment nothing later
  in the same frame - a missile landing, a grenade going off, the rest of a
  blast - deals damage or changes the result, so the first decisive death
  decides. `G_SNDRunFrame` catches teams emptied without a death
  (disconnects, team changes) and the end of the grace period (below). Both
  teams dead at once is the defenders' round before the plant and the
  attackers' after it (`SND_Eliminated`: sd.gsc's onDeadEvent "all"); MW2's
  S&D has no draw. A detonation or defuse
  first waits out sd.gsc's delay (`SND_DETONATION_DELAY` 3000,
  `SND_DEFUSE_DELAY` 1500) with phase still `SND_PLANTED` and `snd.result`
  set: play and damage go on, eliminations are ignored, nothing more can be
  used. Timeouts end at once. The detonation's blast is sd.gsc's
  `radiusDamage( origin, 512, 200, 20, planter )` (`SND_BombBlast`), dealt
  after the result is set, so no death in it decides the round. A player's
  own damage is dealt whole (`G_Damage`: MW2 has no scale for it; Quake 3
  halved it), so the planter beside his bomb takes all of its blast, and a
  frag at one's own feet costs what an enemy's would.
- **The freeze is MW2's `PMF_FROZEN` (owner's rule: do what MW2 does).** It is
  a flag on a living player who stays `PM_NORMAL`, never a pm_type and never a
  skipped Pmove. `ClientEndFrame` sets it (`ClientFrozen`: S&D's prematch
  countdown and round ends, every other mode `ROUNDEND_FREEZE_DELAY` after the
  game ends), as MW2's end frame copies `freezeControls` (0x5d7e33), so the
  snapshot of the frame that freezes already carries it. `PmoveSingle` then
  keeps only the command's stance buttons, zeroes its moves and the velocity
  (0x57440d-0x57445d) and skips gravity; `PM_CheckDuck` reads no stance;
  `PM_Weapon` begins no weapon change. Everything else runs on, so what was
  under way finishes (a reload, a swap, a stance change, a knife or grenade in
  hand, which is thrown) and nothing new starts; the aim and the sprint end and
  the legs idle. MW2 masks only that command, never the one before it
  (oldcmd), so what is held stays held: after a frozen Pmove the player
  state's button records (weaponAction's held bits and Y, `PMF_JUMP_HELD`,
  `PWF_SPRINT_RELEASE`) are written again from the command as it came
  (`PM_KeepHeldThroughFreeze`), and a knife, grenade, jump or sprint held
  through the countdown waits for a fresh press. Never update a button
  record from the masked command alone. A floor gun's hold neither counts
  nor, let go, reloads while frozen. The server reads the command as it came,
  as MW2's does (no frozen test; its use code reads the raw buttons): the
  frozen Pmove drops any use/reload request. The client asks for no Y, alt
  toggle or weapon number while frozen (MW2's `WeaponCycleAllowed` 0x59D590,
  `ActionSlotUsageAllowed` 0x59D6F0), so nothing waits to begin when it lifts.
  The client takes no look input while its last snapshot says frozen
  (`CL_CreateCmd`; MW2's mouse, keyboard and pad turns all return), nor
  through a killcam, whose snapshots carry the killer's state. A spawn is
  put down onto the floor below its Quake 3 lift (`ClientSpawn`; MW2's spawns
  stand on it), or a frozen countdown would leave it hanging; a frozen
  swimmer does not sink. `PM_FREEZE`
  is only S&D's dead waiting for a round. Never freeze by returning before
  Pmove: the gun, the aim and the legs stop where they are and the view still
  turns.
- **S&D counts down only before a first round.** MW2's prematch period is 0
  once the game has started, so a round that follows another starts live with
  nobody frozen (`SND_StartRound( qfalse )` -> `SND_GoLive`); only the match's
  first round counts down (`g_sndCountdown`), frozen, also when a team
  emptied mid-match and the game waited for players.
- **MW2's grace period (`g_sndGracePeriod`, 15).** Before `snd.graceEnd`
  (set by `SND_GoLive`: gracePeriod's fifteen 1s waits) a team wiped out has
  not lost: `SND_CheckElimination` returns, the round goes on with damage and
  the bomb, and gracePeriod's own check `SND_GRACE_CHECK_DELAY` (0.05s) after
  `graceEnd` decides it, with the freeze 1.05s after that frame, not after
  the death. A death from `graceEnd` on decides in the death, as any other.
  Never end a round on a kill inside it. Such a round has no killcam: MW2's
  final kill is the death that made the game postgame (`onNormalDeath`,
  sd.gsc:347, runs after `removeFromAliveCount` may have ended it), so
  `SND_EndRound` asks for one only for a deciding kill past `graceEnd`
  (`SND_DecidingKill`), and the round's pause follows. MW2 also lets late comers spawn
  and drops no weapon in that time; Peace does neither yet.
- Round-winning killcams use a bounded engine snapshot history in
  `sv_snapshot.c`, including the killer's player state and original visibility.
  The window is MW2's final killcam: `KILLCAM_REWIND` 4000 before the death to
  `KILLCAM_LINGER` 1950 after it. `trap_ReplayStart( killer, from, to, begin )`
  records on to `to`, pins the clip, and plays it from `begin` at a constant
  offset: S&D passes the deciding moment plus `KILLCAM_ROUND_DELAY` (5050, roundEndWait(
  4, true )) or, on the match-winning round, `KILLCAM_MATCH_DELAY` (4550).
  Elsewhere only the kill whose score ended the match gets one (MW2's
  `finalKill`, _gamescore.gsc): `player_die` asks for it with the kill plus
  `KILLCAM_GAME_DELAY` (6050) when that kill's `AddScore` queued the
  intermission; a time or capture limit never replays an earlier kill. It
  returns the shared playback deadline; S&D starts the next round
  `SND_KILLCAM_RESTART` (100) after it, and the intermission waits for it.
  Every mode freezes its survivors `ROUNDEND_FREEZE_DELAY` after the game
  ends (MW2's endGame freezes before its round/game branch): `ClientFrozen`,
  with `PMF_FROZEN` as in S&D. Nobody respawns once the game has ended. A
  death after it scores nothing (`player_die`: MW2's `PlayerKilled` returns
  in postgame, _damage.gsc:459), and outside S&D a team change then leaves
  the switcher dead where he fell (`SetTeam` skips `ClientBegin`). The
  scoreboard waits for MW2's screens (`G_GameEndDue`, `G_IntermissionTime`,
  g_combat.c, which CheckExitRules asks each frame): a game
  without rounds shows VICTORY or DEFEAT for `KILLCAM_GAME_DELAY`, then its
  final killcam; S&D's match-winning round shows the round's end
  (`KILLCAM_MATCH_DELAY`), its killcam, then VICTORY or DEFEAT for
  `GAME_END_DELAY`. It used to come 1s after the end, before anyone froze.
  `SV_ReplayStart` refuses a window longer than the history
  (`REPLAY_HISTORY_MSEC` 6500), one ending more than `KILLCAM_LINGER` ahead, or a
  `begin` before the last sample. This changed the game/server syscall, not the
  wire: rebuild the server and game modules together.
  `SNAPFLAG_REPLAY` disables client prediction and marks presentation cuts.
  Playback rebases absolute clocks only; never rewind live entities, change
  weapon/note timers, predict the viewer's input, or use their local PVS history.
  Reset capture each round and on map restart. Objective/world/disconnect wins
  without a decisive enemy kill use the normal result pause. Run
  `dev/tests/run-replay.sh` alongside the S&D tests; `run-snd.sh` includes
  `snd_roundend_test.c`, the round-winning shot and the knife after it, a
  defender's knife still in the air at the deciding death, and a team game's
  score-limit and time-limit ends through the real server.

## Adding a weapon — touches TWO lists

- M16 / Masterkey (`WP_M16` / `WP_MASTERKEY`, 18/19 base or 21/22 missionpack)
  are one inventory instance with two firing modes. `weapalt` is MW2's alt
  toggle (keyboard B, D-pad down), and Y from the Masterkey is the rifle (see
  "Selection is MW2's cycle"). The mode change is MW2's alt switch:
  `PM_BeginWeaponChange` marks the drop `WDRAW_ALT` (MW2's DROPPING_ALT),
  `BG_HolsterTime` gives it the gun's `altDropTime` (0 in MW2's data, so step 3
  finishes it on that think) and its raise is the new mode's `altRaiseTime`
  (730, MW2's RAISING_ALTSWITCH); a sprinting player gets the ordinary drop and
  raise, as in MW2. Slot commands carry mode separately from pickup
  generation; both ammo pools and pump debt survive drops and pickups. The
  geometry and animation are original. `assets/source/m16/weapon.json` holds
  the timing and `dev/tools/build_m16.py` generates the models, clips and
  `bg_m16_data.h` from it, the same bytes on every platform. The swap times,
  the mode switch and the Masterkey's shot (330), pump (430, its bolt 1ms in)
  and reload (a 1000 start that feeds a shell at 600, 750 a shell, a 1350 end)
  are MW2's `m16_shotgun_mp` and `m16_shotgun_attach_mp`. As MW2's, the shot
  ends READY on its last think, the pump starts from READY on the next, its
  bolt on the one after, and it is ready ceil(430/msec) thinks after it
  began; a swap first seen on the bolt's think is the instant one with the
  pump owed (`moni_test.c`'s `TestRechamberTiming` pins it, and the SPAS's,
  command by command at 8, 16 and 33ms). The Masterkey is Single Shot (MW2's
  `fireType`, `weapon.json`'s `fire_type`): held from the shot the pump starts
  on the shot's last think and nothing fires until the trigger is let go
  (`TestHeldRechamber`, `trigger_test.c`). The rifle's fire,
  reload, sprint and aim times are still provisional (it is automatic, `fire_type`
  Full Auto, not MW2's 3-Round Burst, which the shot count supports: a burst
  type's limit is its count), and so are both modes' damage, ammo counts and the Masterkey's
  sights (MW2's has none). It uses ordinary note/holster/raise rules. Never add
  a special NAC for it: MW2's Moni comes out of its data. Protocol 89 / game
  -18 added the alternate ammo fields; protocol 90 / game -19 widened
  `weaponDraw` for the alt switch. The sight files use `zoomfov` in degrees.
  Run `run-masterkey` (which includes `moni_test.c`), `m16_asset_test.py` and
  `test-masterkey.ps1`; see `dev/M16.md`.
- M300 Praetorian (`WP_M40A3`) is a separate, appended weapon (15 basegame / 18 missionpack). The
  Sniper preset occupies the former Scout index; custom-class indices and
  existing serialized weapon IDs are unchanged. Railgun remains selectable.
- `assets/source/m40a3/weapon.json` is the rifle's timing source. Run
  `python3 dev/tools/build_m40a3.py` after changes; it generates the IQM, MD3,
  clip/sight configs and `bg_m40a3_data.h`. `--check` is deterministic/read-only.
  Keep every IQM/MD3 surface below 1000 vertices for GL1. Run
  `dev/tests/run-sniper.sh` and `dev/tests/m40a3_asset_test.py`.
- `zoomfov` in a viewmodel's `.sights` file is MW2's `adsZoomFov` in `cg_fov`
  degrees; the M300's comes from `weapon.json`'s `ads_zoom_fov` (15).
- `scope` in a viewmodel's `.sights` file opts into the aspect-correct ocular
  overlay. At full ADS the overlay replaces the gun picture; its animation
  layers still advance. This never changes ADS permission or note clocks.
  The generator writes that line from `weapon.json`'s `scope_overlay`, which
  also feeds `BG_WeaponScopeOverlay`, MW2's no-knife-while-scoped rule.
- Supported attachment masks live in the shared class weapon catalog. The
  M40's scope is integral, so a red-dot spawn preference cannot invent a
  nonexistent attachment variant. See `dev/M40A3.md`.
- The throwing knife (`WP_THROWING_KNIFE`) is appended lethal equipment
  (16 basegame / 19 missionpack), with original Peace knife/hand/icon art from
  `dev/tools/build_throwingknife.py` (`--check` supported). The world blade
  points along +X; the hand's `tag_weapon` authors its first-person grip.
  `animation.json` beside its source authors the pullout and regular melee's
  horizontal slash. Both use the same blade and animated hand; `throw.cfg` and
  `melee.cfg` supply the frame ranges through the existing clip loader.
  `CG_WeapAnim_ActionFrame` fits the MD3 picture to existing timers only (79ms
  melee hit / 800ms melee end; the throwing knife has no pull, holdFireTime 0,
  so it is drawn at the pullout's last frame at once). The grip tag is interpolated with the
  hand, while the blade stays frame 0. `CG_EquipmentView` hides a thrown blade
  on release but keeps melee's blade through the slash. Semtex's gesture and
  the throwing follow-through are unchanged. See `assets/source/throwingknife/README.md`.
  Its flight, stick, pickup by use and world bound are in `dev/THROWING_KNIFE.md`.
  Its damage and bounce are placeholder weapon data; its pull (0), fuse (30s)
  and launch (1400, 40 up) are throwingknife_mp's. It
  sticks in players (owner's choice of MW2's `stickToPlayers`), rides them and
  their corpse, and falls when the corpse goes; that carry is Semtex's
  (`G_CarryMissile`/`G_FollowHost`, the `host*` fields), and only knives move
  to corpses. A carried missile is sent `TR_INTERPOLATE` (pos and apos) so it
  lerps with its carrier. `CG_Missile` registers a missile's weapon on first
  sight: never rely on someone holding it (a frag in flight is
  `WP_GRENADE_LAUNCHER`, which nobody holds).
- The Tactical Insertion (`WP_TACTICAL_INSERTION`) is appended lethal
  equipment (17 basegame / 20 missionpack; its item is appended at the end of
  `bg_itemlist`), MW2's `flare_mp` in the LETHAL slot (owner's choice of MW2's
  way: equipment, thrown with `BUTTON_LETHAL`, one per class, never a
  tactical). In pmove it is only data: the gun's quick drop, a 2630ms ignite
  (`BG_INSERTION_IGNITE_MS`, holdFireTime) as its raise, held to throw and
  Y-cancelable like the Semtex, the one equipment recovery, the ordinary sea
  lion gate; no TI-named branch. The server (`g_insertion.c`) plants it on its
  fire at the last sampled standing spot (MW2's TISpawnPosition), spawns its
  owner on it (not in S&D; not over another player; that spawn skips
  `G_KillBox`, so a corpse not yet respawned is never gibbed or credited, as
  MW2's spawn there has no telefrag), and frees it on pickup,
  smash, more than 20 counted damage, a new plant, the spawn, a team change,
  disconnect and S&D resets. Placeholder art: the flash's grenade model and
  icon. See `dev/TACTICAL_INSERTION.md`.

The enum is not enough. A weapon in `STAT_WEAPONS` with no `bg_itemlist` entry
crashes on spawn: `CG_RegisterWeapon` calls `CG_Error`, and `BG_FindItemForWeapon`
calls `Com_Error`. This has already caused one map-load crash. Also check every
`MAX_WEAPONS` table in `bg_pmove.c` — they are positional. A gun that models
an MW2 gun also gets that gun's row in `bg_viewKick` (keyed, not positional;
see `dev/VIEW_KICK.md`), and equipment gets its zero row.

Semtex, the throwing knife and the Tactical Insertion (`BG_OffhandCancelable`): pre-release Y cancels to the same gun using its melee-return raise.
The frag and flash wait in hand the same way but take no cancel.
As MW2's offhand states: in the drop and the pull (INIT, PREPARE) Y cancels
whatever the buttons; in hand and ready (HOLD, 578f00-578f69) the throw comes
first unless an offhand button (lethal or tactical) is down on that command
and one was on the one before (`PM_OffhandHeld`, weaponAction's held bits), so
a release and Y on one command, or Y (or a re-press) on the first command
after a queued tap's pull, throws and the Y is eaten. weapnext keeps the slot either way (MW2's
59d5f0-59d622 sets only the cancel bit). No per-weapon or glitch branch.
Post-release Y queues a normal slot swap behind throw and original-gun recovery,
as every quick action now does.
Never treat an emitted throw as a cancellable equip or reset queued selection
during recovery. Keep note ordering intact; no NAC/zoomload feature branches.
See `dev/SEMTEX.md` and the Semtex tests in `dev/tests/run-loadouts.*`.
