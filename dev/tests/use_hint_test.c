/* The knife prompt as cgame draws it: the real CG_DrawWeaponPickup and
 * CG_DrawKnifePickup (cg_draw.c), the real button lookup, formatter and gate
 * (cg_usehint.c), and the real engine side of the new trap (cl_keys.c's
 * Key_GetBindingName over its keys[] binds, bound with the real
 * Key_SetBinding), and the engine's last-input-device note. Only the 2D
 * drawing calls, the config strings and the cvar system are stubbed. The trap's number is checked against
 * cg_syscalls.asm, which the QVM calls it by. */
#include "../../code/cgame/cg_draw.c"
#include "../../code/client/keycodes.h"

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
int cvar_modifiedFlags;
static int checks, lookups, cvarReads, joystick, drawnCount, fills;
static char drawn[4][128], configs[MAX_CONFIGSTRINGS][MAX_INFO_STRING];
static int drawnY[4];
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)

void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
/* cl_keys.c's binds, through the engine's own allocation calls */
void Z_Free(void *p) { free(p); }
char *CopyString(const char *in) { char *out=malloc(strlen(in)+1); strcpy(out,in); return out; }
void Key_SetBinding(int keynum, const char *binding);
int Key_GetBindingName(const char *binding, qboolean gamepad, char *name, int nameSize);
qboolean Key_IsGamepadKey(int keynum);
char *Key_KeynumToString(int keynum);
/* the trap, as cl_cgame.c answers CG_KEY_GETBINDINGNAME */
int trap_Key_GetBindingName(const char *binding, qboolean gamepad, char *name, int nameSize) {
 lookups++; return Key_GetBindingName(binding,gamepad,name,nameSize);
}
void trap_Cvar_VariableStringBuffer(const char *name, char *buffer, int size) {
 CHECK(!strcmp(name,"cl_gamepadInUse")); cvarReads++; Com_sprintf(buffer,size,"%i",joystick);
}
/* the engine's cvar, as CL_NoteInputDevice sets it */
static cvar_t inUse; static int cvarSets;
void Cvar_Set(const char *name, const char *value) {
 CHECK(!strcmp(name,"cl_gamepadInUse")); inUse.integer=atoi(value); cvarSets++;
}
void CL_NoteInputDevice(qboolean gamepad);
extern cvar_t *cl_gamepadInUse;
const char *CG_ConfigString(int index) { return configs[index]; }
void CG_DrawSmallString(int x, int y, const char *s, float alpha) {
 (void)alpha; CHECK(drawnCount<4);
 Q_strncpyz(drawn[drawnCount],s,sizeof(drawn[0]));
 CHECK(x==320-(int)strlen(s)*SMALLCHAR_WIDTH/2);	/* centred, as the gun's prompt */
 drawnY[drawnCount++]=y;
}
int CG_DrawStrlen(const char *s) { return strlen(s); }
void CG_FillRect(float x, float y, float w, float h, const float *color) { (void)x;(void)y;(void)w;(void)h;(void)color; fills++; }

