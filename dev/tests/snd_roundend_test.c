/* MW2's S&D round end through the real server: the real g_snd.c (included,
 * so its round state can be read), the real ClientThink, Pmove, FireWeapon,
 * G_Damage, player_die, the throwing knife's throw and flight (g_active.c,
 * bg_pmove.c, g_weapon.c, g_combat.c, g_missile.c), the real ClientSpawn and
 * ClientEndFrame, and G_RunFrame's own order (entities, S&D, end frames).
 *
 * MW2 (_gamelogic.gsc endGame): the decisive kill makes the game postgame at
 * once - inside the death itself (onDeadEvent), so no damage after it
 * (_damage.gsc 1177), not even later in the same frame, and no use - but
 * every player keeps full control for endGame's waitframe and
 * freezePlayerForRoundEnd( 1.0 ): 1.05s after the death. The owner's case: the round-winning shot, then the
 * throwing knife at once. Peace used to freeze the survivors on the frame
 * after the kill, so the knife's press never ran. Now the press, the throw
 * and the knife's flight run through the real Pmove and missile code anywhere
 * in that window, and the knife hurts nobody; from 1.05s the survivors are
 * frozen. The round-winning killcam is asked for with MW2's window (4s before
 * the death to 1.95s after it), to play 5.05s after the death.
 * A knife the last defender threw before dying, still in the air when he
 * dies, lands on the next frame and hurts nobody: the attackers win, where
 * deciding the round only at the end of that frame let it kill the shooter
 * first and called a draw. Modes without rounds end the same way (endGame
 * freezes every mode): the kill whose score ends the match gets the final
 * killcam, played 6.05s after it; a time limit's end gets none.
 *
 * The world is a floor at z = 0 plus the linked entities, as in
 * offhand_server_test.c; only engine traps and modules outside these files
 * are stubbed. */
#include "../../code/game/g_snd.c"

static int checks;
static usercmd_t nextCmd;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s (%s)\n",__LINE__,#x,caseText); exit(1); } } while(0)
static char caseText[96] = "setup";
level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
static gclient_t clients[3];
vmCvar_t g_gametype, g_weaponRespawn, g_weaponTeamRespawn, g_smoothClients, g_friendlyFire, g_debugDamage,
 g_knockback, g_inactivity, g_quadfactor, g_forcerespawn, g_teamForceBalance, g_maxGameClients, g_dmflags,
 g_blood, g_cheats, g_teamAutoJoin, g_singlePlayer, g_doWarmup, g_warmup, g_motd, g_synchronousClients,
 g_speed, g_gravity, g_allowVote, g_restarted, g_logfile, pmove_fixed, pmove_msec, g_filterBan, g_password,
 g_needpass, g_maxclients, g_timelimit, g_fraglimit, g_capturelimit, g_obeliskHealth, g_obeliskRegenPeriod,
 g_obeliskRegenAmount, g_obeliskRespawnDelay, g_cubeTimeout, g_redteam, g_blueteam, g_enableDust,
 g_enableBreath, g_proxMineTimeout, g_rankings, g_listEntity, g_banIPs, g_dedicated, g_debugMove,
 g_debugAlloc, g_weaponDebug, g_inactivityWarning;
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { va_list a; (void)code; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); abort(); }
void QDECL G_Error(const char *fmt,...) { va_list a; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); abort(); }
void QDECL G_Printf(const char *fmt,...) { (void)fmt; }
void QDECL G_LogPrintf(const char *fmt,...) { (void)fmt; }

/* ---- the engine's half of S&D: cvars, the killcam, the match end ---- */
static int replayStarts, replayKiller, replayFrom, replayTo, replayBegin, exits;
void trap_Cvar_Register(vmCvar_t *v,const char *name,const char *initial,int flags) {
 (void)name; (void)flags; memset(v,0,sizeof(*v));
 v->integer=atoi(initial); v->value=atof(initial); Q_strncpyz(v->string,initial,sizeof(v->string));
}
void trap_Cvar_Update(vmCvar_t *c) { (void)c; }
void trap_Cvar_Set(const char *name,const char *value) { (void)name;(void)value; }
void trap_ReplayReset(qboolean enabled) { (void)enabled; }
int trap_ReplayStart(int killer,int from,int to,int begin) {
 replayStarts++; replayKiller=killer; replayFrom=from; replayTo=to; replayBegin=begin;
 return begin+to-from;	/* SV_ReplayStart's deadline with the whole window recorded */
}
void LogExit(const char *s) { (void)s; exits++; level.intermissionQueued=level.time; }
static int spawnSite;
qboolean G_SpawnInt(const char *key,const char *initial,int *out) { (void)key; (void)initial; *out=spawnSite; return qtrue; }
qboolean G_SpawnFloat(const char *key,const char *initial,float *out) { (void)key; *out=atof(initial); return qtrue; }

