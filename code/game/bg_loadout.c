/* Class catalog and wire/storage validation shared by UI, cgame and server.
 * No engine syscalls or gameplay clocks belong in this file. */
#include "../qcommon/q_shared.h"
#include "bg_public.h"

// q3asm separates byte literals from word data. A static initializer mixing
// inline char arrays with integers does NOT preserve struct layout in a QVM.
// Keep the source table word-only and populate the editable definitions at run
// time, identically in native modules and VMs.
static const struct { const char *name; int primary, secondary, lethal, tactical; } presets[] = {
	{ "Assault", WP_MACHINEGUN, WP_SHOTGUN, 2, 2 },
	{ "Sniper", WP_M40A3, WP_MACHINEGUN, 1, 3 },
	{ "Demolition", WP_ROCKET_LAUNCHER, WP_SHOTGUN, 3, 1 },
	{ "Support", WP_PLASMAGUN, WP_LIGHTNING, 2, 2 }
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
	{ "Expeditious", "10% faster movement. Jump height and weapon speeds unchanged.", 100, 1, 110 }
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
// Derived for each command, never accumulated into ps->speed or velocity.
// Use the same modifier for locomotion and its crouch/water/ladder limits.
float BG_MovementScale( const playerState_t *ps ) {
	int perk;
	float scale = 1.0f;
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

qboolean BG_IsActionWeapon( int weapon ) {
	return weapon == WP_KNIFE || weapon == WP_FRAG ||
		weapon == WP_FLASH || weapon == WP_SEMTEX || weapon == WP_THROWING_KNIFE;
}

static const struct { int weapon; const char *name; int attachments; } classWeapons[] = {
	{WP_MACHINEGUN, "Machinegun", BG_ATTACHMENT_REDDOT}, {WP_SHOTGUN, "SPAS-12", BG_ATTACHMENT_REDDOT},
	{WP_RAILGUN, "Railgun", BG_ATTACHMENT_REDDOT}, {WP_ROCKET_LAUNCHER, "Rocket launcher", BG_ATTACHMENT_REDDOT},
	{WP_GRENADE_LAUNCHER, "Grenade launcher", BG_ATTACHMENT_REDDOT}, {WP_LIGHTNING, "Lightning gun", BG_ATTACHMENT_REDDOT},
	{WP_PLASMAGUN, "Plasma gun", BG_ATTACHMENT_REDDOT}, {WP_BFG, "BFG", BG_ATTACHMENT_REDDOT},
	{WP_M40A3, "M300 Praetorian", 0}
};

int BG_WeaponAttachmentMask(int weapon) {
	int i;
	for (i=0; i<ARRAY_LEN(classWeapons); i++)
		if (classWeapons[i].weapon == weapon) return classWeapons[i].attachments;
	return 0;
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
			bg_classes[i].lethalCount = presets[i].lethal;
			bg_classes[i].tactical = WP_FLASH;
			bg_classes[i].tacticalCount = presets[i].tactical;
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
qboolean BG_ValidateClass( const bg_class_t *loadout ) {
	int i, limit = perks[PERK_NONE].attachmentLimit;
	if ( !loadout || !BG_ClassNameValid(loadout->name) ) return qfalse;
	if ( !BG_IsClassWeapon(loadout->slot[0]) || !BG_IsClassWeapon(loadout->slot[1]) ||
		loadout->slot[0] == loadout->slot[1] ) return qfalse;
	if ( loadout->lethal != WP_NONE && loadout->lethal != WP_FRAG && loadout->lethal != WP_SEMTEX &&
		loadout->lethal != WP_THROWING_KNIFE ) return qfalse;
	if ( loadout->tactical != WP_NONE && loadout->tactical != WP_FLASH ) return qfalse;
	if ( loadout->lethalCount < 0 || loadout->lethalCount > 3 ||
		loadout->tacticalCount < 0 || loadout->tacticalCount > 3 ) return qfalse;
	if ( (loadout->lethal == WP_NONE) != (loadout->lethalCount == 0) ||
		(loadout->tactical == WP_NONE) != (loadout->tacticalCount == 0) ) return qfalse;
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
