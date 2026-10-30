/* Real inventory, hold clock, swap/note engine and server item transactions. */
#include "../../code/game/g_items.c"
void PickupWeaponStep(pmove_t *move, int msec);

level_locals_t level;
gentity_t g_entities[MAX_GENTITIES];
vmCvar_t g_gametype, g_weaponRespawn, g_weaponTeamRespawn;
static gclient_t clients[2];
static pmove_t move;
static int checks, pickups, usedTargets;
static qboolean blocked, bombUsable;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int code,const char *fmt,...) { va_list a; (void)code; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); exit(1); }
void QDECL G_Error(const char *fmt,...) { (void)fmt; abort(); }
void QDECL G_LogPrintf(const char *fmt,...) { (void)fmt; }
/* The engine's Q_SnapVector rounds to nearest, as MW2's does; q_shared's macro truncates. */
void trap_SnapVector(float *v) { v[0] = rintf(v[0]); v[1] = rintf(v[1]); v[2] = rintf(v[2]); }
void trap_LinkEntity(gentity_t *e) { e->r.linked=qtrue; }
void trap_Trace(trace_t *t,const vec3_t start,const vec3_t mins,const vec3_t maxs,const vec3_t end,int pass,int mask) {
 (void)start;(void)mins;(void)maxs;(void)pass;(void)mask;
 memset(t,0,sizeof(*t)); t->fraction=blocked ? .5f : 1; VectorCopy(end,t->endpos);
}
qboolean G_EntitiesFree(void) { return qtrue; }
gentity_t *G_Spawn(void) {
 gentity_t *e=&g_entities[level.num_entities++]; CHECK(level.num_entities<MAX_GENTITIES);
 memset(e,0,sizeof(*e)); e->inuse=qtrue; e->s.number=e-g_entities; return e;
}
void G_FreeEntity(gentity_t *e) { G_ClearCursorHintRefs(e); e->inuse=qfalse; e->r.contents=0; }
void G_SetOrigin(gentity_t *e,vec3_t p) {
 VectorCopy(p,e->s.pos.trBase); VectorCopy(p,e->r.currentOrigin);
 e->s.pos.trType=TR_STATIONARY; VectorClear(e->s.pos.trDelta);
}
void G_AddEvent(gentity_t *e,int event,int parm) { (void)e;(void)parm; if(event==EV_ITEM_PICKUP)pickups++; }
void G_AddPredictableEvent(gentity_t *e,int event,int parm) { G_AddEvent(e,event,parm); }
void G_UseTargets(gentity_t *e,gentity_t *other) { (void)e;(void)other; usedTargets++; }
gentity_t *G_TempEntity(vec3_t p,int event) { gentity_t *e=G_Spawn(); G_SetOrigin(e,p); e->s.event=event; return e; }
int G_SoundIndex(char *name) { (void)name; return 1; }
void trap_GetUserinfo(int client,char *buffer,int size) { (void)client; if(size>0)buffer[0]=0; }
void Team_DroppedFlagThink(gentity_t *e) { G_FreeEntity(e); }
void Team_CheckDroppedItem(gentity_t *e) { (void)e; }
int Pickup_Team(gentity_t *e,gentity_t *other) { (void)e;(void)other; return 1; }
qboolean G_SNDUsable(gentity_t *e) { (void)e; return bombUsable; }
qboolean G_SNDUse(gentity_t *e,usercmd_t *cmd) { (void)e; return bombUsable && (cmd->buttons & (BUTTON_USE|BUTTON_USE_RELOAD)); }

static int selectionLag, seenCount;
static playerState_t seen[256];
static playerState_t *Reset(void) {
 int i;
 memset(g_entities,0,sizeof(g_entities)); memset(clients,0,sizeof(clients));
 memset(&level,0,sizeof(level)); memset(&move,0,sizeof(move));
 level.time=1000; level.num_entities=MAX_CLIENTS; level.maxclients=2;
 g_weaponRespawn.integer=5; g_weaponTeamRespawn.integer=10;
 pickups=usedTargets=0; blocked=bombUsable=qfalse; selectionLag=seenCount=0;
 for(i=0;i<2;i++) {
  gentity_t *e=&g_entities[i]; e->client=&clients[i]; e->inuse=qtrue;
  e->s.number=i; e->health=100; BG_ApplyClass(&clients[i].ps,BG_Class(0));
  clients[i].ps.clientNum=i; clients[i].ps.stats[STAT_HEALTH]=100;
  clients[i].ps.viewheight=26; clients[i].ps.pm_type=PM_NORMAL;
 }
 move.ps=&clients[0].ps; move.cmd.weapon=BG_SLOT_COMMAND;
 return move.ps;
}
static gentity_t *Floor(int weapon,int attachments,int ammo,int reserve,int flags) {
 bg_weaponInstance_t gun={0};
 gentity_t *item;
 gun.weapon=weapon; gun.attachments=attachments; gun.ammo=ammo; gun.reserve=reserve; gun.flags=flags;
 BG_PutSlot(&clients[1].ps,SLOT_PRIMARY,&gun);
 item=G_DropWeapon(&g_entities[1],SLOT_PRIMARY);
 CHECK(item!=NULL); G_SetOrigin(item,clients[0].ps.origin); return item;
}
/* The client re-sends its slot with the generation of the latest snapshot it
 * has, as cg_view.c does: selectionLag is that snapshot's age in commands. */
