/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
// bg_public.h -- definitions shared by both the server game and client game modules

// because games can change separately from the main system version, we need a
// second version that must match between game and cgame

#define	GAME_VERSION		BASEGAME "-17"

#define	DEFAULT_GRAVITY		800
#define	GIB_HEALTH			-40
#define	ARMOR_PROTECTION	0.66

#define	MAX_ITEMS			256

#define	RANK_TIED_FLAG		0x4000

#define DEFAULT_SHOTGUN_SPREAD	700
#define DEFAULT_SHOTGUN_COUNT	11

#define	ITEM_RADIUS			15		// item sizes are needed for client side pickup detection

#define	LIGHTNING_RANGE		768

#define	SCORE_NOT_PRESENT	-9999	// for the CS_SCORES[12] when only one player is present

#define	VOTE_TIME			30000	// 30 seconds before vote times out

// MW2's body (PM_CheckDuck 0x570A90-0x5710E4, view heights 0x570B03-0x570FA8):
// 70 tall standing with the eye at 60, 50 crouched with the eye at 40, 30 wide.
// Peace keeps Quake 3's origin, MINS_Z (24) above the feet, so no map, spawn
// or MINS_Z-translated MW2 offset moves: the box top and the eye are MW2's
// heights above the feet less 24.
#define PLAYER_WIDTH		15
#define	MINS_Z				-24
#define DEFAULT_HEIGHT		46	// MW2's 70 above the feet
#define	DEFAULT_VIEWHEIGHT	36	// MW2's 60
#define CROUCH_HEIGHT		26	// MW2's 50
#define CROUCH_VIEWHEIGHT	16	// MW2's 40
#define DEAD_HEIGHT			-8
#define	DEAD_VIEWHEIGHT		-16
#define INVUL_RADIUS		42

//
// The server captures this window, then streams it to every client. MW2's
// final killcam (_killcam.gsc): camtime 4.0 before the death, postdelay 2.0
// after it less the script's 0.05s settle frame, so 5.95s of playback.
#define	KILLCAM_REWIND			4000	// how far before the kill the replay starts
#define KILLCAM_LINGER			1950	// how far after it the replay ends

// MW2's round end (_gamelogic.gsc endGame), from the deciding death or event:
// endGame's waitframe (0.05s), then freezePlayerForRoundEnd( 1.0 ). Until
// then everyone keeps full control, with damage already off (postgame).
#define ROUNDEND_FREEZE_DELAY	1050
// When the final killcam starts after the death: the waitframe plus
// displayRoundEnd's roundEndWait( 4, matchBonus ) - its splash waits (0.5s
// each, at least once) around the two 2s halves of the match bonus - or, on
// the match-winning round, roundEndWait( 4, false ) = 0.5 + 4. Modes without
// rounds reach it through displayGameEnd's roundEndWait( postRoundTime 5, true ).
#define KILLCAM_ROUND_DELAY		5050
#define KILLCAM_MATCH_DELAY		4550
#define KILLCAM_GAME_DELAY		6050
// sd.gsc: a detonation ends the round 3s later (bombPlanted's wait 3), a
// defuse 1.5s later (bombDefused's wait 1.5); play goes on meanwhile.
#define SND_DETONATION_DELAY	3000
#define SND_DEFUSE_DELAY		1500

// config strings are a general means of communicating variable length strings
// from the server to all connected clients.
//

// CS_SERVERINFO and CS_SYSTEMINFO are defined in q_shared.h
#define	CS_MUSIC				2
#define	CS_MESSAGE				3		// from the map worldspawn's message field
#define	CS_MOTD					4		// g_motd string for server message of the day
#define	CS_WARMUP				5		// server time when the match will be restarted
#define	CS_SCORES1				6
#define	CS_SCORES2				7
#define CS_VOTE_TIME			8
#define CS_VOTE_STRING			9
#define	CS_VOTE_YES				10
#define	CS_VOTE_NO				11

#define CS_TEAMVOTE_TIME		12
#define CS_TEAMVOTE_STRING		14
#define	CS_TEAMVOTE_YES			16
#define	CS_TEAMVOTE_NO			18

#define	CS_GAME_VERSION			20
#define	CS_LEVEL_START_TIME		21		// so the timer only shows the current level
#define	CS_INTERMISSION			22		// when 1, fraglimit/timelimit has been hit and intermission will start in a second or two
#define CS_FLAGSTATUS			23		// string indicating flag status in CTF
#define CS_SHADERSTATE			24
#define CS_BOTINFO				25

#define	CS_ITEMS				27		// string of 0's and 1's that tell which items are present
#define CS_SND                  28      // authoritative round/bomb HUD state
#define CS_SND_SITES            29      // map-authored objective positions

#define	CS_MODELS				32
#define	CS_SOUNDS				(CS_MODELS+MAX_MODELS)
#define	CS_PLAYERS				(CS_SOUNDS+MAX_SOUNDS)
#define CS_LOCATIONS			(CS_PLAYERS+MAX_CLIENTS)
#define CS_PARTICLES			(CS_LOCATIONS+MAX_LOCATIONS) 

#define CS_MAX					(CS_PARTICLES+MAX_LOCATIONS)

#if (CS_MAX) > MAX_CONFIGSTRINGS
#error overflow: (CS_MAX) > MAX_CONFIGSTRINGS
#endif

typedef enum {
	GT_FFA,				// free for all
	GT_TOURNAMENT,		// one on one tournament
	GT_SINGLE_PLAYER,	// single player ffa

	//-- team games go after this --

	GT_TEAM,			// team deathmatch
	GT_CTF,				// capture the flag
	GT_1FCTF,
	GT_OBELISK,
	GT_HARVESTER,
	GT_SND,                     // Search and Destroy; append to preserve old IDs
	GT_MAX_GAME_TYPE
} gametype_t;

typedef enum { SND_WAITING, SND_COUNTDOWN, SND_LIVE, SND_PLANTED, SND_ROUND_OVER } sndPhase_t;
typedef enum { SND_NONE, SND_ELIMINATION, SND_TIME, SND_DEFUSED, SND_EXPLODED, SND_DRAW } sndResult_t;
#define SND_MAX_SITES 2
#define SND_USE_PLANT -2
#define SND_USE_DEFUSE -3

typedef enum { GENDER_MALE, GENDER_FEMALE, GENDER_NEUTER } gender_t;

/*
===================================================================================

PMOVE MODULE

The pmove code takes a player_state_t and a usercmd_t and generates a new player_state_t
and some other output data.  Used for local prediction on the client game and true
movement on the server game.
===================================================================================
*/

typedef enum {
	PM_NORMAL,		// can accelerate and turn
	PM_NOCLIP,		// noclip movement
	PM_SPECTATOR,	// still run into walls
	PM_DEAD,		// no acceleration or turning, but free falling
	PM_FREEZE,		// stuck in place with no control
	PM_INTERMISSION,	// no movement or status bar
	PM_SPINTERMISSION	// no movement or status bar
} pmtype_t;

typedef enum {
	WEAPON_READY,
	WEAPON_RAISING,
	WEAPON_DROPPING,
	WEAPON_FIRING,
	WEAPON_BOLTING,		// the shot is away and the action is being worked. One
						// name for bolt, pump, cock and lever - they differ only
						// in how long it takes and where the notes sit. Its own
						// state because it obeys different rules to firing: it
						// cannot fire, it CAN be holstered out of, and the
						// chamber note clears its lock the same way a mag-in
						// clears a reload's
	WEAPON_RELOADING,
	WEAPON_SPRINT_IN,	// lowering into the sprint carry - a timed action with
						// its own lock, so anything that clears the lock (the
						// mag-in note) collapses the transition
	WEAPON_SPRINTING,	// gun is down; cannot fire
	WEAPON_SPRINT_OUT,	// bringing the gun back up
	WEAPON_LADDER		// hands occupied; entered through WEAPON_DROPPING
} weaponstate_t;

