/* Class catalog and wire/storage validation shared by UI, cgame and server.
 * No engine syscalls or gameplay clocks belong in this file. */
#include "../qcommon/q_shared.h"
#include "bg_public.h"

// q3asm separates byte literals from word data. A static initializer mixing
// inline char arrays with integers does NOT preserve struct layout in a QVM.
// Keep the source table word-only and populate the editable definitions at run
// time, identically in native modules and VMs.
static const struct { const char *name; int primary, secondary; } presets[] = {
	{ "Assault", WP_MACHINEGUN, WP_SHOTGUN },
	{ "Sniper", WP_M40A3, WP_MACHINEGUN },
	{ "Demolition", WP_ROCKET_LAUNCHER, WP_SHOTGUN },
	{ "Support", WP_PLASMAGUN, WP_LIGHTNING }
};
static bg_class_t bg_classes[ARRAY_LEN(presets)];
static qboolean classesReady;

static const struct { int mask; const char *name; } attachmentTypes[] = {
	{ 0, "None" }, { BG_ATTACHMENT_REDDOT, "Red dot" }
};
int BG_AttachmentTypeCount( void ) { return ARRAY_LEN(attachmentTypes); }
int BG_AttachmentType( int index ) {
	return index >= 0 && index < BG_AttachmentTypeCount() ? attachmentTypes[index].mask : 0;
}
const char *BG_AttachmentName( int index ) {
	return index >= 0 && index < BG_AttachmentTypeCount() ? attachmentTypes[index].name : "None";
}

static const bg_perkDef_t perks[BG_PERK_COUNT] = {
	{ "None", "Leave this perk slot empty.", 100, 1, 100 },
	{ "Quick Reload", "Half reload time. Swap and bolt speeds are unchanged.", 50, 1, 100 },
	{ "9 Lives", "Landing cannot damage you. Other damage is unchanged.", 100, 1, 100 },
	{ "Inaudible", "Silent running and wading footsteps for everyone.", 100, 1, 100 },
	{ "Second Nature", "Two attachments per gun. Only red dots are available so far.", 100, 2, 100 },
	// MW2's Lightweight (_perkfunctions.gsc setLightWeight): moveSpeedScaler
	// 1.07, which multiplies the class speed, so it is the walk's alone.
	{ "Lightweight", "7% faster on the ground. Air, ladder, swim and weapon speeds unchanged.", 100, 1, 107 }
};
typedef char perkMaskFitsWire[(BG_PERK_COUNT - 1 <= PS_PERK_BITS) ? 1 : -1];

const bg_perkDef_t *BG_Perk( int perk ) {
	if ( perk < PERK_NONE || perk >= BG_PERK_COUNT ) perk = PERK_NONE;
	return &perks[perk];
}
qboolean BG_HasPerk( const playerState_t *ps, int perk ) {
	return ps && perk > PERK_NONE && perk < BG_PERK_COUNT &&
		(ps->perks & (1 << (perk - 1))) != 0;
}
// MW2's class speeds, by the primary's weaponClass (_weapons.gsc
// updateMoveSpeedScale). Word-only, indexed by moveClass_t.
static const float moveClassFactors[MOVECLASS_COUNT] = {
	1.0f,	// MOVECLASS_NONE: the switch's default
	0.95f,	// rifle
	1.0f,	// smg
	1.0f,	// pistol
	0.875f,	// mg
	0.95f,	// spread
	0.80f,	// rocketlauncher
	1.0f,	// sniper
	0.8f	// a riot shield primary
};
typedef char moveClassFitsWire[(MOVECLASS_COUNT <= (1 << PS_MOVECLASS_BITS)) ? 1 : -1];
float BG_MoveClassFactor( int moveClass ) {
	if ( moveClass < 0 || moveClass >= MOVECLASS_COUNT ) moveClass = MOVECLASS_NONE;
	return moveClassFactors[moveClass];
}
// MW2's ps->moveSpeedScaleMultiplier (setMoveSpeedScale(classScale *
// moveSpeedScaler)): the spawn class's factor times Lightweight's. The walk
// multiplies it last (PM_CmdScale_Walk 0x572FD0); the air, a ladder, swimming
// and the bob's rate never read it. Derived for each command, never
// accumulated into ps->speed or velocity.
float BG_MoveClassScale( const playerState_t *ps ) {
	int perk;
	float scale;
	if ( !ps ) return 1.0f;
	scale = BG_MoveClassFactor( ps->moveClass );
	for ( perk = 1; perk < BG_PERK_COUNT; perk++ )
		if ( BG_HasPerk(ps, perk) ) scale *= perks[perk].movementPercent / 100.0f;
	return scale;
}
qboolean BG_ClassPerksValid( const bg_class_t *loadout ) {
	int slot, seen = 0;
	if ( !loadout ) return qfalse;
	for ( slot = 0; slot < BG_PERK_SLOTS; slot++ ) {
		int perk = loadout->perk[slot], bit;
		if ( perk < PERK_NONE || perk >= BG_PERK_COUNT ) return qfalse;
		if ( perk == PERK_NONE ) continue;
		bit = 1 << (perk - 1);
		if ( seen & bit ) return qfalse;
		seen |= bit;
	}
	return qtrue;
}
int BG_AttachmentLimit( const playerState_t *ps ) {
	int perk, limit = perks[PERK_NONE].attachmentLimit;
	for ( perk = 1; perk < BG_PERK_COUNT; perk++ )
		if ( BG_HasPerk(ps, perk) && perks[perk].attachmentLimit > limit ) limit = perks[perk].attachmentLimit;
	return limit;
}
int BG_AttachmentCount( int mask ) {
	unsigned int bits = (unsigned int)mask;
	int count = 0;
	while ( bits ) { count++; bits &= bits - 1; }
	return count;
}