static void Step(int msec) {
 level.time+=msec; move.cmd.serverTime+=msec;
 seen[seenCount++%256]=*move.ps;
 if(BG_IS_SLOT_COMMAND(move.cmd.weapon))
  move.cmd.weapon=BG_SlotCommand(&seen[(seenCount-1-MIN(selectionLag,seenCount-1))%256],
   BG_RequestedSlot(move.ps,move.cmd.weapon));
 move.pickupItem=G_FindWeaponPickup(&g_entities[0]); PickupWeaponStep(&move,msec);
 G_CommitWeaponPickup(&g_entities[0]);
}
static void Run(int msec) { while(msec-->0) Step(1); }
static void TestDrops(void) {
 playerState_t *ps=Reset(); gentity_t *item; bg_weaponInstance_t gun;
 ps->ammo[ps->weapon]=0; ps->ammoReserve[ps->weapon]=17;
 ps->slotAttachments[0]=BG_ATTACHMENT_REDDOT;
 ps->weaponstate=WEAPON_DROPPING; move.cmd.weapon=BG_SLOT_COMMAND+1;
 TossClientItems(&g_entities[0]); item=&g_entities[MAX_CLIENTS];
 CHECK(level.num_entities==MAX_CLIENTS+1 && BG_WeaponItem(&item->s,&gun));
 CHECK(gun.weapon==WP_MACHINEGUN && gun.ammo==0 && gun.reserve==17 && gun.attachments==1);
 CHECK(item->flags&FL_DROPPED_ITEM); CHECK(item->s.pos.trType==TR_GRAVITY);
 CHECK(item->nextthink==level.time+30000);
}
static void TestScavenge(void) {
 playerState_t *ps=Reset(); gentity_t *item; int original=ps->weapon;
 ps->ammo[WP_SHOTGUN]=2; ps->ammoReserve[WP_SHOTGUN]=3;
 ps->stats[STAT_UNCHAMBERED]|=1<<WP_SHOTGUN;
 item=Floor(WP_SHOTGUN,0,4,12,0);
 Touch_Item(item,&g_entities[0],NULL);
 CHECK(ps->weapon==original && ps->stats[STAT_SLOT_SECONDARY]==WP_SHOTGUN);
 CHECK(ps->ammo[WP_SHOTGUN]==2 && ps->ammoReserve[WP_SHOTGUN]==19);
 CHECK(ps->stats[STAT_UNCHAMBERED]&(1<<WP_SHOTGUN));
 CHECK(!item->r.contents && (item->s.eFlags&EF_NODRAW) && item->freeAfterEvent);
 CHECK(pickups==1 && usedTargets==1);
 Touch_Item(item,&g_entities[0],NULL); CHECK(pickups==1 && ps->ammoReserve[WP_SHOTGUN]==19);
 item=Floor(WP_SHOTGUN,1,1,2,0); Touch_Item(item,&g_entities[0],NULL);
 CHECK(item->r.contents && pickups==1); // attachment mismatch is not ammo
 ps->ammoReserve[WP_SHOTGUN]=39;
 item=Floor(WP_SHOTGUN,0,8,40,0); Touch_Item(item,&g_entities[0],NULL);
 CHECK(ps->ammoReserve[WP_SHOTGUN]==40 && !item->r.contents);
 g_weaponRespawn.integer=0;
 item=Floor(WP_SHOTGUN,0,0,0,0); Touch_Item(item,&g_entities[0],NULL);
 CHECK(!item->r.contents && item->freeAfterEvent); // zero respawn cannot duplicate a consumed drop
}
static void TestHold(void) {
 int step;
 for(step=1;step<=50;step+=7) {
  playerState_t *ps=Reset(); gentity_t *item=Floor(WP_RAILGUN,1,2,9,BG_GUN_UNCHAMBERED);
  int elapsed=0;
  ps->ammo[WP_MACHINEGUN]=7; ps->ammoReserve[WP_MACHINEGUN]=19;
  move.cmd.buttons=BUTTON_RELOAD;
  while(elapsed+step<1000) { Step(step); elapsed+=step; CHECK(pickups==0); }
  Step(1000-elapsed);
  CHECK(pickups==1 && !item->r.contents && ps->stats[STAT_SLOT_PRIMARY]==WP_RAILGUN);
  CHECK(ps->ammo[WP_RAILGUN]==2 && ps->ammoReserve[WP_RAILGUN]==9);
  /* MW2's take: no gun in hand until the client's selection names the new
   * one, which it does with the next command here. */
  CHECK(ps->weapon==WP_NONE && !BG_SlotCommandCurrent(ps,move.cmd.weapon));
  Step(1);
  CHECK(ps->weapon==WP_RAILGUN && ps->weaponstate==WEAPON_RAISING && ps->weaponDraw);
  CHECK((ps->torsoAnim&~ANIM_TOGGLEBIT)==TORSO_RAISE);
  CHECK(ps->weaponTime==BG_WeaponFirstRaiseTime(WP_RAILGUN));
  CHECK(!(ps->slotFlags[0]&BG_GUN_FIRST_DRAW));
  CHECK(ps->stats[STAT_UNCHAMBERED]&(1<<WP_RAILGUN));
  { bg_weaponInstance_t gun; CHECK(BG_WeaponItem(&g_entities[MAX_CLIENTS+1].s,&gun));
    CHECK(gun.weapon==WP_MACHINEGUN && gun.ammo==7 && gun.reserve==19); }
  Run(1100); CHECK(pickups==1);
 }
 { playerState_t *ps=Reset(); gentity_t *item=Floor(WP_RAILGUN,0,1,2,0);
   ps->ammo[WP_MACHINEGUN]=4; move.cmd.buttons=BUTTON_RELOAD; Run(400);
   CHECK(ps->weaponstate==WEAPON_READY && pickups==0);
   move.cmd.buttons=0; Step(1); CHECK(ps->weaponstate==WEAPON_RELOADING && ps->pickupTime==0);
   move.cmd.buttons=BUTTON_RELOAD; Run(600); CHECK(pickups==0);
   item->s.time++; Step(1); CHECK(ps->pickupTime==1);
   blocked=qtrue; Run(1100); CHECK(pickups==0 && ps->pickupTime==0);
 }
}
static void TestSwap(void) {
 int offset;
 for(offset=0;offset<=1100;offset+=25) {
  playerState_t *ps=Reset(); bg_weaponInstance_t gun;
  Floor(WP_RAILGUN,0,3,7,0); move.cmd.buttons=BUTTON_RELOAD;
  Run(offset); move.cmd.weapon=BG_SLOT_COMMAND+1; Run(4000-offset);
  CHECK(ps->stats[STAT_SLOT_PRIMARY]==WP_RAILGUN && ps->stats[STAT_SLOT_SECONDARY]==WP_SHOTGUN);
  CHECK(ps->weapon==WP_SHOTGUN && ps->stats[STAT_ACTIVE_SLOT]==SLOT_SECONDARY);
  CHECK(ps->weaponstate==WEAPON_READY && pickups==1);
  BG_GetSlot(ps,0,&gun); CHECK(gun.ammo==3 && gun.reserve==7);
  move.cmd.buttons=0; move.cmd.weapon=BG_SLOT_COMMAND; Run(4000);
  CHECK(ps->weapon==WP_RAILGUN && !(ps->slotFlags[0]&BG_GUN_FIRST_DRAW));
  move.cmd.weapon=BG_SLOT_COMMAND+1; Step(1); move.cmd.weapon=BG_SLOT_COMMAND;
  move.cmd.buttons=BUTTON_ATTACK; Step(1);
  CHECK(ps->weaponstate==WEAPON_FIRING && ps->ammo[WP_RAILGUN]==2);
 }
}
static void TestDuplicateBase(void) {
 playerState_t *ps=Reset(); bg_weaponInstance_t a,b;
 Floor(WP_SHOTGUN,1,1,6,BG_GUN_UNCHAMBERED);
 ps->ammo[WP_SHOTGUN]=5; ps->ammoReserve[WP_SHOTGUN]=27;
 move.cmd.buttons=BUTTON_RELOAD; Run(1000);
 BG_GetSlot(ps,0,&a); BG_GetSlot(ps,1,&b);
 CHECK(a.weapon==b.weapon && a.attachments==1 && b.attachments==0);
 CHECK(a.ammo==1 && b.ammo==5 && a.reserve==6 && b.reserve==27);
 move.cmd.buttons=0; move.cmd.weapon=BG_SLOT_COMMAND+1; Run(4000);
 CHECK(ps->stats[STAT_ACTIVE_SLOT]==1 && ps->ammo[WP_SHOTGUN]==5);
 CHECK(!(ps->stats[STAT_UNCHAMBERED]&(1<<WP_SHOTGUN)));
 move.cmd.weapon=BG_SLOT_COMMAND; Run(4000);
 CHECK(ps->stats[STAT_ACTIVE_SLOT]==0 && ps->ammo[WP_SHOTGUN]==1);
 BG_GetSlot(ps,1,&b); CHECK(b.ammo==5 && b.reserve==27 && b.attachments==0);
}
/* MW2's use/reload: the server's use code after each command, in
 * ClientThink_real's order: Pmove, the bomb and the floor gun, then the use. */