/* ---- the world: a floor at z = 0 and the linked entities ---- */
static void Sweep(trace_t *t,const vec3_t s,const vec3_t e,const vec3_t mins,const vec3_t maxs,
 const vec3_t lo,const vec3_t hi,int num,int contents) {
 float enter=-1,leave=1; vec3_t normal={0,0,0},a,b; int k; qboolean inside=qtrue;
 for(k=0;k<3;k++) { a[k]=lo[k]-maxs[k]; b[k]=hi[k]-mins[k]; if(!(s[k]>a[k] && s[k]<b[k])) inside=qfalse; }
 if(inside) {
  if(t->fraction>0 || !t->startsolid) {
   t->startsolid=qtrue; t->fraction=0; t->entityNum=num; t->contents=contents; VectorClear(t->plane.normal);
   t->allsolid=e[0]>a[0] && e[0]<b[0] && e[1]>a[1] && e[1]<b[1] && e[2]>a[2] && e[2]<b[2];
  }
  return;
 }
 for(k=0;k<3;k++) {
  float d=e[k]-s[k], t0, t1, side=-1;
  if(d==0) { if(s[k]<=a[k] || s[k]>=b[k]) return; continue; }
  t0=(a[k]-s[k])/d; t1=(b[k]-s[k])/d;
  if(t0>t1) { float x=t0; t0=t1; t1=x; side=1; }
  if(t0>enter) { enter=t0; VectorClear(normal); normal[k]=side; }
  if(t1<leave) leave=t1;
  if(enter>=leave) return;
 }
 if(enter<0 || enter>1) return;
 if(enter<t->fraction) { t->fraction=enter; VectorCopy(normal,t->plane.normal); t->entityNum=num; t->contents=contents; }
}
void trap_Trace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
 static const vec3_t zero={0,0,0};
 vec3_t floorLo={-100000,-100000,-100000}, floorHi={100000,100000,0};
 int i, passOwner=-1;
 if(!mins) mins=zero;
 if(!maxs) maxs=zero;
 memset(t,0,sizeof(*t)); t->fraction=1; t->entityNum=ENTITYNUM_NONE;
 if(mask&(CONTENTS_SOLID|CONTENTS_PLAYERCLIP))
  Sweep(t,start,end,mins,maxs,floorLo,floorHi,ENTITYNUM_WORLD,CONTENTS_SOLID);
 if(pass!=ENTITYNUM_NONE && g_entities[pass].r.ownerNum!=ENTITYNUM_NONE) passOwner=g_entities[pass].r.ownerNum;
 for(i=0;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i]; vec3_t lo,hi;
  if(!e->inuse || !e->r.linked || !(e->r.contents&mask)) continue;
  if(pass!=ENTITYNUM_NONE && (i==pass || e->r.ownerNum==pass || e->r.ownerNum==passOwner)) continue;
  VectorAdd(e->r.currentOrigin,e->r.mins,lo); VectorAdd(e->r.currentOrigin,e->r.maxs,hi);
  Sweep(t,start,end,mins,maxs,lo,hi,i,e->r.contents);
 }
 for(i=0;i<3;i++) t->endpos[i]=start[i]+t->fraction*(end[i]-start[i]);
}
void trap_LinkEntity(gentity_t *e) {
 e->r.linked=qtrue; VectorAdd(e->r.currentOrigin,e->r.mins,e->r.absmin); VectorAdd(e->r.currentOrigin,e->r.maxs,e->r.absmax);
}
void trap_UnlinkEntity(gentity_t *e) { e->r.linked=qfalse; }
static qboolean Overlap(const vec3_t mins,const vec3_t maxs,const gentity_t *e) {
 int k;
 for(k=0;k<3;k++) if(mins[k]>e->r.absmax[k] || maxs[k]<e->r.absmin[k]) return qfalse;
 return qtrue;
}
int trap_EntitiesInBox(const vec3_t mins,const vec3_t maxs,int *list,int maxcount) {
 int i,n=0;
 for(i=0;i<level.num_entities && n<maxcount;i++)
  if(g_entities[i].inuse && g_entities[i].r.linked && Overlap(mins,maxs,&g_entities[i])) list[n++]=i;
 return n;
}
qboolean trap_EntityContact(const vec3_t mins,const vec3_t maxs,const gentity_t *e) { return Overlap(mins,maxs,e); }
int trap_PointContents(const vec3_t p,int pass) { (void)pass; return p[2]<0 ? CONTENTS_SOLID : 0; }

