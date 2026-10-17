/* Peace class select/create. Uses ordinary UI widgets and archived cvars;
 * only the selected, validated definition is sent in userinfo. */
#include "ui_local.h"

#define CLASS_ROWS 9
#define ID_CLASS_PICK 100
#define ID_CLASS_EQUIP 101
#define ID_CLASS_EDIT 102
#define ID_CLASS_BACK 103
#define ID_CLASS_SAVE 104

typedef struct {
	menuframework_s menu;
	menulist_s pick;
	menutext_s equip, edit, back;
	bg_class_t classes[CLASS_ROWS];
	const char *names[CLASS_ROWS + 1];
	char status[80];
} classSelect_t;
typedef struct {
	menuframework_s menu;
	menufield_s name;
	menulist_s primary, secondary, lethal, lethalCount, tactical, tacticalCount;
	menulist_s perk[BG_PERK_SLOTS], attachment[SLOT_COUNT][BG_MAX_CLASS_ATTACHMENTS];
	menutext_s save, back;
	int customSlot;
	char error[80];
	const char *weapons[16];
	const char *perkNames[BG_PERK_COUNT + 1], *attachmentNames[10];
} classEdit_t;

static classSelect_t classSelect;
static classEdit_t classEdit;
static const char *lethals[] = { "None", "Frag grenade", "Semtex", NULL };
static const char *tacticals[] = { "None", "Flash grenade", NULL };
static const char *counts[] = { "1", "2", "3", NULL };
static const char *perkLabels[] = { "PERK 1", "PERK 2", "PERK 3" };
static const char *attachmentLabels[SLOT_COUNT][BG_MAX_CLASS_ATTACHMENTS] = {
	{ "P. ATTACHMENT 1", "P. ATTACHMENT 2" },
	{ "S. ATTACHMENT 1", "S. ATTACHMENT 2" }
};
static vec4_t classBackground = {0.045f, 0.06f, 0.08f, 1};
static vec4_t classPanel = {0.085f, 0.11f, 0.14f, 1};
static vec4_t classAccent = {0.55f, 0.85f, 0.65f, 1};
static vec4_t classMuted = {0.65f, 0.70f, 0.75f, 1};

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
		if ( i < BG_ClassCount() ) {
			classSelect.classes[i] = *BG_Class(i);
		} else {
			int slot = i - BG_ClassCount();
			Com_sprintf(name, sizeof(name), "peace_class_%i", slot);
			trap_Cvar_VariableStringBuffer(name, text, sizeof(text));
			if ( !BG_ParseClass(text, &classSelect.classes[i]) ) {
				classSelect.classes[i] = *BG_Class(slot % BG_ClassCount());
				Com_sprintf(classSelect.classes[i].name, BG_CLASS_NAME_SIZE, "Custom %i", slot + 1);
			}
		}
		classSelect.names[i] = classSelect.classes[i].name;
	}
	classSelect.names[i] = NULL;
}

static void UI_ClassBackdrop( const char *title, const char *subtitle ) {
	UI_FillRect(0, 0, 640, 480, classBackground);
	UI_FillRect(40, 90, 560, 302, classPanel);
	UI_FillRect(40, 34, 4, 34, classAccent);
	UI_DrawString(56, 34, title, UI_LEFT, color_white);
	UI_DrawString(56, 65, subtitle, UI_LEFT | UI_SMALLFONT, classMuted);
	UI_DrawString(320, 458, "D-pad / arrows: choose    A / Enter: select    B / Esc: back",
		UI_CENTER | UI_SMALLFONT, classMuted);
}
static void UI_ClassButtonDraw( void *ptr ) {
	menutext_s *button = (menutext_s *)ptr;
	qboolean focus = button->generic.parent->cursor == button->generic.menuPosition;
	float *color = (button->generic.flags & QMF_GRAYED) ? classMuted : focus ? classAccent : color_white;
	if ( focus ) UI_FillRect(button->generic.left - 8, button->generic.y - 5,
		button->generic.right - button->generic.left + 16, 26, classPanel);
	UI_DrawString(button->generic.x, button->generic.y, button->string,
		UI_CENTER | UI_SMALLFONT, color);
}
static void UI_ClassButton( menuframework_s *menu, menutext_s *button, int id,
	char *label, int x, int y, void (*callback)(void *, int) ) {
	button->generic.type = MTYPE_PTEXT;
	button->generic.flags = QMF_CENTER_JUSTIFY;
	button->generic.id = id;
	button->generic.x = x;
	button->generic.y = y;
	button->generic.callback = callback;
	button->generic.ownerdraw = UI_ClassButtonDraw;
	button->string = label;
	button->style = UI_CENTER | UI_SMALLFONT;
	button->color = color_white;
	Menu_AddItem(menu, button);
	button->generic.left = x - strlen(label) * SMALLCHAR_WIDTH / 2;
	button->generic.right = x + strlen(label) * SMALLCHAR_WIDTH / 2;
	button->generic.top = y - 5;
	button->generic.bottom = y + 21;
}
static void UI_ClassSpin( menuframework_s *menu, menulist_s *spin,
	const char *label, const char **names, int value, int y ) {
	spin->generic.type = MTYPE_SPINCONTROL;
	spin->generic.name = label;
	spin->generic.x = 288;
	spin->generic.y = y;
	spin->itemnames = names;
	spin->curvalue = value;
	Menu_AddItem(menu, spin);
}

