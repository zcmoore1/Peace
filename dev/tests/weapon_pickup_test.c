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
void trap_SnapVector(float *v) { SnapVector(v); }
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
void G_FreeEntity(gentity_t *e) { e->inuse=qfalse; e->r.contents=0; }
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

static playerState_t *Reset(void) {
 int i;
 memset(g_entities,0,sizeof(g_entities)); memset(clients,0,sizeof(clients));
 memset(&level,0,sizeof(level)); memset(&move,0,sizeof(move));
 level.time=1000; level.num_entities=MAX_CLIENTS; level.maxclients=2;
 g_weaponRespawn.integer=5; g_weaponTeamRespawn.integer=10;
 pickups=usedTargets=0; blocked=bombUsable=qfalse;
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
static void Step(int msec) {
 level.time+=msec; move.cmd.serverTime+=msec;
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
  CHECK(ps->weaponstate==WEAPON_RAISING && ps->weaponDraw);
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
int main(void) {
 TestDrops(); TestScavenge(); TestHold(); TestSwap(); TestDuplicateBase(); TestUseReload(); TestUseBusy(); TestBombFirst(); TestContestedPickup();
 printf("PASS: %d death/drop/pickup/slot-intent checks\n",checks); return 0;
}