/* ---- the rest of the engine and the modules these files call ---- */
void trap_GetUsercmd(int n,usercmd_t *cmd) { (void)n; *cmd=nextCmd; }
void BotTestSolid(vec3_t o) { (void)o; }
void trap_GetUserinfo(int n,char *b,int s) { (void)n; if(s>0) b[0]=0; }
void trap_SetUserinfo(int n,const char *b) { (void)n;(void)b; }
void trap_SetConfigstring(int n,const char *s) { (void)n;(void)s; }
void trap_GetConfigstring(int n,char *b,int s) { (void)n; if(s>0) b[0]=0; }
void trap_SendServerCommand(int n,const char *s) { (void)n;(void)s; }
void trap_LocateGameData(gentity_t *g,int n,int s,playerState_t *c,int cs) { (void)g;(void)n;(void)s;(void)c;(void)cs; }
void trap_SnapVector(float *v) { v[0]=rintf(v[0]); v[1]=rintf(v[1]); v[2]=rintf(v[2]); }
void trap_DropClient(int n,const char *r) { (void)n;(void)r; }
void trap_SendConsoleCommand(int when,const char *t) { (void)when;(void)t; }
int trap_BotAllocateClient(void) { return -1; }
void trap_BotFreeClient(int n) { (void)n; }
#ifdef MISSIONPACK
qboolean CheckObeliskAttack(gentity_t *o,gentity_t *a) { (void)o;(void)a; return qfalse; }
gentity_t *neutralObelisk;
void DropPortalSource(gentity_t *e) { (void)e; }
void DropPortalDestination(gentity_t *e) { (void)e; }
#endif
/* g_team.c's, as it is */
qboolean OnSameTeam(gentity_t *a,gentity_t *b) {
 return a->client && b->client && g_gametype.integer>=GT_TEAM && a->client->sess.sessionTeam==b->client->sess.sessionTeam;
}
/* a spot per side; each round the test puts everyone where it wants them */
gentity_t *SelectCTFSpawnPoint(team_t team,int state,vec3_t origin,vec3_t angles,qboolean bot) {
 (void)state;(void)bot; VectorSet(origin,team==TEAM_RED ? -600 : 600,600,24); VectorSet(angles,0,0,0); return NULL;
}
void MoveClientToIntermission(gentity_t *e) { (void)e; }
void FindIntermissionPoint(void) { }
/* g_main.c's CheckExitRules, its team score limit only, as CalculateRanks
 * reaches it from AddScore on the kill (S&D's own match end is SND_EndRound) */
void CalculateRanks(void) {
 if(g_gametype.integer==GT_TEAM && g_fraglimit.integer && !level.intermissionQueued &&
  (level.teamScores[TEAM_RED]>=g_fraglimit.integer || level.teamScores[TEAM_BLUE]>=g_fraglimit.integer))
  LogExit("Fraglimit hit.");
}
int BotAIShutdownClient(int n,qboolean r) { (void)n;(void)r; return 0; }
void G_RemoveQueuedBotBegin(int n) { (void)n; }
void BotTestAAS(vec3_t o) { (void)o; }
void G_CheckBotSpawn(void) { }
qboolean G_FilterPacket(char *from) { (void)from; return qfalse; }
qboolean G_BotConnect(int n,qboolean restart) { (void)n;(void)restart; return qtrue; }
void G_InitSessionData(gclient_t *c,char *u) { (void)c;(void)u; }
void G_ReadSessionData(gclient_t *c) { (void)c; }
void G_WriteClientSessionData(gclient_t *c) { (void)c; }
void Team_CheckHurtCarrier(gentity_t *t,gentity_t *a) { (void)t;(void)a; }
void Team_FragBonuses(gentity_t *t,gentity_t *i,gentity_t *a) { (void)t;(void)i;(void)a; }
void Team_ReturnFlag(int team) { (void)team; }
void Team_CheckDroppedItem(gentity_t *e) { (void)e; }
void Team_DroppedFlagThink(gentity_t *e) { (void)e; }
void Team_FreeEntity(gentity_t *e) { (void)e; }
int Pickup_Team(gentity_t *e,gentity_t *o) { (void)e;(void)o; return 1; }
void SetLeader(int team,int client) { (void)team;(void)client; }
void CheckTeamLeader(int team) { (void)team; }
void AddTournamentQueue(gclient_t *c) { (void)c; }
/* g_mover.c's and g_misc.c's: nothing here is touched or teleported */
void Touch_DoorTrigger(gentity_t *e,gentity_t *o,trace_t *t) { (void)e;(void)o;(void)t; }
void TeleportPlayer(gentity_t *e,vec3_t o,vec3_t a) { (void)e;(void)o;(void)a; }
void G_RunMover(gentity_t *e) { (void)e; abort(); }	/* there are none */
/* g_main.c's */
void G_RunThink(gentity_t *e) {
 if(e->nextthink<=0 || e->nextthink>level.time) return;
 e->nextthink=0; if(e->think) e->think(e);
}

/* ---- the server, as the engine runs it ---- */
#define SHOOTER	0	/* red, the attacker who wins the round */
#define VICTIM	1	/* blue, the last defender */
#define MATE	2	/* red, the shooter's teammate: what the knife is thrown at */
#define FRAME	50	/* sv_fps 20: MW2's 50ms frame and the shipped autoexec's */
#define CMD_MSEC 8	/* a 125fps client: a command every 8ms */
static const vec3_t atVictim={10,90,0};	/* level, down the line at the victim's middle */
static const vec3_t atMate={0,0,0};		/* along +x at the teammate */
static const vec3_t away={0,180,0};		/* along -x, where nobody stands */
static int now;		/* the real clock: frames on FRAME boundaries, commands between them */
static int shooterButtons, shooterForward, victimButtons;
static const float *shooterView;