static void UI_ClassSelectDraw( void ) {
	const bg_class_t *loadout = &classSelect.classes[classSelect.pick.curvalue];
	char line[80];
	int i;
	UI_ClassBackdrop("CLASS SELECT", "Four presets. Five saved custom classes. Changes apply on spawn.");
	Menu_Draw(&classSelect.menu);
	Com_sprintf(line, sizeof(line), "PRIMARY     %s", BG_ClassWeaponName(loadout->slot[SLOT_PRIMARY]));
	UI_DrawString(80, 158, line, UI_SMALLFONT, color_white);
	Com_sprintf(line, sizeof(line), "SECONDARY   %s", BG_ClassWeaponName(loadout->slot[SLOT_SECONDARY]));
	UI_DrawString(80, 184, line, UI_SMALLFONT, color_white);
	Com_sprintf(line, sizeof(line), "GRENADE     %s   x%i", loadout->lethal == WP_SEMTEX ? "Semtex" : loadout->lethal ? "Frag" : "None", loadout->lethalCount);
	UI_DrawString(80, 210, line, UI_SMALLFONT, color_white);
	Com_sprintf(line, sizeof(line), "TACTICAL    %s   x%i", loadout->tactical ? "Flash" : "None", loadout->tacticalCount);
	UI_DrawString(80, 236, line, UI_SMALLFONT, color_white);
	UI_DrawString(80, 262, "MELEE       Button action - returns to your gun", UI_SMALLFONT, color_white);
	for ( i = 0; i < BG_PERK_SLOTS; i++ ) {
		Com_sprintf(line, sizeof(line), "PERK %i      %s", i + 1, BG_Perk(loadout->perk[i])->name);
		UI_DrawString(80, 294 + i * 24, line, UI_SMALLFONT, classMuted);
	}
	UI_DrawString(320, 398, classSelect.status, UI_CENTER | UI_SMALLFONT, classAccent);
}
static void UI_ClassSelectChanged( void ) {
	if ( classSelect.pick.curvalue < BG_ClassCount() ) {
		classSelect.edit.generic.flags |= QMF_GRAYED;
		Q_strncpyz(classSelect.status, "Choose a Custom slot to create or edit a class.", sizeof(classSelect.status));
	} else {
		classSelect.edit.generic.flags &= ~QMF_GRAYED;
		classSelect.status[0] = 0;
	}
}
static void UI_ClassSelectEvent( void *ptr, int event ) {
	char text[BG_CLASS_STRING_SIZE];
	if ( event != QM_ACTIVATED ) return;
	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_CLASS_PICK: UI_ClassSelectChanged(); break;
	case ID_CLASS_BACK: UI_PopMenu(); break;
	case ID_CLASS_EDIT: UI_CreateClassMenu(classSelect.pick.curvalue - BG_ClassCount()); break;
	case ID_CLASS_EQUIP:
		if ( BG_SerializeClass(&classSelect.classes[classSelect.pick.curvalue], text, sizeof(text)) ) {
			trap_Cvar_Set("peace_loadout", text);
			trap_Cvar_SetValue("peace_classSelected", classSelect.pick.curvalue);
			Q_strncpyz(classSelect.status, "Selected. Your next spawn uses this class.", sizeof(classSelect.status));
		}
		break;
	}
}
void UI_ClassesMenu( void ) {
	int selected;
	UI_ClassCvars();
	memset(&classSelect, 0, sizeof(classSelect));
	UI_ReadClasses();
	selected = trap_Cvar_VariableValue("peace_classSelected");
	if ( selected < 0 || selected >= BG_ClassCount() + BG_CUSTOM_CLASS_COUNT ) selected = 0;
	classSelect.menu.fullscreen = qtrue;
	classSelect.menu.wrapAround = qtrue;
	classSelect.menu.draw = UI_ClassSelectDraw;
	classSelect.pick.generic.id = ID_CLASS_PICK;
	classSelect.pick.generic.callback = UI_ClassSelectEvent;
	UI_ClassSpin(&classSelect.menu, &classSelect.pick, "CLASS", classSelect.names, selected, 114);
	UI_ClassButton(&classSelect.menu, &classSelect.equip, ID_CLASS_EQUIP, "SELECT CLASS", 138, 427, UI_ClassSelectEvent);
	UI_ClassButton(&classSelect.menu, &classSelect.edit, ID_CLASS_EDIT, "CREATE / EDIT", 330, 427, UI_ClassSelectEvent);
	UI_ClassButton(&classSelect.menu, &classSelect.back, ID_CLASS_BACK, "BACK", 531, 427, UI_ClassSelectEvent);
	UI_ClassSelectChanged();
	UI_PushMenu(&classSelect.menu);
}