static void Unbind(void) { int i; for(i=0;i<MAX_KEYS;i++) Key_SetBinding(i,""); }
/* Peace's shipped binds: dev/peace-autoexec.cfg's f, and peace-gamepad.cfg's X and d-pad up */
static void ShippedBinds(void) {
 Unbind(); Key_SetBinding('f',"+use"); Key_SetBinding(K_PAD0_X,"+usereload"); Key_SetBinding(K_PAD0_DPAD_UP,"+use");
 Key_SetBinding('r',"+reload"); Key_SetBinding(K_PAD0_RIGHTTRIGGER,"+attack");
}
static void Name(const char *bind, const char *shown) {
 char out[32]; CG_ReadableKeyName(bind,out,sizeof(out)); CHECK(!strcmp(out,shown));
}
static const char *Prompt(qboolean gamepad) {
 static char text[96]; char label[32]; qboolean hold, bound=CG_FindUseButton(gamepad,label,sizeof(label),&hold);
 CG_UsePromptText(bound,hold,label,"pick up knife",text,sizeof(text)); return text;
}
static void TestKeyNames(void) {
 Name("PAD0_X","X"); Name("PAD0_A","A"); Name("PAD0_B","B"); Name("PAD0_Y","Y");
 Name("PAD0_LEFTTRIGGER","LT"); Name("PAD0_RIGHTTRIGGER","RT");
 Name("PAD0_LEFTSHOULDER","LB"); Name("PAD0_RIGHTSHOULDER","RB");
 Name("PAD0_LEFTSTICK_CLICK","LS"); Name("PAD0_RIGHTSTICK_CLICK","RS");
 Name("PAD0_DPAD_UP","D-PAD UP"); Name("PAD0_DPAD_DOWN","D-PAD DOWN");
 Name("PAD0_DPAD_LEFT","D-PAD LEFT"); Name("PAD0_DPAD_RIGHT","D-PAD RIGHT");
 Name("PAD0_LEFTSTICK_UP","LS UP"); Name("PAD0_RIGHTSTICK_RIGHT","RS RIGHT");
 Name("PAD0_BACK","BACK"); Name("PAD0_START","START"); Name("PAD0_GUIDE","GUIDE");
 Name("PAD0_PADDLE1","P1"); Name("PAD0_PADDLE2","P3"); Name("PAD0_PADDLE3","P2"); Name("PAD0_PADDLE4","P4");
 Name("PAD0_MISC1","SHARE"); Name("PAD0_TOUCHPAD","TOUCHPAD");
 Name("f","F"); Name("F","F"); Name("MOUSE1","MOUSE1"); Name("KP_ENTER","KP ENTER");
 Name("SPACE","SPACE"); Name("JOY3","JOY3"); Name("AUX1","AUX1"); Name("0x1f","0x1f"); Name("","");
 { char tiny[4]; CG_ReadableKeyName("PAD0_DPAD_UP",tiny,sizeof(tiny)); CHECK(!strcmp(tiny,"D-P")); }
 /* every name the engine spells comes out readable, pad keys without their PAD0_ */
 { int k; char out[32];
   for(k=K_PAD0_A;k<=K_PAD0_TOUCHPAD;k++) {
    CG_ReadableKeyName(Key_KeynumToString(k),out,sizeof(out));
    CHECK(out[0] && strncmp(out,"PAD0",4) && !strchr(out,'_'));
   } }
}
static void TestEngineLookup(void) {
 char name[32]; int k;
 for(k=0;k<MAX_KEYS;k++)
  CHECK(Key_IsGamepadKey(k)==((k>=K_JOY1 && k<=K_JOY32) || (k>=K_AUX1 && k<=K_AUX16) || (k>=K_PAD0_A && k<=K_PAD0_TOUCHPAD)));
 CHECK(!Key_IsGamepadKey(K_MOUSE1) && !Key_IsGamepadKey('f') && !Key_IsGamepadKey(K_CONSOLE));
 ShippedBinds();
 CHECK(Key_GetBindingName("+usereload",qtrue,name,sizeof(name))==K_PAD0_X && !strcmp(name,"PAD0_X"));
 CHECK(Key_GetBindingName("+usereload",qfalse,name,sizeof(name))==-1 && !name[0]);
 CHECK(Key_GetBindingName("+use",qfalse,name,sizeof(name))=='f' && !strcmp(name,"f"));
 CHECK(Key_GetBindingName("+use",qtrue,name,sizeof(name))==K_PAD0_DPAD_UP && !strcmp(name,"PAD0_DPAD_UP"));
 CHECK(Key_GetBindingName("+USE",qfalse,name,sizeof(name))=='f');	/* as Key_GetKey: case-blind */
 CHECK(Key_GetBindingName("+us",qfalse,name,sizeof(name))==-1);	/* the whole bind, never a prefix */
 CHECK(Key_GetBindingName("",qfalse,name,sizeof(name))==-1 && Key_GetBindingName(NULL,qtrue,name,sizeof(name))==-1);
 Key_SetBinding(K_JOY3,"+usereload");	/* a plain joystick's button is a gamepad key, first by number */
 CHECK(Key_GetBindingName("+usereload",qtrue,name,sizeof(name))==K_JOY3 && !strcmp(name,"JOY3"));
 Key_SetBinding(K_MOUSE4,"+usereload");
 CHECK(Key_GetBindingName("+usereload",qfalse,name,sizeof(name))==K_MOUSE4);
}
static void TestButtonChoice(void) {
 ShippedBinds();
 /* the owner's pad: X is use/reload, held BG_USE_RELOAD_HOLD_TIME */
 CHECK(BG_UseHoldTime(BUTTON_USE_RELOAD)>0 && BG_UseHoldTime(BUTTON_USE)==0);
 CHECK(!strcmp(Prompt(qtrue),"Hold [X] to pick up knife"));
 /* keyboard: no use/reload key, so +use's f, which takes it on the press */
 CHECK(!strcmp(Prompt(qfalse),"Press [F] to pick up knife"));
 Key_SetBinding('e',"+usereload");
 CHECK(!strcmp(Prompt(qfalse),"Hold [E] to pick up knife"));
 Key_SetBinding('e',""); Key_SetBinding(K_PAD0_X,"");
 CHECK(!strcmp(Prompt(qtrue),"Press [D-PAD UP] to pick up knife"));
 Key_SetBinding(K_PAD0_DPAD_UP,"");
 CHECK(!strcmp(Prompt(qtrue),"Press [F] to pick up knife"));	/* then the other kind's */
 Key_SetBinding('f',""); Key_SetBinding(K_PAD0_LEFTSHOULDER,"+usereload");
 CHECK(!strcmp(Prompt(qfalse),"Hold [LB] to pick up knife"));
 Unbind();
 CHECK(!strcmp(Prompt(qtrue),"Bind +use to pick up knife") && !strcmp(Prompt(qfalse),"Bind +use to pick up knife"));
}
/* The device is read every frame; the binds are looked up when it changes, when
 * a prompt appears, and then at most once a second while it stays. */
