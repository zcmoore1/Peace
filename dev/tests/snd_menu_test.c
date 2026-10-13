/* Exercise the real host menu's type mapping, arena filtering and launch
 * settings. Rendering and engine command execution are the only boundaries. */
#include "../../code/q3_ui/ui_startserver.c"
static int checks, varCount;
static struct { char name[40], value[80]; } vars[40];
static char commands[2048];
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
static int Find(const char *name) {
 int n; for(n=0;n<varCount;n++) if(!Q_stricmp(vars[n].name,name)) return n;
 CHECK(varCount<ARRAY_LEN(vars)); Q_strncpyz(vars[n].name,name,sizeof(vars[n].name));
 varCount++; return n;
}
void trap_Cvar_Set(const char *name,const char *value) {
 int n=Find(name); Q_strncpyz(vars[n].value,value,sizeof(vars[n].value));
}
void trap_Cvar_SetValue(const char *name,float value) { trap_Cvar_Set(name,va("%i",(int)value)); }
float trap_Cvar_VariableValue(const char *name) { return atof(vars[Find(name)].value); }
void trap_Cmd_ExecuteText(int when,const char *text) {
 (void)when; Q_strcat(commands,sizeof(commands),text);
}
const char *UI_GetArenaInfoByNumber(int n) {
 return n==0 ? "\\map\\peace_ladder\\type\\ffa tourney team snd" : "\\map\\q3dm1\\type\\ffa";
}
const char *UI_GetArenaInfoByMap(const char *map) { (void)map; return UI_GetArenaInfoByNumber(0); }
char *UI_GetBotInfoByName(const char *name) { (void)name; return ""; }
char *UI_GetBotInfoByNumber(int n) { (void)n; return ""; }
int UI_GetNumArenas(void) { return 2; }
int main(void) {
 int n; char mapname[64];
 CHECK(GT_SND==8 && ARRAY_LEN(gametype_remap2)>GT_SND);
 CHECK(gametype_remap[gametype_remap2[GT_SND]]==GT_SND);
 CHECK(!strcmp(gametype_items[gametype_remap2[GT_SND]],"Search and Destroy"));
 CHECK(GametypeBits("snd")==(1<<GT_SND));
 CHECK(!(GametypeBits("ctf team ffa")&(1<<GT_SND)));
 s_startserver.mapname.string=mapname;
 s_startserver.gametype.curvalue=gametype_remap2[GT_SND];
 StartServer_GametypeEvent(NULL,QM_ACTIVATED);
 CHECK(s_startserver.nummaps==1 && s_startserver.maplist[0]==0);
 CHECK(!strcmp(mapname,"PEACE_LADDER"));
 s_serveroptions.gametype=GT_SND;
 for(n=1;n<PLAYER_SLOTS;n++) s_serveroptions.playerType[n].curvalue=1;
 ServerOptions_InitBotNames();
 for(n=1;n<PLAYER_SLOTS;n++) CHECK(s_serveroptions.playerType[n].curvalue==0);
 strcpy(s_serveroptions.fraglimit.field.buffer,"4");
 strcpy(s_serveroptions.timelimit.field.buffer,"150");
 trap_Cvar_SetValue("g_sndPractice",1); trap_Cvar_SetValue("ui_ffa_fraglimit",37);
 ServerOptions_Start();
 CHECK(trap_Cvar_VariableValue("g_sndWinLimit")==4);
 CHECK(trap_Cvar_VariableValue("g_sndRoundTime")==150);
 CHECK(trap_Cvar_VariableValue("g_sndPractice")==0);
 CHECK(trap_Cvar_VariableValue("fraglimit")==0 && trap_Cvar_VariableValue("capturelimit")==0);
 CHECK(trap_Cvar_VariableValue("timelimit")==0 && trap_Cvar_VariableValue("ui_ffa_fraglimit")==37);
 CHECK(strstr(commands,"map peace_ladder") && !strstr(commands,"addbot"));
 printf("PASS: %i S&D host-menu checks\n",checks); return 0;
}
