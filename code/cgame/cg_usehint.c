/* The use prompt's button and visibility. Picture only: the server says what
 * the use hint is (ps.cursorHint, G_UpdateCursorHint); this file only names
 * the key that uses it and hides the prompt where the server would refuse the
 * use anyway. It never decides whether anything can be used. */
#include "cg_local.h"

// Gamepad controls by the pad's own labels (SDL's game controller names,
// cl_keys.c keynames). Word-only table: q3asm (see CLAUDE.md).
static const char *cg_padKeyNames[] = {
	"LEFTSTICK_CLICK", "LS",
	"RIGHTSTICK_CLICK", "RS",
	"LEFTSHOULDER", "LB",
	"RIGHTSHOULDER", "RB",
	"LEFTTRIGGER", "LT",
	"RIGHTTRIGGER", "RT",
	"DPAD_UP", "D-PAD UP",
	"DPAD_DOWN", "D-PAD DOWN",
	"DPAD_LEFT", "D-PAD LEFT",
	"DPAD_RIGHT", "D-PAD RIGHT",
	"LEFTSTICK_UP", "LS UP",
	"LEFTSTICK_DOWN", "LS DOWN",
	"LEFTSTICK_LEFT", "LS LEFT",
	"LEFTSTICK_RIGHT", "LS RIGHT",
	"RIGHTSTICK_UP", "RS UP",
	"RIGHTSTICK_DOWN", "RS DOWN",
	"RIGHTSTICK_LEFT", "RS LEFT",
	"RIGHTSTICK_RIGHT", "RS RIGHT",
	"MISC1", "SHARE",
	"PADDLE1", "P1",	// SDL's PADDLE1-4 are the Elite's P1, P3, P2, P4 (keycodes.h)
	"PADDLE2", "P3",
	"PADDLE3", "P2",
	"PADDLE4", "P4",
	NULL, NULL
};

/*
CG_ReadableKeyName

A bind name as the engine spells it (Key_KeynumToString: "PAD0_X", "f",
"MOUSE1", "PAD0_LEFTTRIGGER", "KP_ENTER") as a prompt shows it: a gamepad
control by its label on the pad (X, LB, RT, LS, D-PAD UP), a single character
upper-cased, anything else upper-cased with its underscores as spaces. A key
with no name, which the engine spells in hex ("0x1f"), is left as it is.
*/
void CG_ReadableKeyName( const char *bindName, char *out, int outSize ) {
	const char *name = bindName ? bindName : "";
	int i;
	if ( outSize <= 0 ) return;
	if ( !Q_stricmpn( name, "PAD0_", 5 ) ) {
		name += 5;
		for ( i = 0; cg_padKeyNames[i]; i += 2 ) {
			if ( !Q_stricmp( name, cg_padKeyNames[i] ) ) {
				Q_strncpyz( out, cg_padKeyNames[i + 1], outSize );
				return;
			}
		}
	}
	Q_strncpyz( out, name, outSize );
	if ( !Q_stricmpn( name, "0x", 2 ) && name[2] ) return;
	for ( i = 0; out[i]; i++ ) {
		if ( out[i] == '_' ) out[i] = ' ';
		else if ( out[i] >= 'a' && out[i] <= 'z' ) out[i] += 'A' - 'a';
	}
}

/*
CG_FindUseButton

The button that uses a hint, from the binds: the key bound to +usereload,
which holds for BG_UseHoldTime(BUTTON_USE_RELOAD), else the one bound to +use,
which uses it on the press (BG_UseHoldTime(BUTTON_USE) is 0, MW2's
g_useholdtime). The gamepad's keys are looked at first when gamepad is set,
else the keyboard's and mouse's, then the other kind's. *hold says whether the
button must be held. Returns qfalse, with an empty label, when neither
command is bound to any key.
*/
qboolean CG_FindUseButton( qboolean gamepad, char *label, int labelSize, qboolean *hold ) {
	static const char *commands[] = { "+usereload", "+use" };
	static const int buttons[] = { BUTTON_USE_RELOAD, BUTTON_USE };
	char name[64];
	int kind, i;
	*hold = qfalse;
	if ( labelSize > 0 ) label[0] = '\0';
	for ( kind = 0; kind < 2; kind++ ) {
		for ( i = 0; i < 2; i++ ) {
			if ( trap_Key_GetBindingName( commands[i], kind ? !gamepad : gamepad, name, sizeof( name ) ) < 0 )
				continue;
			CG_ReadableKeyName( name, label, labelSize );
			*hold = BG_UseHoldTime( buttons[i] ) > 0;
			return qtrue;
		}
	}
	return qfalse;
}

/*
CG_UsePromptText

"Hold [X] to pick up knife", "Press [F] to pick up knife", or, with neither use
command bound, which command to bind.
*/
void CG_UsePromptText( qboolean bound, qboolean hold, const char *label, const char *action,
	char *out, int outSize ) {
	if ( !bound ) Com_sprintf( out, outSize, "Bind +use to %s", action );
	else Com_sprintf( out, outSize, "%s [%s] to %s", hold ? "Hold" : "Press", label, action );
}

