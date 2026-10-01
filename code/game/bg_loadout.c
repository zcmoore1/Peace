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
	{ "Scout", WP_RAILGUN, WP_MACHINEGUN, 1, 3 },
	{ "Demolition", WP_ROCKET_LAUNCHER, WP_SHOTGUN, 3, 1 },
	{ "Support", WP_PLASMAGUN, WP_LIGHTNING, 2, 2 }
};
static bg_class_t bg_classes[ARRAY_LEN(presets)];
static qboolean classesReady;

static const struct { int weapon; const char *name; } classWeapons[] = {
	{WP_MACHINEGUN, "Machinegun"}, {WP_SHOTGUN, "SPAS-12"},
	{WP_RAILGUN, "Railgun"}, {WP_ROCKET_LAUNCHER, "Rocket launcher"},
	{WP_GRENADE_LAUNCHER, "Grenade launcher"}, {WP_LIGHTNING, "Lightning gun"},
	{WP_PLASMAGUN, "Plasma gun"}, {WP_BFG, "BFG"}
};

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
	int i;
	if ( !loadout || !BG_ClassNameValid(loadout->name) ) return qfalse;
	if ( !BG_IsClassWeapon(loadout->slot[0]) || !BG_IsClassWeapon(loadout->slot[1]) ||
		loadout->slot[0] == loadout->slot[1] ) return qfalse;
	if ( loadout->lethal != WP_NONE && loadout->lethal != WP_FRAG ) return qfalse;
	if ( loadout->tactical != WP_NONE && loadout->tactical != WP_FLASH ) return qfalse;
	if ( loadout->lethalCount < 0 || loadout->lethalCount > 3 ||
		loadout->tacticalCount < 0 || loadout->tacticalCount > 3 ) return qfalse;
	if ( (loadout->lethal == WP_NONE) != (loadout->lethalCount == 0) ||
		(loadout->tactical == WP_NONE) != (loadout->tacticalCount == 0) ) return qfalse;
	for ( i = 0; i < BG_PERK_SLOTS; i++ ) if ( loadout->perk[i] != 0 ) return qfalse;
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
	if ( !BG_ClassNumber(&text, &version) || version != 1 ||
		!BG_ClassNumber(&text, &result.slot[0]) || !BG_ClassNumber(&text, &result.slot[1]) ||
		!BG_ClassNumber(&text, &result.lethal) || !BG_ClassNumber(&text, &result.lethalCount) ||
		!BG_ClassNumber(&text, &result.tactical) || !BG_ClassNumber(&text, &result.tacticalCount) ) return qfalse;
	for ( i = 0; i < BG_PERK_SLOTS; i++ )
		if ( !BG_ClassNumber(&text, &result.perk[i]) ) return qfalse;
	if ( strlen(text) >= sizeof(result.name) ) return qfalse;
	Q_strncpyz(result.name, text, sizeof(result.name));
	if ( !BG_ValidateClass(&result) ) return qfalse;
	*loadout = result; // invalid data never partly overwrites the live choice
	return qtrue;
}
qboolean BG_SerializeClass( const bg_class_t *loadout, char *text, int size ) {
	char result[BG_CLASS_STRING_SIZE];
	if ( !BG_ValidateClass(loadout) ) return qfalse;
	Com_sprintf(result, sizeof(result), "1|%i|%i|%i|%i|%i|%i|%i|%i|%i|%s",
		loadout->slot[0], loadout->slot[1], loadout->lethal, loadout->lethalCount,
		loadout->tactical, loadout->tacticalCount,
		loadout->perk[0], loadout->perk[1], loadout->perk[2], loadout->name);
	if ( size <= (int)strlen(result) ) return qfalse;
	Q_strncpyz(text, result, size);
	return qtrue;
}

// Weapon IDs need not be adjacent. Cycle the TWO SLOT INDICES modulo the slot
// count, using the latest requested weapon rather than an acknowledged snapshot.
int BG_ToggleSlotWeapon( const playerState_t *ps, int selection ) {
	int slot, next, weapon;
	for ( slot = 0; slot < SLOT_COUNT; slot++ )
		if ( selection == ps->stats[STAT_SLOT_PRIMARY + slot] ) break;
	if ( slot == SLOT_COUNT ) slot = ps->stats[STAT_ACTIVE_SLOT] == SLOT_SECONDARY ? SLOT_SECONDARY : SLOT_PRIMARY;
	next = (slot + 1) % SLOT_COUNT;
	weapon = ps->stats[STAT_SLOT_PRIMARY + next];
	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ||
		!(ps->stats[STAT_WEAPONS] & (1 << weapon)) ) return selection;
	return weapon;
}
