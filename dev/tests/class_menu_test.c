/* Real menu stack, widgets, mouse hit testing and gamepad translation. Only
 * engine syscalls are mocked; class editing must never issue gameplay commands. */
#include "../../code/q3_ui/ui_classes.c"
#include "../../code/q3_ui/ui_ingame.c"

static struct { char name[40], value[BG_CLASS_STRING_SIZE]; int flags; } vars[32];
static int varCount, checks, keyCatcher, clearKeys, drawCalls, commands;
static connstate_t connection = CA_DISCONNECTED;
static qboolean overstrike;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)

void trap_Error(const char *message) { fprintf(stderr,"%s\n",message); exit(1); }
void trap_Print(const char *message) { (void)message; }
static int Find(const char *name) {
 int i; for(i=0;i<varCount;i++) if(!strcmp(name,vars[i].name)) return i;
 CHECK(varCount<ARRAY_LEN(vars));
 Q_strncpyz(vars[i].name,name,sizeof(vars[i].name)); varCount++; return i;
}
void trap_Cvar_Register(vmCvar_t *vm,const char *name,const char *value,int flags) {
 int old=varCount,i=Find(name); (void)vm;
 if(i==old) Q_strncpyz(vars[i].value,value,sizeof(vars[i].value));
 vars[i].flags|=flags;
}
void trap_Cvar_Set(const char *name,const char *value) {
 Q_strncpyz(vars[Find(name)].value,value,BG_CLASS_STRING_SIZE);
}
void trap_Cvar_SetValue(const char *name,float value) { trap_Cvar_Set(name,va("%i",(int)value)); }
void trap_Cvar_VariableStringBuffer(const char *name,char *buffer,int size) {
 Q_strncpyz(buffer,vars[Find(name)].value,size);
}
float trap_Cvar_VariableValue(const char *name) { return atof(vars[Find(name)].value); }
void trap_GetClientState(uiClientState_t *state) { memset(state,0,sizeof(*state)); state->connState=connection; }
int trap_GetConfigString(int index,char *buffer,int size) { (void)index; Q_strncpyz(buffer,"",size); return 0; }
void trap_Cmd_ExecuteText(int when,const char *text) { (void)when; (void)text; commands++; }
void trap_Key_SetCatcher(int catcher) { keyCatcher=catcher; }
int trap_Key_GetCatcher(void) { return keyCatcher; }
void trap_Key_ClearStates(void) { clearKeys++; }
qboolean trap_Key_IsDown(int key) { (void)key; return qfalse; }
qboolean trap_Key_GetOverstrikeMode(void) { return overstrike; }
void trap_Key_SetOverstrikeMode(qboolean state) { overstrike=state; }
void trap_GetClipboardData(char *buffer,int size) { Q_strncpyz(buffer,"",size); }
void trap_S_StartLocalSound(sfxHandle_t sound,int channel) { (void)sound; (void)channel; }
qhandle_t trap_R_RegisterShaderNoMip(const char *name) { (void)name; return 1; }
void trap_R_SetColor(const float *color) { (void)color; }
void trap_R_DrawStretchPic(float x,float y,float w,float h,float s1,float t1,float s2,float t2,qhandle_t shader) {
 (void)x; (void)y; (void)w; (void)h; (void)s1; (void)t1; (void)s2; (void)t2; (void)shader; drawCalls++;
}
/* Unrelated pause actions are intentionally unreachable in this harness. */
void UI_TeamMainMenu(void) { CHECK(0); }
void UI_SetupMenu(void) { CHECK(0); }
void UI_CreditMenu(void) { CHECK(0); }
void UI_ServerInfoMenu(void) { CHECK(0); }
void UI_AddBotsMenu(void) { CHECK(0); }
void UI_RemoveBotsMenu(void) { CHECK(0); }
void UI_TeamOrdersMenu(void) { CHECK(0); }
void UI_ConfirmMenu(const char *question,void (*draw)(void),void (*action)(qboolean)) {
 (void)question; (void)draw; (void)action; CHECK(0);
}