/* G_RunFrame's order (g_main.c): every entity, then S&D, then the end frames */
static void ServerFrame(int levelTime) {
 int i;
 gentity_t *ent;
 level.framenum++;
 level.previousTime=level.time;
 level.time=levelTime;
 for(i=0,ent=g_entities;i<level.num_entities;i++,ent++) {
  if(!ent->inuse) continue;
  G_AgeEntityEvents(ent);
  if(level.time-ent->eventTime>EVENT_VALID_MSEC) {
   if(ent->client) ent->client->ps.externalEvent=0;
   if(ent->freeAfterEvent) { G_FreeEntity(ent); continue; }
   else if(ent->unlinkAfterEvent) { ent->unlinkAfterEvent=qfalse; trap_UnlinkEntity(ent); }
  }
  if(ent->freeAfterEvent) continue;
  if(!ent->r.linked && ent->neverFree) continue;
  if(ent->s.eType==ET_MISSILE) { G_RunMissile(ent); continue; }
  if(ent->s.eType==ET_ITEM || ent->physicsObject) { G_RunItem(ent); continue; }
  if(ent->s.eType==ET_MOVER) { G_RunMover(ent); continue; }
  if(i<MAX_CLIENTS) { G_RunClient(ent); continue; }
  G_RunThink(ent);
 }
 G_SNDRunFrame();
 for(i=0,ent=g_entities;i<level.maxclients;i++,ent++) if(ent->inuse) ClientEndFrame(ent);
}
/* A command as SV_ClientThink hands it to the game, between frames: level.time
 * is the last frame's. The aim is the command's own (delta_angles cleared). */
static void Command(int n,int buttons,const float *view,int forward) {
 int i;
 memset(&nextCmd,0,sizeof(nextCmd));
 nextCmd.serverTime=now; nextCmd.buttons=buttons; nextCmd.weapon=WP_SHOTGUN; nextCmd.forwardmove=forward;
 for(i=0;i<3;i++) nextCmd.angles[i]=ANGLE2SHORT(view[i])-clients[n].ps.delta_angles[i];
 ClientThink(n);
}
/* One millisecond: the frame on its boundary, then the commands due */
static void Tick(void) {
 int i;
 now++;
 if(now%FRAME==0) ServerFrame(now);
 if(now%CMD_MSEC || snd.phase==SND_WAITING) return;	/* nobody is spawned before the round */
 for(i=0;i<3;i++) if(g_entities[i].inuse) {
  if(i==SHOOTER) Command(i,shooterButtons,shooterView,shooterForward);
  else Command(i,i==VICTIM ? victimButtons : 0,i==VICTIM ? tv(0,270,0) : tv(0,180,0),0);
 }
}
static void Run(int ms) { while(ms-->0) Tick(); }
/* Runs on until the next tick is a command's; the frame, if any, runs first */
static void ToCommand(void) { while((now+1)%CMD_MSEC) Tick(); }
/* where the test wants each player, standing still */
static void Place(int n,float x,float y) {
 gentity_t *e=&g_entities[n]; playerState_t *ps=&clients[n].ps;
 VectorSet(ps->origin,x,y,24); VectorClear(ps->velocity); VectorClear(ps->delta_angles);
 ps->groundEntityNum=ENTITYNUM_WORLD;
 VectorCopy(ps->origin,e->r.currentOrigin); VectorCopy(ps->origin,e->s.pos.trBase); trap_LinkEntity(e);
}
static int Knives(int *restNum) {
 int i, n=0;
 for(i=MAX_CLIENTS;i<level.num_entities;i++) {
  gentity_t *e=&g_entities[i];
  if(e->inuse && e->s.eType==ET_MISSILE && e->s.weapon==WP_THROWING_KNIFE && e->r.ownerNum==SHOOTER) {
   n++; if(restNum) *restNum=e->restNum;
  }
 }
 return n;
}

/* A fresh S&D match on a one-site map, the round live and every gun drawn:
 * the shooter (red) at the origin, the last defender 200 down +y, and the
 * shooter's teammate 200 along +x. Friendly fire is on, so only the round's
 * end can keep the knife from hurting the teammate. */