static void UseStep(int buttons) {
 qboolean snd;
 move.cmd.buttons=buttons; level.time++; move.cmd.serverTime++;
 move.pickupItem=G_FindWeaponPickup(&g_entities[0]); PickupWeaponStep(&move,1);
 snd=G_UseThink(&g_entities[0],&move.cmd);
 BG_UpdateActivate(move.ps,&clients[0].activate,buttons,!snd && G_FindWeaponPickup(&g_entities[0])!=NULL,snd);
}
static void TestUseReload(void) {
 playerState_t *ps=Reset(); int i;
 /* Nothing to use: the press asks the weapon for a reload on the next command.
  * The weapon never reads the button itself. */
 ps->ammo[WP_MACHINEGUN]=4;
 UseStep(BUTTON_USE_RELOAD); CHECK(ps->weaponstate==WEAPON_READY && (ps->weapFlags&PWF_USE_RELOAD));
 UseStep(BUTTON_USE_RELOAD); CHECK(ps->weaponstate==WEAPON_RELOADING && (ps->weapFlags&PWF_USE_RELOAD));
 UseStep(0); CHECK(!(ps->weapFlags&PWF_USE_RELOAD)); /* asked while held, for one command at a time */
 /* A quick action does not read the request, from the press to its end. */
 ps=Reset(); ps->ammo[WP_MACHINEGUN]=4;
 move.cmd.buttons=BUTTON_MELEE; Step(1); move.cmd.buttons=0;
 CHECK((ps->weaponAction&WA_TYPE_MASK)==WA_MELEE);
 ps->weapFlags=PWF_USE_RELOAD;
 for(i=0;i<3000 && (ps->weaponAction&WA_TYPE_MASK);i++) { Step(1); CHECK(ps->weapFlags&PWF_USE_RELOAD); }
 CHECK(i<3000 && ps->weaponstate==WEAPON_READY);
 Step(1); CHECK(ps->weaponstate==WEAPON_RELOADING && !(ps->weapFlags&PWF_USE_RELOAD));
 /* Asked on the knife's own command, it is taken and the knife wins. */
 ps=Reset(); ps->ammo[WP_MACHINEGUN]=4; ps->weapFlags=PWF_USE_RELOAD;
 move.cmd.buttons=BUTTON_MELEE; Step(1); move.cmd.buttons=0;
 CHECK((ps->weaponAction&WA_TYPE_MASK)==WA_MELEE && !(ps->weapFlags&PWF_USE_RELOAD) &&
  ps->weaponstate==WEAPON_DROPPING);
 /* Anywhere else it is taken at once, and a gun that cannot reload drops it. */
 ps=Reset(); ps->weapFlags=PWF_USE_RELOAD; Step(1);
 CHECK(ps->weaponstate==WEAPON_READY && !(ps->weapFlags&PWF_USE_RELOAD)); /* full magazine */
 /* Over a floor gun, a hold collects it and asks for nothing ... */
 ps=Reset(); Floor(WP_RAILGUN,0,2,8,0); ps->ammo[WP_MACHINEGUN]=4;
 for(i=0;i<1000;i++) { UseStep(BUTTON_USE_RELOAD); CHECK(!(ps->weapFlags&PWF_USE_RELOAD)); }
 CHECK(pickups==1 && ps->stats[STAT_SLOT_PRIMARY]==WP_RAILGUN);
 UseStep(0); CHECK(!(ps->weapFlags&PWF_USE_RELOAD) && ps->weaponstate!=WEAPON_RELOADING);
 /* ... and an early release is a reload, on the server's word alone. */
 ps=Reset(); Floor(WP_RAILGUN,0,2,8,0); ps->ammo[WP_MACHINEGUN]=4;
 for(i=0;i<400;i++) UseStep(BUTTON_USE_RELOAD);
 CHECK(ps->pickupHeld==3 && ps->pickupTime==400 && !(ps->weapFlags&PWF_USE_RELOAD));
 UseStep(0); CHECK(ps->weaponstate==WEAPON_READY && (ps->weapFlags&PWF_USE_RELOAD) && !ps->pickupHeld);
 UseStep(0); CHECK(ps->weaponstate==WEAPON_RELOADING && !(ps->weapFlags&PWF_USE_RELOAD) && pickups==0);
 /* A use at once (a bomb site) is a use: no reload, held or released. */
 ps=Reset(); ps->ammo[WP_MACHINEGUN]=4;
 for(i=0;i<3;i++) { move.cmd.buttons=BUTTON_USE_RELOAD; Step(1);
  BG_UpdateActivate(ps,&clients[0].activate,BUTTON_USE_RELOAD,qfalse,qtrue); CHECK(!(ps->weapFlags&PWF_USE_RELOAD)); }
 move.cmd.buttons=0; Step(1); BG_UpdateActivate(ps,&clients[0].activate,0,qfalse,qfalse);
 CHECK(!(ps->weapFlags&PWF_USE_RELOAD) && ps->weaponstate==WEAPON_READY);
}
/* MW2's Player_ActivateCmd takes a press made while sprinting, or while
 * equipment is coming out or going, and does nothing with it: no use, no hold,
 * no reload, and none while it stays held. The gun's return after a throw is
 * not refused, so a tap there still asks for the reload. */
