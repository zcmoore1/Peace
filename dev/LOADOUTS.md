# Loadouts and weapon selection

Open the in-game pause menu and choose **EDIT LOADOUT** to edit your selected
custom class, or Custom 1 when a preset is equipped. **CHANGE CLASS** opens a
browser with five saved custom classes and four fixed presets, a loadout preview,
and an **EQ** marker for the next-spawn selection. The main menu's **CLASS SELECT**
and **CREATE A CLASS** entries and the existing console commands remain available.

1. Open a custom class with **EDIT LOADOUT**, **X** on a controller, or **E**.
2. Choose **PRIMARY → Sniper rifles → M300 Praetorian**, for example. Primary and
   secondary both browse the full gun catalog by category. The gun in the other
   slot is marked **IN USE** and cannot be duplicated. Choosing a gun returns to
   the loadout overview; Back returns one level without changing that choice.
3. Open a gun's **Attachments** row to fit/remove compatible attachments. Open
   **LETHAL** or **TACTICAL** and choose equipment. LETHAL offers MW2's
   equipment: Frag grenade, Semtex, Throwing knife and **Tactical Insertion**
   ("Plant it where you stand; your next life starts there; pick it back up
   with use."); TACTICAL offers the flash. Counts are MW2's weapon data, fixed
   per item (`BG_ClassEquipmentCount`): one frag, Semtex, throwing knife or
   Tactical Insertion, two flashes. There is no quantity screen. A class saved
   when counts were chosen (1–3) loads with MW2's count and the rest of the
   class unchanged. Choosing None empties that slot.
4. Open any **PERK** card to read descriptions and select a perk. Perks already in
   another slot are marked **IN USE**. Choose the name row to rename the class.
5. **SAVE & EQUIP** saves the class and resumes the match. It applies on the next
   spawn or S&D round. **SAVE ONLY** saves without selecting a different class;
   editing an already equipped class updates its next-spawn definition.

The editor uses a private draft. Back from a changed loadout asks whether to
**KEEP EDITING** or **DISCARD CHANGES**; Back from a picker preserves the draft.
Selecting or editing a class never refills the current life's ammo.

Melee is always a button action. It is not selectable as a primary/secondary.
Each of the three perk slots can select **None**, **Quick Reload**, **9 Lives**,
**Inaudible**, **Second Nature**, or **Expeditious**. Perks must
be distinct; choose any three of the five, or leave slots empty. Existing presets
are unchanged.

- Quick Reload halves authored reload segments and their notetrack timestamps
  together. The predicted animation and server ammo events stay aligned. It does
  not speed up firing/bolting, swapping, first draw, equipment or pickup holds.
- 9 Lives prevents server-applied landing damage. It does not change
  gravity, landing movement, camera feedback, or protection from other damage.
- Inaudible suppresses running, metal, wading and swimming footstep
  events at their shared source, for all listeners including replay viewers.
  It is not a mute-all perk: shots, reloads, jump sounds, heavier landing impacts
  and entering/leaving water still behave normally.
- Second Nature unlocks a second optional attachment slot **on each gun**.
  Without it the class editor allows one. The current optional attachment
  roster contains only Red dot, so a distinct two-attachment combination is
  not yet available. The M300's integral scope does not consume a slot and
  does not support a red dot. No fictional attachment is added to fill the slot.
- Expeditious gives **10% faster movement**, including sprinting, ADS movement,
  crouching, swimming and ladder climbing. It changes desired movement speed,
  not jump height, gravity, knockback or weapon timings. The catalog's
  `movementPercent` is the tuning value. Shared movement derives the multiplier
  per command without modifying `ps.speed`, so prediction cannot compound it.

Attachment choices are now per gun in Create Class. Capacity and compatibility
are validated on the server as part of the saved class. They apply to spawn
loadouts, not to looted guns: pickup instances keep the attachments they carried.
Removing Second Nature trims attachments to the remaining capacity. Duplicate
perks/attachments are rejected rather than stacked.

Class serialization version 2 adds the two attachment masks; version 1 classes
still load and inherit the old `cg_sight` preference at spawn. New editor saves
store explicit choices, so changing `cg_sight` will not overwrite them. The active
perk mask is a predicted/replayed playerstate field. The current build (MW2's
event rings: four playerstate events and every entity's own ring, with the
view kick's cgame import and client-side bullets) uses protocol **86**, game
**-15**; protocol 85 / game -14 was MW2's `grenadeTimeLeft` playerstate field,
and protocol 84 / game -13 the Tactical Insertion's 3-bit use hint type.
Rebuild the executable, server and all modules together. A saved class stores
raw weapon numbers, so a Tactical Insertion class saved in baseq3 (17) is not
the same number in missionpack (20), as for every appended weapon. Changing
class during a life takes effect only at the next spawn.

Custom names allow 1–23 letters, numbers, spaces, hyphens and underscores. Leaving
an edited draft requires confirming discard. Editing an already selected class updates its next-spawn
definition. Custom classes and the selected class persist through game restarts
in archived cvars; this is local persistence, not cloud account synchronization.

## Controls

| Action | Console | Updated Xbox config |
| --- | --- | --- |
| Class select | `ui_classes` or `class` in a match | D-pad left |
| Class editor | `ui_createclass` | D-pad right |
| Choose/menu navigation | Arrow keys, Enter, Escape | D-pad/left stick, A, B |
| Edit highlighted custom class | E | X |
| Name entry | Keyboard | Keep the default name or use a keyboard |
| Toggle weapon slots | `weapnext` / `weapprev` | Y |
| Melee | `+melee` | B |
| Lethal (frag, Semtex, knife, Tactical Insertion) / tactical | `+lethal` / `+tactical` | Existing shoulder bindings |

Copy the updated `dev/peace-gamepad.cfg` to the build's `baseq3` directory if
using the supplied controller config. Classes can also be selected with
`class 0`–`class 3` for presets and `class 4`–`class 8` for custom slots.
In a match, press Escape and choose **CHANGE CLASS**, or enter `class` in the
console. Highlight a saved class and activate it with Enter/A or a mouse click
to equip it and resume the match. `class 4` directly selects the first saved custom
class. The new loadout arrives on the next spawn (next round in S&D), not instantly.

## YY behavior

The slot selection is `(requestedSlot + 1) % SLOT_COUNT`. Gun IDs are looked up
through the two slots, so any pair works; the gun IDs themselves are not treated
as contiguous slot numbers. The previous code toggled from an acknowledged
active slot, causing two rapid presses to request the same destination before
the first swap had completed.

When the request returns to the gun still held during an unfinished ordinary
drop, shared pmove makes it READY with no remaining drop timer. The normal firing
checks run in that same update, without an extra raise or animation lock. This
also allows YY after a new gun appears during its raise, or during a reload tail.
The viewmodel travels back along its current draw/drop clip, easing over the
remaining motion's natural duration, capped at 250ms, then fading to idle over
40ms. An interrupted draw reverses that same clip from its current position.
Base-action releases (including a reload interrupted by a holster) blend from
the last rendered pose over 100ms. These add no gameplay delay; a new shot
cancels the visual return.

This does not manufacture ammunition, erase an unfinished bolt/pump cycle, or
override sprint/ladder restrictions. A reload canceled before insertion does
not fill the magazine. A reload canceled after insertion keeps the inserted
ammo. Both presses must reach input processing; presses coalesced into one
usercmd cannot start a transition the simulation never observed.

The initial holster, note timing and finish-before-fill ordering remain intact.
There is no named-glitch branch, NAC helper, fake reload or zoomload feature.

## Sprint and animation ownership

Starting sprint during a draw, drop or fire/bolt animation keeps that clip as
the visible source. If a holster starts, that source owns the picture for the
whole drop. A one-shot finishes and holds its final pose; a looping clip keeps
playing. Sprinting into a ladder uses the same ownership rule. Reload clips
are excluded, and equipment actions/recovery block sprint.

During a fire/bolt cycle the sprint flag runs (speed, meter) while pmove keeps
what the gun is doing: as MW2's `PM_Weapon_CheckForSprint`, step 1b never
lowers a shot in progress or `WEAPON_BOLTING`, just as it never lowers a raise
or a drop. MW2's shot ends READY with the action owed and its sprint check
comes before the rechamber, so a sprint already on when the shot ends (the
think the fire clock reaches the open note, `PM_ShotOver`) lowers the gun
there: the picture carries the fire clip it was showing, the action stays
owed, and once the gun is back up it plays the whole cycle from its start. A
sprint begun once the action is under way lets the fire clip play to its end;
the cycle ends `WEAPON_READY` and chambered, and the next think starts the
sprint entry from rest. A sprint therefore never cuts the pump or bolt, and
the close note never meets a sprint lock. A reload is still lowered at once
(MW2's sprint raise is taken out of its reload states).

Sprint + Y and sprint + YY use the ordinary slot-toggle/drop-cancellation path.
On cancellation a retained draw/drop or sprint transition returns along its
visible clip. A swap does not end a sprint (a sprint latches, as in MW2), so
its entry resumes without jumping back to frame zero. Sprint, Y, end the
sprint (a fresh sprint press, aim, letting go of forward), Y brings the
retained entry, carry or exit home through the same move-layer return, as
the ordinary sprint exit would bring that posture home. A carry (finished
entry or the loop) plays the whole exit, `BG_WeaponSprintOutTime` (250ms
for SPAS, 260ms for M300); a partial entry runs back over the same whole
exit time, like a sprint tap's reversal, so a slight entry eases home rather
than flicking back; an exit under way keeps its pace. The clock is linear
and uncapped by `MOVE_RETURN_MS`: the authored clip already eases the pose,
and easing the clock as well made the start nearly motionless. A carry loop
drops its bob onto the exit's first frame, as an ordinary stop in the loop does.
This is only visual travel after gameplay has already become ready. Firing owns
the picture immediately once gameplay permits it. The blend never changes a
busy timer, ammunition or chamber debt.

A sprint can start or go on in the air, as in MW2, with the same
entry/carry/exit animation; MW2's ending conditions still apply there, holding
jump included (see `CLAUDE.md`, "Sprint latches and ends the MW2 way").

Clip ranges and loop flags come from the model's config. The rule works with
any weapon that provides those clips; SPAS currently has the complete authored
set. It does not manufacture missing animations for legacy placeholder models.

## Quick actions and networking

A press latches melee/lethal/tactical, fires the action once without requiring
the trigger, then returns to the selected gun. A held action button does not
repeat.

The knife is MW2's (MeleeInit, MeleeFire, MeleeEnd). It never lowers the gun:
the knife is out on the press command itself, the hit lands
`BG_MELEE_DELAY` (79ms, MW2's meleeDelay) after the press, and the knife is
over `BG_MELEE_TIME` (800ms, MW2's meleeTime) after it, where the gun's quick
raise (`BG_WeaponQuickRaiseTime`) starts with nothing put down in between. Both
numbers are MW2 weapon data, the same for every MW2 MP gun. Equipment (frag,
flash, Semtex, throwing knife, Tactical Insertion) goes the other way, through
MW2's offhand machine with each item's own weapon data (the IW4 dump's
`weapons/mp` files; the table in `bg_loadout.c` and the raise table in
`bg_pmove.c`): the gun goes down on its quick drop (250ms; the throwing knife
comes straight out), then the item's prepare, its holdFireTime (600ms for the
frag, flash and Semtex, none for the throwing knife, the TI's 2.63s ignite).
Then it waits in hand (MW2's HOLD) while an offhand button - the lethal or the
tactical, either one - is down on this command and the one before, and is
thrown on the first command that is not so. A tap is queued through the drop
and the prepare; nothing is ever thrown early, and nothing throws itself. Only
the Semtex, the throwing knife and the TI take a Y cancel (offhandHoldIsCancelable);
with the frag or flash out, Y only asks for the other slot. The frag cooks
(cookOffHold): its 3.5s fuse starts when it is in hand and runs while it is
held, a release throws it with what is left, and held to the end it goes off
in the hand and kills its holder (MW2's grenade suicide; in god mode the frag
is simply gone and the gun comes back on its quick raise). The flash's fuse is
1.5s and the Semtex's 2s, from the throw. Frag, flash and Semtex leave at 940
along the view and 120 up, the throwing knife at 1400 and 40 up, each plus the
thrower's own velocity along the throw. Held at death, any of them is dropped
at the body with the fuse it has (a TI is planted). After the throw the item
is held `BG_QUICK_ACTION_HOLD` (500ms) and goes away in `BG_RAPID_DROP_TIME`
(50ms), then the same quick raise. Neither can be sprinted
through: the latch holds until the gun is fully back. Empty equipment does nothing; a ladder
grab clears queued actions, and takes a cooking frag with it. Every knife and grenade defers a Y until the gun it
left is fully back on its quick raise (MW2's CheckForChangeWeapon); YY cannot
cancel that raise. The Semtex's hold/release and pre-throw cancel (MW2's: in
the drop or the pull Y always cancels; once held ready only while an offhand
button is still down, so letting go with Y on one command throws) are
described in `dev/SEMTEX.md` with its controls, timing, art and compatibility
notes. The throwing knife is a lethal choice too: it is in hand on the press
without lowering the gun, throws on release, sticks where it lands and is
taken back with use; see `dev/THROWING_KNIFE.md`. The Tactical Insertion is
MW2's lethal equipment as well: the gun's quick drop, a 2.63s ignite no release
cuts short, held until released, Y-cancelable before the release like the
Semtex, then the same recovery. It is planted at your last standing spot; your
next spawn is there (never in S&D), its owner picks it up with use and enemies
smash it with use or 21 damage; see `dev/TACTICAL_INSERTION.md`.

Only the selected validated definition is sent in `peace_loadout` userinfo.
`peace_class_0`–`peace_class_4` stay local. The server rejects malformed names,
invalid guns, duplicate slots, unsupported perks, and impossible counts without
partially replacing the last valid selection.

The ten-bit `weaponAction` field is carried by playerstate deltas so prediction
replays do not forget a tap or replay it twice. So is MW2's `grenadeTimeLeft`
(signed 16 bits, protocol 85): the fuse of the offhand in hand, written on
both sides by pmove and zeroed by the server at the throw or the death drop;
-1 after a cook-out until the next offhand starts. **Rebuild the executable, server
and all modules together; mixing old and new network/module layouts is unsupported.**
QVM build rules now depend on shared headers to avoid stale structure offsets.
The engine also translates the skeletal pose's nested 32-bit QVM address at
the cgame/UI rendering boundary. Previously, selecting an IQM weapon such as
the SPAS from a QVM could pass that address to the native renderer as a pointer
and crash; native DLLs bypass this translation and retain their actual pointers.

This UI and gameplay validation target Peace/baseq3. The legacy Team Arena
missionpack builds but does not have this class menu. Its equipment capacity and
positional tables are fixed in protocol 72: 24 ammo entries and a full-width
inventory mask. Both builds run the shared class/action regression tests.

## Checks

Run `dev/tests/run-loadouts.cmd` from the VS native tools prompt, or
`sh dev/tests/run-loadouts.sh` on Linux. These exercise the real gameplay,
menu callbacks, real menu input/stack, archived cvar boundary, playerstate codec
and pose layers. Menu checks cover mouse hitboxes, directional gamepad navigation,
nested Back, dirty-draft confirmation, perk/weapon compatibility, all five custom
slots, and the pause menu's Save & Equip/resume path.
The native/QVM pose boundary test links the production `cl_vm.c` and
`vm_ptr.c` directly, so it also runs on MSVC without linking the rest of the
engine or relying on discarded functions to hide unresolved dependencies.
`sh dev/tests/run-perks.sh` checks perk combinations, both class formats,
reload note/cancel boundaries, footstep generation, authoritative fall damage,
movement caps, jump invariance and repeatable prediction.
Existing reload, sight and ladder regressions are separate CI gates.

Manual checks: reopen a saved class; select it while partially empty and verify
ammo is unchanged until respawn; YY while ready, during raise, and before/after
reload insertion; tap and hold melee/equipment; repeat with a controller.
`peacedump` includes active perks, base speed, movement scale, effective reload
note times and the action type/fired latch/held buttons for diagnosis.
