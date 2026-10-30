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
   **LETHAL** or **TACTICAL**, choose equipment, then choose 1–3 charges. Choosing
   None empties that slot. Back from the quantity screen cancels that selection.
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
perk mask is a predicted/replayed playerstate field. The combined use/reload
build uses protocol **82**, game **-11**: rebuild the executable, server and all
modules together. Changing class during a life takes effect only at the next spawn.

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
| Frag / tactical | `+lethal` / `+tactical` | Existing shoulder bindings |

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
The viewmodel blends out of the canceled transition promptly.

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

Sprint + Y and sprint + YY use the ordinary slot-toggle/drop-cancellation path.
On cancellation the retained pose blends back to the current posture. A swap
does not end a sprint (a sprint latches, as in MW2), so its entry resumes
without jumping back to frame zero; ending the sprint (a fresh sprint press,
aim, letting go of forward) returns the picture toward ready. Firing owns
the picture immediately once gameplay permits it. The blend never changes a
busy timer, ammunition or chamber debt.

Clip ranges and loop flags come from the model's config. The rule works with
any weapon that provides those clips; SPAS currently has the complete authored
set. It does not manufacture missing animations for legacy placeholder models.

## Quick actions and networking

A press latches melee/lethal/tactical through normal holster and deploy timing,
fires the action once without requiring the trigger, then returns to the selected
gun. A held action button does not repeat. Empty equipment does nothing; a ladder
grab clears queued actions. Every knife and grenade defers a Y until the gun it
left is fully back on its quick raise (MW2's CheckForChangeWeapon); YY cannot
cancel that raise. Semtex adds hold/release and a pre-throw cancel; see
`dev/SEMTEX.md` for controls, timing, art and compatibility notes.

Only the selected validated definition is sent in `peace_loadout` userinfo.
`peace_class_0`–`peace_class_4` stay local. The server rejects malformed names,
invalid guns, duplicate slots, unsupported perks, and impossible counts without
partially replacing the last valid selection.

The nine-bit `weaponAction` field is carried by playerstate deltas so prediction
replays do not forget a tap or replay it twice. **Rebuild the executable, server
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
`sh dev/tests/run-perks.sh` checks perk combinations, both class formats,
reload note/cancel boundaries, footstep generation, authoritative fall damage,
movement caps, jump invariance and repeatable prediction.
Existing reload, sight and ladder regressions are separate CI gates.

Manual checks: reopen a saved class; select it while partially empty and verify
ammo is unchanged until respawn; YY while ready, during raise, and before/after
reload insertion; tap and hold melee/equipment; repeat with a controller.
`peacedump` includes active perks, base speed, movement scale, effective reload
note times and the action type/fired latch/held buttons for diagnosis.