static void TestUseBusy(void) {
 playerState_t *ps; int i;
 /* A press the server sees while sprinting asks for nothing, even held on
  * after the sprint ends. (Use/reload ends a sprint in pmove first, so in play
  * this is a use press.) */
 ps=Reset(); ps->ammo[WP_MACHINEGUN]=4; ps->pm_flags|=PMF_SPRINTING;
 BG_UpdateActivate(ps,&clients[0].activate,BUTTON_USE_RELOAD,qfalse,qfalse);
 CHECK(!(ps->weapFlags&PWF_USE_RELOAD) && clients[0].activate.done);
 ps->pm_flags&=~PMF_SPRINTING;
 for(i=0;i<5;i++) {
  BG_UpdateActivate(ps,&clients[0].activate,BUTTON_USE_RELOAD,qfalse,qfalse);
  CHECK(!(ps->weapFlags&PWF_USE_RELOAD));
 }
 BG_UpdateActivate(ps,&clients[0].activate,0,qfalse,qfalse);
 BG_UpdateActivate(ps,&clients[0].activate,BUTTON_USE_RELOAD,qfalse,qfalse);
 CHECK(ps->weapFlags&PWF_USE_RELOAD);
 /* Held over a floor gun from during a throw, it collects nothing, even after
  * the gun is back, until it is pressed again. */
 ps=Reset(); Floor(WP_RAILGUN,0,2,8,0);
 move.cmd.buttons=BUTTON_LETHAL; Step(1); move.cmd.buttons=0;
 for(i=0;i<4000;i++) UseStep(BUTTON_USE_RELOAD);
 CHECK(!(ps->weaponAction&WA_TYPE_MASK) && pickups==0 && ps->stats[STAT_SLOT_PRIMARY]==WP_MACHINEGUN);
 UseStep(0); for(i=0;i<1500;i++) UseStep(BUTTON_USE_RELOAD);
 CHECK(pickups==1 && ps->stats[STAT_SLOT_PRIMARY]==WP_RAILGUN);
 /* Use/reload pressed while the grenade is out asks for nothing, held or not;
  * pressed again once the gun is coming back, it does. */
 ps=Reset(); ps->ammo[WP_MACHINEGUN]=4;
 CHECK(ps->ammo[BG_LethalWeapon(ps)]>0);
 move.cmd.buttons=BUTTON_LETHAL; Step(1); move.cmd.buttons=0;
 CHECK((ps->weaponAction&WA_TYPE_MASK)==WA_LETHAL);
 UseStep(BUTTON_USE_RELOAD); CHECK(!(ps->weapFlags&PWF_USE_RELOAD));
 for(i=0;i<9000 && !(!BG_IsActionWeapon(ps->weapon) && (ps->weaponAction&WA_FIRED));i++) {
  UseStep(BUTTON_USE_RELOAD); CHECK(!(ps->weapFlags&PWF_USE_RELOAD));
 }
 CHECK(i<9000 && ps->weaponstate==WEAPON_RAISING && (ps->weaponAction&WA_TYPE_MASK));
 UseStep(BUTTON_USE_RELOAD); CHECK(!(ps->weapFlags&PWF_USE_RELOAD));
 UseStep(0); UseStep(BUTTON_USE_RELOAD); CHECK(ps->weapFlags&PWF_USE_RELOAD);
}
/* One use per press, and the bomb comes first. */
static void TestBombFirst(void) {
 playerState_t *ps; gentity_t *item; int i;
 /* At a site with a dropped gun in range, a hold plants: it collects nothing
  * and its release reloads nothing. */
 ps=Reset(); Floor(WP_RAILGUN,0,2,8,0); ps->ammo[WP_MACHINEGUN]=4; bombUsable=qtrue;
 for(i=0;i<1500;i++) UseStep(BUTTON_USE_RELOAD);
 CHECK(pickups==0 && ps->stats[STAT_SLOT_PRIMARY]==WP_MACHINEGUN && ps->weaponstate==WEAPON_READY);
 UseStep(0); UseStep(0);
 CHECK(!(ps->weapFlags&PWF_USE_RELOAD) && ps->weaponstate==WEAPON_READY);
 /* A press begun over the gun and carried onto the site is the bomb's. */
 ps=Reset(); Floor(WP_RAILGUN,0,2,8,0); ps->ammo[WP_MACHINEGUN]=4;
 for(i=0;i<200;i++) UseStep(BUTTON_USE_RELOAD);
 CHECK(ps->pickupHeld==3);
 bombUsable=qtrue; for(i=0;i<2000;i++) UseStep(BUTTON_USE_RELOAD);
 UseStep(0); UseStep(0);
 CHECK(pickups==0 && !(ps->weapFlags&PWF_USE_RELOAD) && ps->weaponstate==WEAPON_READY);
 /* A press used at the site and carried onto a gun collects nothing. */
 ps=Reset(); ps->ammo[WP_MACHINEGUN]=4; bombUsable=qtrue;
 for(i=0;i<300;i++) UseStep(BUTTON_USE_RELOAD);
 bombUsable=qfalse; item=Floor(WP_RAILGUN,0,2,8,0);
 for(i=0;i<1500;i++) UseStep(BUTTON_USE_RELOAD);
 CHECK(pickups==0 && item->r.contents && ps->stats[STAT_SLOT_PRIMARY]==WP_MACHINEGUN);
 UseStep(0); CHECK(!(ps->weapFlags&PWF_USE_RELOAD));
 /* A press that used nothing still finds the gun, as MW2 asks again. */
 ps=Reset(); ps->ammo[WP_MACHINEGUN]=BG_WeaponMagSize(WP_MACHINEGUN);
 for(i=0;i<100;i++) UseStep(BUTTON_USE_RELOAD);
 Floor(WP_RAILGUN,0,2,8,0);
 for(i=0;i<1100;i++) UseStep(BUTTON_USE_RELOAD);
 CHECK(pickups==1 && ps->stats[STAT_SLOT_PRIMARY]==WP_RAILGUN);
 /* A tap over a gun leaves nothing held: the next hold, after a knife,
  * still asks on every command and reloads once the gun is back. */
 ps=Reset(); item=Floor(WP_RAILGUN,0,2,8,0); ps->ammo[WP_MACHINEGUN]=4;
 for(i=0;i<100;i++) UseStep(BUTTON_USE_RELOAD);
 UseStep(0); UseStep(0);
 CHECK(ps->weaponstate==WEAPON_RELOADING && !clients[0].activate.hold);
 for(i=0;i<4000;i++) UseStep(0);
 G_FreeEntity(item); ps->ammo[WP_MACHINEGUN]=4;
 UseStep(BUTTON_MELEE);
 for(i=0;i<3000 && ps->weaponstate!=WEAPON_RELOADING;i++) UseStep(BUTTON_USE_RELOAD);
 CHECK(ps->weaponstate==WEAPON_RELOADING && !(ps->weaponAction&WA_TYPE_MASK));
}

