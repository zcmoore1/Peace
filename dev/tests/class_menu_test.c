/* Real menu callbacks with an in-memory engine cvar/menu boundary. Check saved
 * definitions survive reopening, cancel discards drafts, and selection uses
 * userinfo rather than a gameplay/refill command. Rendering is smoke-tested
 * separately in the game. */
#include "../../code/q3_ui/ui_classes.c"

static struct { char name[32], value[BG_CLASS_STRING_SIZE]; int flags; } vars[16];
static int varCount, checks, depth;
vec4_t color_white={1,1,1,1}, color_yellow={1,1,0,1};
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL Com_Error(int level,const char *fmt,...) {
 va_list args; (void)level; va_start(args,fmt); vfprintf(stderr,fmt,args);
 va_end(args); exit(1);
}
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
 int i=Find(name); Q_strncpyz(vars[i].value,value,sizeof(vars[i].value));
}
void trap_Cvar_SetValue(const char *name,float value) {
 char text[32]; Com_sprintf(text,sizeof(text),"%i",(int)value); trap_Cvar_Set(name,text);
}
void trap_Cvar_VariableStringBuffer(const char *name,char *buffer,int size) {
 Q_strncpyz(buffer,vars[Find(name)].value,size);
}
float trap_Cvar_VariableValue(const char *name) { return atof(vars[Find(name)].value); }
void Menu_AddItem(menuframework_s *menu,void *ptr) {
 menucommon_s *item=ptr; item->parent=menu; item->menuPosition=menu->nitems++;
 if(item->type==MTYPE_FIELD) memset(&((menufield_s *)ptr)->field.buffer,0,MAX_EDIT_LINE);
}
void UI_PushMenu(menuframework_s *menu) { (void)menu; depth++; }
void UI_PopMenu(void) { depth--; CHECK(depth>=0); }
void Menu_Draw(menuframework_s *menu) { (void)menu; }
void UI_FillRect(float x,float y,float width,float height,const float *color) {
 (void)x;(void)y;(void)width;(void)height;(void)color;
}
void UI_DrawString(int x,int y,const char *text,int style,vec4_t color) {
 (void)x;(void)y;(void)text;(void)style;(void)color;
}
int main(void) {
 bg_class_t cl;
 char saved[BG_CLASS_STRING_SIZE], active[BG_CLASS_STRING_SIZE];
 int i;
 UI_ClassesMenu(); CHECK(depth==1);
 CHECK((vars[Find("peace_loadout")].flags&(CVAR_ARCHIVE|CVAR_USERINFO))==(CVAR_ARCHIVE|CVAR_USERINFO));
 CHECK(classSelect.edit.generic.flags&QMF_GRAYED);
 for(i=0;i<BG_CUSTOM_CLASS_COUNT;i++) {
  char name[32]; Com_sprintf(name,sizeof(name),"peace_class_%i",i);
  CHECK(vars[Find(name)].flags==CVAR_ARCHIVE);
  CHECK(BG_ParseClass(vars[Find(name)].value,&cl));
 }
 strcpy(active,vars[Find("peace_loadout")].value);
 UI_CreateClassMenu(0); CHECK(!strcmp(classEdit.name.field.buffer,"Custom 1"));
 strcpy(classEdit.name.field.buffer,"Discarded");
 UI_ClassEditEvent(&classEdit.back,QM_ACTIVATED); CHECK(depth==1);
 UI_CreateClassMenu(0); CHECK(!strcmp(classEdit.name.field.buffer,"Custom 1"));
 strcpy(classEdit.name.field.buffer,"Trickshot");
 classEdit.primary.curvalue=1; classEdit.secondary.curvalue=1;
 UI_ClassEditEvent(&classEdit.save,QM_ACTIVATED);
 CHECK(depth==2 && classEdit.error[0]);
 classEdit.secondary.curvalue=2;
 classEdit.lethal.curvalue=0; classEdit.tacticalCount.curvalue=2;
 UI_ClassEditEvent(&classEdit.save,QM_ACTIVATED); CHECK(depth==1);
 CHECK(!strcmp(active,vars[Find("peace_loadout")].value));
 strcpy(saved,vars[Find("peace_class_0")].value);
 CHECK(BG_ParseClass(saved,&cl)); CHECK(!strcmp(cl.name,"Trickshot"));
 CHECK(cl.slot[0]==WP_SHOTGUN && cl.slot[1]==WP_RAILGUN);
 CHECK(cl.lethal==WP_NONE && cl.lethalCount==0 && cl.tacticalCount==3);
 UI_ClassSelectEvent(&classSelect.equip,QM_ACTIVATED);
 CHECK(!strcmp(saved,vars[Find("peace_loadout")].value));
 CHECK(trap_Cvar_VariableValue("peace_classSelected")==BG_ClassCount());
 UI_PopMenu(); UI_ClassesMenu();
 CHECK(classSelect.pick.curvalue==BG_ClassCount());
 CHECK(!strcmp(classSelect.names[BG_ClassCount()],"Trickshot"));
 UI_CreateClassMenu(0); CHECK(!strcmp(classEdit.name.field.buffer,"Trickshot"));
 strcpy(classEdit.name.field.buffer,"Equipped edit");
 UI_ClassEditEvent(&classEdit.save,QM_ACTIVATED);
 CHECK(BG_ParseClass(vars[Find("peace_loadout")].value,&cl));
 CHECK(!strcmp(cl.name,"Equipped edit"));
 trap_Cvar_Set("peace_class_1","corrupt"); UI_ReadClasses();
 CHECK(BG_ValidateClass(&classSelect.classes[BG_ClassCount()+1]));
 CHECK(!strcmp(classSelect.names[BG_ClassCount()+1],"Custom 2"));
 printf("PASS: %d class menu persistence checks\n",checks); return 0;
}
