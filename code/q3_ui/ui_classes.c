/* Native class browser and nested loadout editor. A private draft stays in the
 * UI until Save; only a validated next-spawn definition goes into userinfo. */
#include "ui_local.h"

#define CLASS_ROWS 9
#define CLASS_PAGE_ROWS 16
#define CLASS_BACK -1
#define CLASS_EDIT -2
#define CLASS_SAVE -3
#define CLASS_EQUIP -4
#define CLASS_RENAME -5
#define CLASS_DISCARD -6
#define CLASS_EDIT_SLOT -20

typedef struct {
	menucommon_s generic;
	char title[64], caption[40], tag[16], description[192];
	int value;
	qboolean selected;
} classRow_t;

typedef struct {
	menuframework_s menu;
	classRow_t rows[CLASS_ROWS], edit, back;
	classRow_t editSlot[BG_CUSTOM_CLASS_COUNT];
	bg_class_t classes[CLASS_ROWS];
	int choice;
	char status[80];
} classSelect_t;

typedef struct {
	menuframework_s menu;
	classRow_t name, gun[SLOT_COUNT], attachment[SLOT_COUNT];
	classRow_t lethal, tactical, perk[BG_PERK_SLOTS], save, equip, back;
	bg_class_t draft, original;
	int customSlot;
	char error[80];
} classEdit_t;

enum { CLASS_CATEGORIES, CLASS_WEAPONS, CLASS_LETHAL, CLASS_TACTICAL,
	CLASS_QUANTITY, CLASS_PERKS, CLASS_ATTACHMENTS };
enum { CATEGORY_RIFLES, CATEGORY_SNIPERS, CATEGORY_SHOTGUNS,
	CATEGORY_LAUNCHERS, CATEGORY_ENERGY, CATEGORY_COUNT };

typedef struct {
	menuframework_s menu;
	classRow_t rows[CLASS_PAGE_ROWS], back;
	char title[48], breadcrumb[80], note[160];
	int kind, slot, count, choice;
} classPage_t;

typedef struct {
	menuframework_s menu;
	menufield_s field;
	classRow_t apply, back;
	char error[80];
} className_t;

typedef struct {
	menuframework_s menu;
	classRow_t keep, discard;
} classDiscard_t;

static classSelect_t classSelect;
static classEdit_t classEdit;
static classPage_t classCategories, classPicker, classQuantity;
static className_t className;
static classDiscard_t classDiscard;

/* Pointer/word-only tables also retain their layout in q3asm. */
static const char *categoryNames[] = {
	"Assault rifles", "Sniper rifles", "Shotguns", "Launchers", "Energy weapons"
};
static const char *categoryDescriptions[] = {
	"Automatic fire for an all-purpose loadout.",
	"Scoped and precision weapons for deliberate shots.",
	"Close-range weapons with a wide pellet spread.",
	"Explosive projectiles for splash damage.",
	"Beam and energy projectile weapons."
};
static const char *slotNames[] = { "PRIMARY", "SECONDARY" };
static vec4_t classBackground = {0.035f, 0.045f, 0.05f, 1};
static vec4_t classPanel = {0.075f, 0.09f, 0.095f, 1};
static vec4_t classFocus = {0.16f, 0.19f, 0.16f, 1};
static vec4_t classAccent = {0.80f, 0.88f, 0.54f, 1};
static vec4_t classMuted = {0.65f, 0.71f, 0.70f, 1};
static vec4_t classDisabled = {0.46f, 0.50f, 0.49f, 1};

static void UI_ClassEditRefresh( void );
static void UI_ClassOpenCategories( int slot );
static void UI_ClassOpenPicker( int kind, int slot, int category );
static void UI_ClassOpenName( void );
static void UI_ClassLeaveEditor( void );

void UI_ClassCvars( void ) {
	char text[BG_CLASS_STRING_SIZE], name[32];
	bg_class_t loadout;
	int i;
	BG_SerializeClass(BG_Class(0), text, sizeof(text));
	trap_Cvar_Register(NULL, "peace_loadout", text, CVAR_ARCHIVE | CVAR_USERINFO);
	trap_Cvar_Register(NULL, "peace_classSelected", "0", CVAR_ARCHIVE);
	for ( i = 0; i < BG_CUSTOM_CLASS_COUNT; i++ ) {
		loadout = *BG_Class(i % BG_ClassCount());
		Com_sprintf(loadout.name, sizeof(loadout.name), "Custom %i", i + 1);
		BG_SerializeClass(&loadout, text, sizeof(text));
		Com_sprintf(name, sizeof(name), "peace_class_%i", i);
		trap_Cvar_Register(NULL, name, text, CVAR_ARCHIVE);
	}
}

static void UI_ReadClasses( void ) {
	char text[BG_CLASS_STRING_SIZE], name[32];
	int i;
	for ( i = 0; i < BG_ClassCount() + BG_CUSTOM_CLASS_COUNT; i++ ) {
		if ( i < BG_ClassCount() ) classSelect.classes[i] = *BG_Class(i);
		else {
			int slot = i - BG_ClassCount();
			Com_sprintf(name, sizeof(name), "peace_class_%i", slot);
			trap_Cvar_VariableStringBuffer(name, text, sizeof(text));
			if ( !BG_ParseClass(text, &classSelect.classes[i]) ) {
				classSelect.classes[i] = *BG_Class(slot % BG_ClassCount());
				Com_sprintf(classSelect.classes[i].name, BG_CLASS_NAME_SIZE, "Custom %i", slot + 1);
			}
		}
	}
}

static int UI_ClassSelected( void ) {
	int selected = (int)trap_Cvar_VariableValue("peace_classSelected");
	return selected >= 0 && selected < BG_ClassCount() + BG_CUSTOM_CLASS_COUNT ? selected : 0;
}

static qboolean UI_ClassInGame( void ) {
	uiClientState_t state;
	trap_GetClientState(&state);
	return state.connState == CA_ACTIVE;
}