static void TestContestedPickup(void) {
 playerState_t *ps=Reset(); gentity_t *item=Floor(WP_RAILGUN,0,2,8,0);
 playerState_t *other=&clients[1].ps;
 BG_ApplyClass(other,BG_Class(0)); other->stats[STAT_HEALTH]=100;
 level.time+=501;
 ps->pickupHeld=other->pickupHeld=1; ps->pickupTime=other->pickupTime=1000;
 ps->pickupTarget=other->pickupTarget=item->s.number;
 ps->pickupSerial=other->pickupSerial=item->s.time;
 G_CommitWeaponPickup(&g_entities[0]); G_CommitWeaponPickup(&g_entities[1]);
 CHECK(pickups==1 && ps->stats[STAT_SLOT_PRIMARY]==WP_RAILGUN);
 CHECK(other->stats[STAT_SLOT_PRIMARY]==WP_MACHINEGUN);
 CHECK(level.num_entities==MAX_CLIENTS+2);
}
/* MW2's reach: player_useRadius from the eye to the item, and your own dropped
 * gun is not yours to pick up again for BG_PICKUP_OWNER_DELAY. */
static void TestReach(void) {
 playerState_t *ps=Reset();
 gentity_t *item=Floor(WP_RAILGUN,0,3,7,0);
 bg_weaponInstance_t rail={0};
 vec3_t at;
 float flat=sqrt(BG_PICKUP_RANGE*BG_PICKUP_RANGE-ps->viewheight*ps->viewheight);
 VectorSet(ps->origin,0,0,0);
 VectorSet(at,BG_PICKUP_RANGE-1,0,ps->viewheight); G_SetOrigin(item,at);
 CHECK(BG_WeaponPickupDistance(ps,&item->s,level.time)>=0);
 VectorSet(at,BG_PICKUP_RANGE+1,0,ps->viewheight); G_SetOrigin(item,at);
 CHECK(BG_WeaponPickupDistance(ps,&item->s,level.time)<0);
 VectorSet(at,flat-1,0,0); G_SetOrigin(item,at);
 CHECK(BG_WeaponPickupDistance(ps,&item->s,level.time)>=0);
 VectorSet(at,BG_PICKUP_RANGE-1,0,0); G_SetOrigin(item,at);
 CHECK(BG_WeaponPickupDistance(ps,&item->s,level.time)<0);
 ps=Reset(); VectorSet(ps->origin,0,0,0);
 item=G_DropWeapon(&g_entities[0],SLOT_PRIMARY);
 CHECK(item!=NULL); VectorSet(at,0,0,ps->viewheight); G_SetOrigin(item,at);
 rail.weapon=WP_RAILGUN; rail.ammo=3; BG_PutSlot(ps,SLOT_PRIMARY,&rail);
 CHECK(BG_WeaponPickupDistance(ps,&item->s,level.time+BG_PICKUP_OWNER_DELAY-1)<0);
 CHECK(BG_WeaponPickupDistance(ps,&item->s,level.time+BG_PICKUP_OWNER_DELAY)>=0);
}
/* MW2's sticky use hint (Player_UpdateCursorHints) and how a free ends it
 * (G_FreeEntity's 0x416250): the sea lion enters by collecting what the hint
 * names, and leaves when a usable gun becomes the hint. */
static void TestCursorHint(void) {
 playerState_t *ps=Reset(), *other=&clients[1].ps;
 gentity_t *item=Floor(WP_RAILGUN,0,3,7,0), *last;
 vec3_t far;
 int t, number=item->s.number;
 CHECK(ps->cursorHintEntIndex==0);			/* the fixture's start, as ClientSpawn's memset */
 G_UpdateCursorHint(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==number && (item->flags & FL_CURSOR_HINT));
 VectorSet(far,4000,0,0); G_SetOrigin(item,far);	/* out of reach: still named */
 G_UpdateCursorHint(&g_entities[0]); CHECK(ps->cursorHintEntIndex==number);
 G_SetOrigin(item,ps->origin);
 /* frozen while sprinting or with equipment out */
 last=Floor(WP_ROCKET_LAUNCHER,0,4,8,0);
 VectorSet(far,ps->origin[0],ps->origin[1],ps->origin[2]+ps->viewheight); G_SetOrigin(last,far);
 ps->pm_flags|=PMF_SPRINTING;
 G_UpdateCursorHint(&g_entities[0]); CHECK(ps->cursorHintEntIndex==number);
 ps->pm_flags&=~PMF_SPRINTING; ps->weaponAction=WA_LETHAL;
 G_UpdateCursorHint(&g_entities[0]); CHECK(ps->cursorHintEntIndex==number);
 /* and through the throw itself (MW2 freezes it for every offhand state) */
 { int weapon=ps->weapon;
   ps->weaponAction=WA_LETHAL|WA_FIRED; ps->weapon=BG_LethalWeapon(ps); ps->weaponstate=WEAPON_FIRING;
   G_UpdateCursorHint(&g_entities[0]); CHECK(ps->cursorHintEntIndex==number);
   ps->weapon=weapon; ps->weaponstate=WEAPON_READY; }
 ps->weaponAction=0;
 G_UpdateCursorHint(&g_entities[0]); CHECK(ps->cursorHintEntIndex==last->s.number);	/* unfrozen: the nearer gun */
 G_FreeEntity(last); CHECK(ps->cursorHintEntIndex==ENTITYNUM_NONE);
 G_UpdateCursorHint(&g_entities[0]); CHECK(ps->cursorHintEntIndex==number);
 /* collecting it frees it: no hint at all */
 move.cmd.buttons=BUTTON_RELOAD;
 for(t=0;t<2000 && !pickups;t++) { Step(1); G_UpdateCursorHint(&g_entities[0]); }
 move.cmd.buttons=0;
 CHECK(pickups==1 && !item->inuse && ps->cursorHintEntIndex==ENTITYNUM_NONE);
 /* the gun just dropped is not the hint for BG_PICKUP_OWNER_DELAY, then is */
 for(t=MAX_CLIENTS;t<level.num_entities;t++) if(g_entities[t].inuse && g_entities[t].s.clientNum==0 &&
  g_entities[t].s.modelindex2) G_SetOrigin(&g_entities[t],ps->origin);
 for(t=0;t<BG_PICKUP_OWNER_DELAY-50;t++) { Step(1); G_UpdateCursorHint(&g_entities[0]); }
 CHECK(ps->cursorHintEntIndex==ENTITYNUM_NONE);
 for(t=0;t<100;t++) { Step(1); G_UpdateCursorHint(&g_entities[0]); }
 CHECK(ps->cursorHintEntIndex>=MAX_CLIENTS && ps->cursorHintEntIndex!=ENTITYNUM_NONE);
 /* someone else collecting the gun you were last hinted on gives no hint */
 ps=Reset(); item=Floor(WP_RAILGUN,0,3,7,0); number=item->s.number;
 { bg_weaponInstance_t mg={0}; mg.weapon=WP_MACHINEGUN; mg.ammo=30; BG_PutSlot(other,SLOT_PRIMARY,&mg); }
 level.time+=BG_PICKUP_OWNER_DELAY; move.cmd.serverTime+=BG_PICKUP_OWNER_DELAY;
 G_UpdateCursorHint(&g_entities[1]); CHECK(other->cursorHintEntIndex==number);
 move.cmd.buttons=BUTTON_RELOAD;
 for(t=0;t<2000 && !pickups;t++) { Step(1); G_UpdateCursorHint(&g_entities[0]); }
 move.cmd.buttons=0;
 CHECK(pickups==1 && !item->inuse && other->cursorHintEntIndex==ENTITYNUM_NONE);
 /* freeing what no hint ever named leaves 0 alone, entity 0 included */
 ps=Reset(); item=Floor(WP_RAILGUN,0,3,7,0);
 G_FreeEntity(item); G_FreeEntity(&g_entities[0]);
 CHECK(ps->cursorHintEntIndex==0 && other->cursorHintEntIndex==0);
}
/* The take window: after each pickup into the held slot the hand is empty
 * until a selection made after it arrives, about a round trip, then the new
 * gun comes up; a selection is never stale while a gun is in hand. */