// A quick action borrows the existing holster/deploy/fire states, but is never
// an inventory selection. Held-button history is predicted alongside the action.
#define WA_TYPE_MASK       3
#define WA_MELEE           1
#define WA_LETHAL          2
#define WA_TACTICAL        3
#define WA_FIRED           4
#define WA_HELD_SHIFT      3
#define WA_HELD_MASK       (7 << WA_HELD_SHIFT)
#define WA_RETURNING       64   // keep the original slot until cmd.weapon acknowledges it
#define WA_SWAP_HELD       128  // edge latch across usercmd pmove substeps
#define WA_RECOVERING      256  // thrown equipment returns original gun before queued selection
#define WA_LEFT_EMPTY      512  // the quick action left no gun in hand (MW2's weapon 0): it returns to none

// Ladder tuning is shared by prediction, authority and presentation. The drop
// is an initial feel value, not MW2's (its ladder drop is the gun's
// quickDropTime, 250ms, an open owner call). A grab always pays a positive,
// weapon-independent holster; animation notes still own the busy lock.
#define BG_LADDER_DROP_TIME       100
// One rapid drop, same value as the minimum swap: thrown equipment going away
// at the start of its recovery. The knife has none (BG_QuickActionInPlace).
#define BG_RAPID_DROP_TIME        50
// How long thrown equipment is held before its recovery starts.
#define BG_QUICK_ACTION_HOLD      500
// MW2's killstreak_uav_mp, the trigger the UAV is called in with: raised in
// raiseTime, and on the command it is in hand (MW2's weapon_change, which
// _killstreaks.gsc answers with the UAV and a switch back) put away in dropTime.
#define BG_UAV_TRIGGER_RAISE_MS   1200
#define BG_UAV_TRIGGER_DROP_MS    899
// MW2's melee (weapon data: meleeDelay 0.079 and meleeTime 0.8, identical for
// every MW2 MP gun in the dump). MeleeInit writes the melee state straight over
// the gun's, the hit lands meleeDelay after the press (MeleeFire), and the knife
// is over meleeTime after it (MeleeEnd), where the gun's quick raise starts.
#define BG_MELEE_DELAY            79
#define BG_MELEE_TIME             800
// MW2's offhand prepare, holdFireTime (weapon data, the IW4 dump's
// weapons/mp files): OffHandPrepare (577800) locks the offhand for it, and the
// offhand waits in HOLD for its release once it is over. The frag, flash and
// Semtex take 0.6s; the throwing knife none at all (it is in hand on its
// press); the Tactical Insertion's is its ignite (below).
#define BG_GRENADE_HOLD_FIRE_MS   600	// frag_grenade_mp, flash_grenade_mp, semtex_mp holdFireTime 0.6
#define BG_THROWING_KNIFE_PULL_MS 0	// throwingknife_mp holdFireTime 0
// The rest of throwingknife_mp is weapon data still to be supplied:
#define BG_THROWING_KNIFE_DAMAGE  135	// throwingknife_mp damage (IW4 dump): one hit kills
#define BG_THROWING_KNIFE_BOUNCE  0.4f	// per-surface bounce scale (weapon data, placeholder)
// MW2's code and dvar defaults for the throwing knife (iw4x.exe):
#define BG_THROWING_KNIFE_SPIN    2000	// deg/s end over end in flight (0x6ddf68)
#define BG_THROWING_KNIFE_LIFE    30000	// ms in flight before it is freed: its fuseTime 30 (5e947c caps an unset one the same)
// MW2's death drop (player_die 42bd08 -> 5df920): a held offhand is fired from
// 40 above the feet at up to 160 u/s on each axis. Peace's origin is MINS_Z
// (24) above its feet, so the drop leaves 16 above the origin.
#define BG_OFFHAND_DEATH_SPEED    160.0f	// 0x6f2de0
#define BG_OFFHAND_DEATH_HEIGHT   16.0f	// MW2's 40 above the feet (0x739aa8)
#define BG_THROWING_KNIFE_WORLD_MAX 32	// Peace's bound on knives out at once: the oldest is freed
#define BG_THROWING_KNIFE_HIT_SPEED 400.0f	// g_minGrenadeDamageSpeed: slower does no damage
#define BG_THROWING_KNIFE_REST_SPEED 20.0f	// grenadeRestThreshold: settles on a floor below it
#define BG_THROWING_KNIFE_STICK_DOT 0.7f	// head-on within ~45.6 degrees sticks (0x717154)
#define BG_THROWING_KNIFE_FLOOR_Z  0.7f	// a floor it may settle on
#define BG_KNIFE_PICKUP_RADIUS    90.0f	// player_throwbackInnerRadius, flat, from the origin
#define BG_KNIFE_PICKUP_REACH     160.0f	// player_throwbackOuterRadius, from the eye
#define BG_KNIFE_PICKUP_STILL     100.0f	// bg_maxGrenadeIndicatorSpeed: moving faster is no pickup
// MW2's Tactical Insertion, flare_mp (weapon data) and _perkfunctions.gsc. Its
// pull is the ignite, holdFireTime: a release before it ends is queued, and it
// is planted at the release (MW2's HOLD waits for it; cookOffHold 0, so it
// never throws itself). The plant is the owner's last good standing spot.
#define BG_INSERTION_IGNITE_MS    2630	// flare_mp holdFireTime 2.63
#define BG_INSERTION_HEALTH       5000	// GlowStickDamageListener: health 5000 ...
#define BG_INSERTION_DESTROY_DAMAGE 20	// ... destroyed once below 4980
#define BG_INSERTION_HINT_BIAS    192.0f	// MW2's use list: a scriptmover's score less 192
#define BG_INSERTION_GROUND_DROP  2048.0f	// TIGroundPosition: traced from +16 to -2048
// Reach, climb speed, friction and acceleration are Peace's first-pass values:
// MW2's ladder move has not been decoded. The push is MW2's speed, but Peace
// adds it along the face's normal, where MW2's Jump_PushOffLadder (0x56DA80)
// sets the flat velocity to it along the view, reflected off the ladder when
// the view faces into it: the same push only when looking straight at it.
#define BG_LADDER_REACH           4.0f
#define BG_LADDER_SPEED           160.0f
#define BG_LADDER_FRICTION        5.0f
#define BG_LADDER_ACCELERATE      10.0f
#define BG_LADDER_JUMP_PUSH       128.0f	// MW2's jump_ladderPushVel (0x7A9F38, read at 0x56DB62)
#define BG_LADDER_JUMP_SCALE      0.75f	// MW2's Jump_PushOffLadder: of a jump's take-off speed
#define BG_LADDER_YAW_CAP         100.0f	// MW2's bg_ladder_yawcap (0x7ADD0C, clamp 0x571B60): look either side of the ladder

// One signed-short stat: three flags + 12-bit inward-facing yaw. No extra
// pm_flags bit or animation sequence (both fields are already full).
#define LADDER_ATTACHED           1
#define LADDER_HOLSTER            2
#define LADDER_JUMP_OFF           4  // re-arm on separation, not on a timer
#define LADDER_YAW_SHIFT          3
#define LADDER_YAW_MASK           4095
#define LADDER_FACING(state) SHORT2ANGLE( (((state) >> LADDER_YAW_SHIFT) & LADDER_YAW_MASK) << 4 )

// Typed notes on a weapon animation. Predicted in bg_pmove - never driven from
// cgame, and never from weaponTime. They fire when the anim's ELAPSED clock
// (ps->weaponAnimTime) crosses their timestamp.
typedef enum {
	WNOTE_NONE,			// list terminator
	WNOTE_BOLT_OPEN,	// shot is away, the action starts opening
	WNOTE_MAG_OUT,		// sound only
	WNOTE_MAG_IN,		// gameplay: queue the clip fill and clear the busy lock
	WNOTE_BOLT_CLOSED	// pump/bolt guns: identical meaning to MAG_IN
} weaponNote_t;

typedef struct {
	int				time;	// ms into the SEGMENT
	weaponNote_t	note;
	int				param;	// MAG_IN/BOLT_CLOSED: rounds to insert. <= 0 = fill the magazine
} bg_weaponNote_t;