static void Reset(void) {
 int i;
 gentity_t *site;
 memset(&level,0,sizeof(level)); memset(g_entities,0,sizeof(g_entities)); memset(clients,0,sizeof(clients));
 now=100000 - 100000%FRAME;
 level.time=now; level.previousTime=now-FRAME; level.num_entities=MAX_CLIENTS; level.maxclients=3;
 level.clients=clients; level.gentities=g_entities;
 g_gametype.integer=GT_SND; g_knockback.integer=0; g_speed.value=320; g_gravity.value=DEFAULT_GRAVITY;
 g_friendlyFire.integer=1; pmove_msec.integer=8; pmove_fixed.integer=0;
 replayStarts=exits=0;
 G_SNDInit(); sndCountdown.integer=1;
 site=G_Spawn(); VectorSet(site->s.origin,-400,-400,8); spawnSite=0; SP_peace_bombsite(site); G_SNDMapReady();
 InitBodyQue();
 for(i=0;i<3;i++) {
  gentity_t *e=&g_entities[i]; bg_class_t c=*BG_Class(0);
  c.slot[0]=WP_SHOTGUN; c.slot[1]=WP_MACHINEGUN;
  c.lethal=WP_THROWING_KNIFE; c.lethalCount=BG_ClassEquipmentCount(WP_THROWING_KNIFE);
  CHECK(BG_ValidateClass(&c));
  e->client=&clients[i]; e->inuse=qtrue; e->s.number=i; e->r.ownerNum=ENTITYNUM_NONE;
  clients[i].pers.connected=CON_CONNECTED; clients[i].pers.selectedLoadout=c;
  clients[i].sess.sessionTeam=i==VICTIM ? TEAM_BLUE : TEAM_RED;
 }
 shooterButtons=shooterForward=victimButtons=0; shooterView=atVictim;
 /* the round starts on the next frame (SND_StartRound spawns everyone through
  * the real ClientSpawn), counts down, and goes live */
 for(i=0;snd.phase!=SND_LIVE;i++) { Run(1); CHECK(i<5000); }
 CHECK(snd.round==1 && snd.attacking==TEAM_RED);
 Place(SHOOTER,0,0); Place(VICTIM,0,200); Place(MATE,200,0);
 for(i=0;i<3;i++) CHECK(SND_Alive(i) && g_entities[i].health==100);
 /* the first raise, through Pmove */
 Run(3000);
 for(i=0;i<3;i++) CHECK(clients[i].ps.weaponstate==WEAPON_READY && clients[i].ps.pm_type==PM_NORMAL);
 CHECK(clients[SHOOTER].ps.weapon==WP_SHOTGUN && clients[SHOOTER].ps.ammo[WP_THROWING_KNIFE]==1);
 CHECK(snd.phase==SND_LIVE && !Knives(NULL));
 g_entities[VICTIM].health=clients[VICTIM].ps.stats[STAT_HEALTH]=10;	/* already hurt: one pellet ends it */
}
/* Runs on to the frame boundary plus phase, then fires one command at the
 * victim; returns D, the level time the death was dealt at. */
static int WinningShot(int phase) {
 int death;
 while((now+1)%FRAME!=phase || (now+1)%CMD_MSEC) Run(1);
 shooterButtons=BUTTON_ATTACK;
 Run(1);	/* the shot's command, phase ms into its frame */
 shooterButtons=0;
 CHECK(g_entities[VICTIM].health<=0 && !SND_Alive(VICTIM));
 death=level.time;
 CHECK(snd.lastDeath[TEAM_BLUE].killer==SHOOTER && snd.lastDeath[TEAM_BLUE].time==death);
 /* decided in the death, between frames: postgame, the killcam asked for */
 CHECK(snd.phase==SND_ROUND_OVER && snd.winner==TEAM_RED && snd.result==SND_ELIMINATION);
 CHECK(snd.freezeTime==death+ROUNDEND_FREEZE_DELAY && !G_SNDCanDamage(&g_entities[MATE]));
 CHECK(replayStarts==1 && replayKiller==SHOOTER);
 return death;
}

/* The shot wins the round; the shooter turns to the teammate and taps the
 * throwing knife pressAfter ms after the shot's command, or, with fromDeath
 * set, on the first command run at level time D + pressAfter. Returns whether
 * the knife was thrown. */
