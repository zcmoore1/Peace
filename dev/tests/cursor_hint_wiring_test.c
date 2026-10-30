/* The sea lion's server wiring, through the real ClientEndFrame (g_active.c),
 * the real G_FreeEntity (g_utils.c) and the real hint pass (g_items.c): the
 * end-frame pass writes the use hint, a free of what it names empties it, and
 * a usable S&D site or bomb is a live hint that is never freed. Only engine
 * traps and modules outside these three files are stubbed. */
#include "../../code/game/g_local.h"
static int checks;
static qboolean bombUsable;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
vmCvar_t g_gametype, g_weaponRespawn, g_weaponTeamRespawn, g_smoothClients;
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { (void)code;(void)fmt; abort(); }
void QDECL G_Error(const char *fmt,...) { (void)fmt; abort(); }
void QDECL G_Printf(const char *fmt,...) { (void)fmt; }
void QDECL G_LogPrintf(const char *fmt,...) { (void)fmt; }
void ClientBegin(int n) { (void)n; }
void G_Damage(gentity_t *t,gentity_t *i,gentity_t *a,vec3_t d,vec3_t p,int damage,int flags,int mod) {(void)t;(void)i;(void)a;(void)d;(void)p;(void)damage;(void)flags;(void)mod;}
qboolean G_SNDClientEndFrame(gentity_t *e) { (void)e; return qfalse; }
qboolean G_SNDUsable(gentity_t *e) { (void)e; return bombUsable; }
int Pickup_Team(gentity_t *e,gentity_t *o) { (void)e;(void)o; return 1; }
void Team_CheckDroppedItem(gentity_t *e) { (void)e; }
void Team_DroppedFlagThink(gentity_t *e) { (void)e; }
void trap_GetConfigstring(int n,char *b,int s) { (void)n; if(s>0)b[0]=0; }
void trap_GetUserinfo(int n,char *b,int s) { (void)n; if(s>0)b[0]=0; }
void trap_SetConfigstring(int n,const char *s) { (void)n;(void)s; }
void trap_LinkEntity(gentity_t *e) { e->r.linked=qtrue; }
void trap_UnlinkEntity(gentity_t *e) { e->r.linked=qfalse; }
void trap_LocateGameData(gentity_t *g,int n,int s,playerState_t *c,int cs) { (void)g;(void)n;(void)s;(void)c;(void)cs; }
void trap_Trace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
 (void)start;(void)mins;(void)maxs;(void)pass;(void)mask;
 memset(t,0,sizeof(*t)); t->fraction=1; t->entityNum=ENTITYNUM_NONE; VectorCopy(end,t->endpos);
}
int main(void) {
 static gclient_t cl[2];
 gentity_t *item,*knife; playerState_t *ps=&cl[0].ps; int i;
 memset(&level,0,sizeof(level));
 level.time=1000; level.num_entities=MAX_CLIENTS; level.maxclients=2;
 for(i=0;i<2;i++) {
  gentity_t *e=&g_entities[i]; e->client=&cl[i]; e->inuse=qtrue;
  e->s.number=i; e->health=100; BG_ApplyClass(&cl[i].ps,BG_Class(0));
  cl[i].ps.clientNum=i; cl[i].ps.pm_type=PM_NORMAL; cl[i].ps.viewheight=26; cl[i].ps.stats[STAT_HEALTH]=100;
  cl[i].lastCmdTime=level.time;
  e->r.maxs[2]=32; e->r.mins[2]=-24;
 }
 { bg_weaponInstance_t gun={0}; gun.weapon=WP_RAILGUN; gun.ammo=3; gun.reserve=7;
   BG_PutSlot(&cl[1].ps,SLOT_PRIMARY,&gun); }
 item=G_DropWeapon(&g_entities[1],SLOT_PRIMARY); CHECK(item!=NULL);
 G_SetOrigin(item,ps->origin);
 CHECK(G_FindWeaponPickup(&g_entities[0])!=NULL && ps->cursorHintEntIndex==0);
 ClientEndFrame(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==item->s.number && (item->flags&FL_CURSOR_HINT));
 G_FreeEntity(item);
 CHECK(ps->cursorHintEntIndex==ENTITYNUM_NONE);
 /* a usable site or the bomb: a live hint, never freed, which ends a sea lion */
 bombUsable=qtrue; ClientEndFrame(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==ENTITYNUM_WORLD);
 bombUsable=qfalse;
 /* a resting throwing knife, for a player who carries them, is named and its
  * free empties the hint the same way */
 cl[0].knifeMax=1; ps->ammo[WP_THROWING_KNIFE]=0;
 knife=G_Spawn(); knife->classname="throwingknife"; knife->s.eType=ET_MISSILE;
 knife->s.weapon=WP_THROWING_KNIFE; knife->timestamp=level.time; G_SetOrigin(knife,ps->origin);
 ClientEndFrame(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==knife->s.number && (knife->flags&FL_CURSOR_HINT));
 G_FreeEntity(knife);
 CHECK(ps->cursorHintEntIndex==ENTITYNUM_NONE);
 printf("PASS: %d cursor-hint wiring checks\n",checks); return 0;
}