// A reload is three segments: an optional open-up, a body that repeats while
// there is still room and stock, and an optional close-up. A magazine gun is
// simply a weapon whose loop fills the whole clip in one pass and therefore
// never repeats - there is no per-weapon-type branch anywhere.
typedef enum {
	RSEQ_START,
	RSEQ_LOOP,
	RSEQ_END,
	RSEQ_COUNT,
	// The fire cycle is an animation with notes on it exactly like a reload
	// segment, so it rides in the same field rather than growing a second one.
	// ps->weaponAnimSeq goes over the wire in 2 bits and this is the last value
	// that fits; a fifth would have to widen it in msg.c.
	ASEQ_FIRE = RSEQ_COUNT
} reloadSeq_t;

typedef struct {
	int						length;	// segment duration in ms. 0 = segment absent
	const bg_weaponNote_t	*notes;
} bg_weaponAnimSeg_t;

typedef struct {
	bg_weaponAnimSeg_t	seg[RSEQ_COUNT];
	qboolean			interruptible;	// attack can stop loading and play END once ammo is available
} bg_weaponReload_t;

// pmove->pm_flags
#define	PMF_DUCKED			1
#define	PMF_JUMP_HELD		2
#define PMF_SPRINTING		4		// player is currently sprinting (bit 2)
#define	PMF_BACKWARDS_JUMP	8		// go into backwards land
#define	PMF_BACKWARDS_RUN	16		// coast down to backwards run
#define	PMF_TIME_LAND		32		// pm_time is time before rejump
#define	PMF_TIME_KNOCKBACK	64		// pm_time is an air-accelerate only time
#define PMF_ADS				128		// aiming down sights (bit 7)
#define	PMF_TIME_WATERJUMP	256		// pm_time is waterjump
#define	PMF_RESPAWNED		512		// clear after attack and jump buttons come up
#define	PMF_USE_ITEM_HELD	1024
#define PMF_GRAPPLE_PULL	2048	// pull towards grapple location
#define PMF_FOLLOW			4096	// spectate following another player
#define PMF_SCOREBOARD		8192	// spectate as a scoreboard
#define PMF_INVULEXPAND		16384	// invulnerability sphere set to full size
#define PMF_PENDING_MAG		32768	// WNOTE_MAG_IN fired and the clip fill is queued for later this think. If a holster finishes first the fill never happens - that is the NAC. bit 15

#define	PMF_ALL_TIMES	(PMF_TIME_WATERJUMP|PMF_TIME_LAND|PMF_TIME_KNOCKBACK)


#define	MAXTOUCH	32
typedef struct {
	// state (in / out)
	playerState_t	*ps;

	// command (in)
	usercmd_t	cmd;
	int			tracemask;			// collide against these types of surfaces
	int			debugLevel;			// if set, diagnostic output will be printed
	qboolean	noFootsteps;		// if the game is setup for no footsteps by the server
	qboolean	gauntletHit;		// true if a gauntlet attack would actually hit something

	int			framecount;

	// results (out)
	int			numtouch;
	int			touchents[MAXTOUCH];

	vec3_t		mins, maxs;			// bounding box size

	int			watertype;
	int			waterlevel;

	float		xyspeed;

	// for fixed msec Pmove
	int			pmove_fixed;
	int			pmove_msec;
	// Read-only nearby item selected by the host's world query. Shared pmove
	// owns the hold clock; only the server may commit an inventory transfer.
	const entityState_t *pickupItem;

	// callbacks to test the world
	// these will be different functions during game and cgame
	void		(*trace)( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentMask );
	int			(*pointcontents)( const vec3_t point, int passEntityNum );
} pmove_t;

// if a full pmove isn't done on the client, you can just update the angles
void PM_UpdateViewAngles( playerState_t *ps, const usercmd_t *cmd );
void Pmove (pmove_t *pmove);

int  BG_WeaponMagSize( int weapon );
int  BG_WeaponMaxReserve( int weapon );
int  BG_WeaponStartReserve( int weapon );	// reserve a class spawns with
int  BG_WeaponReloadTime( int weapon );		// nominal single-pass length (start+loop+end)
const bg_weaponReload_t *BG_WeaponReload( int weapon );
int  BG_WeaponReloadSegLength( int weapon, int seq );
int  BG_ReloadTime( const playerState_t *ps, int authoredTime );
int  BG_ReloadSegmentTime( const playerState_t *ps, int seq );
int  BG_HolsterTime( const playerState_t *ps );	// the holster this player is doing right now
int  BG_WeaponFireLength( int weapon );		// 0 = no modelled fire animation
int  BG_WeaponDropTime( int weapon );		// holster length, never 0
int  BG_WeaponRaiseTime( int weapon );		// deploy length, never 0 for a gun; an offhand's holdFireTime
int  BG_WeaponQuickDropTime( int weapon );	// MW2's quickDropTime: going away for a grenade
int  BG_WeaponQuickRaiseTime( int weapon );	// MW2's quickRaiseTime: back after a knife or a grenade
int  BG_WeaponDrawTime( const playerState_t *ps );
int  BG_WeaponFirstRaiseTime( int weapon );
int  BG_WeaponSprintInTime( int weapon );	// lower-into-sprint length
int  BG_WeaponSprintOutTime( int weapon );	// raise-out-of-sprint length
int  BG_WeaponBaseSpread( int weapon );
int  BG_WeaponScopeOverlay( int weapon );	// aimed through an overlay scope (MW2: no knife while aimed)
float BG_SpreadScale( const playerState_t *ps );
float BG_WeaponSpread( const playerState_t *ps );

//===================================================================================


// player_state->stats[] indexes
// NOTE: may not have more than 16
typedef enum {
	STAT_HEALTH,
	STAT_HOLDABLE_ITEM,
	STAT_WEAPONS = PS_STAT_WEAPONS, // full-width inventory mask
	STAT_ARMOR,
	STAT_DEAD_YAW,					// look this direction when dead (FIXME: get rid of?)
	STAT_CLIENTS_READY,				// bit mask of clients wishing to exit the intermission (FIXME: configstring?)
	STAT_MAX_HEALTH,				// health / armor limit, changeable by handicap

	// --- loadout ---
	// stats[] entries are networked as 16-bit shorts (msg.c), which is plenty
	// for weapon indices and small counts, and costs no new playerState field.
	STAT_SLOT_PRIMARY,				// weapon_t held in slot 1
	STAT_SLOT_SECONDARY,			// weapon_t held in slot 2
	STAT_ACTIVE_SLOT,				// weaponSlot_t the player is "on" (melee and
									// equipment do not change it - they return here)
									// equipment counts live in ammo[WP_FRAG] /
									// ammo[WP_FLASH] - no stat needed
	STAT_USE_TARGET,				// entity being picked up, or -1
	STAT_USE_PROGRESS,				// ms of hold accumulated toward the pickup
	STAT_UNCHAMBERED = PS_STAT_UNCHAMBERED, // full-width per-weapon chamber debt
									// action has NOT been worked since. Per weapon
									// because you carry two and only one of them
									// is mid-cycle. Same wire width as weapon mask.
	STAT_LADDER,				// LADDER_* flags and surface-facing yaw
#ifdef MISSIONPACK
	STAT_PERSISTANT_POWERUP, // keep engine-visible stat indices identical in both builds
#endif
	STAT_COUNT
} statIndex_t;

typedef char bg_stats_fit_network[(STAT_COUNT <= MAX_STATS) ? 1 : -1];


