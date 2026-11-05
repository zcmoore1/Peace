/* The sea lion's server wiring, through the real ClientEndFrame (g_active.c),
 * the real G_FreeEntity (g_utils.c) and the real hint pass (g_items.c): the
 * end-frame pass writes the use hint, a free of what it names empties it, and
 * a usable S&D site or bomb is a live hint that is never freed. The same
 * end-frame samples MW2's TISpawnPosition (g_insertion.c), and a Tactical
 * Insertion is named by the same pass and emptied by the same free. Only
 * engine traps and modules outside these four files are stubbed. */
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
static qboolean solid;	/* the player's spot starts in solid (a low ceiling) */
void trap_Trace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
 (void)start;(void)mins;(void)maxs;(void)pass;(void)mask;
 memset(t,0,sizeof(*t)); t->fraction=1; t->entityNum=ENTITYNUM_NONE; VectorCopy(end,t->endpos);
 if(solid && mins) { t->startsolid=t->allsolid=qtrue; t->fraction=0; VectorCopy(start,t->endpos); }
}
int trap_EntitiesInBox(const vec3_t mins,const vec3_t maxs,int *list,int maxcount) { (void)mins;(void)maxs;(void)list;(void)maxcount; return 0; }
qboolean trap_EntityContact(const vec3_t mins,const vec3_t maxs,const gentity_t *e) { (void)mins;(void)maxs;(void)e; return qfalse; }
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
 CHECK(G_FindWeaponPickup(&g_entities[0])!=NULL && ps->cursorHintEntIndex==0 && ps->cursorHint==CURSOR_HINT_NONE);
 ClientEndFrame(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==item->s.number && (item->flags&FL_CURSOR_HINT) && ps->cursorHint==CURSOR_HINT_WEAPON);
 G_FreeEntity(item);
 CHECK(ps->cursorHintEntIndex==ENTITYNUM_NONE);
 /* the type is rewritten every end-frame: nothing in reach, nothing usable */
 ClientEndFrame(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==ENTITYNUM_NONE && ps->cursorHint==CURSOR_HINT_NONE);
 /* a usable site or the bomb: a live hint, never freed, which ends a sea lion */
 bombUsable=qtrue; ClientEndFrame(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==ENTITYNUM_WORLD && ps->cursorHint==CURSOR_HINT_OBJECTIVE);
 bombUsable=qfalse;
 /* a resting throwing knife, for a player who carries them, is named and its
  * free empties the hint the same way */
 cl[0].knifeMax=1; ps->ammo[WP_THROWING_KNIFE]=0;
 knife=G_Spawn(); knife->classname="throwingknife"; knife->s.eType=ET_MISSILE;
 knife->s.weapon=WP_THROWING_KNIFE; knife->timestamp=level.time; G_SetOrigin(knife,ps->origin);
 ClientEndFrame(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==knife->s.number && (knife->flags&FL_CURSOR_HINT) && ps->cursorHint==CURSOR_HINT_KNIFE);
 /* the knife prompt's type is the server's, through the networked field only */
 cl[0].ps.pm_flags|=PMF_SPRINTING; ClientEndFrame(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==knife->s.number && ps->cursorHint==CURSOR_HINT_NONE);
 cl[0].ps.pm_flags&=~PMF_SPRINTING; ClientEndFrame(&g_entities[0]);
 CHECK(ps->cursorHint==CURSOR_HINT_KNIFE);
 G_FreeEntity(knife);
 CHECK(ps->cursorHintEntIndex==ENTITYNUM_NONE);
 ClientEndFrame(&g_entities[0]); CHECK(ps->cursorHint==CURSOR_HINT_NONE);
 /* MW2's TISpawnPosition, sampled by the end-frame: only alive, PM_NORMAL, on
  * the ground and with the standing box clear at the origin */
 { gentity_t *ti; vec3_t at;
   cl[0].pers.connected=CON_CONNECTED; cl[0].pers.enterTime=7;
   cl[0].insertionSpotValid=qfalse;	/* a new life, as ClientSpawn's clear leaves it */
   ps->groundEntityNum=ENTITYNUM_NONE; VectorSet(ps->origin,40,0,0); ClientEndFrame(&g_entities[0]);
   CHECK(!cl[0].insertionSpotValid);	/* in the air */
   ps->groundEntityNum=ENTITYNUM_WORLD; solid=qtrue; ClientEndFrame(&g_entities[0]);
   CHECK(!cl[0].insertionSpotValid);	/* a box that starts in solid */
   solid=qfalse; ClientEndFrame(&g_entities[0]);
   CHECK(cl[0].insertionSpotValid && VectorCompare(cl[0].insertionSpot,ps->origin));
   VectorSet(ps->origin,50,0,0); ps->groundEntityNum=ENTITYNUM_NONE; ClientEndFrame(&g_entities[0]);
   CHECK(cl[0].insertionSpot[0]==40);	/* jumped: the last good spot stays */
   ps->groundEntityNum=ENTITYNUM_WORLD; g_entities[0].health=0; ClientEndFrame(&g_entities[0]);
   CHECK(cl[0].insertionSpot[0]==40);	/* dead */
   g_entities[0].health=100; ps->pm_type=PM_SPECTATOR; ClientEndFrame(&g_entities[0]);
   CHECK(cl[0].insertionSpot[0]==40);
   ps->pm_type=PM_NORMAL; ClientEndFrame(&g_entities[0]); CHECK(cl[0].insertionSpot[0]==50);
   /* the owner's TI: named by the same pass, typed for its owner, emptied by its free */
   ti=G_PlantInsertion(&g_entities[0]); CHECK(ti && VectorCompare(ti->pos1,cl[0].insertionSpot));
   VectorSet(at,ps->origin[0]+10,0,ps->origin[2]); G_SetOrigin(ti,at);
   ClientEndFrame(&g_entities[0]);
   CHECK(ps->cursorHintEntIndex==ti->s.number && ps->cursorHint==CURSOR_HINT_INSERTION && (ti->flags&FL_CURSOR_HINT));
   G_FreeEntity(ti); CHECK(ps->cursorHintEntIndex==ENTITYNUM_NONE);
   ClientEndFrame(&g_entities[0]); CHECK(ps->cursorHint==CURSOR_HINT_NONE && ps->cursorHintEntIndex==ENTITYNUM_NONE); }
 printf("PASS: %d cursor-hint wiring checks\n",checks); return 0;
}