static void UI_ClassBackdrop( const char *title, const char *breadcrumb ) {
	UI_FillRect(0, 0, 640, 480, classBackground);
	UI_FillRect(0, 0, 640, 8, classAccent);
	UI_FillRect(28, 30, 4, 28, classAccent);
	UI_DrawString(44, 28, title, UI_LEFT, color_white);
	UI_DrawString(28, 66, breadcrumb, UI_SMALLFONT, classMuted);
	UI_FillRect(28, 88, 584, 1, classFocus);
}

static void UI_ClassFooter( const char *hint ) {
	UI_DrawString(320, 452, hint, UI_CENTER | UI_SMALLFONT, classMuted);
}

/* Fixed-width wrapping keeps every catalog description inside the panel at
 * 640x480, including the longer perk descriptions and future weapon names. */
static void UI_ClassParagraph( int x, int y, int width, const char *text, vec4_t color ) {
	char line[80];
	int length, take, end, columns = width / SMALLCHAR_WIDTH;
	if ( columns > (int)sizeof(line) - 1 ) columns = sizeof(line) - 1;
	while ( *text ) {
		length = strlen(text);
		take = length < columns ? length : columns;
		for ( end = 0; end < take && text[end] != '\n'; end++ ) {}
		if ( end < take ) take = end;
		else if ( length > take ) {
			for ( end = take; end > 0 && text[end] != ' '; end-- ) {}
			if ( end ) take = end;
		}
		memcpy(line, text, take);
		line[take] = 0;
		UI_DrawString(x, y, line, UI_SMALLFONT, color);
		y += 20;
		text += take;
		while ( *text == ' ' || *text == '\n' ) text++;
	}
}

static void UI_ClassRowDraw( void *ptr ) {
	classRow_t *row = (classRow_t *)ptr;
	menucommon_s *item = &row->generic;
	qboolean focus = item->parent->cursor == item->menuPosition;
	qboolean disabled = (item->flags & QMF_GRAYED) != 0;
	int width = item->right - item->left, height = item->bottom - item->top;
	int y = item->top + (height - SMALLCHAR_HEIGHT) / 2;
	char title[64];
	int room = (width - 24 - (row->tag[0] ? (strlen(row->tag) + 2) * SMALLCHAR_WIDTH : 0)) / SMALLCHAR_WIDTH;
	float *color = disabled ? classDisabled : focus ? classAccent : color_white;
	UI_FillRect(item->left, item->top, width, height, focus ? classFocus : classPanel);
	if ( focus || row->selected ) UI_FillRect(item->left, item->top, 3, height, classAccent);
	if ( row->caption[0] ) {
		UI_DrawString(item->left + 12, item->top + 7, row->caption, UI_SMALLFONT, classMuted);
		y = item->top + 27;
	}
	Q_strncpyz(title, row->title, sizeof(title));
	if ( room > 3 && (int)strlen(title) > room ) {
		title[room] = 0;
		title[room - 1] = title[room - 2] = title[room - 3] = '.';
	}
	UI_DrawString(item->left + 12, y, title, UI_SMALLFONT, color);
	if ( row->tag[0] ) UI_DrawString(item->right - 10, y, row->tag,
		UI_RIGHT | UI_SMALLFONT, disabled ? classDisabled : classAccent);
}

static void UI_ClassRow( menuframework_s *menu, classRow_t *row, int id,
	const char *title, const char *caption, int x, int y, int width, int height,
	void (*callback)(void *, int) ) {
	row->generic.type = MTYPE_ACTION;
	row->generic.flags = QMF_NODEFAULTINIT;
	row->generic.id = id;
	row->generic.x = row->generic.left = x;
	row->generic.y = row->generic.top = y;
	row->generic.right = x + width;
	row->generic.bottom = y + height;
	row->generic.callback = callback;
	row->generic.ownerdraw = UI_ClassRowDraw;
	Q_strncpyz(row->title, title, sizeof(row->title));
	Q_strncpyz(row->caption, caption, sizeof(row->caption));
	Menu_AddItem(menu, row);
}

static const char *UI_ClassEquipmentName( int weapon ) {
	switch ( weapon ) {
	case WP_FRAG: return "Frag grenade";
	case WP_SEMTEX: return "Semtex";
	case WP_FLASH: return "Flash grenade";
	default: return "None";
	}
}

static int UI_ClassWeaponCategory( int weapon ) {
	switch ( weapon ) {
	case WP_MACHINEGUN: return CATEGORY_RIFLES;
	case WP_M40A3: case WP_RAILGUN: return CATEGORY_SNIPERS;
	case WP_SHOTGUN: return CATEGORY_SHOTGUNS;
	case WP_ROCKET_LAUNCHER: case WP_GRENADE_LAUNCHER: return CATEGORY_LAUNCHERS;
	default: return CATEGORY_ENERGY;
	}
}

static const char *UI_ClassWeaponDescription( int weapon ) {
	switch ( weapon ) {
	case WP_MACHINEGUN: return "Automatic rifle. A flexible choice for sustained fire.";
	case WP_SHOTGUN: return "Semi-automatic shotgun. Load shells individually for close-range fights.";
	case WP_M40A3: return "Bolt-action sniper rifle with an integral scope. No optional attachments.";
	case WP_RAILGUN: return "Single-shot precision weapon with a long recovery between shots.";
	case WP_ROCKET_LAUNCHER: return "Direct-fire explosive rockets. Watch your distance from the blast.";
	case WP_GRENADE_LAUNCHER: return "Arcing, bouncing explosive projectiles.";
	case WP_LIGHTNING: return "Continuous short-range energy beam.";
	case WP_PLASMAGUN: return "Rapid-fire plasma projectiles.";
	default: return "Heavy energy weapon with explosive projectiles.";
	}
}

static void UI_ClassSelectRefresh( void ) {
	int i, selected = UI_ClassSelected();
	for ( i = 0; i < CLASS_ROWS; i++ ) {
		Q_strncpyz(classSelect.rows[i].title, classSelect.classes[i].name, sizeof(classSelect.rows[i].title));
		classSelect.rows[i].selected = i == selected;
		Q_strncpyz(classSelect.rows[i].tag, i == selected ? "EQ" : "", sizeof(classSelect.rows[i].tag));
	}
	if ( classSelect.choice < BG_ClassCount() ) classSelect.edit.generic.flags |= QMF_GRAYED;
	else classSelect.edit.generic.flags &= ~QMF_GRAYED;
}