static void TestTakeWindow(void) {
 static const int lags[4]={1,34,67,100}, guns[3]={WP_RAILGUN,WP_ROCKET_LAUNCHER,WP_RAILGUN};
 int l,p,t;
 for(l=0;l<4;l++) {
  playerState_t *ps=Reset(); selectionLag=lags[l];
  for(p=0;p<3;p++) {
   int before=pickups;
   Floor(guns[p],0,2,8,0); move.cmd.buttons=BUTTON_RELOAD;
   for(t=0;t<2000 && pickups==before;t++) { Step(1); if(pickups==before) CHECK(BG_SlotCommandCurrent(ps,move.cmd.weapon)); }
   move.cmd.buttons=0;
   CHECK(pickups==before+1 && ps->weapon==WP_NONE);
   for(t=0;t<1000 && ps->weapon==WP_NONE;t++) Step(1);
   CHECK(t==selectionLag+1 && ps->weapon==guns[p] && ps->weaponstate==WEAPON_RAISING);
   for(t=0;t<3000;t++) { Step(1); CHECK(BG_SlotCommandCurrent(ps,move.cmd.weapon)); }
  }
 }
}
/* MW2's take writes only the weapon: a pickup into the held slot while that gun
 * is going down leaves the drop running to its own end. A Y back toward the
 * slot just replaced, sent before the client saw the pickup, names no gun: it
 * is no YY and starts nothing, the drop ends in an empty hand, and the new gun
 * keeps its first raise for when the selection catches up. */
static void TestTakeKeepsDrop(void) {
 int lag,back,drop;
 { playerState_t *ps=Reset(); move.cmd.weapon=BG_SLOT_COMMAND+1; Step(1);
   drop=ps->weaponTime; CHECK(ps->weaponstate==WEAPON_DROPPING && drop>4); }
 for(lag=0;lag<2;lag++) for(back=0;back<2;back++) {
  playerState_t *ps=Reset(); int t,left;
  selectionLag=lag ? drop+20 : 0;
  Floor(WP_RAILGUN,0,2,8,0); move.cmd.buttons=BUTTON_RELOAD;
  Run(BG_PICKUP_HOLD_TIME-drop/2);
  move.cmd.weapon=BG_SLOT_COMMAND+1;
  for(t=0;t<drop && !pickups;t++) Step(1);
  CHECK(pickups==1 && ps->weapon==WP_NONE && ps->weaponstate==WEAPON_DROPPING);
  left=ps->weaponTime; CHECK(left>0 && left<drop);
  move.cmd.buttons=0;
  if(back) move.cmd.weapon=BG_SLOT_COMMAND;
  for(t=0;t<left;t++) {
   CHECK(ps->weapon==WP_NONE && ps->weaponstate==WEAPON_DROPPING && ps->weaponTime==left-t);
   Step(1);
  }
  if(!back) CHECK(ps->weapon==WP_SHOTGUN && ps->weaponstate==WEAPON_RAISING);
  else if(lag) {
   CHECK(ps->weapon==WP_NONE && ps->weaponstate==WEAPON_READY && (ps->slotFlags[0]&BG_GUN_FIRST_DRAW));
   for(t=0;t<1000 && ps->weapon==WP_NONE;t++) Step(1);
   CHECK(ps->weapon==WP_RAILGUN && ps->weaponstate==WEAPON_RAISING);
   CHECK(ps->weaponTime==BG_WeaponFirstRaiseTime(WP_RAILGUN));
  } else {
   CHECK(ps->weapon==WP_RAILGUN && ps->weaponstate==WEAPON_RAISING);
   CHECK(ps->weaponTime==BG_WeaponFirstRaiseTime(WP_RAILGUN));
  }
 }
}
/* A throwing knife at rest, where FireWeapon's knife came down. */
static gentity_t *GroundKnife(float x,float y,float z) {
 gentity_t *knife=G_Spawn(); vec3_t at;
 knife->classname="throwingknife"; knife->s.eType=ET_MISSILE; knife->s.weapon=WP_THROWING_KNIFE;
 knife->timestamp=level.time; knife->r.ownerNum=1;
 VectorSet(at,x,y,z); G_SetOrigin(knife,at); return knife;
}
static playerState_t *KnifeThrower(int count) {
 playerState_t *ps=Reset(); bg_class_t cl=*BG_Class(0);
 cl.lethal=WP_THROWING_KNIFE; cl.lethalCount=count;
 BG_ApplyClass(ps,&cl); ps->stats[STAT_HEALTH]=100;
 clients[0].knifeMax=ps->ammo[WP_THROWING_KNIFE]; clients[0].knifeHold=ENTITYNUM_NONE;
 return ps;
}
/* ClientEndFrame's hint pass, once per server frame. */
static void EndFrame(void) { G_UpdateCursorHint(&g_entities[0]); }
/* The server's use code alone, with no command run through the weapon. */
static void UseOnly(int buttons) {
 qboolean used;
 move.cmd.buttons=buttons; level.time++;
 used=G_UseThink(&g_entities[0],&move.cmd);
 BG_UpdateActivate(move.ps,&clients[0].activate,buttons,!used && G_FindWeaponPickup(&g_entities[0])!=NULL,used);
}
/* MW2's throwing knife on the ground (its use list, Player_ActivateCmd and
 * Touch_Item): never collected by walking over it. A use or use/reload press
 * while the hint names it is a use, never a reload; the knife is taken on that
 * command - or, too soon after it spawned, once it comes of age while the
 * button stays held - and freed, which empties the hint. The hands are not
 * touched. The player must carry knives and have room for one more. Anyone's
 * knife will do, and a knife outranks any gun. */