static int KnifeAfterWinningShot(int phase,int pressAfter,qboolean fromDeath) {
 int death, shotAt, pressAt, pressLevel, releaseLevel, mateHealth, rest=-1, t, thrown, end;
 Com_sprintf(caseText,sizeof(caseText),"shot at frame phase %d, knife %dms after the %s",phase,pressAfter,fromDeath ? "death" : "shot");
 Reset();
 death=WinningShot(phase); shotAt=now;
 pressAt=fromDeath ? death+pressAfter+(CMD_MSEC-(death+pressAfter)%CMD_MSEC)%CMD_MSEC : shotAt+pressAfter;
 CHECK(pressAt%CMD_MSEC==0 && pressAt>shotAt);
 shooterView=atMate;
 while(now+1<pressAt) {
  Run(1);
  CHECK(snd.phase==SND_ROUND_OVER && snd.winner==TEAM_RED && !G_SNDCanDamage(&g_entities[MATE]));
 }
 CHECK((now+1)%CMD_MSEC==0);
 /* a tap: pressed on two commands, then the release throws */
 shooterButtons=BUTTON_LETHAL; Run(1); pressLevel=level.time; Run(CMD_MSEC*2-1);
 shooterButtons=0; Run(1); releaseLevel=level.time; Run(CMD_MSEC-1);
 if(fromDeath) CHECK(pressLevel==death+pressAfter);
 CHECK(snd.phase==SND_ROUND_OVER);
 mateHealth=g_entities[MATE].health;
 thrown=Knives(NULL);
 if(releaseLevel<death+ROUNDEND_FREEZE_DELAY) {
  /* the press, pull and throw all ran: Pmove and the knife's FireWeapon */
  CHECK(clients[SHOOTER].ps.ammo[WP_THROWING_KNIFE]==0 && Knives(NULL)==1);
  CHECK(clients[SHOOTER].ps.pm_type==PM_NORMAL);
 } else if(pressLevel>=death+ROUNDEND_FREEZE_DELAY) {
  /* frozen: no command reaches Pmove, so nothing is pulled */
  CHECK(clients[SHOOTER].ps.ammo[WP_THROWING_KNIFE]==1 && !Knives(NULL));
  CHECK(clients[SHOOTER].ps.weapon==WP_SHOTGUN && clients[SHOOTER].ps.pm_type==PM_FREEZE);
 }
 /* the knife's flight, into the teammate: it sticks, and hurts nobody */
 Run(600);
 if(Knives(NULL)) {
  Knives(&rest);
  CHECK(rest==MATE);
 }
 CHECK(g_entities[MATE].health==mateHealth && mateHealth==100 && g_entities[SHOOTER].health==100);
 /* the round-winning killcam: MW2's window round the death, after the round's pause */
 CHECK(replayStarts==1 && replayKiller==SHOOTER);
 CHECK(replayFrom==death-KILLCAM_REWIND && replayTo==death+KILLCAM_LINGER && replayBegin==death+KILLCAM_ROUND_DELAY);
 CHECK(level.replayEndTime==replayBegin+KILLCAM_REWIND+KILLCAM_LINGER);
 CHECK(snd.deadline==level.replayEndTime+SND_KILLCAM_RESTART && !exits);
 /* everyone frozen by now; the next round comes after the killcam */
 if(level.time<death+ROUNDEND_FREEZE_DELAY) Run(death+ROUNDEND_FREEZE_DELAY-level.time+FRAME);
 for(t=0;t<CMD_MSEC*2;t++) Tick();
 CHECK(clients[SHOOTER].ps.pm_type==PM_FREEZE && clients[MATE].ps.pm_type==PM_FREEZE);
 end=level.replayEndTime;
 while(snd.phase==SND_ROUND_OVER) Run(FRAME);
 CHECK(snd.phase==SND_COUNTDOWN && snd.round==2 && !level.replayEndTime);
 CHECK(level.time==end+SND_KILLCAM_RESTART);	/* a frame boundary: D is one */
 CHECK(SND_Alive(VICTIM) && clients[SHOOTER].ps.ammo[WP_THROWING_KNIFE]==1);
 return thrown;
}

/* The control: the same throw at the teammate in live play hurts them (friendly
 * fire on), so it is the round's end that spares them above. */
static void KnifeInLivePlay(void) {
 int rest=-1;
 (void)rest;
 Com_sprintf(caseText,sizeof(caseText),"knife in live play");
 Reset();
 shooterView=atMate; Run(CMD_MSEC*2); ToCommand();
 shooterButtons=BUTTON_LETHAL; Run(CMD_MSEC*2);
 shooterButtons=0; Run(600);
 CHECK(Knives(&rest)==1 && g_entities[MATE].health<100);
 CHECK(snd.phase==SND_LIVE);	/* red is not out: the shooter lives */
}

/* MW2 keeps control for 1.05s, then freezes: movement and every action run
 * through Pmove up to then, and none after. */
static void ControlWindow(void) {
 int death;
 vec3_t before;
 Com_sprintf(caseText,sizeof(caseText),"control window");
 Reset();
 death=WinningShot(24);
 while(level.time<death+600) Run(1);
 CHECK(snd.phase==SND_ROUND_OVER);
 VectorCopy(clients[SHOOTER].ps.origin,before);
 shooterView=away; shooterForward=127;
 while(level.time<death+ROUNDEND_FREEZE_DELAY-FRAME) Run(1);
 Run(FRAME-1);
 CHECK(level.time==death+ROUNDEND_FREEZE_DELAY-FRAME);
 CHECK(clients[SHOOTER].ps.origin[0]<before[0]-50 && clients[SHOOTER].ps.pm_type==PM_NORMAL);	/* ran */
 Run(1+CMD_MSEC);	/* the freeze's frame, then a command */
 CHECK(level.time==death+ROUNDEND_FREEZE_DELAY && clients[SHOOTER].ps.pm_type==PM_FREEZE);
 VectorCopy(clients[SHOOTER].ps.origin,before);
 shooterButtons=BUTTON_LETHAL; Run(500);
 CHECK(VectorCompare(clients[SHOOTER].ps.origin,before) && clients[SHOOTER].ps.ammo[WP_THROWING_KNIFE]==1);
 CHECK(clients[SHOOTER].ps.commandTime==now-now%CMD_MSEC);	/* the command was taken, and did nothing */
}