static void UI_ClassSelectDraw( void ) {
	const bg_class_t *loadout = &classSelect.classes[classSelect.choice];
	char line[80];
	int i;
	UI_ClassBackdrop("CHANGE CLASS", "Choose a class for your next spawn or S&D round.");
	UI_DrawString(28, 96, "CUSTOM CLASSES", UI_SMALLFONT, classMuted);
	UI_DrawString(28, 274, "PRESET CLASSES", UI_SMALLFONT, classMuted);
	UI_FillRect(304, 116, 308, 286, classPanel);
	UI_DrawString(320, 130, loadout->name, UI_SMALLFONT, classAccent);
	UI_DrawString(320, 154, classSelect.choice == UI_ClassSelected() ? "EQUIPPED FOR NEXT SPAWN" : "A / ENTER TO EQUIP", UI_SMALLFONT, classMuted);
	UI_DrawString(320, 188, "PRIMARY", UI_SMALLFONT, classMuted);
	UI_DrawString(320, 208, BG_ClassWeaponName(loadout->slot[0]), UI_SMALLFONT, color_white);
	UI_DrawString(320, 236, "SECONDARY", UI_SMALLFONT, classMuted);
	UI_DrawString(320, 256, BG_ClassWeaponName(loadout->slot[1]), UI_SMALLFONT, color_white);
	Com_sprintf(line, sizeof(line), "%s x%i / %s x%i", loadout->lethal == WP_SEMTEX ? "Semtex" : loadout->lethal ? "Frag" : "None", loadout->lethalCount, loadout->tactical ? "Flash" : "None", loadout->tacticalCount);
	UI_DrawString(320, 290, line, UI_SMALLFONT, color_white);
	for ( i = 0; i < BG_PERK_SLOTS; i++ ) {
		Com_sprintf(line, sizeof(line), "%i  %s", i + 1, BG_Perk(loadout->perk[i])->name);
		UI_DrawString(320, 322 + i * 22, line, UI_SMALLFONT, classMuted);
	}
	Menu_Draw(&classSelect.menu);
	UI_ClassFooter(classSelect.status[0] ? classSelect.status :
		classSelect.choice < BG_ClassCount() ? "Presets are fixed. Choose a custom class to edit." : "A / Enter: equip    X / E: edit    B / Esc: back");
}

static void UI_ClassSelectEvent( void *ptr, int event ) {
	menucommon_s *item = (menucommon_s *)ptr;
	char text[BG_CLASS_STRING_SIZE];
	if ( item->id <= CLASS_EDIT_SLOT ) {
		int slot = CLASS_EDIT_SLOT - item->id;
		if ( event == QM_GOTFOCUS ) {
			classSelect.choice = BG_ClassCount() + slot;
			classSelect.status[0] = 0;
			UI_ClassSelectRefresh();
		}
		if ( event == QM_ACTIVATED ) UI_CreateClassMenu(slot);
		return;
	}
	if ( item->id >= 0 && event == QM_GOTFOCUS ) {
		classSelect.choice = item->id;
		classSelect.status[0] = 0;
		UI_ClassSelectRefresh();
	}
	if ( event != QM_ACTIVATED ) return;
	if ( item->id == CLASS_BACK ) { UI_PopMenu(); return; }
	if ( item->id == CLASS_EDIT ) {
		if ( classSelect.choice >= BG_ClassCount() ) UI_CreateClassMenu(classSelect.choice - BG_ClassCount());
		return;
	}
	if ( item->id < 0 || item->id >= CLASS_ROWS ) return;
	classSelect.choice = item->id;
	if ( !BG_SerializeClass(&classSelect.classes[item->id], text, sizeof(text)) ) return;
	trap_Cvar_Set("peace_loadout", text);
	trap_Cvar_SetValue("peace_classSelected", item->id);
	UI_ClassSelectRefresh();
	Q_strncpyz(classSelect.status, "Equipped. Applies on your next spawn or S&D round.", sizeof(classSelect.status));
	if ( UI_ClassInGame() ) UI_ForceMenuOff();
}

static sfxHandle_t UI_ClassSelectKey( int key ) {
	sfxHandle_t sound;
	if ( key == 'e' || key == 'E' || key == K_PAD0_X ) {
		UI_ClassSelectEvent(&classSelect.edit, QM_ACTIVATED);
		return menu_move_sound;
	}
	sound = Menu_DefaultKey(&classSelect.menu, key);
	/* Up/down traverses class rows. Tab and the mouse can reach inline Edit;
	 * controller X also edits the highlighted custom class directly. */
	if ( (key == K_UPARROW || key == K_DOWNARROW || key == K_KP_UPARROW || key == K_KP_DOWNARROW) &&
		((menucommon_s *)Menu_ItemAtCursor(&classSelect.menu))->id <= CLASS_EDIT_SLOT )
		sound = Menu_DefaultKey(&classSelect.menu, key);
	return sound;
}