static struct {
	qboolean	valid;
	qboolean	gamepad;		// the kind of input the lookup was for
	int			lookupTime;		// cg.time of the last lookup
	int			askTime;		// cg.time a prompt last asked
	qboolean	bound, hold;
	char		label[32];
} cg_useButton;

/*
CG_UseButtonPrompt

The prompt for action at this time, naming the key of the kind of input the
player used last (cl_gamepadInUse, which the engine sets on every pad or
keyboard and mouse input, as IW4x switches its prompts): a pad's X for a pad
player, a keyboard's F for a keyboard player, whatever the configs set. That
one cvar is read every frame the prompt shows, so a switch shows at once. The
binds are looked up again when it changes, when a prompt appears, and then at
most once per CG_USE_BUTTON_REFRESH while it stays up.
*/
void CG_UseButtonPrompt( int time, const char *action, char *out, int outSize ) {
	char device[16];
	qboolean gamepad;
	trap_Cvar_VariableStringBuffer( "cl_gamepadInUse", device, sizeof( device ) );
	gamepad = atoi( device ) != 0;
	if ( !cg_useButton.valid || gamepad != cg_useButton.gamepad || time < cg_useButton.askTime ||
		time - cg_useButton.askTime > CG_USE_BUTTON_GAP ||
		time - cg_useButton.lookupTime >= CG_USE_BUTTON_REFRESH ) {
		cg_useButton.bound = CG_FindUseButton( gamepad, cg_useButton.label,
			sizeof( cg_useButton.label ), &cg_useButton.hold );
		cg_useButton.gamepad = gamepad;
		cg_useButton.lookupTime = time;
		cg_useButton.valid = qtrue;
	}
	cg_useButton.askTime = time;
	CG_UsePromptText( cg_useButton.bound, cg_useButton.hold, cg_useButton.label, action, out, outSize );
}

/*
CG_UsePromptAction

What a use does to a hint of this type, as the prompt says it, for the kinds
the server holds a use press for (G_UseThink): a throwing knife taken back,
your own Tactical Insertion picked up (MW2's MP_PICKUP_TI), an enemy's
smashed (MP_DESTROY_TI). NULL for any other type: a floor gun has its own
prompt and bar, and the bomb its own HUD.
*/
const char *CG_UsePromptAction( int cursorHint ) {
	switch ( cursorHint ) {
	case CURSOR_HINT_KNIFE: return "pick up knife";
	case CURSOR_HINT_INSERTION: return "pick up Tactical Insertion";
	case CURSOR_HINT_INSERTION_ENEMY: return "smash Tactical Insertion";
	default: return NULL;
	}
}

/*
CG_UsePromptShown

The use prompt's action when it shows, else NULL: the server's last end-frame
found a throwing knife or Tactical Insertion this player can use now
(ps.cursorHint, CG_UsePromptAction). The type is the server's, per player: a
teammate's TI is never one, so it has no prompt. That type already covers the
bomb: the server writes CURSOR_HINT_OBJECTIVE wherever a site or the bomb owns
the use (G_UpdateCursorHint), so a knife or TI type means a press is its.
The rest only hides it where the server would refuse the use anyway (dead,
spectating or following someone, sprinting, equipment out: G_UpdateCursorHint
and G_UseThink), a frame sooner than the server's own end-frame would, and
while a hold already running on a floor gun keeps that gun's prompt and bar:
a plain reload hold (pickupHeld 1, predicted; a knife or TI never takes it),
or a use or use/reload hold the server gave the gun (server's pickupHeld 3
with time on it). A use press a knife or TI takes is rewritten to 2 on its
first server command (G_UseThink), so only the snapshot tells the two apart:
prediction counts any held use over a floor gun as the gun's until it arrives.
One that comes into reach during a gun's hold is not taken by that hold (MW2
holds for what the press chose).
*/
const char *CG_UsePromptShown( const playerState_t *ps, const playerState_t *server ) {
	const char *action = CG_UsePromptAction( ps->cursorHint );
	if ( !action ||
		ps->persistant[PERS_TEAM] == TEAM_SPECTATOR || ps->stats[STAT_HEALTH] <= 0 ||
		ps->pm_type != PM_NORMAL || (ps->pm_flags & (PMF_SPRINTING | PMF_FOLLOW)) ||
		BG_OffhandOut( ps ) || (ps->pickupHeld == 1 && ps->pickupTime > 0) ||
		(server && server->pickupHeld == 3 && server->pickupTime > 0) ) return NULL;
	return action;
}

// What a hint type is, for peacedump and cg_weaponDebug.
const char *CG_CursorHintName( int cursorHint ) {
	switch ( cursorHint ) {
	case CURSOR_HINT_OBJECTIVE: return "objective";
	case CURSOR_HINT_WEAPON: return "gun";
	case CURSOR_HINT_KNIFE: return "knife";
	case CURSOR_HINT_INSERTION: return "own insertion";
	case CURSOR_HINT_INSERTION_ENEMY: return "enemy insertion";
	default: return "nothing";
	}
}