// player_state->persistant[] indexes
// these fields are the only part of player_state that isn't
// cleared on respawn
// NOTE: may not have more than 16
typedef enum {
	PERS_SCORE,						// !!! MUST NOT CHANGE, SERVER AND GAME BOTH REFERENCE !!!
	PERS_HITS,						// total points damage inflicted so damage beeps can sound on change
	PERS_RANK,						// player rank or team rank
	PERS_TEAM,						// player team
	PERS_SPAWN_COUNT,				// incremented every respawn
	PERS_PLAYEREVENTS,				// 16 bits that can be flipped for events
	PERS_ATTACKER,					// clientnum of last damage inflicter
	PERS_ATTACKEE_ARMOR,			// health/armor of last person we attacked
	PERS_KILLED,					// count of the number of times you died
	// player awards tracking
	PERS_IMPRESSIVE_COUNT,			// two railgun hits in a row
	PERS_EXCELLENT_COUNT,			// two successive kills in a short amount of time
	PERS_DEFEND_COUNT,				// defend awards
	PERS_ASSIST_COUNT,				// assist awards
	PERS_GAUNTLET_FRAG_COUNT,		// kills with the guantlet
	PERS_CAPTURES					// captures
} persEnum_t;


// entityState_t->eFlags
#define	EF_DEAD				0x00000001		// don't draw a foe marker over players with EF_DEAD
#ifdef MISSIONPACK
#define EF_TICKING			0x00000002		// used to make players play the prox mine ticking sound
#endif
#define	EF_TELEPORT_BIT		0x00000004		// toggled every time the origin abruptly changes
#define	EF_AWARD_EXCELLENT	0x00000008		// draw an excellent sprite
#define EF_PLAYER_EVENT		0x00000010
#define	EF_BOUNCE			0x00000010		// for missiles
#define	EF_BOUNCE_HALF		0x00000020		// for missiles
#define	EF_AWARD_GAUNTLET	0x00000040		// draw a gauntlet sprite
#define	EF_NODRAW			0x00000080		// may have an event, but no model (unspawned items)
#define	EF_FIRING			0x00000100		// for lightning gun
#define	EF_KAMIKAZE			0x00000200
#define	EF_MOVER_STOP		0x00000400		// will push otherwise
#define EF_AWARD_CAP		0x00000800		// draw the capture sprite
#define	EF_TALK				0x00001000		// draw a talk balloon
#define	EF_CONNECTION		0x00002000		// draw a connection trouble sprite
#define	EF_VOTED			0x00004000		// already cast a vote
#define	EF_AWARD_IMPRESSIVE	0x00008000		// draw an impressive sprite
#define	EF_AWARD_DEFEND		0x00010000		// draw a defend sprite
#define	EF_AWARD_ASSIST		0x00020000		// draw a assist sprite
#define EF_AWARD_DENIED		0x00040000		// denied
#define EF_TEAMVOTED		0x00080000		// already cast a team vote

// NOTE: may not have more than 16
typedef enum {
	PW_NONE,

	PW_QUAD,
	PW_BATTLESUIT,
	PW_HASTE,
	PW_INVIS,
	PW_REGEN,
	PW_FLIGHT,

	PW_REDFLAG,
	PW_BLUEFLAG,
	PW_NEUTRALFLAG,

	PW_SCOUT,
	PW_GUARD,
	PW_DOUBLER,
	PW_AMMOREGEN,
	PW_INVULNERABILITY,

	PW_NUM_POWERUPS

} powerup_t;

typedef enum {
	HI_NONE,

	HI_TELEPORTER,
	HI_MEDKIT,
	HI_KAMIKAZE,
	HI_PORTAL,
	HI_INVULNERABILITY,

	HI_NUM_HOLDABLE
} holdable_t;


typedef enum {
	WP_NONE,

	WP_GAUNTLET,
	WP_MACHINEGUN,
	WP_SHOTGUN,
	WP_GRENADE_LAUNCHER,
	WP_ROCKET_LAUNCHER,
	WP_LIGHTNING,
	WP_RAILGUN,
	WP_PLASMAGUN,
	WP_BFG,
	WP_GRAPPLING_HOOK,
#ifdef MISSIONPACK
	WP_NAILGUN,
	WP_PROX_LAUNCHER,
	WP_CHAINGUN,
#endif

	// Loadout extras. These are weapon_t entries so they reuse the whole pmove
	// state machine (switch timing, notes, firing) for free, but they are NOT
	// inventory slots - melee is always available and equipment lives in its
	// own slots. Basegame indices: KNIFE 11, FRAG 12, FLASH 13, SEMTEX 14,
	// M40A3 15, THROWING_KNIFE 16, TACTICAL_INSERTION 17, MASTERKEY 18,
	// KILLSTREAK_UAV 19 (missionpack +3). Append only: saved classes store
	// these numbers.
	WP_KNIFE,			// melee, always carried
	WP_FRAG,			// lethal equipment
	WP_FLASH,			// tactical equipment
	WP_SEMTEX,           // held lethal equipment; release to throw
	WP_M40A3,            // scoped bolt-action rifle; appended to preserve class IDs
	WP_THROWING_KNIFE,   // lethal equipment: thrown, sticks, picked back up (MW2's)
	WP_TACTICAL_INSERTION,	// lethal equipment: planted where you stand, your next spawn (MW2's)
	WP_MASTERKEY,        // MW2's underbarrel shotgun (m4_shotgun_attach_mp): the alt weapon of a gun with
	                     // the Shotgun attachment, never a slot of its own
	WP_KILLSTREAK_UAV,   // MW2's killstreak_uav_mp: the trigger, in hand only to call the UAV in;
	                     // ammo[] is how many UAVs the player has earned and not used

	WP_NUM_WEAPONS
} weapon_t;

int BG_LethalWeapon( const playerState_t *ps );
int BG_ActiveSlotWeapon( const playerState_t *ps );
qboolean BG_IsActionWeapon( int weapon );
qboolean BG_CanAimDownSights( const playerState_t *ps );

// Inventory slots. Two weapons, CoD style.
typedef enum {
	SLOT_PRIMARY,
	SLOT_SECONDARY,
	SLOT_COUNT
} weaponSlot_t;