void UI_ClassesMenu( void ) {
	int i, index;
	UI_ClassCvars();
	memset(&classSelect, 0, sizeof(classSelect));
	UI_ReadClasses();
	classSelect.choice = UI_ClassSelected();
	classSelect.menu.fullscreen = qtrue;
	classSelect.menu.wrapAround = qtrue;
	classSelect.menu.draw = UI_ClassSelectDraw;
	classSelect.menu.key = UI_ClassSelectKey;
	/* Customs first visually and in keyboard order; stored indices stay stable. */
	for ( i = 0; i < BG_CUSTOM_CLASS_COUNT; i++ ) {
		index = BG_ClassCount() + i;
		UI_ClassRow(&classSelect.menu, &classSelect.rows[index], index, "", "", 28, 116 + i * 30, 188, 26, UI_ClassSelectEvent);
		UI_ClassRow(&classSelect.menu, &classSelect.editSlot[i], CLASS_EDIT_SLOT - i, "EDIT", "", 220, 116 + i * 30, 60, 26, UI_ClassSelectEvent);
	}
	for ( i = 0; i < BG_ClassCount(); i++ )
		UI_ClassRow(&classSelect.menu, &classSelect.rows[i], i, "", "", 28, 296 + i * 27, 252, 25, UI_ClassSelectEvent);
	UI_ClassRow(&classSelect.menu, &classSelect.edit, CLASS_EDIT, "EDIT LOADOUT", "", 28, 414, 252, 30, UI_ClassSelectEvent);
	UI_ClassRow(&classSelect.menu, &classSelect.back, CLASS_BACK, "BACK", "", 304, 414, 308, 30, UI_ClassSelectEvent);
	UI_PushMenu(&classSelect.menu);
	Menu_SetCursorToItem(&classSelect.menu, &classSelect.rows[UI_ClassSelected()]);
	UI_ClassSelectRefresh();
}

static int UI_ClassAttachmentLimit( void ) {
	playerState_t ps;
	int i;
	memset(&ps, 0, sizeof(ps));
	for ( i = 0; i < BG_PERK_SLOTS; i++ )
		if ( classEdit.draft.perk[i] > PERK_NONE && classEdit.draft.perk[i] < BG_PERK_COUNT )
			ps.perks |= 1 << (classEdit.draft.perk[i] - 1);
	return BG_AttachmentLimit(&ps);
}

static void UI_ClassNormalizeAttachments( void ) {
	int slot, i, mask, kept, limit = UI_ClassAttachmentLimit();
	for ( slot = 0; slot < SLOT_COUNT; slot++ ) {
		mask = classEdit.draft.attachments[slot];
		if ( mask < 0 ) mask = (int)trap_Cvar_VariableValue("cg_sight") == 1 ? BG_ATTACHMENT_REDDOT : 0;
		mask &= BG_WeaponAttachmentMask(classEdit.draft.slot[slot]);
		kept = 0;
		for ( i = 1; i < BG_AttachmentTypeCount(); i++ ) {
			int bit = BG_AttachmentType(i);
			if ( (mask & bit) && BG_AttachmentCount(kept) < limit ) kept |= bit;
		}
		classEdit.draft.attachments[slot] = kept;
	}
}

static void UI_ClassEditRefresh( void ) {
	int i;
	Q_strncpyz(classEdit.name.title, classEdit.draft.name, sizeof(classEdit.name.title));
	for ( i = 0; i < SLOT_COUNT; i++ ) {
		Q_strncpyz(classEdit.gun[i].title, BG_ClassWeaponName(classEdit.draft.slot[i]), sizeof(classEdit.gun[i].title));
		if ( !BG_WeaponAttachmentMask(classEdit.draft.slot[i]) )
			Q_strncpyz(classEdit.attachment[i].title, "Integral scope", sizeof(classEdit.attachment[i].title));
		else Com_sprintf(classEdit.attachment[i].title, sizeof(classEdit.attachment[i].title), "Attachments %i/%i", BG_AttachmentCount(classEdit.draft.attachments[i]), UI_ClassAttachmentLimit());
	}
	Com_sprintf(classEdit.lethal.title, sizeof(classEdit.lethal.title), "%s%s", UI_ClassEquipmentName(classEdit.draft.lethal), classEdit.draft.lethal ? va(" x%i", classEdit.draft.lethalCount) : "");
	Com_sprintf(classEdit.tactical.title, sizeof(classEdit.tactical.title), "%s%s", UI_ClassEquipmentName(classEdit.draft.tactical), classEdit.draft.tactical ? va(" x%i", classEdit.draft.tacticalCount) : "");
	for ( i = 0; i < BG_PERK_SLOTS; i++ ) Q_strncpyz(classEdit.perk[i].title, BG_Perk(classEdit.draft.perk[i])->name, sizeof(classEdit.perk[i].title));
}

static qboolean UI_ClassDirty( void ) {
	char before[BG_CLASS_STRING_SIZE], after[BG_CLASS_STRING_SIZE];
	if ( !BG_SerializeClass(&classEdit.draft, after, sizeof(after)) ) return qtrue;
	BG_SerializeClass(&classEdit.original, before, sizeof(before));
	return strcmp(before, after) != 0;
}

static void UI_ClassEditDraw( void ) {
	char breadcrumb[80];
	Com_sprintf(breadcrumb, sizeof(breadcrumb), "CLASSES / CUSTOM %i     %s", classEdit.customSlot + 1, UI_ClassDirty() ? "UNSAVED CHANGES" : "SAVED LOADOUT");
	UI_ClassBackdrop("EDIT LOADOUT", breadcrumb);
	Menu_Draw(&classEdit.menu);
	UI_DrawString(28, 388, classEdit.error[0] ? classEdit.error : "Applies next spawn / S&D round. Melee is always available.", UI_SMALLFONT, classEdit.error[0] ? color_yellow : classMuted);
	UI_ClassFooter("D-pad / arrows: browse    A / Enter: open    B / Esc: back");
}

static void UI_ClassSave( qboolean equip ) {
	char text[BG_CLASS_STRING_SIZE], name[32];
	int selected = BG_ClassCount() + classEdit.customSlot;
	if ( !BG_SerializeClass(&classEdit.draft, text, sizeof(text)) ) {
		Q_strncpyz(classEdit.error, "Check the name, weapons, perks and attachments before saving.", sizeof(classEdit.error));
		return;
	}
	Com_sprintf(name, sizeof(name), "peace_class_%i", classEdit.customSlot);
	trap_Cvar_Set(name, text);
	if ( equip || UI_ClassSelected() == selected ) trap_Cvar_Set("peace_loadout", text);
	if ( equip ) trap_Cvar_SetValue("peace_classSelected", selected);
	UI_ReadClasses();
	classSelect.choice = selected;
	UI_ClassSelectRefresh();
	Menu_SetCursorToItem(&classSelect.menu, &classSelect.rows[selected]);
	Q_strncpyz(classSelect.status, equip || UI_ClassSelected() == selected ?
		"Saved and equipped for your next spawn or S&D round." : "Saved. Choose this class to equip it.", sizeof(classSelect.status));
	UI_PopMenu();
	if ( equip && UI_ClassInGame() ) UI_ForceMenuOff();
}