/* The last defender throws his knife at the shooter, and the shooter's
 * round-winning shot kills him while it is still in the air, on a command
 * in the frame before the one where it lands. MW2 decides the round in the
 * death: postgame at once, so the knife does nothing and the attackers win
 * with their killcam. (Deciding at the end of the next frame let the knife's
 * G_RunMissile, earlier in that frame, kill the shooter first: a draw.) */
static int KnifeHitFrame(void) {	/* the control: when the knife kills the shooter in live play */
 int t, hit=-1;
 Com_sprintf(caseText,sizeof(caseText),"defender's knife in live play");
 Reset();
 victimButtons=BUTTON_LETHAL; ToCommand(); Run(CMD_MSEC*2); victimButtons=0;
 for(t=0;t<1500 && hit<0;t++) { Run(1); if(g_entities[SHOOTER].health<=0) hit=level.time; }
 CHECK(hit>0 && hit%FRAME==0);	/* in a frame's entity pass: the missile's flight */
 /* the only attacker left would be the mate, so the round goes on */
 CHECK(snd.phase==SND_LIVE);
 return hit;
}
static void KnifeInFlightAfterDecisiveDeath(void) {
 int hit=KnifeHitFrame(), death, t;
 gentity_t *mate=&g_entities[MATE];
 Com_sprintf(caseText,sizeof(caseText),"defender's knife in the air after the deciding death");
 Reset();
 /* the mate is already down, so the shooter is the last attacker */
 G_Damage(mate,NULL,NULL,NULL,NULL,1000,DAMAGE_NO_PROTECTION,MOD_FALLING);
 CHECK(mate->health<=0 && !SND_Alive(MATE) && snd.phase==SND_LIVE);
 victimButtons=BUTTON_LETHAL; ToCommand(); Run(CMD_MSEC*2); victimButtons=0;
 /* the same throw as the control: run on to the last command before the
  * frame the knife lands in, the knife in the air */
 while(now+1<(hit-1)/CMD_MSEC*CMD_MSEC) Run(1);
 CHECK((now+1)%CMD_MSEC==0 && now+1<hit && now+1>hit-FRAME && level.time==hit-FRAME);
 CHECK(Knives(NULL)==0);	/* the shooter's: none; the defender's is in the air */
 for(t=MAX_CLIENTS,death=0;t<level.num_entities;t++) death+=g_entities[t].inuse && g_entities[t].s.eType==ET_MISSILE &&
  g_entities[t].s.weapon==WP_THROWING_KNIFE && g_entities[t].r.ownerNum==VICTIM;
 CHECK(death==1);
 shooterButtons=BUTTON_ATTACK; Run(1); shooterButtons=0;
 CHECK(g_entities[VICTIM].health<=0 && level.time==hit-FRAME);
 death=level.time;
 CHECK(snd.phase==SND_ROUND_OVER && snd.winner==TEAM_RED && snd.result==SND_ELIMINATION);
 /* the knife lands on the next frame, and does nothing */
 while(level.time<hit+FRAME) Run(1);
 CHECK(g_entities[SHOOTER].health==100 && SND_Alive(SHOOTER));
 CHECK(snd.phase==SND_ROUND_OVER && snd.winner==TEAM_RED && snd.result==SND_ELIMINATION);
 CHECK(level.teamScores[TEAM_RED]==1 && !level.teamScores[TEAM_BLUE]);
 CHECK(replayStarts==1 && replayKiller==SHOOTER && replayFrom==death-KILLCAM_REWIND);
}

/* A mode without rounds (TDM here): MW2's endGame freezes every mode
 * ROUNDEND_FREEZE_DELAY after the game ends, and only the kill whose score
 * ended it gets the final killcam (_gamescore.gsc finalKill), 6.05s after it.
 * The real AddScore reaches the score limit through CalculateRanks. */
static void TeamGame(int limit) {
 int i;
 Reset();
 g_gametype.integer=GT_TEAM; g_fraglimit.integer=limit;
 level.teamScores[TEAM_RED]=level.teamScores[TEAM_BLUE]=0; level.warmupTime=0;
 for(i=0;i<3;i++) CHECK(clients[i].ps.pm_type==PM_NORMAL);
}
static int TeamKill(void) {
 int death;
 while((now+1)%FRAME!=24 || (now+1)%CMD_MSEC) Run(1);
 shooterButtons=BUTTON_ATTACK; Run(1); shooterButtons=0;
 CHECK(g_entities[VICTIM].health<=0);
 death=level.time;
 CHECK(level.lastKillTime==death && level.lastKillerNum==SHOOTER && level.teamScores[TEAM_RED]==1);
 return death;
}
/* Everyone moves and acts through Pmove up to the freeze, the shooter's knife
 * hurts nobody (the game is over), and from end + 1050 the living are frozen;
 * the dead stay down. */