static void TestCache(void) {
 char text[96]; int t, frames=0;
 ShippedBinds(); joystick=1; lookups=cvarReads=0;
 CG_UseButtonPrompt(5000,"pick up knife",text,sizeof(text));
 CHECK(!strcmp(text,"Hold [X] to pick up knife") && cvarReads==1 && lookups==1);
 for(t=5016;t<6000;t+=16) { CG_UseButtonPrompt(t,"pick up knife",text,sizeof(text)); frames++; }
 CHECK(frames>60 && cvarReads==frames+1 && lookups==1);
 joystick=0;	/* the player touches the keyboard: the next frame names its key */
 CG_UseButtonPrompt(6000-8,"pick up knife",text,sizeof(text));
 CHECK(!strcmp(text,"Press [F] to pick up knife") && lookups>1);
 joystick=1; lookups=0;	/* and back to the pad */
 CG_UseButtonPrompt(6000,"pick up knife",text,sizeof(text));
 CHECK(!strcmp(text,"Hold [X] to pick up knife") && lookups>0);
 Key_SetBinding(K_PAD0_LEFTSHOULDER,"+usereload"); Key_SetBinding(K_PAD0_X,"");	/* a rebind while shown */
 for(t=6016;t<6000+CG_USE_BUTTON_REFRESH;t+=16) {	/* not before the refresh */
  CG_UseButtonPrompt(t,"pick up knife",text,sizeof(text)); CHECK(!strcmp(text,"Hold [X] to pick up knife"));
 }
 CG_UseButtonPrompt(6000+CG_USE_BUTTON_REFRESH,"pick up knife",text,sizeof(text));
 CHECK(!strcmp(text,"Hold [LB] to pick up knife"));
 Key_SetBinding('g',"+usereload"); joystick=0;	/* a rebind while hidden shows when it reappears */
 CG_UseButtonPrompt(8000,"pick up knife",text,sizeof(text));
 CHECK(!strcmp(text,"Hold [G] to pick up knife"));
 lookups=0; CG_UseButtonPrompt(100,"pick up knife",text,sizeof(text));	/* a new map's clock */
 CHECK(lookups>0);
}
/* The engine notes which kind of input came last, and sets the cvar only when
 * it changes (Key_IsGamepadKey classifies the keys, TestEngineLookup). */