static void UI_ClassEditEvent( void *ptr, int event ) {
	menucommon_s *item = (menucommon_s *)ptr;
	if ( event != QM_ACTIVATED ) return;
	classEdit.error[0] = 0;
	switch ( item->id ) {
	case CLASS_BACK: UI_ClassLeaveEditor(); break;
	case CLASS_SAVE: UI_ClassSave(qfalse); break;
	case CLASS_EQUIP: UI_ClassSave(qtrue); break;
	case CLASS_RENAME: UI_ClassOpenName(); break;
	case 0: case 1: UI_ClassOpenCategories(item->id); break;
	case 2: case 3: UI_ClassOpenPicker(CLASS_ATTACHMENTS, item->id - 2, 0); break;
	case 4: UI_ClassOpenPicker(CLASS_LETHAL, 0, 0); break;
	case 5: UI_ClassOpenPicker(CLASS_TACTICAL, 0, 0); break;
	default: UI_ClassOpenPicker(CLASS_PERKS, item->id - 6, 0); break;
	}
}

/* Match the card layout with directional navigation; Tab retains linear order.
 * Distance across the direction of travel has priority over diagonal jumps. */
static sfxHandle_t UI_ClassEditKey( int key ) {
	menucommon_s *current, *candidate;
	int i, dx, dy, along, across, score, best = -1, bestScore = 999999;
	if ( key == K_ESCAPE || key == K_MOUSE2 ) { UI_ClassLeaveEditor(); return menu_out_sound; }
	if ( key != K_LEFTARROW && key != K_RIGHTARROW && key != K_UPARROW && key != K_DOWNARROW )
		return Menu_DefaultKey(&classEdit.menu, key);
	current = (menucommon_s *)Menu_ItemAtCursor(&classEdit.menu);
	for ( i = 0; i < classEdit.menu.nitems; i++ ) {
		candidate = (menucommon_s *)classEdit.menu.items[i];
		if ( candidate->flags & (QMF_GRAYED | QMF_INACTIVE) ) continue;
		dx = candidate->left + candidate->right - current->left - current->right;
		dy = candidate->top + candidate->bottom - current->top - current->bottom;
		along = key == K_LEFTARROW ? -dx : key == K_RIGHTARROW ? dx : key == K_UPARROW ? -dy : dy;
		across = key == K_LEFTARROW || key == K_RIGHTARROW ? abs(dy) : abs(dx);
		if ( along <= 0 ) continue;
		score = across * 4 + along;
		if ( score < bestScore ) { best = i; bestScore = score; }
	}
	if ( best >= 0 ) Menu_SetCursor(&classEdit.menu, best);
	return menu_move_sound;
}

void UI_CreateClassMenu( int slot ) {
	int i;
	char caption[32];
	if ( slot < 0 || slot >= BG_CUSTOM_CLASS_COUNT ) slot = 0;
	UI_ClassCvars();
	UI_ReadClasses();
	memset(&classEdit, 0, sizeof(classEdit));
	classEdit.customSlot = slot;
	classEdit.draft = classSelect.classes[BG_ClassCount() + slot];
	UI_ClassNormalizeAttachments();
	classEdit.original = classEdit.draft;
	classEdit.menu.fullscreen = qtrue;
	classEdit.menu.wrapAround = qtrue;
	classEdit.menu.draw = UI_ClassEditDraw;
	classEdit.menu.key = UI_ClassEditKey;
	UI_ClassRow(&classEdit.menu, &classEdit.name, CLASS_RENAME, "", "", 28, 104, 584, 32, UI_ClassEditEvent);
	Q_strncpyz(classEdit.name.tag, "RENAME", sizeof(classEdit.name.tag));
	for ( i = 0; i < SLOT_COUNT; i++ ) {
		UI_ClassRow(&classEdit.menu, &classEdit.gun[i], i, "", slotNames[i], 28 + i * 298, 148, 286, 58, UI_ClassEditEvent);
		Q_strncpyz(classEdit.gun[i].tag, ">", sizeof(classEdit.gun[i].tag));
	}
	for ( i = 0; i < SLOT_COUNT; i++ )
		UI_ClassRow(&classEdit.menu, &classEdit.attachment[i], i + 2, "", "", 28 + i * 298, 210, 286, 28, UI_ClassEditEvent);
	UI_ClassRow(&classEdit.menu, &classEdit.lethal, 4, "", "LETHAL", 28, 250, 286, 56, UI_ClassEditEvent);
	UI_ClassRow(&classEdit.menu, &classEdit.tactical, 5, "", "TACTICAL", 326, 250, 286, 56, UI_ClassEditEvent);
	for ( i = 0; i < BG_PERK_SLOTS; i++ ) {
		Com_sprintf(caption, sizeof(caption), "PERK %i", i + 1);
		UI_ClassRow(&classEdit.menu, &classEdit.perk[i], i + 6, "", caption, 28 + i * 198, 318, 188, 58, UI_ClassEditEvent);
	}
	UI_ClassRow(&classEdit.menu, &classEdit.equip, CLASS_EQUIP, "SAVE & EQUIP", "", 28, 414, 212, 30, UI_ClassEditEvent);
	UI_ClassRow(&classEdit.menu, &classEdit.save, CLASS_SAVE, "SAVE ONLY", "", 252, 414, 164, 30, UI_ClassEditEvent);
	UI_ClassRow(&classEdit.menu, &classEdit.back, CLASS_BACK, "BACK", "", 428, 414, 184, 30, UI_ClassEditEvent);
	UI_ClassEditRefresh();
	UI_PushMenu(&classEdit.menu);
	Menu_SetCursorToItem(&classEdit.menu, &classEdit.gun[0]);
}