// Retiming is a pure lookup, never a clock accumulator. Apply it equally to
// authored notes and segment endpoints; positive timestamps round up to 1 ms.
// Fire/bolt, swap, equipment and pickup clocks never use this rule.
int BG_ReloadTime( const playerState_t *ps, int authoredTime ) {
	int perk, time = authoredTime;
	if ( time <= 0 ) return 0;
	for ( perk = 1; perk < BG_PERK_COUNT; perk++ )
		if ( BG_HasPerk(ps, perk) ) time = (time * perks[perk].reloadPercent + 99) / 100;
	return time;
}

// Quick actions reuse weapon states, but never become either inventory slot.
// Gameplay and presentation share this classification, including recovery.
// MW2's weapClass 9, the throwing knife: OffHandInit calls OffHandPrepare in
// the same call (578eec), so the knife is in hand on the press and the gun is
// never lowered first. Every other offhand pays the gun's quickDropTime.
qboolean BG_OffhandPreparesAtOnce( int weapon ) {
	return weapon == WP_THROWING_KNIFE;
}

// MW2's knife is no weapon change: MeleeInit writes the melee state straight
// over the gun's, and MeleeEnd writes the gun's quick raise straight over the
// knife's. So the knife comes out on its press and puts nothing down after it;
// its draw is meleeDelay, and it ends in the hit (MeleeFire) on that same think.
qboolean BG_QuickActionInPlace( int weapon ) {
	return weapon == WP_KNIFE;
}

// MW2's offhand weapon data, from the IW4 dump's weapons/mp files
// (frag_grenade_mp, flash_grenade_mp, semtex_mp, throwingknife_mp, flare_mp).
// Word-only, as q3asm needs. The prepare, holdFireTime, is the equipment's row
// in bg_pmove.c's raise table. holdButtonToThrow is 0 and timedDetonation 1
// for all five, so none has a row of its own: every one waits in HOLD for its
// release, and every fuse is timed.
static const struct {
	int weapon;
	int fuseTime;			// fuseTime: written into grenadeTimeLeft at HOLD entry (OffHandHold 577958)
	int cookOffHold;		// the fuse runs down in hand from HOLD entry (PM_UpdateGrenadeThrow 577c1e)
	int cancelable;			// offhandHoldIsCancelable: Y ends it before the throw (59d5f0, 577d70, 578f41)
	int speed, speedUp, speedForward;	// projectileSpeed, projectileSpeedUp, projectileSpeedForward (4c3690)
} offhands[] = {
	{ WP_FRAG,               3500,                  1, 0,  940,    120,  0 },
	{ WP_FLASH,              1500,                  0, 0,  940,    120,  0 },
	{ WP_SEMTEX,             2000,                  0, 1,  940,    120,  0 },
	{ WP_THROWING_KNIFE,     BG_THROWING_KNIFE_LIFE, 0, 1, 1400,     40,  0 },
	{ WP_TACTICAL_INSERTION, 50,                    0, 1,  100, -10000, 50 }	// an invisible flare; Peace plants instead
};
static int BG_OffhandRow( int weapon ) {
	int i;
	for ( i = 0; i < ARRAY_LEN(offhands); i++ )
		if ( offhands[i].weapon == weapon ) return i;
	return -1;
}