// Slot intent survives replacement of its contents (and two differently
// attached copies of one gun). Plain weapon IDs remain valid for bots/tools.
#define BG_SLOT_COMMAND 128
// cmd.weapon BG_SLOT_COMMAND + slot + SLOT_COUNT * the slot's generation bit,
// so a selection made before the client saw a pickup can be told apart,
// + 2 * SLOT_COUNT when it asks for the slot gun's alt weapon (MW2's usercmd
// names the alt weapon itself), + 4 * SLOT_COUNT for the killstreak toggle:
// the trigger is wanted while that bit differs from PWF_KILLSTREAK_SERVED.
#define BG_SLOT_CMD_GENERATION 1
#define BG_SLOT_CMD_ALT 2
#define BG_SLOT_CMD_KILLSTREAK 4
#define BG_IS_SLOT_COMMAND(s) ((s) >= BG_SLOT_COMMAND && (s) < BG_SLOT_COMMAND + 8 * SLOT_COUNT)
#define BG_ATTACHMENT_REDDOT 1
#define BG_ATTACHMENT_SHOTGUN 2	// MW2's underbarrel shotgun: the gun's alt weapon is WP_MASTERKEY
#define BG_MAX_CLASS_ATTACHMENTS 2
#define BG_ATTACHMENTS_LEGACY 256 // version-2 class encoding of cg_sight fallback
int BG_AttachmentTypeCount( void );
int BG_AttachmentType( int index );
const char *BG_AttachmentName( int index );
int BG_WeaponAttachmentMask(int weapon);
#define BG_GUN_FIRST_DRAW 1
#define BG_GUN_UNCHAMBERED 2
#define BG_GUN_GENERATION 4	// slotFlags only: flipped each time a pickup replaces the slot
#define BG_GUN_ALT 8	// slotFlags only: the slot's gun is in its alt weapon (the underbarrel); never travels
#define BG_GUN_ALT_UNCHAMBERED 16	// the alt weapon owes its pump: its chamber debt, which travels
#define BG_PICKUP_RANGE 128.0f	// MW2's player_useRadius, eye to item
// MW2's Player_ActivateHoldCmd, one rule for every hold-to-use usable, the
// throwing knife and the floor gun alike, in ms from the press: a use from
// +activate is used once g_useholdtime has passed (0 in the exe: on the press);
// one from use/reload also needs IW4x's gpad_use_hold_time (its controller
// rule, hooked into that same check at 0x5FE396), and a shorter tap reloads.
// Peace's plain reload holds for a floor gun the same way (owner's rule). No
// use at all this soon after the PLAYER spawns (g_useholdspawndelay, 500 in
// the exe). Nothing counts from an item's age. BG_UseHoldTime picks the hold.
#define BG_USE_HOLD_TIME 0		// g_useholdtime, the exe's default
#define BG_USE_RELOAD_HOLD_TIME 250	// IW4x's gpad_use_hold_time
#define BG_USE_SPAWN_DELAY 500
int BG_UseHoldTime(int buttons);
// ps.cursorHint, MW2's playerState.cursorHint (ps+0x194, beside the index at
// 0x19c): what the use hint pass found usable on the last end-frame. MW2's
// Player_UpdateCursorHints zeroes it before any of its early returns (4942c8)
// and writes it only beside an index (49461f, 49438d), so unlike the sticky
// cursorHintEntIndex it is
// live: it is what says whether that index can be used now. Written only by
// the server (G_UpdateCursorHint); pmove never reads or writes it, and cgame
// only draws from it. PS_CURSOR_HINT_BITS on the wire.
typedef enum {
	CURSOR_HINT_NONE,	// nothing usable: dead, sprinting, equipment out, or nothing in reach
	CURSOR_HINT_OBJECTIVE,	// an S&D site or the bomb (the index is ENTITYNUM_WORLD)
	CURSOR_HINT_WEAPON,	// a floor gun in reach
	CURSOR_HINT_KNIFE,	// a resting throwing knife this player can take now
	CURSOR_HINT_INSERTION,	// this player's own Tactical Insertion: a use picks it up
	CURSOR_HINT_INSERTION_ENEMY,	// an enemy's Tactical Insertion: a use smashes it
	CURSOR_HINT_COUNT
} cursorHint_t;
#define BG_PICKUP_OWNER_DELAY 1000	// MW2: your own dropped gun waits this long
typedef struct {
	int weapon, attachments, ammo, reserve, flags;
	int altAmmo, altReserve;	// the alt weapon's magazine and reserve (the underbarrel's shells)
} bg_weaponInstance_t;
int BG_RequestedSlot(const playerState_t *ps, int selection);
qboolean BG_RequestedAlt(int selection);
qboolean BG_RequestedKillstreak(int selection);
int BG_RequestedWeapon(const playerState_t *ps, int selection);
int BG_SlotCommand(const playerState_t *ps, int slot);
int BG_SelectionCommand(const playerState_t *ps, int slot, qboolean alt, qboolean killstreak);
qboolean BG_SlotCommandCurrent(const playerState_t *ps, int selection);
// MW2's alt weapons (WeaponCompleteDef altWeaponIndex): the gun a slot holds
// carries its alt weapon when its attachments give it one.
int BG_AltWeapon(int weapon, int attachments);
qboolean BG_IsAltWeapon(int weapon);
int BG_SlotWeapon(const playerState_t *ps, int slot, qboolean alt);
qboolean BG_SlotAltActive(const playerState_t *ps, int slot);
qboolean BG_IsAltSwitch(const playerState_t *ps, int from, int to);
int BG_WeaponAltRaiseTime(int weapon);
// MW2's spread-class pellets: shotCount bullets of the weapon's spread from one
// seed, 0 for a gun that fires one bullet, and each one's damage by distance.
int BG_WeaponPellets(int weapon);
int BG_PelletDamage(int weapon, float distance);
#define BG_DRAW_FIRST 1	// weaponDraw: the slot's first deployment
#define BG_DRAW_ALT 2	// weaponDraw: MW2's RAISING_ALTSWITCH, the raise after an alt switch
// MW2's UAV (killstreakTable.csv, maps/mp/killstreaks/_uav.gsc) and the
// compass that shows it (iw4mp's compass dvars, their registered defaults).
#define BG_UAV_KILLS 3	// kills in one life that earn it
#define BG_UAV_TIME 30000	// level.radarViewTime: how long it stays up
#define BG_RADAR_UPDATE_TIME 4000	// compassRadarUpdateTime: one UAV's sweep
#define BG_FAST_RADAR_UPDATE_TIME 2000	// compassFastRadarUpdateTime: two or more
#define BG_RADAR_PING_FADE_TIME 4000	// compassRadarPingFadeTime: a ping is shown this long
#define BG_COMPASS_MAX_RANGE 2500	// compassMaxRange, in world units; compassClampIcons 1 holds the rest at the edge
// MW2's compass sound ping (cgame CG_FireWeapon 0x59d8d9-0x59d915, recorded by
// 0x57fe60, drawn at 0x4fe381): an enemy's shot from a gun that is neither
// silenced nor a grenade shows them where they fired, held at full strength
// and then faded over compassSoundPingFadeTime (2.0 s, registered at 0x4e9bc2).
// MW2 holds it for the fire sound's playing length, 1500 when it is not found
// (0x59d901); cgame cannot read a sound's length, so that fallback is the hold.
#define BG_SOUND_PING_HOLD 1500
#define BG_SOUND_PING_FADE_TIME 2000
qboolean BG_OffhandOut(const playerState_t *ps);
qboolean BG_OffhandPreparesAtOnce( int weapon );
qboolean BG_QuickActionInPlace( int weapon );
qboolean BG_OffhandHeldToThrow( int weapon );
qboolean BG_OffhandCancelable( int weapon );
qboolean BG_OffhandCooks( int weapon );
int BG_OffhandFuseTime( int weapon );
void BG_OffhandLaunchVelocity( int weapon, const vec3_t forward, vec3_t velocity );
int BG_QuickActionWeapon( const playerState_t *ps, int action );
#define BG_EMPTY_HAND_OFFHAND_TIME 100	// MW2's OffHandInit with no gun in hand
void BG_GetSlot(const playerState_t *ps, int slot, bg_weaponInstance_t *gun);
void BG_PutSlot(playerState_t *ps, int slot, const bg_weaponInstance_t *gun);
void BG_SelectSlot(playerState_t *ps, int slot);
void BG_BeginWeaponDraw(playerState_t *ps);
// MW2's use/reload (+usereload). It is never a reload press in pmove (pmove
// reads it only to hold for a floor gun): the server's use code turns a press
// with nothing to use into PWF_USE_RELOAD on playerState_t.weapFlags, and the
// weapon takes the bit as a reload press.
#define PWF_USE_RELOAD 1
#define BG_SPRINT_FORWARD_MINIMUM 105	// MW2's player_sprintForwardMinimum, of 127
#define PWF_SPRINT_RELEASE 2	// the sprint button is still down from the sprint it started or ended
#define PWF_KILLSTREAK_SERVED 4	// the killstreak toggle last served: the trigger came out for it
// MW2's sprint time (player_sprintTime 4s; a start needs player_sprintMinTime
// 1s left). Used while sprinting, refilled 1:1 while not (playerState_t.sprintUsed).
#define BG_SPRINT_TIME        4000
#define BG_SPRINT_MIN_TIME    1000
// MW2's jump and landing (Jump_Check, Jump_ApplySlowdown, PM_CrashLand).
#define BG_JUMP_WAIT          500	// no jump within this long of the last
#define BG_JUMP_LAND_TIME     1800	// the slowdown after an ordinary landing
#define BG_JUMP_LAND_HIGH     1200	// ... after landing a step above the take-off
#define BG_JUMP_LAND_STEP     18.0f
#define BG_HARD_LAND_HEIGHT   12.0f	// a fall this high keeps 0.67 of the velocity
#define BG_JUMP_HEIGHT        39.0f	// MW2's jump_height: how high a jump goes
// MW2's walk (PM_CmdScale_Walk 0x572DB0, PM_WalkMove 0x573240), at IW4x's exe
// defaults (owner's choice; the retail playlist set strafe and back to 1).
// Each is a per-command factor, never written into ps.speed or the command.
#define BG_PLAYER_SPEED       190	// g_speed (0x5E380A): its default and the tests' base speed
#define BG_SPRINT_SPEED_SCALE  1.5f	// player_sprintSpeedScale (0x7ADCA0), the walk only
#define BG_SPRINT_STRAFE_SCALE 0.667f	// player_sprintStrafeSpeedScale (0x7ADCEC): rightmove, truncated
#define BG_ADS_SPEED_SCALE     0.4f	// PMF_WALKING's walk and air scale (0x572EE6, 0x56F470)
#define BG_CROUCH_SPEED_SCALE  0.65f	// PM_CmdScaleForStance's crouch, a multiply (0x572C50)
#define BG_STRAFE_SPEED_SCALE  0.8f	// player_strafeSpeedScale (0x7ADCFC)
#define BG_BACK_SPEED_SCALE    0.7f	// player_backSpeedScale (0x7ADC44)
// MW2's player_view_pitch_up and _down (0x7AB3B4, 0x7A9F90; PM clamp 0x5718D0),
// exe defaults (owner's choice; the retail playlist set 70). Constants, not
// cvars, so prediction and the server always agree.
#define BG_VIEW_PITCH_UP      85
#define BG_VIEW_PITCH_DOWN    85
#define BG_PITCH_SHORT(deg)   ( (deg) * 65536 / 360 )	// whole short units, never past the limit
// MW2's class speed (_weapons.gsc updateMoveSpeedScale): set at spawn from the
// class's primary, by its weaponClass, and multiplied last into the walk only
// (ps->moveSpeedScaleMultiplier, 0x572FD0). playerState_t.moveClass carries it.
typedef enum {
	MOVECLASS_NONE,			// the script's default case: 1.0
	MOVECLASS_RIFLE,		// 0.95
	MOVECLASS_SMG,			// 1.0
	MOVECLASS_PISTOL,		// 1.0
	MOVECLASS_MG,			// 0.875
	MOVECLASS_SPREAD,		// 0.95 (shotguns)
	MOVECLASS_ROCKETLAUNCHER,	// 0.80
	MOVECLASS_SNIPER,		// 1.0
	MOVECLASS_RIOTSHIELD,		// 0.8 (a riot shield primary, tested before the class)
	MOVECLASS_COUNT
} moveClass_t;
float BG_MoveClassFactor( int moveClass );
float BG_MoveClassScale( const playerState_t *ps );
int BG_WeaponMoveClass( int weapon );
float BG_WeaponAdsMoveScale( int weapon );
float BG_JumpLandFactor( int jumpLand );
float BG_JumpVelocity( const playerState_t *ps );
// MW2's view bob: the eye's offset on z and along the view's right (BG_GetPlayerViewOrigin).
float BG_ViewBobCycle( const playerState_t *ps );
float BG_VerticalViewBob( const playerState_t *ps, float cycle, float speed, float adsFrac );
float BG_HorizontalViewBob( const playerState_t *ps, float cycle, float speed, float adsFrac );
// MW2's view kick: a shot assigns an angular velocity (BG_WeaponFireRecoil's
// view part), cgame's CG_KickAngles runs it out, and the client adds the
// angles into the command's aim. Pmove never reads it. adsFrac is the aim
// clock (MW2's fWeaponPosFrac): the ADS kick needs it at exactly 1, the ADS
// centre speed above one half.
#define BG_KICK_NO_DEF_CENTER_SPEED	2400.0f	// MW2's weapon 0, no weapon def (596870)
#define BG_KICK_MAX_ANGLE			10.0f	// each axis clamps at +-10 (0x6fd688, 0x6e24f0)
#define BG_KICK_RETURN_SCALE		0.06f	// a step back toward 0 is cut to this (0x6f1dd8)
#define BG_KICK_STEP_MSEC			5		// CG_KickAngles' substep
int BG_ViewmodelWeapon( const playerState_t *ps );
float BG_WeaponViewKickCenterSpeed( int weapon, float adsFrac );
qboolean BG_WeaponFireRecoil( const playerState_t *ps, float adsFrac, vec3_t kickAVel, unsigned int *holdrand );
typedef struct {
	int buttons;		// the last command's buttons, as the use code saw them
	qboolean done;		// this press has used something (MW2's useButtonDone)
	qboolean hold;		// this press began over a hold-to-use usable (useHoldEntity)
} bg_activate_t;
void BG_UpdateActivate( playerState_t *ps, bg_activate_t *use, int buttons,
	qboolean holdUsable, qboolean usedAtOnce );
