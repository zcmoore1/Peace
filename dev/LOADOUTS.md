# Loadouts and weapon selection

Peace's baseq3 main menu has **CLASS SELECT** and **CREATE A CLASS** directly
below Multiplayer. The in-game menu also has **CLASS SELECT**. The four presets
remain available, alongside five saved custom class slots.

**CREATE A CLASS** opens the editor for the selected custom class, or Custom 1
when a preset is selected. Back returns to class selection; another Back returns
to the main menu.

1. Choose a Custom slot and **CREATE / EDIT**, or run `ui_createclass`.
2. Set the name, primary, secondary, grenade and tactical equipment. Both weapon
   slots use the existing gun catalog and must contain different guns. Equipment
   can be None, Frag or Semtex for lethal, and Flash for tactical, with 1–3 charges.
3. **SAVE CLASS**, then **SELECT CLASS**. Selection applies on the next spawn;
   selecting or editing a class never refills the current life's ammo.

Melee is always a button action. It is not selectable as a primary/secondary.
The three perk slots are reserved and displayed as unassigned. There are no perk
effects or invented perk roster in this change. Nonzero perk IDs are rejected
until a supported roster is defined.

Custom names allow 1–23 letters, numbers, spaces, hyphens and underscores. Back
discards unsaved edits. Editing an already selected class updates its next-spawn
definition. Custom classes and the selected class persist through game restarts
in archived cvars; this is local persistence, not cloud account synchronization.

## Controls

| Action | Console | Updated Xbox config |
| --- | --- | --- |
| Class select | `ui_classes` or `class` in a match | D-pad left |
| Class editor | `ui_createclass` | D-pad right |
| Choose/menu navigation | Arrow keys, Enter, Escape | D-pad/left stick, A, B |
| Name entry | Keyboard | Keep the default name or use a keyboard |
| Toggle weapon slots | `weapnext` / `weapprev` | Y |
| Melee | `+melee` | B |
| Frag / tactical | `+lethal` / `+tactical` | Existing shoulder bindings |

Copy the updated `dev/peace-gamepad.cfg` to the build's `baseq3` directory if
using the supplied controller config. Classes can also be selected with
`class 0`–`class 3` for presets and `class 4`–`class 8` for custom slots.

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
On cancellation the retained pose blends back to the current posture. A held
sprint input still requests sprint, so its entry resumes without jumping back
to frame zero; releasing sprint returns the picture toward ready. Firing owns
the picture immediately once gameplay permits it. The blend never changes a
busy timer, ammunition or chamber debt.

Clip ranges and loop flags come from the model's config. The rule works with
any weapon that provides those clips; SPAS currently has the complete authored
set. It does not manufacture missing animations for legacy placeholder models.

## Quick actions and networking

A press latches melee/lethal/tactical through normal holster and deploy timing,
fires the action once without requiring the trigger, then returns to the selected
gun. A held action button does not repeat. Empty equipment does nothing; a ladder
grab clears queued actions. Frag/flash/melee keep their existing behavior.
Semtex uses hold/release, pre-throw cancel, and post-throw queued swaps; see
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
menu callbacks, archived cvar boundary, playerstate codec and pose layers.
Existing reload, sight and ladder regressions are separate CI gates.

Manual checks: reopen a saved class; select it while partially empty and verify
ammo is unchanged until respawn; YY while ready, during raise, and before/after
reload insertion; tap and hold melee/equipment; repeat with a controller.
`peacedump` includes the action type/fired latch/held buttons for diagnosis.