static void TestInputDevice(void) {
 cl_gamepadInUse=&inUse; inUse.integer=0; cvarSets=0;
 CL_NoteInputDevice(qfalse); CHECK(inUse.integer==0 && cvarSets==0);
 CL_NoteInputDevice(Key_IsGamepadKey(K_PAD0_X)); CHECK(inUse.integer==1 && cvarSets==1);
 CL_NoteInputDevice(qtrue); CHECK(cvarSets==1);
 CL_NoteInputDevice(Key_IsGamepadKey('f')); CHECK(inUse.integer==0 && cvarSets==2);
 CL_NoteInputDevice(Key_IsGamepadKey(K_MOUSE1)); CHECK(inUse.integer==0 && cvarSets==2);
 cl_gamepadInUse=NULL; CL_NoteInputDevice(qtrue); CHECK(cvarSets==2);	/* before the cvar exists */
}
static playerState_t *Live(void) {
 playerState_t *ps=&cg.predictedPlayerState;
 memset(&cg,0,sizeof(cg)); memset(&cgs,0,sizeof(cgs)); memset(configs,0,sizeof(configs));
 cg.snap=&cg.activeSnapshots[0]; cg.time=10000;
 ps->pm_type=PM_NORMAL; ps->stats[STAT_HEALTH]=100; ps->persistant[PERS_TEAM]=TEAM_FREE;
 ps->weapon=WP_MACHINEGUN; ps->weaponstate=WEAPON_READY;
 ps->cursorHint=CURSOR_HINT_KNIFE; ps->cursorHintEntIndex=MAX_CLIENTS+3; ps->pickupTarget=ENTITYNUM_NONE;
 cg.snap->ps=*ps; drawnCount=fills=0;
 return ps;
}
static void TestGate(void) {
 playerState_t *ps, *sv; int i;
 ShippedBinds(); joystick=1;
 ps=Live(); sv=&cg.snap->ps; CHECK(CG_KnifePromptShown(ps,sv) && CG_KnifePromptShown(ps,NULL));
 for(i=CURSOR_HINT_NONE;i<CURSOR_HINT_COUNT;i++) { ps->cursorHint=i; CHECK(CG_KnifePromptShown(ps,sv)==(i==CURSOR_HINT_KNIFE)); }
 ps=Live(); ps->pm_flags|=PMF_SPRINTING; CHECK(!CG_KnifePromptShown(ps,sv));
 ps=Live(); ps->pm_flags|=PMF_FOLLOW; CHECK(!CG_KnifePromptShown(ps,sv));
 ps=Live(); ps->stats[STAT_HEALTH]=0; CHECK(!CG_KnifePromptShown(ps,sv));
 ps=Live(); ps->pm_type=PM_DEAD; CHECK(!CG_KnifePromptShown(ps,sv));
 ps=Live(); ps->pm_type=PM_SPECTATOR; CHECK(!CG_KnifePromptShown(ps,sv));
 ps=Live(); ps->persistant[PERS_TEAM]=TEAM_SPECTATOR; CHECK(!CG_KnifePromptShown(ps,sv));
 ps=Live(); ps->weaponAction=WA_TACTICAL; CHECK(BG_OffhandOut(ps) && !CG_KnifePromptShown(ps,sv));
 ps=Live(); ps->weaponAction=WA_LETHAL|WA_FIRED; CHECK(!BG_OffhandOut(ps) && CG_KnifePromptShown(ps,sv));	/* the gun is back */
 /* a plain reload hold on a floor gun keeps the gun's prompt */
 ps=Live(); ps->pickupHeld=1; ps->pickupTime=1; CHECK(!CG_KnifePromptShown(ps,sv));
 ps->pickupTime=0; CHECK(CG_KnifePromptShown(ps,sv));
 /* a use/reload hold the server gave a floor gun (its pickupHeld stays 3) keeps
  * the gun's prompt, even after a knife comes into reach: that hold takes the gun */
 ps=Live(); sv->pickupHeld=3; sv->pickupTime=100; ps->pickupHeld=3; ps->pickupTime=116; CHECK(!CG_KnifePromptShown(ps,sv));
 sv->pickupTime=0; CHECK(CG_KnifePromptShown(ps,sv));
 /* a hold the knife took is 2 on the server, though prediction still counts it
  * as a gun's hold until the snapshot says so */
 ps=Live(); sv->pickupHeld=2; ps->pickupHeld=3; ps->pickupTime=48; CHECK(CG_KnifePromptShown(ps,sv));
 ps->pickupHeld=2; CHECK(CG_KnifePromptShown(ps,sv));
}
/* The real draw: in the gun prompt's place, instead of it; the bomb is the server's type. */
static void TestDraw(void) {
 playerState_t *ps; entityState_t floor;
 ShippedBinds(); joystick=1;
 ps=Live(); CG_DrawWeaponPickup();
 CHECK(drawnCount==1 && !strcmp(drawn[0],"Hold [X] to pick up knife") && drawnY[0]==300 && !fills);
 ps=Live(); ps->cursorHint=CURSOR_HINT_NONE; CG_DrawWeaponPickup(); CHECK(drawnCount==0);
 /* a floor gun in reach too: the knife outranks it */
 memset(&floor,0,sizeof(floor)); floor.eType=ET_ITEM; floor.modelindex=BG_FindItemForWeapon(WP_RAILGUN)-bg_itemlist;
 ps=Live(); ps->pickupTarget=MAX_CLIENTS+4; cg_entities[MAX_CLIENTS+4].currentState=floor;
 CG_DrawWeaponPickup(); CHECK(drawnCount==1 && !strcmp(drawn[0],"Hold [X] to pick up knife"));
 ps->cursorHint=CURSOR_HINT_WEAPON; drawnCount=0;
 CG_DrawWeaponPickup(); CHECK(drawnCount==1 && !strncmp(drawn[0],"Hold RELOAD: ",13));
 ps->cursorHint=CURSOR_HINT_KNIFE; ps->pickupHeld=1; ps->pickupTime=50; drawnCount=0;
 CG_DrawWeaponPickup(); CHECK(drawnCount==1 && !strncmp(drawn[0],"Hold RELOAD: ",13) && fills==2);
 /* a use/reload hold the server gave the gun keeps the gun's prompt and bar */
 ps->pickupHeld=3; ps->pickupTime=50; cg.snap->ps.pickupHeld=3; cg.snap->ps.pickupTime=34; drawnCount=fills=0;
 CG_DrawWeaponPickup(); CHECK(drawnCount==1 && !strncmp(drawn[0],"Hold RELOAD: ",13) && fills==2);
 cg.snap->ps.pickupHeld=2; drawnCount=fills=0;	/* the knife took it */
 CG_DrawWeaponPickup(); CHECK(drawnCount==1 && !strcmp(drawn[0],"Hold [X] to pick up knife") && !fills);
 /* the carrier at a site: where the bomb owns the use the server's type says
  * so, and there is no knife prompt; inside a site's radius with the site out
  * of sight the server gives the press to the knife, and so does the prompt */
 ps=Live(); cgs.gametype=GT_SND; cg.clientNum=0; VectorSet(cg.snap->ps.origin,10,0,0);
 Q_strncpyz(configs[CS_SND],va("\\phase\\%i\\carrier\\0\\attack\\%i",SND_LIVE,TEAM_RED),sizeof(configs[0]));
 Q_strncpyz(configs[CS_SND_SITES],"\\s0\\0 0 0 72",sizeof(configs[0]));
 ps->cursorHint=CURSOR_HINT_OBJECTIVE; ps->cursorHintEntIndex=ENTITYNUM_WORLD;
 CG_DrawWeaponPickup(); CHECK(drawnCount==0);
 ps->cursorHint=CURSOR_HINT_KNIFE; ps->cursorHintEntIndex=MAX_CLIENTS+3;
 CG_DrawWeaponPickup(); CHECK(drawnCount==1 && !strcmp(drawn[0],"Hold [X] to pick up knife"));
}
static void TestTrapNumber(void) {
 FILE *f=fopen("code/cgame/cg_syscalls.asm","r"); char line[256], name[64]; int number, found=0;
 CHECK(f!=NULL);
 while(fgets(line,sizeof(line),f))
  if(sscanf(line," equ %63s %d",name,&number)==2 && !strcmp(name,"trap_Key_GetBindingName")) {
   CHECK(number==-1-CG_KEY_GETBINDINGNAME); found++;
  }
 fclose(f); CHECK(found==1);
 CHECK(CG_KEY_GETBINDINGNAME==CG_R_BLENDMODELPOSES+1);	/* appended: nothing before it moved */
}
int main(void) {
 TestKeyNames(); TestEngineLookup(); TestButtonChoice(); TestCache(); TestInputDevice(); TestGate(); TestDraw(); TestTrapNumber();
 printf("PASS: %d use-hint prompt checks\n",checks); return 0;
}
