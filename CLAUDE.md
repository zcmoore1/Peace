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

## Branches

- `dev1` — hipfire spread, dynamic crosshair, sprint/ADS first pass
- `dev2` — NAC via a one-frame `WEAPON_RELOAD_END` state (superseded)
- `dev2-standalone` — skips the retail-data boot check (`fs_checkPak0`)
- `dev3` — NAC rebuilt on notetracks + segmented reloads
- `dev3b` — Two-weapon loadout, melee/lethal/tactical, classes,
  sprint transitions, still swap, gamepad, logging
- `dev-gpt` — current GPT work: classes, YY, ladders, SPAS art/sprint, Search and Destroy, Semtex.

## Config files

Both go in `out\build\x64-Debug\Debug\baseq3\`:
- `dev/peace-autoexec.cfg` → rename to `autoexec.cfg`
- `dev/peace-gamepad.cfg` → copy as-is, keep the name (autoexec execs it by name)

## Diagnostics

- `qconsole.log` and `peacedump.txt` land in the **homepath**, not the build dir:
  `%APPDATA%\Quake3\baseq3\`
- Logging defaults to `logfile 2` (unbuffered, survives a hard crash) and
  appends, with a `===== session started` banner per run.
- `peacedump` prints the full weapon/loadout state. `F11` = `peacedump; condump`.
- In-game: `cg_weaponDebug 1`, `g_debugMove 1`, `devmap <map>` (not `map`).
- `spawndummy [red|blue]` (server console) adds a stationary training dummy on
  any map. It needs no bot AI and no `.aas` file — Peace's maps have none, and
  botlib refuses a map without one. The mark is the `peace_dummy` userinfo key,
  so it survives `map_restart`. In team games it joins the named or smaller
  team, which makes S&D testable alone: `team red`, then `spawndummy blue`.
  `pers.isDummy` survives `ClientSpawn`'s clear/restore; putting the flag in the
  transient part of `gclient_t` loses it on the very first spawn and every round.

## Design invariants — do not violate

- **The NAC is not code.** It emerges from ordering in `PM_Weapon`: a holster
  ALWAYS stamps a real time from `BG_HolsterTime`, `WNOTE_MAG_IN` zeroes `weaponTime`, and a
  transition whose lock is already spent finishes that think and `return`s —
  before the queued clip fill. Never add `if (nac)`, a NAC-named function, a
  special state, or a zero drop time.
- **Two clocks, kept separate.** `weaponAnimTime` counts UP and is what notes
  fire off; `weaponTime` counts DOWN and is only a busy lock. Never derive note
  times from `weaponTime`.
- **Reload never resumes.** It always restarts from 0. No partial-reload state.
- **Animation ownership is generic.** Sprint preserves a running non-reload
  action; a holster keeps that source through its whole duration, including a
  ladder holster. One-shots finish and hold their endpoint; loops keep playing.
  Ordinary sprint entry/exit still reverses continuously within its own layer.
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
- **Bolting is a state, not a phase of firing.** `WEAPON_FIRING` is the shot;
  `WEAPON_BOLTING` is the action being worked. "Bolting" covers bolt, pump,
  cock and lever — they differ only in cycle length and note placement, never
  in code. It cannot fire and CAN be holstered out of. It ends on the anim
  clock, never on the lock. A reload is taken at once out of the shot or the
  action, as MW2's `PM_BeginWeaponReload` accepts READY, FIRING and
  RECHAMBERING (owner's rule: shoot and reload with no delay); the fire cycle
  is simply abandoned, its debt still owed. An empty gun reloads itself out of
  the action, never out of the shot.
- **The bolt cancel is the reload cancel.** `WNOTE_BOLT_CLOSED` on the fire
  animation carries no rounds, so it clears the lock and the unchambered bit —
  the same note, in the same place, that opens the reload's window. Never write
  a second cancel.
- **An open action is a debt, and it is never resumed.** Firing sets the
  weapon's bit in `STAT_UNCHAMBERED`; only the close note clears it. A gun that
  reaches `WEAPON_READY` with its bit set re-enters `WEAPON_BOLTING` from
  elapsed 0 — the whole cycle, exactly like a reload, which never resumes
  either. This is what makes the cancel window a window: miss it and the swap
  costs you the full bolt when you come back. Never add a partial-bolt state,
  and never clear the bit anywhere but the fire cycle's close note or the
  reload's delayed action (spawn is the one exception).
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
- **Clip frame ranges are data.** They live in the `.cfg` beside the model and
  are read by `CG_WeapAnim_RegisterClips`. Re-exporting a model must never mean
  editing C, and no weapon may be named in that loader.
- **cgame is picture only.** It never touches ammo or weapon state.
- **Movement modifiers are per-command values.** `PM_CmdScale` must not write
  sprint/ADS multipliers into `ps.speed`: prediction replay and pmove substeps
  would apply them repeatedly, making speed depend on latency or command count.
- **Selection is a slot toggle.** `weapnext` advances the latest requested slot
  modulo `SLOT_COUNT`, not the server's last acknowledged active slot. If an
  unfinished ordinary holster's destination returns to the gun still held,
  shared pmove cancels the drop and reaches the normal fire gates that think.
  There is no YY timing window or extra delay. Initial drops still stamp their
  real times; chamber debt, ammo, sprint and ladder ownership still apply.
- **Inventory instances own attachments and ammunition.** `BG_GetSlot` /
  `BG_PutSlot` / `BG_SelectSlot` bridge the two instances to the existing
  `ammo[]`, `ammoReserve[]` and `STAT_UNCHAMBERED` working registers. Stowed
  variants of the same base gun must keep separate magazines and chamber debt.
  Do not replace a floor pickup with a full-mag grant. Death drops the held
  slot, not the requested destination. Exact weapon+attachment matches scavenge
  to reserve on touch (capacity-capped) and consume the item; other guns need a
  continuous 1000ms reload or use/reload hold. A short tap still reloads: a
  reload tap in pmove, a use/reload tap through the server's bit (`pickupHeld`
  3 marks a use/reload hold so pmove does not also reload). S&D use is separate.
- **Pickup does not own selection.** The reload hold latches its replacement
  slot, while `cmd.weapon = BG_SLOT_COMMAND + slot` continues carrying the
  latest selection. Old plain weapon IDs are supported for bots/tools. Neither
  inventory replacement nor a delayed pickup event may overwrite Y intent.
  First deployment is pending until the instance is drawn, then consumed on
  start; normal holster/YY/sprint retention may interrupt it. No CAN state.
  `first_raise` is optional clip data, with ordinary raise as the art fallback.
  `cg_sight` seeds attachments at spawn; owned/dropped gun state drives optics
  thereafter, including spectators and replays. See `dev/WEAPON_PICKUPS.md`.
- **Melee is its own kind of swap.** Whatever gun is out, it goes down for the
  knife in the one universal `BG_RAPID_DROP_TIME` (50ms, the minimum swap), not
  its own holster; melee asked for part way through a heavy holster shortens it
  to that, never lengthens it. `BG_HolsterTime` is the single rule — gun's own
  drop, rapid, or the ladder's — used by pmove to stamp and by cgame to fit
  the drop clip, so the two cannot disagree. Frag/flash keep the gun's holster;
  Semtex uses the shared fast-destination time. Do not add a second timing rule
  in the renderer.
- **Recovering from melee and from equipment is ONE thing, in time and
  function.** Knife, frag, flash and a released Semtex use `BG_QUICK_ACTION_HOLD` (500ms),
  go away in `BG_RAPID_DROP_TIME`, then the gun comes back with its own normal
  draw. As in MW2 that draw is the normal one except **you cannot sprint
  through it**: the `weaponAction` latch lasts until the gun is fully drawn,
  and the latch is what blocks sprint. Once fired, selection asks for the gun.
  YY cancels an ordinary recovery draw like any draw; a grenade pressed during
  the draw starts the next one, as MW2 lets one come out of the gun's return,
  but a knife pressed while a gun is being drawn back, the draw's last command
  included, is lost (owner's rule: MW2's knife check skips every raise). A swap
  asked for during it is an ordinary holster, which a knife still shortens. The owner's Semtex exception queues
  slot changes until the throw and original-gun recovery finish. The requested
  slot/HUD updates immediately; the physical holster starts at that boundary.
- **An ordinary raise is cancellable by YY.** That is the owner's rule, not a
  bug: the NAC skips a holster, YY skips a draw, so long swap times on both
  halves reward technique instead of only slowing play.
- **Sprint ends the MW2 way (owner's rule).** `PM_CheckSprint` uses MW2's
  sprint-ending conditions: ladder, a quick action, the trigger (the gun fires
  once it is back up if it is still held), a reload or use/reload button, a
  jump, not moving forward, ducking. Any sprint that ends while its
  button is held waits for the button to come up before it can start again
  (`PWF_SPRINT_RELEASE`, CoD4's `sprintButtonUpRequired`; a quick action sets it
  in `PM_UpdateWeaponAction`). MW2 has no ground test: a sprint goes on and can
  start in the air (once jump is let go). Its speed applies only on the ground
  (`PM_CmdScale` with `pml.walking`, as MW2 scales its walk and not its air
  move), so a sprint begun in the air pays off on landing. Deep water only
  pauses a sprint (MW2 has no swimming). Prediction carries the flag in
  `weapFlags`.
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
  bomb comes first (one use per press, as MW2's use list; `G_UseThink`). CoD4
  also counts a press made with a grenade out as used; Peace does not, because
  MW2 players do fake reloads after equipment. As in MW2 (owner's rule), X and
  R end a sprint and keep one from starting (`PM_CheckSprint`), and the reload
  is taken straight out of the rise (step 4e); a fresh sprint press still
  cancels the reload. A
  released press holds nothing (CoD4 keeps its hold entity until the next
  press). `PM_Weapon` takes the bit as a reload press and
  clears it — except during a quick action, from the press through the
  recovery draw, where it is not read at all (MW2's reload check skips melee
  and offhand) — and a gun that is reloading when the command begins drops it
  (MW2's rule). Peace's own rule: the trigger may fire on the command a reload
  ends, so a held trigger and reload alternate (MW2 loops its segmented reload
  instead). The client never sets the bit; it
  gets it from a snapshot and replays its commands over it, so a tap made as
  the gun comes back can reach the client's return after the server has already
  dropped it: the fake reload. A tap just before a shot is taken by the server
  first and reaches the client late: the fake bullet. `+reload` is the
  weapon's own button, predicted exactly, with no such tricks — as in MW2,
  where the scripts do every trick with `+usereload`. A reload request beats the
  trigger on the same command, as MW2 checks reload before it fires. Never
  predict the bit on the client, never hold the gun back after a quick action,
  and never name a trick. `dev/FAKE_RELOAD.md` has the measured windows.
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
  fake reload short, and that a run which would begin something by itself
  hands back to prediction.
  Aim, camera and HUD always read the predicted state. Run
  `dev/tests/run-fakereload.sh`; `dev/FAKE_RELOAD.md` has the measured windows.

## Capacity limits

- `pm_flags` — **full.** All 16 networked bits used. A 17th needs the wire
  format widened.
- `STAT_WEAPONS` — full 32-bit wire mask at `PS_STAT_WEAPONS`. `MAX_WEAPONS`
  is 24, with equipment through 14 in baseq3 and 17 in missionpack. Other
  stats remain signed int16: their bit 15 must stay unused.
- `weaponAnimSeq` — **full.** 2 bits in `msg.c`; all four values are taken
  (`RSEQ_START/LOOP/END` + `ASEQ_FIRE`). A fifth animation segment has to widen
  the field.
- `weaponstate` — 4 bits, 10 of 16 values used, including `WEAPON_LADDER`.
- `weaponAction` — a separate 9-bit predicted/networked field: action type (2),
  fired latch (1), previous action buttons (3), cancel return acknowledgement,
  swap edge latch, and post-throw recovery. Protocol 79 requires client, server
  and all modules rebuilt together. Older builds/demos are incompatible.
- Slot caches/attachments/flags, first draw, and pickup target/serial/hold are
  explicit playerstate net fields, not extra `stats[]` or `pm_flags` bits.
  `weapFlags` (MW2's name; 2 bits, protocol 79) carries `PWF_USE_RELOAD`, the
  server's use/reload request, and `PWF_SPRINT_RELEASE`. Neither is a clock,
  and neither is rebased.
  Dropped ET_ITEM uses `generic1` for attachments, `time2` for packed magazine
  and reserve, `otherEntityNum2` for chamber debt, and `time` as a spawn serial.
- `perks` is a five-bit spawn-owned playerstate field. All three class perk
  slots share the roster in `bg_loadout.c`; duplicate nonzero IDs are invalid.
  Quick Reload retimes authored notes, segment endpoints and presentation via
  `BG_ReloadTime`/`BG_ReloadSegmentTime`, never by changing clock progression or
  note/holster/fill ordering. Fire/bolt, swap and pickup timings stay unchanged.
  Inaudible filters shared footstep events only; falling damage events still
  reach the server, which separately applies 9 Lives (no fall damage).
  Expeditious scales each command's movement and crouch/water/ladder limits
  through `BG_MovementScale`; never mutate `ps.speed`, jump impulse or timers.
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
  Never add a reload/NAC-specific ladder branch. See `dev/LADDERS.md`.

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
- SPAS normal holster and draw are both 1500ms; ladder holster remains the
  universal 100ms. Both clips are retimed to 1500ms in `view.cfg`, the IQM and
  the importer (`CLIP_SECONDS`). Transition-note ordering and YY cancellation are unchanged.
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
  Frame-only QVM/GL1 rendering crossfades the released source; native GL2 blends
  skeletal poses. The full authored clip coverage currently belongs to SPAS;
  legacy placeholder weapons still need their own animation assets.
- Known SPAS gaps: the drop is the draw reversed, and hands still use a flat placeholder.
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
gun with a worked action. Every
timing in it is read from the shipped tables; never write a literal there, or
retiming a weapon breaks the test without telling you anything.

`dev/tests/run-loadouts.cmd` (or `.sh`) checks class validation/persistence,
modulo selection, same-update YY firing, reload/pull-out cancellation, tap/hold
actions, all nine action bits through the actual network codec, and pose release.
Pose checks cover source retention, ladder holsters, loop/endpoint semantics,
reload/action exclusions, ordinary sprint reversal and frame-only crossfades,
plus YY flow (reversed draw, eased return, fire cut-in) through the real
`PM_Weapon`, which is why the pose test links `pickup_pmove_driver.c`.
Loadouts UI and class definitions are documented in `dev/LOADOUTS.md`.

`dev/tests/run-fakereload.cmd` (or `.sh`) models ioquake3's client/server
clocks (snapshot drift, sv_fps frames, ping) with the real `Pmove` on both
sides, the server's use/reload step after each command, and the real `cg_anim`
+ `cg_weappicture.c` on the client. On the SPAS and the M300 it checks the fake
reload after knife/frag/Semtex cancel, tap vs hold, aiming/shooting/real reload
during it, `+reload` never faking, no fake shot, the lone knife, the fake bullet
and the M300 wild scope at 0/50/100/200 ms. `FAKERELOAD_PROBE=1` prints every
tap timing; `FAKERELOAD_SVFPS` changes the server rate.

Do not statically initialize structs with inline char arrays mixed with ints:
q3asm splits byte literals and word data into different segments. Use word-only
source tables and fill editable structs at runtime, as in `bg_loadout.c`.

## Search and Destroy

- `g_gametype 8` (`GT_SND`) uses server-owned `g_snd.c`, configstrings 28/29,
  and the existing Use stats. Do not use weapon states or playerState bits
  for bomb ownership or round membership. `bg_pmove.c` remains unchanged.
- `peace_ladder` has Site A on the platform. Normal rounds need both teams;
  `exec peace-snd-practice` is a separate cheat-only solo test setup.
- Run `dev/tests/run-snd.cmd` or `.sh`; see `dev/SEARCH_AND_DESTROY.md`.
- QVM `sscanf` returns zero even after parsing! Do not use its return count
  to validate HUD data. The S&D site reader uses `COM_Parse` instead.
- Round-winning killcams use a bounded engine snapshot history in
  `sv_snapshot.c`, including the killer's player state and original visibility.
  `trap_ReplayStart` returns the shared playback deadline; S&D must wait for it
  before spawning the next round, and `LogExit` must reuse it on the final round.
  `SNAPFLAG_REPLAY` disables client prediction and marks presentation cuts.
  Playback rebases absolute clocks only; never rewind live entities, change
  weapon/note timers, predict the viewer's input, or use their local PVS history.
  Reset capture each round and on map restart. Objective/world/disconnect wins
  without a decisive enemy kill use the normal result pause. Run
  `dev/tests/run-replay.sh` alongside the S&D tests.

## Adding a weapon — touches TWO lists

- M300 Praetorian (`WP_M40A3`) is a separate, appended weapon (15 basegame / 18 missionpack). The
  Sniper preset occupies the former Scout index; custom-class indices and
  existing serialized weapon IDs are unchanged. Railgun remains selectable.
- `assets/source/m40a3/weapon.json` is the rifle's timing source. Run
  `python3 dev/tools/build_m40a3.py` after changes; it generates the IQM, MD3,
  clip/sight configs and `bg_m40a3_data.h`. `--check` is deterministic/read-only.
  Keep every IQM/MD3 surface below 1000 vertices for GL1. Run
  `dev/tests/run-sniper.sh` and `dev/tests/m40a3_asset_test.py`.
- `scope` in a viewmodel's `.sights` file opts into the aspect-correct ocular
  overlay. At full ADS the overlay replaces the gun picture; its animation
  layers still advance. This never changes ADS permission or note clocks.
  The generator writes that line from `weapon.json`'s `scope_overlay`, which
  also feeds `BG_WeaponScopeOverlay`, MW2's no-knife-while-scoped rule.
- Supported attachment masks live in the shared class weapon catalog. The
  M40's scope is integral, so a red-dot spawn preference cannot invent a
  nonexistent attachment variant. See `dev/M40A3.md`.

The enum is not enough. A weapon in `STAT_WEAPONS` with no `bg_itemlist` entry
crashes on spawn: `CG_RegisterWeapon` calls `CG_Error`, and `BG_FindItemForWeapon`
calls `Com_Error`. This has already caused one map-load crash. Also check every
`MAX_WEAPONS` table in `bg_pmove.c` — they are positional.

Semtex: pre-release Y cancels to the same gun using its melee-return raise.
Post-release Y queues a normal slot swap behind throw and original-gun recovery.
Never treat an emitted throw as a cancellable equip or reset queued selection
during recovery. Keep note ordering intact; no NAC/zoomload feature branches.
See `dev/SEMTEX.md` and the Semtex tests in `dev/tests/run-loadouts.*`.