qboolean BG_WeaponItem(const entityState_t *item, bg_weaponInstance_t *gun);
int BG_WeaponItemAltFrame(const bg_weaponInstance_t *gun);
void BG_WeaponItemAlt(int frame, bg_weaponInstance_t *gun);
int BG_MatchingWeaponSlot(const playerState_t *ps, const bg_weaponInstance_t *gun);
float BG_WeaponPickupDistance(const playerState_t *ps, const entityState_t *item, int time);
qboolean BG_UpdateWeaponPickup(playerState_t *ps, const usercmd_t *cmd,
	const entityState_t *item, int msec);

#define BG_CUSTOM_CLASS_COUNT 5
#define BG_CLASS_NAME_SIZE    24
#define BG_CLASS_STRING_SIZE 160
#define BG_PERK_SLOTS         3

// Stable saved-class IDs. Nonzero IDs occupy bit (ID - 1) in ps->perks.
typedef enum {
	PERK_NONE, PERK_QUICK_RELOAD, PERK_NO_FALL_DAMAGE, PERK_SILENT_FOOTSTEPS,
	PERK_SECOND_NATURE, PERK_LIGHTWEIGHT, BG_PERK_COUNT
} bg_perk_t;
typedef struct {
	const char *name, *description;
	int reloadPercent, attachmentLimit, movementPercent;	// movementPercent: of the class speed
} bg_perkDef_t;
const bg_perkDef_t *BG_Perk( int perk );
qboolean BG_HasPerk( const playerState_t *ps, int perk );
int BG_AttachmentLimit( const playerState_t *ps );
int BG_AttachmentCount( int mask );

// A class has two guns, separate equipment and three distinct optional perks.
// Melee is always a button action, never one of the two gun slots.
typedef struct {
	char        name[BG_CLASS_NAME_SIZE];
	int			slot[SLOT_COUNT];	// weapon_t in each weapon slot
	int			lethal;				// weapon_t thrown by BUTTON_LETHAL
	int			lethalCount;
	int			tactical;			// weapon_t thrown by BUTTON_TACTICAL
	int			tacticalCount;
	int         perk[BG_PERK_SLOTS];
	int         attachments[SLOT_COUNT]; // -1 = legacy cg_sight preference
} bg_class_t;
qboolean BG_ClassPerksValid( const bg_class_t *loadout );

int					BG_ClassCount( void );
const bg_class_t	*BG_Class( int index );
void				BG_ApplyLoadout( playerState_t *ps, int classIndex );
void                BG_ApplyClass( playerState_t *ps, const bg_class_t *loadout );
int                 BG_ClassEquipmentCount( int weapon );
qboolean            BG_ValidateClass( const bg_class_t *loadout );
qboolean            BG_ParseClass( const char *text, bg_class_t *loadout );
qboolean            BG_SerializeClass( const bg_class_t *loadout, char *text, int size );
int                 BG_ClassWeaponCount( void );
int                 BG_ClassWeapon( int index );
const char          *BG_ClassWeaponName( int weapon );
void				BG_RebuildWeaponMask( playerState_t *ps );
void				BG_SetSlotWeapon( playerState_t *ps, int slot, int weapon );
int                 BG_ToggleSlotWeapon( const playerState_t *ps, int selection );



// reward sounds (stored in ps->persistant[PERS_PLAYEREVENTS])
#define	PLAYEREVENT_DENIEDREWARD		0x0001
#define	PLAYEREVENT_GAUNTLETREWARD		0x0002
#define PLAYEREVENT_HOLYSHIT			0x0004

// entityState_t->event values
// entity events are for effects that take place relative
// to an existing entities origin.  Very network efficient.