// Every offhand (holdButtonToThrow 0): once its prepare is over it waits in
// hand, MW2's HOLD (578f00-578f69), while either offhand button is down on
// this command and the one before, and is thrown on the first think it is not.
qboolean BG_OffhandHeldToThrow( int weapon ) {
	return BG_OffhandRow( weapon ) >= 0;
}

// offhandHoldIsCancelable: the Semtex, the throwing knife and the Tactical
// Insertion. A Y before the throw ends them back to the gun with nothing spent
// (MW2's OffHandEnd). The frag and flash cannot be cancelled: a Y only asks for
// the other slot, which waits for the gun's return (CheckForChangeWeapon).
qboolean BG_OffhandCancelable( int weapon ) {
	int row = BG_OffhandRow( weapon );
	return row >= 0 && offhands[row].cancelable;
}

// cookOffHold: only the frag. Its fuse runs in hand from HOLD entry, so it
// explodes fuseTime after it, whenever it is released.
qboolean BG_OffhandCooks( int weapon ) {
	int row = BG_OffhandRow( weapon );
	return row >= 0 && offhands[row].cookOffHold;
}

int BG_OffhandFuseTime( int weapon ) {
	int row = BG_OffhandRow( weapon );
	return row >= 0 ? offhands[row].fuseTime : 0;
}

// MW2's throw (4c3690): the view's forward at projectileSpeed, projectileSpeedUp
// added straight up, and projectileSpeedForward along the flattened forward.
// The thrower's own velocity along the result is added after the missile's
// trajectory is snapped (4c375f-4c37c3), by the server.
void BG_OffhandLaunchVelocity( int weapon, const vec3_t forward, vec3_t velocity ) {
	int row = BG_OffhandRow( weapon );
	float flat;
	VectorClear( velocity );
	if ( row < 0 ) return;
	VectorScale( forward, offhands[row].speed, velocity );
	velocity[2] += offhands[row].speedUp;
	if ( offhands[row].speedForward ) {
		flat = sqrt( forward[0] * forward[0] + forward[1] * forward[1] );
		if ( flat > 0 ) {
			velocity[0] += forward[0] / flat * offhands[row].speedForward;
			velocity[1] += forward[1] / flat * offhands[row].speedForward;
		}
	}
}

qboolean BG_IsActionWeapon( int weapon ) {
	return weapon == WP_KNIFE || weapon == WP_FRAG ||
		weapon == WP_FLASH || weapon == WP_SEMTEX || weapon == WP_THROWING_KNIFE ||
		weapon == WP_TACTICAL_INSERTION;
}

// The class weapon catalog. moveClass is the MW2 twin's weaponClass (m4_mp
// rifle, spas12_mp spread, rpg_mp rocketlauncher, cheytac_mp sniper); a gun
// with no MW2 twin takes the script's default class (owner's choice).
// adsMoveScale is the twin's adsMoveSpeedScale (spas12_mp 2, the rest 1); its
// moveSpeedScale is 1 on all four, so the hip needs no column.
static const struct { int weapon; const char *name; int attachments; int moveClass; float adsMoveScale; } classWeapons[] = {
	{WP_MACHINEGUN, "Machinegun", BG_ATTACHMENT_REDDOT, MOVECLASS_RIFLE, 1.0f},
	{WP_SHOTGUN, "SPAS-12", BG_ATTACHMENT_REDDOT, MOVECLASS_SPREAD, 2.0f},
	{WP_RAILGUN, "Railgun", BG_ATTACHMENT_REDDOT, MOVECLASS_NONE, 1.0f},
	{WP_ROCKET_LAUNCHER, "Rocket launcher", BG_ATTACHMENT_REDDOT, MOVECLASS_ROCKETLAUNCHER, 1.0f},
	{WP_GRENADE_LAUNCHER, "Grenade launcher", BG_ATTACHMENT_REDDOT, MOVECLASS_NONE, 1.0f},
	{WP_LIGHTNING, "Lightning gun", BG_ATTACHMENT_REDDOT, MOVECLASS_NONE, 1.0f},
	{WP_PLASMAGUN, "Plasma gun", BG_ATTACHMENT_REDDOT, MOVECLASS_NONE, 1.0f},
	{WP_BFG, "BFG", BG_ATTACHMENT_REDDOT, MOVECLASS_NONE, 1.0f},
	{WP_M40A3, "M300 Praetorian", 0, MOVECLASS_SNIPER, 1.0f}
};