static void UI_ClassPageDraw( classPage_t *page ) {
	classRow_t *row = (classRow_t *)Menu_ItemAtCursor(&page->menu);
	UI_ClassBackdrop(page->title, page->breadcrumb);
	UI_FillRect(308, 108, 304, 294, classPanel);
	UI_DrawString(324, 124, "DETAILS", UI_SMALLFONT, classMuted);
	UI_ClassParagraph(324, 158, 272, row->generic.id == CLASS_BACK ? "Return to the previous screen." : row->title, classAccent);
	if ( row->generic.id != CLASS_BACK ) UI_ClassParagraph(324, 208, 272, row->description, color_white);
	UI_ClassParagraph(324, 324, 272, page->note, classMuted);
	Menu_Draw(&page->menu);
	UI_ClassFooter("D-pad / arrows: browse    A / Enter: choose    B / Esc: back");
}
static void UI_ClassCategoriesDraw( void ) { UI_ClassPageDraw(&classCategories); }
static void UI_ClassPickerDraw( void ) { UI_ClassPageDraw(&classPicker); }
static void UI_ClassQuantityDraw( void ) { UI_ClassPageDraw(&classQuantity); }

static sfxHandle_t UI_ClassPageKey( classPage_t *page, int key ) {
	if ( key == K_LEFTARROW ) key = K_ESCAPE;
	if ( key == K_RIGHTARROW ) key = K_ENTER;
	return Menu_DefaultKey(&page->menu, key);
}
static sfxHandle_t UI_ClassCategoriesKey( int key ) { return UI_ClassPageKey(&classCategories, key); }
static sfxHandle_t UI_ClassPickerKey( int key ) { return UI_ClassPageKey(&classPicker, key); }
static sfxHandle_t UI_ClassQuantityKey( int key ) { return UI_ClassPageKey(&classQuantity, key); }

static void UI_ClassPageEvent( void *ptr, int event );

static void UI_ClassPageInit( classPage_t *page, int kind, int slot,
	const char *title, const char *breadcrumb, void (*draw)(void), sfxHandle_t (*key)(int) ) {
	memset(page, 0, sizeof(*page));
	page->kind = kind;
	page->slot = slot;
	page->menu.fullscreen = qtrue;
	page->menu.wrapAround = qtrue;
	page->menu.draw = draw;
	page->menu.key = key;
	Q_strncpyz(page->title, title, sizeof(page->title));
	Q_strncpyz(page->breadcrumb, breadcrumb, sizeof(page->breadcrumb));
}

static classRow_t *UI_ClassPageRow( classPage_t *page, int value, const char *title,
	const char *description, qboolean selected, qboolean disabled ) {
	classRow_t *row;
	if ( page->count >= CLASS_PAGE_ROWS ) return NULL;
	row = &page->rows[page->count];
	UI_ClassRow(&page->menu, row, page->count, title, "", 28, 108 + page->count * 48, 260, 42, UI_ClassPageEvent);
	row->value = value;
	row->selected = selected;
	if ( selected ) Q_strncpyz(row->tag, "SET", sizeof(row->tag));
	if ( disabled ) {
		row->generic.flags |= QMF_GRAYED;
		Q_strncpyz(row->tag, "IN USE", sizeof(row->tag));
	}
	Q_strncpyz(row->description, description, sizeof(row->description));
	page->count++;
	return row;
}

static void UI_ClassPagePush( classPage_t *page ) {
	int i;
	UI_ClassRow(&page->menu, &page->back, CLASS_BACK, "BACK", "", 28, 414, 584, 30, UI_ClassPageEvent);
	UI_PushMenu(&page->menu);
	for ( i = 0; i < page->count; i++ )
		if ( page->rows[i].selected && !(page->rows[i].generic.flags & QMF_GRAYED) ) {
			Menu_SetCursorToItem(&page->menu, &page->rows[i]);
			break;
		}
}

static void UI_ClassOpenCategories( int slot ) {
	int i;
	char breadcrumb[80];
	Com_sprintf(breadcrumb, sizeof(breadcrumb), "EDIT LOADOUT / %s / WEAPON TYPE", slotNames[slot]);
	UI_ClassPageInit(&classCategories, CLASS_CATEGORIES, slot, "CHOOSE WEAPON TYPE", breadcrumb, UI_ClassCategoriesDraw, UI_ClassCategoriesKey);
	for ( i = 0; i < CATEGORY_COUNT; i++ )
		UI_ClassPageRow(&classCategories, i, categoryNames[i], categoryDescriptions[i], UI_ClassWeaponCategory(classEdit.draft.slot[slot]) == i, qfalse);
	Q_strncpyz(classCategories.note, "Both slots can use any weapon type. Choose a different gun in each slot.", sizeof(classCategories.note));
	UI_ClassPagePush(&classCategories);
}