// two bits at the top of the entityState->event field
// will be incremented with each change in the event so
// that an identical event started twice in a row can
// be distinguished.  And off the value with ~EV_EVENT_BITS
// to retrieve the actual event number
#define	EV_EVENT_BIT1		0x00000100
#define	EV_EVENT_BIT2		0x00000200
#define	EV_EVENT_BITS		(EV_EVENT_BIT1|EV_EVENT_BIT2)

#define	EVENT_VALID_MSEC	300

typedef enum {
	EV_NONE,

	EV_FOOTSTEP,
	EV_FOOTSTEP_METAL,
	EV_FOOTSPLASH,
	EV_FOOTWADE,
	EV_SWIM,

	EV_STEP_4,
	EV_STEP_8,
	EV_STEP_12,
	EV_STEP_16,

	EV_FALL_SHORT,
	EV_FALL_MEDIUM,
	EV_FALL_FAR,

	EV_JUMP_PAD,			// boing sound at origin, jump sound on player

	EV_JUMP,
	EV_WATER_TOUCH,	// foot touches
	EV_WATER_LEAVE,	// foot leaves
	EV_WATER_UNDER,	// head touches
	EV_WATER_CLEAR,	// head leaves

	EV_ITEM_PICKUP,			// normal item pickups are predictable
	EV_GLOBAL_ITEM_PICKUP,	// powerup / team sounds are broadcast to everyone

	EV_NOAMMO,
	EV_CHANGE_WEAPON,
	EV_FIRE_WEAPON,
	EV_RELOAD,
	EV_WEAPON_NOTETRACK,	// mag seated or round chambered: cancel window open

	EV_USE_ITEM0,
	EV_USE_ITEM1,
	EV_USE_ITEM2,
	EV_USE_ITEM3,
	EV_USE_ITEM4,
	EV_USE_ITEM5,
	EV_USE_ITEM6,
	EV_USE_ITEM7,
	EV_USE_ITEM8,
	EV_USE_ITEM9,
	EV_USE_ITEM10,
	EV_USE_ITEM11,
	EV_USE_ITEM12,
	EV_USE_ITEM13,
	EV_USE_ITEM14,
	EV_USE_ITEM15,

	EV_ITEM_RESPAWN,
	EV_ITEM_POP,
	EV_PLAYER_TELEPORT_IN,
	EV_PLAYER_TELEPORT_OUT,

	EV_GRENADE_BOUNCE,		// eventParm will be the soundindex

	EV_GENERAL_SOUND,
	EV_GLOBAL_SOUND,		// no attenuation
	EV_GLOBAL_TEAM_SOUND,

	EV_BULLET_HIT_FLESH,
	EV_BULLET_HIT_WALL,

	EV_MISSILE_HIT,
	EV_MISSILE_MISS,
	EV_MISSILE_MISS_METAL,
	EV_RAILTRAIL,
	EV_SHOTGUN,
	EV_BULLET,				// otherEntity is the shooter

	EV_PAIN,
	EV_DEATH1,
	EV_DEATH2,
	EV_DEATH3,
	EV_OBITUARY,

	EV_POWERUP_QUAD,
	EV_POWERUP_BATTLESUIT,
	EV_POWERUP_REGEN,

	EV_GIB_PLAYER,			// gib a previously living player
	EV_SCOREPLUM,			// score plum

//#ifdef MISSIONPACK
	EV_PROXIMITY_MINE_STICK,
	EV_PROXIMITY_MINE_TRIGGER,
	EV_KAMIKAZE,			// kamikaze explodes
	EV_OBELISKEXPLODE,		// obelisk explodes
	EV_OBELISKPAIN,			// obelisk is in pain
	EV_INVUL_IMPACT,		// invulnerability sphere impact
	EV_JUICED,				// invulnerability juiced effect
	EV_LIGHTNINGBOLT,		// lightning bolt bounced of invulnerability sphere
//#endif

	EV_DEBUG_LINE,
	EV_STOPLOOPINGSOUND,
	EV_TAUNT,
	EV_TAUNT_YES,
	EV_TAUNT_NO,
	EV_TAUNT_FOLLOWME,
	EV_TAUNT_GETFLAG,
	EV_TAUNT_GUARDBASE,
	EV_TAUNT_PATROL,

	EV_STEP_DOWN_4,			// MW2's step down (PM_StepSlideMove), smoothed like a step up
	EV_STEP_DOWN_8,

	EV_GRENADE_SUICIDE,		// MW2's 0x5d: a cooked grenade ran out in its holder's hand

	// MW2's offhand press (CheckForOffHand 579030, OffHandInit 578e00,
	// OffHandPrepare 577800). Each parm is the offhand but EV_RESET_ADS's (0).
	EV_SWITCH_OFFHAND,		// MW2's 0x32, singleClient: the HUD's equipped offhand
	EV_WEAPON_SWITCH_STARTED,	// MW2's 0x1c, serverOnly: the server's switch-start notice
	EV_RESET_ADS,			// MW2's 0x11, singleClient: the client leaves a toggled aim
	EV_PREP_OFFHAND,		// MW2's 0x30: the offhand's pullback sound
	EV_USE_KILLSTREAK		// the killstreak trigger came out: MW2's weapon_change to it, which uses the streak

} entity_event_t;

// MW2's event routing (56eab0, the lists at 0x739b38 and 0x739af0): whether
// an event the player raised is put on the player's entity for others.
qboolean BG_EventReachesOthers( int event );
void BG_AddEntityEvent( entityState_t *es, int event, int eventParm );


typedef enum {
	GTS_RED_CAPTURE,
	GTS_BLUE_CAPTURE,
	GTS_RED_RETURN,
	GTS_BLUE_RETURN,
	GTS_RED_TAKEN,
	GTS_BLUE_TAKEN,
	GTS_REDOBELISK_ATTACKED,
	GTS_BLUEOBELISK_ATTACKED,
	GTS_REDTEAM_SCORED,
	GTS_BLUETEAM_SCORED,
	GTS_REDTEAM_TOOK_LEAD,
	GTS_BLUETEAM_TOOK_LEAD,
	GTS_TEAMS_ARE_TIED,
	GTS_KAMIKAZE
} global_team_sound_t;

// animations
typedef enum {
	BOTH_DEATH1,
	BOTH_DEAD1,
	BOTH_DEATH2,
	BOTH_DEAD2,
	BOTH_DEATH3,
	BOTH_DEAD3,

	TORSO_GESTURE,

	TORSO_ATTACK,
	TORSO_ATTACK2,

	TORSO_DROP,
	TORSO_RAISE,

	TORSO_STAND,
	TORSO_STAND2,

	LEGS_WALKCR,
	LEGS_WALK,
	LEGS_RUN,
	LEGS_BACK,
	LEGS_SWIM,

	LEGS_JUMP,
	LEGS_LAND,

	LEGS_JUMPB,
	LEGS_LANDB,

	LEGS_IDLE,
	LEGS_IDLECR,

	LEGS_TURN,

	TORSO_GETFLAG,
	TORSO_GUARDBASE,
	TORSO_PATROL,
	TORSO_FOLLOWME,
	TORSO_AFFIRMATIVE,
	TORSO_NEGATIVE,

	MAX_ANIMATIONS,

	LEGS_BACKCR,
	LEGS_BACKWALK,
	FLAG_RUN,
	FLAG_STAND,
	FLAG_STAND2RUN,

	MAX_TOTALANIMATIONS
} animNumber_t;


typedef struct animation_s {
	int		firstFrame;
	int		numFrames;
	int		loopFrames;			// 0 to numFrames
	int		frameLerp;			// msec between frames
	int		initialLerp;		// msec to get to first frame
	int		reversed;			// true if animation is reversed
	int		flipflop;			// true if animation should flipflop back to base
} animation_t;


// flip the togglebit every time an animation
// changes so a restart of the same anim can be detected
#define	ANIM_TOGGLEBIT		128


typedef enum {
	TEAM_FREE,
	TEAM_RED,
	TEAM_BLUE,
	TEAM_SPECTATOR,

	TEAM_NUM_TEAMS
} team_t;