static void TestKnifePickup(void) {
 playerState_t *ps; gentity_t *knife,*gun; int i,weapon,state; vec3_t at;
 ps=KnifeThrower(1); ps->ammo[WP_THROWING_KNIFE]=0; BG_RebuildWeaponMask(ps);
 CHECK(!(ps->stats[STAT_WEAPONS]&(1<<WP_THROWING_KNIFE)));
 knife=GroundKnife(10,0,0); level.time+=BG_USE_SPAWN_DELAY;
 for(i=0;i<100;i++) { UseStep(0); EndFrame(); }
 CHECK(knife->inuse && pickups==0 && ps->cursorHintEntIndex==knife->s.number && (knife->flags&FL_CURSOR_HINT));
 ps->ammo[WP_MACHINEGUN]=4; weapon=ps->weapon; state=ps->weaponstate;
 UseStep(BUTTON_USE_RELOAD);
 CHECK(!knife->inuse && pickups==1 && ps->ammo[WP_THROWING_KNIFE]==1 && ps->cursorHintEntIndex==ENTITYNUM_NONE);
 CHECK((ps->stats[STAT_WEAPONS]&(1<<WP_THROWING_KNIFE)) && BG_LethalWeapon(ps)==WP_THROWING_KNIFE);
 CHECK(ps->weapon==weapon && ps->weaponstate==state && !(ps->weapFlags&PWF_USE_RELOAD));
 for(i=0;i<50;i++) { UseStep(BUTTON_USE_RELOAD); EndFrame(); CHECK(!(ps->weapFlags&PWF_USE_RELOAD)); }
 UseStep(0); UseStep(0); CHECK(ps->weaponstate==WEAPON_READY && pickups==1);
 /* +activate takes it as well; the reload button alone never does */
 ps=KnifeThrower(1); ps->ammo[WP_THROWING_KNIFE]=0;
 knife=GroundKnife(10,0,0); level.time+=BG_USE_SPAWN_DELAY; EndFrame();
 for(i=0;i<50;i++) { UseStep(BUTTON_RELOAD); EndFrame(); }
 UseStep(0); CHECK(knife->inuse);
 UseStep(BUTTON_USE); CHECK(!knife->inuse && ps->ammo[WP_THROWING_KNIFE]==1);
 /* Too young: a tap takes nothing and asks for nothing; held, it is taken on
  * the command it comes of age. */
 ps=KnifeThrower(1); ps->ammo[WP_THROWING_KNIFE]=0; ps->ammo[WP_MACHINEGUN]=4;
 knife=GroundKnife(10,0,0); EndFrame();
 UseStep(BUTTON_USE_RELOAD); UseStep(0); UseStep(0);
 CHECK(knife->inuse && ps->weaponstate==WEAPON_READY && !(ps->weapFlags&PWF_USE_RELOAD));
 EndFrame();
 for(i=0;i<1000 && knife->inuse;i++) { UseStep(BUTTON_USE_RELOAD); EndFrame(); }
 CHECK(!knife->inuse && level.time-knife->timestamp==BG_USE_SPAWN_DELAY && pickups==1);
 /* No room for another, or no knives in the class: no hint, and the press is
  * the reload it would be anywhere. */
 ps=KnifeThrower(1); ps->ammo[WP_MACHINEGUN]=4;
 knife=GroundKnife(10,0,0); level.time+=BG_USE_SPAWN_DELAY; EndFrame();
 CHECK(ps->cursorHintEntIndex==0);
 UseStep(BUTTON_USE_RELOAD); UseStep(0);
 CHECK(knife->inuse && ps->weaponstate==WEAPON_RELOADING && ps->ammo[WP_THROWING_KNIFE]==1);
 ps=Reset(); ps->ammo[WP_MACHINEGUN]=4; clients[0].knifeHold=ENTITYNUM_NONE;
 CHECK(BG_LethalWeapon(ps)!=WP_THROWING_KNIFE);
 knife=GroundKnife(10,0,0); level.time+=BG_USE_SPAWN_DELAY; EndFrame();
 UseStep(BUTTON_USE_RELOAD); UseStep(0);
 CHECK(knife->inuse && ps->weaponstate==WEAPON_RELOADING && ps->cursorHintEntIndex==0 && !ps->ammo[WP_THROWING_KNIFE]);
 /* The reach: a new knife must come within the inner radius of the feet; one
  * the hint names keeps its place out to the eye reach. Out of reach it is
  * still named but not usable: the press is a reload. */
 ps=KnifeThrower(1); ps->ammo[WP_THROWING_KNIFE]=0; ps->ammo[WP_MACHINEGUN]=4;
 knife=GroundKnife(BG_KNIFE_PICKUP_RADIUS+1,0,0); level.time+=BG_USE_SPAWN_DELAY; EndFrame();
 CHECK(ps->cursorHintEntIndex==0);
 VectorSet(at,BG_KNIFE_PICKUP_RADIUS-1,0,0); G_SetOrigin(knife,at); EndFrame();
 CHECK(ps->cursorHintEntIndex==knife->s.number);
 VectorSet(at,BG_KNIFE_PICKUP_REACH-1,0,ps->viewheight); G_SetOrigin(knife,at); EndFrame();
 CHECK(clients[0].cursorHint && ps->cursorHintEntIndex==knife->s.number);
 VectorSet(at,BG_KNIFE_PICKUP_REACH+1,0,ps->viewheight); G_SetOrigin(knife,at); EndFrame();
 CHECK(!clients[0].cursorHint && ps->cursorHintEntIndex==knife->s.number);
 UseStep(BUTTON_USE_RELOAD); UseStep(0);
 CHECK(knife->inuse && ps->weaponstate==WEAPON_RELOADING);
 /* still moving faster than bg_maxGrenadeIndicatorSpeed, or out of sight: not yet */
 ps=KnifeThrower(1); ps->ammo[WP_THROWING_KNIFE]=0;
 knife=GroundKnife(10,0,0); level.time+=BG_USE_SPAWN_DELAY;
 VectorSet(knife->s.pos.trDelta,BG_KNIFE_PICKUP_STILL+1,0,0); EndFrame();
 CHECK(ps->cursorHintEntIndex==0);
 VectorSet(knife->s.pos.trDelta,BG_KNIFE_PICKUP_STILL,0,0); blocked=qtrue; EndFrame();
 CHECK(ps->cursorHintEntIndex==0);
 blocked=qfalse; EndFrame(); CHECK(ps->cursorHintEntIndex==knife->s.number);
 /* Over a knife and a floor gun nearer the eye, the press is the knife's: the
  * gun is not collected however long it is held, and the release asks for
  * nothing. */
 ps=KnifeThrower(1); ps->ammo[WP_THROWING_KNIFE]=0; ps->ammo[WP_MACHINEGUN]=4;
 gun=Floor(WP_RAILGUN,0,2,8,0); VectorSet(at,0,0,ps->viewheight); G_SetOrigin(gun,at);
 knife=GroundKnife(60,0,0); level.time+=BG_USE_SPAWN_DELAY; EndFrame();
 CHECK(ps->cursorHintEntIndex==knife->s.number);
 for(i=0;i<1500;i++) { UseStep(BUTTON_USE_RELOAD); EndFrame(); }
 CHECK(!knife->inuse && gun->r.contents && pickups==1 && ps->stats[STAT_SLOT_PRIMARY]==WP_MACHINEGUN);
 UseStep(0); UseStep(0); CHECK(ps->weaponstate==WEAPON_READY && !(ps->weapFlags&PWF_USE_RELOAD));
 CHECK(ps->cursorHintEntIndex==gun->s.number); /* the gun is the hint after the knife */
 /* A press while sprinting uses nothing, held on or not; a new one does. */
 ps=KnifeThrower(1); ps->ammo[WP_THROWING_KNIFE]=0;
 knife=GroundKnife(10,0,0); level.time+=BG_USE_SPAWN_DELAY; EndFrame();
 ps->pm_flags|=PMF_SPRINTING; UseOnly(BUTTON_USE); EndFrame();
 ps->pm_flags&=~PMF_SPRINTING;
 for(i=0;i<BG_USE_SPAWN_DELAY;i++) { UseOnly(BUTTON_USE); EndFrame(); }
 CHECK(knife->inuse);
 UseOnly(0); UseOnly(BUTTON_USE); CHECK(!knife->inuse && ps->ammo[WP_THROWING_KNIFE]==1);
 /* A knife freed under a held press (another player took it) is not taken,
  * nor is whatever reuses its number. */
 ps=KnifeThrower(1); ps->ammo[WP_THROWING_KNIFE]=0;
 knife=GroundKnife(10,0,0); EndFrame();
 UseOnly(BUTTON_USE); G_FreeEntity(knife);
 level.num_entities--; knife=GroundKnife(10,0,0); knife->timestamp-=BG_USE_SPAWN_DELAY;
 for(i=0;i<BG_USE_SPAWN_DELAY;i++) UseOnly(BUTTON_USE);
 CHECK(knife->inuse && ps->ammo[WP_THROWING_KNIFE]==0);
}
/* The sea lion by the knife, end to end in ClientThink's order: the knife comes
 * straight out, is thrown and lands; the gun's quick raise names it as the
 * hint; a use press takes it back and the free empties the hint. Equipment
 * pressed in a quick raise then comes straight out, chain after chain, until a
 * gun in reach becomes the hint. */