static void Key(int key) { UI_KeyEvent(key,qtrue); UI_KeyEvent(key,qfalse); }
static void Focus(void *item) { Menu_SetCursorToItem(uis.activemenu,item); }
static void Activate(void *item) { Focus(item); Key(K_PAD0_A); }
static void Click(classRow_t *row) {
 UI_MouseEvent(row->generic.left+8-uis.cursorx,row->generic.top+8-uis.cursory);
 Key(K_MOUSE1);
}
static classRow_t *Option(classPage_t *page,int value) {
 int i; for(i=0;i<page->count;i++) if(page->rows[i].value==value) return &page->rows[i];
 CHECK(0); return NULL;
}
static void Screen(menuframework_s *menu,int depth) {
 int i,j;
 CHECK(uis.activemenu==menu && uis.menusp==depth && depth<=MAX_MENUDEPTH);
 CHECK(menu->nitems>0);
 for(i=0;i<menu->nitems;i++) {
  menucommon_s *a=menu->items[i];
  CHECK(a->left>=0 && a->right<=640 && a->top>=0 && a->bottom<=480);
  CHECK(a->left<a->right && a->top<a->bottom);
  for(j=i+1;j<menu->nitems;j++) {
   menucommon_s *b=menu->items[j];
   CHECK(a->right<=b->left || b->right<=a->left || a->bottom<=b->top || b->bottom<=a->top);
  }
 }
 if(menu->draw) menu->draw(); else Menu_Draw(menu);
 CHECK(drawCalls>0);
}
static void OpenEditor(int slot) {
 UI_ForceMenuOff(); UI_ClassesMenu();
 Focus(&classSelect.rows[BG_ClassCount()+slot]); Key(K_PAD0_X);
 Screen(&classEdit.menu,2);
 CHECK(classEdit.customSlot==slot);
}
static void ChooseWeapon(int slot,int weapon) {
 Activate(&classEdit.gun[slot]); Screen(&classCategories.menu,3);
 Activate(Option(&classCategories,UI_ClassWeaponCategory(weapon))); Screen(&classPicker.menu,4);
 Click(Option(&classPicker,weapon)); Screen(&classEdit.menu,2);
 CHECK(classEdit.draft.slot[slot]==weapon);
}
static void ChoosePerk(int slot,int perk) {
 Activate(&classEdit.perk[slot]); Screen(&classPicker.menu,3);
 Activate(Option(&classPicker,perk)); Screen(&classEdit.menu,2);
 CHECK(classEdit.draft.perk[slot]==perk);
}
static void TypeName(const char *name) {
 Key(K_CHAR_FLAG|3); /* Ctrl+C clears the stock field. */
 while(*name) Key(K_CHAR_FLAG|*name++);
}
static bg_class_t ReadClass(const char *name) {
 bg_class_t cl; CHECK(BG_ParseClass(vars[Find(name)].value,&cl)); return cl;
}