// Time between location updates
#define TEAM_LOCATION_UPDATE_TIME		1000

// How many players on the overlay
#define TEAM_MAXOVERLAY		32

//team task
typedef enum {
	TEAMTASK_NONE,
	TEAMTASK_OFFENSE, 
	TEAMTASK_DEFENSE,
	TEAMTASK_PATROL,
	TEAMTASK_FOLLOW,
	TEAMTASK_RETRIEVE,
	TEAMTASK_ESCORT,
	TEAMTASK_CAMP
} teamtask_t;

// means of death
typedef enum {
	MOD_UNKNOWN,
	MOD_SHOTGUN,
	MOD_GAUNTLET,
	MOD_MACHINEGUN,
	MOD_GRENADE,
	MOD_GRENADE_SPLASH,
	MOD_ROCKET,
	MOD_ROCKET_SPLASH,
	MOD_PLASMA,
	MOD_PLASMA_SPLASH,
	MOD_RAILGUN,
	MOD_LIGHTNING,
	MOD_BFG,
	MOD_BFG_SPLASH,
	MOD_WATER,
	MOD_SLIME,
	MOD_LAVA,
	MOD_CRUSH,
	MOD_TELEFRAG,
	MOD_FALLING,
	MOD_SUICIDE,
	MOD_TARGET_LASER,
	MOD_TRIGGER_HURT,
#ifdef MISSIONPACK
	MOD_NAIL,
	MOD_CHAINGUN,
	MOD_PROXIMITY_MINE,
	MOD_KAMIKAZE,
	MOD_JUICED,
#endif
	MOD_GRAPPLE,
	MOD_M40A3
} meansOfDeath_t;


//---------------------------------------------------------

// gitem_t->type
typedef enum {
	IT_BAD,
	IT_WEAPON,				// EFX: rotate + upscale + minlight
	IT_AMMO,				// EFX: rotate
	IT_ARMOR,				// EFX: rotate + minlight
	IT_HEALTH,				// EFX: static external sphere + rotating internal
	IT_POWERUP,				// instant on, timer based
							// EFX: rotate + external ring that rotates
	IT_HOLDABLE,			// single use, holdable item
							// EFX: rotate + bob
	IT_PERSISTANT_POWERUP,
	IT_TEAM
} itemType_t;

#define MAX_ITEM_MODELS 4

typedef struct gitem_s {
	char		*classname;	// spawning name
	char		*pickup_sound;
	char		*world_model[MAX_ITEM_MODELS];

	char		*icon;
	char		*pickup_name;	// for printing on pickup

	int			quantity;		// for ammo how much, or duration of powerup
	itemType_t  giType;			// IT_* flags

	int			giTag;

	char		*precaches;		// string of all models and images this item will use
	char		*sounds;		// string of all sounds this item will use
} gitem_t;

// included in both the game dll and the client
extern	gitem_t	bg_itemlist[];
extern	int		bg_numItems;

gitem_t	*BG_FindItem( const char *pickupName );
gitem_t	*BG_FindItemForWeapon( weapon_t weapon );
gitem_t	*BG_FindItemForPowerup( powerup_t pw );
gitem_t	*BG_FindItemForHoldable( holdable_t pw );
#define	ITEM_INDEX(x) ((x)-bg_itemlist)

qboolean	BG_CanItemBeGrabbed( int gametype, const entityState_t *ent, const playerState_t *ps );


// g_dmflags->integer flags
#define	DF_NO_FALLING			8
#define DF_FIXED_FOV			16
#define	DF_NO_FOOTSTEPS			32

// content masks
#define	MASK_ALL				(-1)
#define	MASK_SOLID				(CONTENTS_SOLID)
#define	MASK_PLAYERSOLID		(CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_BODY)
#define	MASK_DEADSOLID			(CONTENTS_SOLID|CONTENTS_PLAYERCLIP)
#define	MASK_WATER				(CONTENTS_WATER|CONTENTS_LAVA|CONTENTS_SLIME)
#define	MASK_OPAQUE				(CONTENTS_SOLID|CONTENTS_SLIME|CONTENTS_LAVA)
#define	MASK_SHOT				(CONTENTS_SOLID|CONTENTS_BODY|CONTENTS_CORPSE)


//
// entityState_t->eType
//
typedef enum {
	ET_GENERAL,
	ET_PLAYER,
	ET_ITEM,
	ET_MISSILE,
	ET_MOVER,
	ET_BEAM,
	ET_PORTAL,
	ET_SPEAKER,
	ET_PUSH_TRIGGER,
	ET_TELEPORT_TRIGGER,
	ET_INVISIBLE,
	ET_GRAPPLE,				// grapple hooked on wall
	ET_TEAM,

	ET_EVENTS				// any of the EV_* events can be added freestanding
							// by setting eType to ET_EVENTS + eventNum
							// this avoids having to set eFlags and eventNum
} entityType_t;



void	BG_EvaluateTrajectory( const trajectory_t *tr, int atTime, vec3_t result );
void	BG_EvaluateTrajectoryDelta( const trajectory_t *tr, int atTime, vec3_t result );

void	BG_AddPredictableEventToPlayerstate( int newEvent, int eventParm, playerState_t *ps );

void	BG_TouchJumpPad( playerState_t *ps, entityState_t *jumppad );

void	BG_PlayerStateToEntityState( playerState_t *ps, entityState_t *s, qboolean snap );
void	BG_PlayerStateToEntityStateExtraPolate( playerState_t *ps, entityState_t *s, int time, qboolean snap );

qboolean	BG_PlayerTouchesItem( playerState_t *ps, entityState_t *item, int atTime );

// MW2's client-side bullets (sv_clientSideBullets): the server's damage
// traces and every client's drawn bullets are built by these, from the same
// seed (the shot command's time) and the same shapes. bg_misc.c.
#define	BG_BULLET_RANGE		(8192*16)
int		BG_BulletSeed( int gameTime );
float	BG_BulletRandom( int *seed );
float	BG_BulletCrandom( int *seed );
void	BG_ShotMuzzle( const vec3_t base, int viewheight, const vec3_t forward, vec3_t muzzle );
void	BG_PlayerShotBase( const playerState_t *ps, vec3_t base );
void	BG_BulletEnd( int *seed, float spread, const vec3_t muzzle, const vec3_t forward,
			const vec3_t right, const vec3_t up, vec3_t end );
void	BG_ShotgunAxes( const vec3_t aim, vec3_t forward, vec3_t right, vec3_t up );
void	BG_ShotgunPelletEnd( int *seed, const vec3_t origin, const vec3_t forward,
			const vec3_t right, const vec3_t up, vec3_t end );


#define ARENAS_PER_TIER		4
#define MAX_ARENAS			1024
#define	MAX_ARENAS_TEXT		8192

#define MAX_BOTS			1024
#define MAX_BOTS_TEXT		8192


// Kamikaze

// 1st shockwave times
#define KAMI_SHOCKWAVE_STARTTIME		0
#define KAMI_SHOCKWAVEFADE_STARTTIME	1500
#define KAMI_SHOCKWAVE_ENDTIME			2000
// explosion/implosion times
#define KAMI_EXPLODE_STARTTIME			250
#define KAMI_IMPLODE_STARTTIME			2000
#define KAMI_IMPLODE_ENDTIME			2250
// 2nd shockwave times
#define KAMI_SHOCKWAVE2_STARTTIME		2000
#define KAMI_SHOCKWAVE2FADE_STARTTIME	2500
#define KAMI_SHOCKWAVE2_ENDTIME			3000
// radius of the models without scaling
#define KAMI_SHOCKWAVEMODEL_RADIUS		88
#define KAMI_BOOMSPHEREMODEL_RADIUS		72
// maximum radius of the models during the effect
#define KAMI_SHOCKWAVE_MAXRADIUS		1320
#define KAMI_BOOMSPHERE_MAXRADIUS		720
#define KAMI_SHOCKWAVE2_MAXRADIUS		704