static void UI_ClassOpenPicker( int kind, int slot, int category ) {
	int i, j, weapon, current, supported;
	qboolean used;
	char breadcrumb[80];
	const char *title;
	if ( kind == CLASS_WEAPONS ) {
		title = categoryNames[category];
		Com_sprintf(breadcrumb, sizeof(breadcrumb), "EDIT LOADOUT / %s / %s", slotNames[slot], title);
	} else if ( kind == CLASS_PERKS ) {
		title = "CHOOSE PERK";
		Com_sprintf(breadcrumb, sizeof(breadcrumb), "EDIT LOADOUT / PERK %i", slot + 1);
	} else if ( kind == CLASS_ATTACHMENTS ) {
		title = "ATTACHMENTS";
		Com_sprintf(breadcrumb, sizeof(breadcrumb), "EDIT LOADOUT / %s / ATTACHMENTS", slotNames[slot]);
	} else {
		title = kind == CLASS_LETHAL ? "LETHAL EQUIPMENT" : "TACTICAL EQUIPMENT";
		Com_sprintf(breadcrumb, sizeof(breadcrumb), "EDIT LOADOUT / %s", kind == CLASS_LETHAL ? "LETHAL" : "TACTICAL");
	}
	UI_ClassPageInit(&classPicker, kind, slot, title, breadcrumb, UI_ClassPickerDraw, UI_ClassPickerKey);
	if ( kind == CLASS_WEAPONS ) {
		for ( i = 0; i < BG_ClassWeaponCount(); i++ ) {
			weapon = BG_ClassWeapon(i);
			if ( UI_ClassWeaponCategory(weapon) != category ) continue;
			used = weapon == classEdit.draft.slot[1 - slot];
			UI_ClassPageRow(&classPicker, weapon, BG_ClassWeaponName(weapon), UI_ClassWeaponDescription(weapon), weapon == classEdit.draft.slot[slot], used);
		}
		Q_strncpyz(classPicker.note, "IN USE = equipped in the other slot. Pick a gun to return to your loadout.", sizeof(classPicker.note));
	} else if ( kind == CLASS_PERKS ) {
		for ( i = 0; i < BG_PERK_COUNT; i++ ) {
			used = qfalse;
			for ( j = 0; j < BG_PERK_SLOTS; j++ ) if ( j != slot && i != PERK_NONE && classEdit.draft.perk[j] == i ) used = qtrue;
			UI_ClassPageRow(&classPicker, i, BG_Perk(i)->name, BG_Perk(i)->description, classEdit.draft.perk[slot] == i, used);
		}
		Q_strncpyz(classPicker.note, "Each perk can be used once. None leaves this slot empty.", sizeof(classPicker.note));
	} else if ( kind == CLASS_ATTACHMENTS ) {
		weapon = classEdit.draft.slot[slot];
		supported = BG_WeaponAttachmentMask(weapon);
		UI_ClassPageRow(&classPicker, 0, supported ? "No attachments" : "Integral scope", supported ? "Remove optional attachments from this weapon." : "The M300 has a built-in sniper scope. It does not accept optional attachments.", !classEdit.draft.attachments[slot], qfalse);
		for ( i = 1; i < BG_AttachmentTypeCount(); i++ ) if ( BG_AttachmentType(i) & supported )
			UI_ClassPageRow(&classPicker, BG_AttachmentType(i), BG_AttachmentName(i), "Select to fit or remove this attachment.", (classEdit.draft.attachments[slot] & BG_AttachmentType(i)) != 0, qfalse);
		Com_sprintf(classPicker.note, sizeof(classPicker.note), "%i attachment%s per gun. Second Nature allows two. Only compatible options are listed.", UI_ClassAttachmentLimit(), UI_ClassAttachmentLimit() == 1 ? "" : "s");
	} else {
		current = kind == CLASS_LETHAL ? classEdit.draft.lethal : classEdit.draft.tactical;
		UI_ClassPageRow(&classPicker, WP_NONE, "None", "Leave this equipment slot empty.", current == WP_NONE, qfalse);
		if ( kind == CLASS_LETHAL ) {
			UI_ClassPageRow(&classPicker, WP_FRAG, "Frag grenade", "A timed explosive grenade. Choose how many to carry on the next screen.", current == WP_FRAG, qfalse);
			UI_ClassPageRow(&classPicker, WP_SEMTEX, "Semtex", "A sticky explosive grenade. Choose how many to carry on the next screen.", current == WP_SEMTEX, qfalse);
		} else UI_ClassPageRow(&classPicker, WP_FLASH, "Flash grenade", "Disorients players in its blast. Choose how many to carry on the next screen.", current == WP_FLASH, qfalse);
		Q_strncpyz(classPicker.note, "Carry one to three of each equipment type. Counts reset on spawn.", sizeof(classPicker.note));
	}
	UI_ClassPagePush(&classPicker);
	if ( kind == CLASS_ATTACHMENTS ) Q_strncpyz(classPicker.back.title, "DONE", sizeof(classPicker.back.title));
}

static void UI_ClassPageEvent( void *ptr, int event ) {
	classRow_t *row = (classRow_t *)ptr;
	classPage_t *page = (classPage_t *)row->generic.parent;
	int i, count, mask;
	char title[32], breadcrumb[80];
	if ( event != QM_ACTIVATED || row->generic.flags & QMF_GRAYED ) return;
	if ( row->generic.id == CLASS_BACK ) { UI_PopMenu(); return; }
	switch ( page->kind ) {
	case CLASS_CATEGORIES:
		UI_ClassOpenPicker(CLASS_WEAPONS, page->slot, row->value);
		return;
	case CLASS_WEAPONS:
		classEdit.draft.slot[page->slot] = row->value;
		UI_ClassNormalizeAttachments();
		UI_PopMenu(); /* weapon list */
		UI_PopMenu(); /* categories */
		break;
	case CLASS_PERKS:
		classEdit.draft.perk[page->slot] = row->value;
		UI_ClassNormalizeAttachments();
		UI_PopMenu();
		break;
	case CLASS_ATTACHMENTS:
		mask = row->value ? classEdit.draft.attachments[page->slot] ^ row->value : 0;
		if ( BG_AttachmentCount(mask) > UI_ClassAttachmentLimit() ) {
			Q_strncpyz(page->note, "Attachment slots full. Remove one first, or equip Second Nature for two slots.", sizeof(page->note));
			return;
		}
		classEdit.draft.attachments[page->slot] = mask;
		for ( i = 0; i < page->count; i++ ) {
			page->rows[i].selected = page->rows[i].value ? (mask & page->rows[i].value) != 0 : !mask;
			Q_strncpyz(page->rows[i].tag, page->rows[i].selected ? "SET" : "", sizeof(page->rows[i].tag));
		}
		break;
	case CLASS_LETHAL: case CLASS_TACTICAL:
		if ( row->value == WP_NONE ) {
			if ( page->kind == CLASS_LETHAL ) classEdit.draft.lethal = classEdit.draft.lethalCount = 0;
			else classEdit.draft.tactical = classEdit.draft.tacticalCount = 0;
			UI_PopMenu();
			break;
		}
		Com_sprintf(breadcrumb, sizeof(breadcrumb), "EDIT LOADOUT / %s / %s", page->kind == CLASS_LETHAL ? "LETHAL" : "TACTICAL", row->title);
		UI_ClassPageInit(&classQuantity, CLASS_QUANTITY, page->kind, "CHOOSE QUANTITY", breadcrumb, UI_ClassQuantityDraw, UI_ClassQuantityKey);
		classQuantity.choice = row->value;
		count = page->kind == CLASS_LETHAL ? classEdit.draft.lethalCount : classEdit.draft.tacticalCount;
		for ( i = 1; i <= 3; i++ ) {
			Com_sprintf(title, sizeof(title), "%i %s", i, i == 1 ? "grenade" : "grenades");
			UI_ClassPageRow(&classQuantity, i, title, "This quantity will be available each time you spawn with this class.", i == (count ? count : 1), qfalse);
		}
		Q_strncpyz(classQuantity.note, "Back returns to equipment without changing your selection.", sizeof(classQuantity.note));
		UI_ClassPagePush(&classQuantity);
		return;
	case CLASS_QUANTITY:
		if ( page->slot == CLASS_LETHAL ) {
			classEdit.draft.lethal = page->choice;
			classEdit.draft.lethalCount = row->value;
		} else {
			classEdit.draft.tactical = page->choice;
			classEdit.draft.tacticalCount = row->value;
		}
		UI_PopMenu(); /* quantity */
		UI_PopMenu(); /* equipment */
		break;
	}
	classEdit.error[0] = 0;
	UI_ClassEditRefresh();
}