int main(void) {
 bg_class_t cl;
 char saved[BG_CLASS_STRING_SIZE], active[BG_CLASS_STRING_SIZE];
 int i,j,seen[WP_NUM_WEAPONS],before;
 uis.xscale=uis.yscale=1;
 UI_ClassesMenu(); Screen(&classSelect.menu,1);
 CHECK(classSelect.choice==0 && (classSelect.edit.generic.flags&QMF_GRAYED));
 Key(K_PAD0_X); CHECK(uis.menusp==1);
 CHECK((vars[Find("peace_loadout")].flags&(CVAR_ARCHIVE|CVAR_USERINFO))==(CVAR_ARCHIVE|CVAR_USERINFO));
 for(i=0;i<BG_CUSTOM_CLASS_COUNT;i++) {
  char name[32]; Com_sprintf(name,sizeof(name),"peace_class_%i",i);
  CHECK(vars[Find(name)].flags==CVAR_ARCHIVE); cl=ReadClass(name);
  CHECK(BG_ValidateClass(&cl));
 }
 strcpy(active,vars[Find("peace_loadout")].value);
 strcpy(saved,vars[Find("peace_class_0")].value);
 Focus(&classSelect.rows[BG_ClassCount()]); Key(K_PAD0_DPAD_DOWN);
 CHECK(Menu_ItemAtCursor(&classSelect.menu)==&classSelect.rows[BG_ClassCount()+1]);
 Click(&classSelect.editSlot[2]); CHECK(uis.activemenu==&classEdit.menu && classEdit.customSlot==2);
 Key(K_ESCAPE);
 OpenEditor(0);
 CHECK(!strcmp(classEdit.draft.name,"Custom 1") && !UI_ClassDirty());
 CHECK(Menu_ItemAtCursor(&classEdit.menu)==&classEdit.gun[0]);
 Key(K_PAD0_DPAD_RIGHT); CHECK(Menu_ItemAtCursor(&classEdit.menu)==&classEdit.gun[1]);
 Key(K_PAD0_DPAD_LEFT); CHECK(Menu_ItemAtCursor(&classEdit.menu)==&classEdit.gun[0]);
 Key(K_PAD0_DPAD_DOWN); CHECK(Menu_ItemAtCursor(&classEdit.menu)==&classEdit.attachment[0]);
 Key(K_PAD0_DPAD_UP);
 ChooseWeapon(0,WP_M40A3);
 CHECK(!classEdit.draft.attachments[0]);
 CHECK(!strcmp(saved,vars[Find("peace_class_0")].value) && !strcmp(active,vars[Find("peace_loadout")].value));
 Activate(&classEdit.gun[0]); Activate(Option(&classCategories,CATEGORY_SNIPERS));
 Key(K_PAD0_B); Screen(&classCategories.menu,3);
 CHECK(Menu_ItemAtCursor(&classCategories.menu)==Option(&classCategories,CATEGORY_SNIPERS));
 Key(K_LEFTARROW); Screen(&classEdit.menu,2);
 CHECK(Menu_ItemAtCursor(&classEdit.menu)==&classEdit.gun[0] && classEdit.draft.slot[0]==WP_M40A3);
 Key(K_ESCAPE); Screen(&classDiscard.menu,3);
 CHECK(Menu_ItemAtCursor(&classDiscard.menu)==&classDiscard.keep);
 Key(K_PAD0_B); Screen(&classEdit.menu,2);
 Key(K_MOUSE2); Activate(&classDiscard.discard); Screen(&classSelect.menu,1);
 CHECK(!strcmp(saved,vars[Find("peace_class_0")].value));
 OpenEditor(0); CHECK(classEdit.draft.slot[0]==WP_MACHINEGUN);
 Key(K_PAD0_B); CHECK(uis.menusp==1);

 /* Every existing gun is reachable exactly once; duplicate slots are blocked. */
 OpenEditor(0); memset(seen,0,sizeof(seen));
 Activate(&classEdit.gun[0]);
 for(i=0;i<CATEGORY_COUNT;i++) {
  Activate(Option(&classCategories,i)); Screen(&classPicker.menu,4);
  CHECK(classPicker.count>0);
  for(j=0;j<classPicker.count;j++) seen[classPicker.rows[j].value]++;
  Key(K_ESCAPE);
 }
 for(i=0;i<BG_ClassWeaponCount();i++) CHECK(seen[BG_ClassWeapon(i)]==1);
 Activate(Option(&classCategories,CATEGORY_SHOTGUNS));
 CHECK(Option(&classPicker,WP_SHOTGUN)->generic.flags&QMF_GRAYED);
 Click(Option(&classPicker,WP_SHOTGUN)); CHECK(uis.menusp==4 && classEdit.draft.slot[0]==WP_MACHINEGUN);
 Key(K_ESCAPE); Key(K_ESCAPE);

 /* Equipment commits only after choosing a quantity. */
 Activate(&classEdit.lethal); Activate(Option(&classPicker,WP_SEMTEX)); Screen(&classQuantity.menu,4);
 CHECK(classEdit.draft.lethal==WP_FRAG);
 Key(K_PAD0_B); CHECK(classEdit.draft.lethal==WP_FRAG);
 Activate(Option(&classPicker,WP_SEMTEX)); Click(Option(&classQuantity,3)); Screen(&classEdit.menu,2);
 CHECK(classEdit.draft.lethal==WP_SEMTEX && classEdit.draft.lethalCount==3);
 Activate(&classEdit.tactical); Activate(Option(&classPicker,WP_NONE));
 CHECK(classEdit.draft.tactical==WP_NONE && classEdit.draft.tacticalCount==0);
 Activate(&classEdit.tactical); Activate(Option(&classPicker,WP_FLASH)); Activate(Option(&classQuantity,2));
 CHECK(classEdit.draft.tactical==WP_FLASH && classEdit.draft.tacticalCount==2);

 ChoosePerk(0,PERK_QUICK_RELOAD);
 Activate(&classEdit.perk[1]);
 CHECK(Option(&classPicker,PERK_QUICK_RELOAD)->generic.flags&QMF_GRAYED);
 Click(Option(&classPicker,PERK_QUICK_RELOAD)); CHECK(uis.menusp==3 && classEdit.draft.perk[1]==PERK_NONE);
 Activate(Option(&classPicker,PERK_SECOND_NATURE)); CHECK(UI_ClassAttachmentLimit()==2);
 Activate(&classEdit.attachment[0]); Screen(&classPicker.menu,3);
 Activate(Option(&classPicker,BG_ATTACHMENT_REDDOT)); CHECK(classEdit.draft.attachments[0]==BG_ATTACHMENT_REDDOT);
 Activate(Option(&classPicker,BG_ATTACHMENT_REDDOT)); CHECK(!classEdit.draft.attachments[0]);
 Activate(Option(&classPicker,BG_ATTACHMENT_REDDOT)); Activate(&classPicker.back);
 ChoosePerk(1,PERK_NO_FALL_DAMAGE); CHECK(UI_ClassAttachmentLimit()==1 && classEdit.draft.attachments[0]==BG_ATTACHMENT_REDDOT);
 ChooseWeapon(0,WP_M40A3); CHECK(!classEdit.draft.attachments[0]);
 Activate(&classEdit.attachment[0]); CHECK(classPicker.count==1 && !classPicker.rows[0].value);
 CHECK(strstr(classPicker.rows[0].description,"built-in")); Key(K_ESCAPE);
 ChoosePerk(2,PERK_EXPEDITIOUS);

 /* Actual name widget, cancel and validation; Save Only versus equip. */
 Activate(&classEdit.name); Screen(&className.menu,3);
 TypeName("Invalid|name"); Key(K_ENTER); CHECK(uis.menusp==3 && className.error[0]);
 TypeName("Trickshot"); Key(K_ENTER); CHECK(!strcmp(classEdit.draft.name,"Trickshot"));
 Activate(&classEdit.name); TypeName("Canceled"); Key(K_PAD0_B);
 CHECK(!strcmp(classEdit.draft.name,"Trickshot"));
 Activate(&classEdit.save); Screen(&classSelect.menu,1);
 CHECK(!strcmp(active,vars[Find("peace_loadout")].value));
 cl=ReadClass("peace_class_0"); CHECK(!strcmp(cl.name,"Trickshot"));
 CHECK(cl.slot[0]==WP_M40A3 && cl.slot[1]==WP_SHOTGUN && cl.lethal==WP_SEMTEX && cl.lethalCount==3);
 CHECK(cl.tacticalCount==2 && cl.perk[0]==PERK_QUICK_RELOAD && cl.perk[1]==PERK_NO_FALL_DAMAGE && cl.perk[2]==PERK_EXPEDITIOUS);
 Activate(&classSelect.rows[BG_ClassCount()]); CHECK(UI_ClassSelected()==BG_ClassCount());
 CHECK(!strcmp(vars[Find("peace_loadout")].value,vars[Find("peace_class_0")].value));
 OpenEditor(0); CHECK(!UI_ClassDirty());
 Activate(&classEdit.name); TypeName("Equipped edit"); Key(K_ENTER); Activate(&classEdit.save);
 cl=ReadClass("peace_loadout"); CHECK(!strcmp(cl.name,"Equipped edit"));

 for(i=0;i<BG_CUSTOM_CLASS_COUNT;i++) {
  OpenEditor(i); Activate(&classEdit.name); TypeName(va("Saved %i",i+1)); Key(K_ENTER); Activate(&classEdit.save);
  OpenEditor(i); CHECK(!strcmp(classEdit.draft.name,va("Saved %i",i+1))); Key(K_ESCAPE);
 }
 trap_Cvar_Set("peace_class_1","corrupt"); OpenEditor(1);
 CHECK(BG_ValidateClass(&classEdit.draft) && !strcmp(classEdit.draft.name,"Custom 2"));
 CHECK(!strcmp(vars[Find("peace_class_1")].value,"corrupt")); Key(K_ESCAPE);
 trap_Cvar_SetValue("peace_classSelected",999); UI_ForceMenuOff(); UI_ClassesMenu(); CHECK(classSelect.choice==0);
 UI_CreateClassMenu(-1); CHECK(classEdit.customSlot==0); Key(K_ESCAPE);

 /* The real pause menu reaches the editor; Save & Equip resumes the match. */
 connection=CA_ACTIVE; UI_ForceMenuOff(); trap_Cvar_SetValue("peace_classSelected",BG_ClassCount()+2);
 UI_InGameMenu(); CHECK(uis.activemenu==&s_ingame.menu);
 CHECK(!strcmp(s_ingame.classes.string,"CHANGE CLASS") && !strcmp(s_ingame.editloadout.string,"EDIT LOADOUT"));
 Activate(&s_ingame.editloadout); Screen(&classEdit.menu,3); CHECK(classEdit.customSlot==2);
 Activate(&classEdit.gun[0]); Screen(&classCategories.menu,4);
 Activate(Option(&classCategories,CATEGORY_SNIPERS)); Screen(&classPicker.menu,5);
 Activate(Option(&classPicker,WP_M40A3)); Screen(&classEdit.menu,3);
 before=clearKeys; Activate(&classEdit.equip);
 CHECK(!uis.activemenu && !uis.menusp && !(keyCatcher&KEYCATCH_UI) && clearKeys>before);
 CHECK(trap_Cvar_VariableValue("cl_paused")==0 && UI_ClassSelected()==BG_ClassCount()+2);
 cl=ReadClass("peace_loadout"); CHECK(cl.slot[0]==WP_M40A3);
 UI_InGameMenu(); Activate(&s_ingame.classes); Screen(&classSelect.menu,2);
 Click(&classSelect.rows[1]); CHECK(!uis.activemenu && UI_ClassSelected()==1);
 cl=ReadClass("peace_loadout"); CHECK(!strcmp(cl.name,"Sniper"));
 CHECK(commands==0);
 printf("PASS: %d class UI checks (real input, navigation, pause entry, drafts and persistence)\n",checks);
 return 0;
}