static void UI_ClassEditDraw( void ) {
	int i;
	const char *hint = "Melee is always available. Changes apply on your next spawn.";
	UI_ClassBackdrop("CREATE A CLASS", "Three perks. One attachment per gun; Second Nature allows two.");
	Menu_Draw(&classEdit.menu);
	for ( i = 0; i < BG_PERK_SLOTS; i++ ) {
		if ( classEdit.menu.cursor == classEdit.perk[i].generic.menuPosition )
			hint = BG_Perk(classEdit.perk[i].curvalue)->description;
	}
	UI_DrawString(320, 398, classEdit.error[0] ? classEdit.error : hint,
		UI_CENTER | UI_SMALLFONT, classEdit.error[0] ? color_yellow : classMuted);
}
static void UI_ClassAttachmentsChanged( void *ptr, int event ) {
	playerState_t ps;
	int i, slot, entry, limit;
	(void)ptr;
	if ( event != QM_ACTIVATED ) return;
	memset(&ps, 0, sizeof(ps));
	for ( i = 0; i < BG_PERK_SLOTS; i++ )
		if ( classEdit.perk[i].curvalue > PERK_NONE && classEdit.perk[i].curvalue < BG_PERK_COUNT )
			ps.perks |= 1 << (classEdit.perk[i].curvalue - 1);
	limit = BG_AttachmentLimit(&ps);
	for ( slot = 0; slot < SLOT_COUNT; slot++ ) {
		int weapon = BG_ClassWeapon(slot == SLOT_PRIMARY ? classEdit.primary.curvalue : classEdit.secondary.curvalue);
		int supported = BG_WeaponAttachmentMask(weapon);
		for ( entry = 0; entry < BG_MAX_CLASS_ATTACHMENTS; entry++ ) {
			menulist_s *spin = &classEdit.attachment[slot][entry];
			if ( entry >= limit || !supported ) {
				spin->curvalue = 0;
				spin->generic.flags |= QMF_GRAYED;
			} else {
				spin->generic.flags &= ~QMF_GRAYED;
				if ( BG_AttachmentType(spin->curvalue) & ~supported ) spin->curvalue = 0;
			}
		}
	}
	classEdit.error[0] = 0;
}
static void UI_ClassEquipmentChanged( void *ptr, int event ) {
	(void)ptr;
	if ( event != QM_ACTIVATED ) return;
	if ( classEdit.lethal.curvalue ) classEdit.lethalCount.generic.flags &= ~QMF_GRAYED;
	else classEdit.lethalCount.generic.flags |= QMF_GRAYED;
	if ( classEdit.tactical.curvalue ) classEdit.tacticalCount.generic.flags &= ~QMF_GRAYED;
	else classEdit.tacticalCount.generic.flags |= QMF_GRAYED;
}
static void UI_ClassEditEvent( void *ptr, int event ) {
	bg_class_t loadout;
	int i, slot, entry;
	char text[BG_CLASS_STRING_SIZE], name[32];
	if ( event != QM_ACTIVATED ) return;
	if ( ((menucommon_s *)ptr)->id == ID_CLASS_BACK ) { UI_PopMenu(); return; }
	if ( ((menucommon_s *)ptr)->id != ID_CLASS_SAVE ) return;
	memset(&loadout, 0, sizeof(loadout));
	Q_strncpyz(loadout.name, classEdit.name.field.buffer, sizeof(loadout.name));
	loadout.slot[0] = BG_ClassWeapon(classEdit.primary.curvalue);
	loadout.slot[1] = BG_ClassWeapon(classEdit.secondary.curvalue);
	loadout.lethal = classEdit.lethal.curvalue == 2 ? WP_SEMTEX : classEdit.lethal.curvalue ? WP_FRAG : WP_NONE;
	loadout.lethalCount = loadout.lethal ? classEdit.lethalCount.curvalue + 1 : 0;
	loadout.tactical = classEdit.tactical.curvalue ? WP_FLASH : WP_NONE;
	loadout.tacticalCount = loadout.tactical ? classEdit.tacticalCount.curvalue + 1 : 0;
	for ( i = 0; i < BG_PERK_SLOTS; i++ ) loadout.perk[i] = classEdit.perk[i].curvalue;
	if ( !BG_ClassPerksValid(&loadout) ) {
		Q_strncpyz(classEdit.error, "Choose different perks, or None for an empty slot.", sizeof(classEdit.error));
		return;
	}
	for ( slot = 0; slot < SLOT_COUNT; slot++ ) for ( entry = 0; entry < BG_MAX_CLASS_ATTACHMENTS; entry++ ) {
		int bit = BG_AttachmentType(classEdit.attachment[slot][entry].curvalue);
		if ( loadout.attachments[slot] & bit ) {
			Q_strncpyz(classEdit.error, "Choose different attachments on each gun.", sizeof(classEdit.error));
			return;
		}
		loadout.attachments[slot] |= bit;
	}
	if ( loadout.slot[0] == loadout.slot[1] ) {
		Q_strncpyz(classEdit.error, "Choose different primary and secondary weapons.", sizeof(classEdit.error));
		return;
	}
	if ( !BG_SerializeClass(&loadout, text, sizeof(text)) ) {
		Q_strncpyz(classEdit.error, "Name: 1-23 letters, numbers, spaces, - or _.", sizeof(classEdit.error));
		return;
	}
	Com_sprintf(name, sizeof(name), "peace_class_%i", classEdit.customSlot);
	trap_Cvar_Set(name, text);
	// Editing an equipped class changes only its NEXT spawn definition.
	if ( (int)trap_Cvar_VariableValue("peace_classSelected") == BG_ClassCount() + classEdit.customSlot )
		trap_Cvar_Set("peace_loadout", text);
	UI_ReadClasses();
	classSelect.pick.curvalue = BG_ClassCount() + classEdit.customSlot;
	UI_ClassSelectChanged();
	Q_strncpyz(classSelect.status, "Saved. Select this class to equip it on your next spawn.", sizeof(classSelect.status));
	UI_PopMenu();
}
void UI_CreateClassMenu( int slot ) {
	bg_class_t loadout;
	int i, gun, entry, primary = 0, secondary = 1;
	if ( slot < 0 || slot >= BG_CUSTOM_CLASS_COUNT ) slot = 0;
	loadout = classSelect.classes[BG_ClassCount() + slot];
	memset(&classEdit, 0, sizeof(classEdit));
	classEdit.customSlot = slot;
	classEdit.menu.fullscreen = qtrue;
	classEdit.menu.wrapAround = qtrue;
	classEdit.menu.draw = UI_ClassEditDraw;
	for ( i = 0; i < BG_PERK_COUNT; i++ ) classEdit.perkNames[i] = BG_Perk(i)->name;
	for ( i = 0; i < BG_AttachmentTypeCount() && i < ARRAY_LEN(classEdit.attachmentNames)-1; i++ )
		classEdit.attachmentNames[i] = BG_AttachmentName(i);
	for ( i = 0; i < BG_ClassWeaponCount(); i++ ) {
		classEdit.weapons[i] = BG_ClassWeaponName(BG_ClassWeapon(i));
		if ( loadout.slot[0] == BG_ClassWeapon(i) ) primary = i;
		if ( loadout.slot[1] == BG_ClassWeapon(i) ) secondary = i;
	}
	classEdit.name.generic.type = MTYPE_FIELD;
	classEdit.name.generic.flags = QMF_SMALLFONT;
	classEdit.name.generic.name = "NAME";
	classEdit.name.generic.x = 288;
	classEdit.name.generic.y = 100;
	classEdit.name.field.widthInChars = BG_CLASS_NAME_SIZE - 1;
	classEdit.name.field.maxchars = BG_CLASS_NAME_SIZE - 1;
	Menu_AddItem(&classEdit.menu, &classEdit.name);
	Q_strncpyz(classEdit.name.field.buffer, loadout.name, sizeof(classEdit.name.field.buffer));
	UI_ClassSpin(&classEdit.menu, &classEdit.primary, "PRIMARY", classEdit.weapons, primary, 120);
	for ( gun = 0; gun < SLOT_COUNT; gun++ ) {
		int mask = loadout.attachments[gun];
		if ( gun == SLOT_SECONDARY ) UI_ClassSpin(&classEdit.menu, &classEdit.secondary, "SECONDARY", classEdit.weapons, secondary, 180);
		if ( mask < 0 ) mask = (int)trap_Cvar_VariableValue("cg_sight") == 1 ? BG_ATTACHMENT_REDDOT : 0;
		for ( entry = 0; entry < BG_MAX_CLASS_ATTACHMENTS; entry++ ) {
			int value = 0;
			for ( i = 1; i < BG_AttachmentTypeCount(); i++ )
				if ( mask & BG_AttachmentType(i) ) { value = i; mask &= ~BG_AttachmentType(i); break; }
			UI_ClassSpin(&classEdit.menu, &classEdit.attachment[gun][entry], attachmentLabels[gun][entry],
				classEdit.attachmentNames, value, 140 + gun * 60 + entry * 20);
		}
	}
	UI_ClassSpin(&classEdit.menu, &classEdit.lethal, "GRENADE", lethals, loadout.lethal == WP_SEMTEX ? 2 : !!loadout.lethal, 240);
	UI_ClassSpin(&classEdit.menu, &classEdit.lethalCount, "GRENADE COUNT", counts, loadout.lethalCount ? loadout.lethalCount - 1 : 0, 260);
	UI_ClassSpin(&classEdit.menu, &classEdit.tactical, "TACTICAL", tacticals, !!loadout.tactical, 280);
	UI_ClassSpin(&classEdit.menu, &classEdit.tacticalCount, "TACTICAL COUNT", counts, loadout.tacticalCount ? loadout.tacticalCount - 1 : 0, 300);
	for ( i = 0; i < BG_PERK_SLOTS; i++ ) {
		UI_ClassSpin(&classEdit.menu, &classEdit.perk[i], perkLabels[i], classEdit.perkNames, loadout.perk[i], 322 + i * 22);
		classEdit.perk[i].generic.callback = UI_ClassAttachmentsChanged;
	}
	classEdit.primary.generic.callback = classEdit.secondary.generic.callback = UI_ClassAttachmentsChanged;
	UI_ClassAttachmentsChanged(NULL, QM_ACTIVATED);
	classEdit.lethal.generic.callback = UI_ClassEquipmentChanged;
	classEdit.tactical.generic.callback = UI_ClassEquipmentChanged;
	UI_ClassEquipmentChanged(NULL, QM_ACTIVATED);
	UI_ClassButton(&classEdit.menu, &classEdit.save, ID_CLASS_SAVE, "SAVE CLASS", 224, 427, UI_ClassEditEvent);
	UI_ClassButton(&classEdit.menu, &classEdit.back, ID_CLASS_BACK, "BACK", 440, 427, UI_ClassEditEvent);
	UI_PushMenu(&classEdit.menu);
}