static void UI_ClassNameDraw( void ) {
	UI_ClassBackdrop("RENAME CLASS", "EDIT LOADOUT / CLASS NAME");
	UI_FillRect(28, 108, 584, 294, classPanel);
	UI_DrawString(60, 134, "CLASS NAME", UI_SMALLFONT, classMuted);
	UI_FillRect(52, 166, 536, 40, classFocus);
	UI_ClassParagraph(60, 238, 520, "Use 1-23 letters, numbers, spaces, - or _. The name is saved with the rest of your loadout.", classMuted);
	UI_DrawString(60, 336, className.error, UI_SMALLFONT, color_yellow);
	Menu_Draw(&className.menu);
	UI_ClassFooter("Type a name    Enter: apply    B / Esc: cancel");
}

static void UI_ClassNameEvent( void *ptr, int event ) {
	bg_class_t renamed;
	if ( event != QM_ACTIVATED ) return;
	if ( ((menucommon_s *)ptr)->id == CLASS_BACK ) { UI_PopMenu(); return; }
	renamed = classEdit.draft;
	Q_strncpyz(renamed.name, className.field.field.buffer, sizeof(renamed.name));
	if ( !BG_ValidateClass(&renamed) ) {
		Q_strncpyz(className.error, "Use 1-23 letters, numbers, spaces, - or _.", sizeof(className.error));
		return;
	}
	classEdit.draft = renamed;
	UI_ClassEditRefresh();
	UI_PopMenu();
}

static sfxHandle_t UI_ClassNameKey( int key ) {
	if ( (key == K_ENTER || key == K_KP_ENTER) && Menu_ItemAtCursor(&className.menu) == &className.field ) {
		UI_ClassNameEvent(&className.apply, QM_ACTIVATED);
		return menu_move_sound;
	}
	return Menu_DefaultKey(&className.menu, key);
}

static void UI_ClassOpenName( void ) {
	memset(&className, 0, sizeof(className));
	className.menu.fullscreen = qtrue;
	className.menu.wrapAround = qtrue;
	className.menu.draw = UI_ClassNameDraw;
	className.menu.key = UI_ClassNameKey;
	className.field.generic.type = MTYPE_FIELD;
	className.field.generic.flags = QMF_SMALLFONT;
	className.field.generic.x = 68;
	className.field.generic.y = 178;
	className.field.field.widthInChars = BG_CLASS_NAME_SIZE;
	className.field.field.maxchars = BG_CLASS_NAME_SIZE - 1;
	Menu_AddItem(&className.menu, &className.field);
	Q_strncpyz(className.field.field.buffer, classEdit.draft.name, sizeof(className.field.field.buffer));
	className.field.field.cursor = strlen(className.field.field.buffer);
	UI_ClassRow(&className.menu, &className.apply, CLASS_SAVE, "APPLY NAME", "", 28, 414, 286, 30, UI_ClassNameEvent);
	UI_ClassRow(&className.menu, &className.back, CLASS_BACK, "CANCEL", "", 326, 414, 286, 30, UI_ClassNameEvent);
	UI_PushMenu(&className.menu);
}

static void UI_ClassDiscardDraw( void ) {
	UI_ClassBackdrop("UNSAVED CHANGES", "EDIT LOADOUT / BACK");
	UI_FillRect(28, 108, 584, 294, classPanel);
	UI_DrawString(60, 142, "Discard your edits?", UI_SMALLFONT, classAccent);
	UI_ClassParagraph(60, 190, 520, "Your saved class has not changed. Keep editing to save your new loadout, or discard to leave it as it was.", color_white);
	Menu_Draw(&classDiscard.menu);
	UI_ClassFooter("A / Enter: choose    B / Esc: keep editing");
}

static void UI_ClassDiscardEvent( void *ptr, int event ) {
	if ( event != QM_ACTIVATED ) return;
	UI_PopMenu();
	if ( ((menucommon_s *)ptr)->id == CLASS_DISCARD ) UI_PopMenu();
}

static void UI_ClassLeaveEditor( void ) {
	if ( !UI_ClassDirty() ) { UI_PopMenu(); return; }
	memset(&classDiscard, 0, sizeof(classDiscard));
	classDiscard.menu.fullscreen = qtrue;
	classDiscard.menu.wrapAround = qtrue;
	classDiscard.menu.draw = UI_ClassDiscardDraw;
	UI_ClassRow(&classDiscard.menu, &classDiscard.keep, CLASS_BACK, "KEEP EDITING", "", 28, 414, 286, 30, UI_ClassDiscardEvent);
	UI_ClassRow(&classDiscard.menu, &classDiscard.discard, CLASS_DISCARD, "DISCARD CHANGES", "", 326, 414, 286, 30, UI_ClassDiscardEvent);
	UI_PushMenu(&classDiscard.menu);
}