static void GameEndWindow(int end) {
 vec3_t before; int ammo;
 shooterView=atMate; ToCommand();
 shooterButtons=BUTTON_LETHAL; Run(CMD_MSEC*2); shooterButtons=0; Run(600);
 CHECK(level.time<end+ROUNDEND_FREEZE_DELAY && Knives(NULL)==1);
 CHECK(g_entities[MATE].health==100 && clients[SHOOTER].ps.pm_type==PM_NORMAL);
 VectorCopy(clients[SHOOTER].ps.origin,before);
 shooterView=away; shooterForward=127;
 while(level.time<end+ROUNDEND_FREEZE_DELAY-FRAME) Run(1);
 CHECK(clients[SHOOTER].ps.origin[0]<before[0]-50 && clients[SHOOTER].ps.pm_type==PM_NORMAL);
 Run(FRAME+CMD_MSEC);
 CHECK(level.time==end+ROUNDEND_FREEZE_DELAY && clients[SHOOTER].ps.pm_type==PM_FREEZE);
 CHECK(clients[MATE].ps.pm_type==PM_FREEZE && g_entities[VICTIM].health<=0 && clients[VICTIM].ps.pm_type==PM_DEAD);
 VectorCopy(clients[SHOOTER].ps.origin,before); ammo=clients[SHOOTER].ps.ammo[WP_SHOTGUN];
 shooterButtons=BUTTON_ATTACK; victimButtons=BUTTON_ATTACK; Run(500);
 CHECK(VectorCompare(clients[SHOOTER].ps.origin,before) && clients[SHOOTER].ps.ammo[WP_SHOTGUN]==ammo);
 CHECK(clients[SHOOTER].ps.commandTime==now-now%CMD_MSEC);	/* taken, and did nothing */
 CHECK(g_entities[VICTIM].health<=0);	/* no respawn */
 shooterButtons=victimButtons=0; shooterForward=0;
}
static void TeamScoreLimit(void) {
 int death;
 Com_sprintf(caseText,sizeof(caseText),"team score limit reached by a kill");
 TeamGame(1);
 death=TeamKill();
 CHECK(exits==1 && level.intermissionQueued==death);
 CHECK(replayStarts==1 && replayKiller==SHOOTER);
 CHECK(replayFrom==death-KILLCAM_REWIND && replayTo==death+KILLCAM_LINGER && replayBegin==death+KILLCAM_GAME_DELAY);
 CHECK(level.replayEndTime==replayBegin+KILLCAM_REWIND+KILLCAM_LINGER);
 GameEndWindow(death);
 CHECK(replayStarts==1);
}
static void TeamTimeLimit(void) {
 int death, end;
 Com_sprintf(caseText,sizeof(caseText),"time limit 1.5s after a kill");
 TeamGame(0);
 death=TeamKill();
 CHECK(!exits && !level.intermissionQueued && !replayStarts);	/* no limit reached */
 /* time runs out 1.5s after it: CheckExitRules' time limit, on a frame */
 while(level.time<death+1500) Run(1);
 LogExit("Timelimit hit."); end=level.time;
 GameEndWindow(end);
 CHECK(!replayStarts && !level.replayEndTime);	/* MW2 shows no final killcam on a time limit */
}

int main(void) {
 /* ms after the shot's command: the owner's "at once", then across the window
  * and past its end. With the shot anywhere in its frame, a tap that starts by
  * 904ms is released (16ms later) before D + 1050; one from 1104ms is frozen. */
 static const int presses[]={ 8, 16, 24, 40, 56, 104, 304, 600, 904, 1104, 1304 };
 int p, n, thrown=0, frozen=0;
 KnifeInLivePlay();
 ControlWindow();
 KnifeInFlightAfterDecisiveDeath();
 TeamScoreLimit();
 TeamTimeLimit();
 for(p=0;p<FRAME;p+=CMD_MSEC) for(n=0;n<(int)ARRAY_LEN(presses);n++) {
  int knife=KnifeAfterWinningShot(p,presses[n],qfalse);
  CHECK(knife==(presses[n]<=904));
  if(knife) thrown++; else frozen++;
 }
 /* the last frame before the freeze: a tap run at level time D + 1000 */
 for(p=0;p<FRAME;p+=CMD_MSEC) {
  CHECK(KnifeAfterWinningShot(p,ROUNDEND_FREEZE_DELAY-FRAME,qtrue)); thrown++;
  CHECK(!KnifeAfterWinningShot(p,ROUNDEND_FREEZE_DELAY,qtrue)); frozen++;
 }
 printf("PASS: %d S&D round-end checks: after the round-winning shot the knife is thrown through Pmove in all %d cases "
  "up to level time D+1000 and hurts nobody, nothing is pulled once frozen at D+1050 (%d cases), and MW2's killcam "
  "window and the next round follow\n", checks,thrown,frozen);
 return 0;
}