static void TestKnifeSeaLion(void) {
 playerState_t *ps=KnifeThrower(1); gentity_t *knife=NULL,*gun; int t,i;
 ps->ammo[WP_FLASH]=3; BG_RebuildWeaponMask(ps);
 EndFrame(); CHECK(ps->cursorHintEntIndex==0);
 UseStep(BUTTON_LETHAL); EndFrame();
 CHECK(ps->weapon==WP_THROWING_KNIFE && ps->weaponstate==WEAPON_RAISING);
 for(t=0;t<3000 && ps->ammo[WP_THROWING_KNIFE];t++) { UseStep(0); EndFrame(); }
 CHECK(t<3000 && ps->cursorHintEntIndex==0);
 knife=GroundKnife(20,0,0);
 for(t=0;t<12000 && !(ps->weapon==WP_MACHINEGUN && ps->weaponstate==WEAPON_RAISING);t++) {
  UseStep(0); CHECK(ps->cursorHintEntIndex==0 || !BG_OffhandOut(ps)); EndFrame();
 }
 CHECK((ps->weaponAction&(WA_TYPE_MASK|WA_FIRED))==(WA_LETHAL|WA_FIRED));
 CHECK(ps->cursorHintEntIndex==knife->s.number);
 UseStep(BUTTON_USE); EndFrame();
 CHECK(!knife->inuse && ps->ammo[WP_THROWING_KNIFE]==1 && ps->cursorHintEntIndex==ENTITYNUM_NONE);
 CHECK(ps->weapon==WP_MACHINEGUN && ps->weaponstate==WEAPON_RAISING && !(ps->weapFlags&PWF_USE_RELOAD));
 for(i=0;i<3;i++) {
  UseStep(BUTTON_TACTICAL); EndFrame();
  CHECK(ps->weapon==WP_FLASH && ps->weaponstate==WEAPON_RAISING);	/* the gun never went down */
  for(t=0;t<12000 && !(ps->weapon==WP_MACHINEGUN && ps->weaponstate==WEAPON_RAISING);t++) { UseStep(0); EndFrame(); }
  CHECK(ps->cursorHintEntIndex==ENTITYNUM_NONE && ps->ammo[WP_FLASH]==2-i);
  if(i==2) break;
 }
 for(t=0;t<12000 && ps->weaponstate!=WEAPON_READY;t++) { UseStep(0); EndFrame(); }
 /* a gun in reach ends it */
 gun=Floor(WP_RAILGUN,0,2,8,0); EndFrame();
 CHECK(ps->cursorHintEntIndex==gun->s.number);
 ps->ammo[WP_FLASH]=1;
 UseStep(BUTTON_LETHAL); EndFrame();
 CHECK(ps->weapon==WP_THROWING_KNIFE && ps->weaponstate==WEAPON_RAISING);	/* the knife's own pull */
 for(t=0;t<12000 && !(ps->weapon==WP_MACHINEGUN && ps->weaponstate==WEAPON_RAISING);t++) { UseStep(0); EndFrame(); }
 UseStep(BUTTON_TACTICAL); EndFrame();
 CHECK(ps->weapon==WP_MACHINEGUN && ps->weaponstate==WEAPON_DROPPING &&
  ps->weaponTime==BG_WeaponQuickDropTime(WP_MACHINEGUN));
}
int main(void) {
 TestDrops(); TestScavenge(); TestHold(); TestSwap(); TestDuplicateBase(); TestUseReload(); TestUseBusy(); TestBombFirst(); TestContestedPickup(); TestReach(); TestCursorHint();
 TestTakeWindow(); TestTakeKeepsDrop(); TestKnifePickup(); TestKnifeSeaLion();
 printf("PASS: %d death/drop/pickup/slot-intent checks\n",checks); return 0;
}