int BG_WeaponAttachmentMask(int weapon) {
	int i;
	for (i=0; i<ARRAY_LEN(classWeapons); i++)
		if (classWeapons[i].weapon == weapon) return classWeapons[i].attachments;
	return 0;
}

// The move class a class's primary gives its owner at spawn (BG_ApplyClass).
int BG_WeaponMoveClass( int weapon ) {
	int i;
	for ( i = 0; i < ARRAY_LEN(classWeapons); i++ )
		if ( classWeapons[i].weapon == weapon ) return classWeapons[i].moveClass;
	return MOVECLASS_NONE;
}

// MW2's adsMoveSpeedScale of the gun in hand (PM_CmdScale_Walk 0x572F5C-
// 0x572F99, ps->weapon), read only while aiming. Anything else in hand,
// equipment included, aims at 1 (MW2's offhands have 0, which skips it).
float BG_WeaponAdsMoveScale( int weapon ) {
	int i;
	for ( i = 0; i < ARRAY_LEN(classWeapons); i++ )
		if ( classWeapons[i].weapon == weapon ) return classWeapons[i].adsMoveScale;
	return 1.0f;
}

int BG_ClassCount( void ) { return ARRAY_LEN(bg_classes); }
const bg_class_t *BG_Class( int index ) {
	int i;
	if ( !classesReady ) {
		for ( i = 0; i < BG_ClassCount(); i++ ) {
			Q_strncpyz(bg_classes[i].name, presets[i].name, sizeof(bg_classes[i].name));
			bg_classes[i].slot[0] = presets[i].primary;
			bg_classes[i].slot[1] = presets[i].secondary;
			bg_classes[i].lethal = WP_FRAG;
			bg_classes[i].lethalCount = BG_ClassEquipmentCount(WP_FRAG);
			bg_classes[i].tactical = WP_FLASH;
			bg_classes[i].tacticalCount = BG_ClassEquipmentCount(WP_FLASH);
			bg_classes[i].attachments[0] = bg_classes[i].attachments[1] = -1;
		}
		classesReady = qtrue;
	}
	if ( index < 0 || index >= BG_ClassCount() ) index = 0;
	return &bg_classes[index];
}
int BG_ClassWeaponCount( void ) { return ARRAY_LEN(classWeapons); }
int BG_ClassWeapon( int index ) {
	if ( index < 0 || index >= BG_ClassWeaponCount() ) return WP_NONE;
	return classWeapons[index].weapon;
}
const char *BG_ClassWeaponName( int weapon ) {
	int i;
	for ( i = 0; i < BG_ClassWeaponCount(); i++ )
		if ( classWeapons[i].weapon == weapon ) return classWeapons[i].name;
	return "None";
}
static qboolean BG_IsClassWeapon( int weapon ) {
	int i;
	for ( i = 0; i < BG_ClassWeaponCount(); i++ )
		if ( classWeapons[i].weapon == weapon ) return qtrue;
	return qfalse;
}
static qboolean BG_ClassNameValid( const char *name ) {
	int i;
	qboolean visible = qfalse;
	for ( i = 0; i < BG_CLASS_NAME_SIZE; i++ ) {
		unsigned char c = name[i];
		if ( !c ) return visible;
		// Safe in userinfo, config files and quoted server print messages.
		if ( !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_') ) return qfalse;
		if ( c != ' ' ) visible = qtrue;
	}
	return qfalse;
}
// How many of a piece of equipment a class carries each life: MW2's weapon
// data, startAmmo = maxAmmo (frag_grenade_mp, semtex_mp, throwingknife_mp and
// flare_mp, the Tactical Insertion, 1; flash_grenade_mp 2). It is fixed, as in
// MW2: there is no count to choose.
#define BG_CLASS_EQUIPMENT_LEGACY_MAX 3	// every count a class could save before MW2's counts
int BG_ClassEquipmentCount( int weapon ) {
	if ( weapon == WP_NONE ) return 0;
	return weapon == WP_FLASH ? 2 : 1;
}

qboolean BG_ValidateClass( const bg_class_t *loadout ) {
	int i, limit = perks[PERK_NONE].attachmentLimit;
	if ( !loadout || !BG_ClassNameValid(loadout->name) ) return qfalse;
	if ( !BG_IsClassWeapon(loadout->slot[0]) || !BG_IsClassWeapon(loadout->slot[1]) ||
		loadout->slot[0] == loadout->slot[1] ) return qfalse;
	// MW2's equipment (isValidEquipment): the Tactical Insertion is a lethal
	// beside the frag, Semtex and throwing knife, thrown with the same button.
	if ( loadout->lethal != WP_NONE && loadout->lethal != WP_FRAG && loadout->lethal != WP_SEMTEX &&
		loadout->lethal != WP_THROWING_KNIFE && loadout->lethal != WP_TACTICAL_INSERTION ) return qfalse;
	if ( loadout->tactical != WP_NONE && loadout->tactical != WP_FLASH ) return qfalse;
	if ( loadout->lethalCount != BG_ClassEquipmentCount(loadout->lethal) ||
		loadout->tacticalCount != BG_ClassEquipmentCount(loadout->tactical) ) return qfalse;
	if ( !BG_ClassPerksValid(loadout) ) return qfalse;
	for ( i = 0; i < BG_PERK_SLOTS; i++ )
		if ( perks[loadout->perk[i]].attachmentLimit > limit ) limit = perks[loadout->perk[i]].attachmentLimit;
	for ( i = 0; i < SLOT_COUNT; i++ ) {
		int mask = loadout->attachments[i];
		if ( mask == -1 ) continue; // old saved classes still use cg_sight
		if ( mask < 0 || (mask & ~BG_WeaponAttachmentMask(loadout->slot[i])) ||
			BG_AttachmentCount(mask) > limit ) return qfalse;
	}
	return qtrue;
}

static qboolean BG_ClassNumber( const char **cursor, int *value ) {
	const char *p = *cursor;
	int n = 0, digits = 0;
	while ( *p >= '0' && *p <= '9' && digits < 3 ) {
		n = n * 10 + *p++ - '0';
		digits++;
	}
	if ( !digits || *p != '|' ) return qfalse;
	*value = n;
	*cursor = p + 1;
	return qtrue;
}
qboolean BG_ParseClass( const char *text, bg_class_t *loadout ) {
	bg_class_t result;
	int version, i;
	if ( !text || strlen(text) >= BG_CLASS_STRING_SIZE ) return qfalse;
	memset(&result, 0, sizeof(result));
	if ( !BG_ClassNumber(&text, &version) || (version != 1 && version != 2) ||
		!BG_ClassNumber(&text, &result.slot[0]) || !BG_ClassNumber(&text, &result.slot[1]) ||
		!BG_ClassNumber(&text, &result.lethal) || !BG_ClassNumber(&text, &result.lethalCount) ||
		!BG_ClassNumber(&text, &result.tactical) || !BG_ClassNumber(&text, &result.tacticalCount) ) return qfalse;
	for ( i = 0; i < BG_PERK_SLOTS; i++ )
		if ( !BG_ClassNumber(&text, &result.perk[i]) ) return qfalse;
	for ( i = 0; i < SLOT_COUNT; i++ ) {
		result.attachments[i] = -1;
		if ( version == 2 && !BG_ClassNumber(&text, &result.attachments[i]) ) return qfalse;
		// Version 2 encodes the legacy preference as 256, outside its 8-bit mask.
		if ( result.attachments[i] == BG_ATTACHMENTS_LEGACY ) result.attachments[i] = -1;
	}
	if ( strlen(text) >= sizeof(result.name) ) return qfalse;
	Q_strncpyz(result.name, text, sizeof(result.name));
	// A class saved when counts were chosen (one to three of each) keeps the
	// rest of its loadout: any count that rule took becomes MW2's count, never
	// the class refused or reset. Anything else is still invalid.
	if ( result.lethal != WP_NONE && result.lethalCount >= 1 && result.lethalCount <= BG_CLASS_EQUIPMENT_LEGACY_MAX )
		result.lethalCount = BG_ClassEquipmentCount(result.lethal);
	if ( result.tactical != WP_NONE && result.tacticalCount >= 1 && result.tacticalCount <= BG_CLASS_EQUIPMENT_LEGACY_MAX )
		result.tacticalCount = BG_ClassEquipmentCount(result.tactical);
	if ( !BG_ValidateClass(&result) ) return qfalse;
	*loadout = result; // invalid data never partly overwrites the live choice
	return qtrue;
}
qboolean BG_SerializeClass( const bg_class_t *loadout, char *text, int size ) {
	char result[BG_CLASS_STRING_SIZE];
	if ( !BG_ValidateClass(loadout) ) return qfalse;
	Com_sprintf(result, sizeof(result), "2|%i|%i|%i|%i|%i|%i|%i|%i|%i|%i|%i|%s",
		loadout->slot[0], loadout->slot[1], loadout->lethal, loadout->lethalCount,
		loadout->tactical, loadout->tacticalCount,
		loadout->perk[0], loadout->perk[1], loadout->perk[2],
		loadout->attachments[0] < 0 ? BG_ATTACHMENTS_LEGACY : loadout->attachments[0],
		loadout->attachments[1] < 0 ? BG_ATTACHMENTS_LEGACY : loadout->attachments[1], loadout->name);
	if ( size <= (int)strlen(result) ) return qfalse;
	Q_strncpyz(text, result, size);
	return qtrue;
}

// Weapon IDs need not be adjacent. Cycle the TWO SLOT INDICES modulo the slot
// count, using the latest requested weapon rather than an acknowledged snapshot.
// The slot command a client sends for a slot, carrying the generation it
// last saw for it (a pickup flips it, server side, in the slot's flags).
int BG_SlotCommand(const playerState_t *ps, int slot) {
	if (slot < 0 || slot >= SLOT_COUNT) slot = 0;
	return BG_SLOT_COMMAND + slot + ((ps->slotFlags[slot] & BG_GUN_GENERATION) ? SLOT_COUNT : 0);
}

// False for a slot command sent before the client saw that slot's pickup:
// it names the gun that was dropped. Plain weapon IDs are always current.
qboolean BG_SlotCommandCurrent(const playerState_t *ps, int selection) {
	int slot;
	if (!BG_IS_SLOT_COMMAND(selection)) return qtrue;
	slot = (selection - BG_SLOT_COMMAND) % SLOT_COUNT;
	return ((selection - BG_SLOT_COMMAND) >= SLOT_COUNT) == !!(ps->slotFlags[slot] & BG_GUN_GENERATION);
}

int BG_ToggleSlotWeapon( const playerState_t *ps, int selection ) {
	int slot, next, weapon;
	if (BG_IS_SLOT_COMMAND(selection))
		return BG_SlotCommand(ps, ((selection - BG_SLOT_COMMAND) % SLOT_COUNT + 1) % SLOT_COUNT);
	for ( slot = 0; slot < SLOT_COUNT; slot++ )
		if ( selection == ps->stats[STAT_SLOT_PRIMARY + slot] ) break;
	if ( slot == SLOT_COUNT ) slot = ps->stats[STAT_ACTIVE_SLOT] == SLOT_SECONDARY ? SLOT_SECONDARY : SLOT_PRIMARY;
	next = (slot + 1) % SLOT_COUNT;
	weapon = ps->stats[STAT_SLOT_PRIMARY + next];
	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ||
		!(ps->stats[STAT_WEAPONS] & (1 << weapon)) ) return selection;
	return weapon;
}
