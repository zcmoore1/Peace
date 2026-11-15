/* The attached shotgun is an inventory mode, not a third gun. Exercise the
 * actual note engine and codec; the server variant also uses real item transfers. */
#ifdef MASTERKEY_SERVER_TEST
#define main ExistingPickupTests
#include "weapon_pickup_test.c"
#undef main

static void TestAttachedInventory(void) {
 playerState_t *state=Reset();
 bg_weaponInstance_t gun={0}, taken;
 gentity_t *item;
 gun.weapon=WP_M16; gun.ammo=17; gun.reserve=59; gun.altAmmo=2; gun.altReserve=9;
 gun.flags=BG_GUN_ALTERNATE|BG_GUN_ALT_UNCHAMBERED;
 BG_PutSlot(state,0,&gun); state->weapon=WP_MASTERKEY;
 item=G_DropWeapon(&g_entities[0],0);
 CHECK(item && item->item->giTag==WP_M16);
 CHECK(BG_WeaponItem(&item->s,&taken));
 CHECK(taken.ammo==17 && taken.reserve==59 && taken.altAmmo==2 && taken.altReserve==9);
 CHECK((taken.flags&(BG_GUN_ALTERNATE|BG_GUN_ALT_UNCHAMBERED))==gun.flags);
 BG_PutSlot(&clients[1].ps,1,&taken); BG_SelectSlot(&clients[1].ps,1);
 CHECK(BG_ActiveSlotWeapon(&clients[1].ps)==WP_MASTERKEY);
 CHECK(clients[1].ps.ammo[WP_M16]==17 && clients[1].ps.ammo[WP_MASTERKEY]==2);
 CHECK(clients[1].ps.stats[STAT_UNCHAMBERED]&(1<<WP_MASTERKEY));

 /* Scavenging feeds both reserves, never either magazine or chamber. */
 gun.ammo=11; gun.reserve=5; gun.altAmmo=1; gun.altReserve=1;
 BG_PutSlot(state,0,&gun); state->weapon=WP_MASTERKEY;
 CHECK(Pickup_Weapon(item,&g_entities[0])!=0);
 BG_GetSlot(state,0,&taken);
 CHECK(taken.ammo==11 && taken.reserve==81 && taken.altAmmo==1 && taken.altReserve==12);
 CHECK((taken.flags&(BG_GUN_ALTERNATE|BG_GUN_ALT_UNCHAMBERED))==gun.flags);
 CHECK(Pickup_Weapon(item,&g_entities[0])!=0);
 BG_GetSlot(state,0,&taken);
 CHECK(taken.reserve==120 && taken.altReserve==16 && taken.ammo==11 && taken.altAmmo==1);

 /* A use pickup replaces the latched slot, preserves payload and invalidates
  * only that instance's previous command generation. */
 state=Reset();
 BG_PutSlot(&clients[1].ps,0,&gun);
 item=G_DropWeapon(&g_entities[1],0); CHECK(item!=NULL);
 G_SetOrigin(item,state->origin);
 state->pickupTarget=item->s.number; state->pickupSerial=item->s.time;
 state->pickupHeld=1; state->pickupSlot=0; state->pickupTime=BG_USE_RELOAD_HOLD_TIME;
 G_CommitWeaponPickup(&g_entities[0],BUTTON_RELOAD);
 CHECK(state->stats[STAT_SLOT_PRIMARY]==WP_M16 && state->weapon==WP_NONE);
 CHECK(!BG_SlotCommandCurrent(state,BG_SLOT_COMMAND));
 CHECK(BG_RequestedWeapon(state,BG_SlotCommand(state,0))==WP_MASTERKEY);
 BG_GetSlot(state,0,&taken);
 CHECK(taken.ammo==11 && taken.reserve==5 && taken.altAmmo==1 && taken.altReserve==1);
 CHECK(taken.flags&BG_GUN_ALT_UNCHAMBERED);
}

int main(void) {
 TestAttachedInventory();
 printf("PASS: %d Masterkey server drop/pickup/scavenge checks\n",checks);
 return 0;
}
#else
#define main ExistingLoadoutTests
#include "loadout_test.c"
#undef main
#include "../../code/qcommon/qcommon.h"
cvar_t *cl_shownet;

static void ModeReady(qboolean alternate) {
 Ready(WP_M16,WP_SHOTGUN);
 if(alternate) { ps.slotFlags[0]|=BG_GUN_ALTERNATE; ps.weapon=WP_MASTERKEY; }
 move.cmd.weapon=BG_WeaponCommand(&ps,0,alternate);
}
static void FinishSelection(void) {
 int timeout=10000;
 do { Step(1); } while(--timeout && (ps.weapon!=BG_RequestedWeapon(&ps,move.cmd.weapon) ||
  ps.weaponstate!=WEAPON_READY || (ps.weaponAction&WA_TYPE_MASK)));
 CHECK(timeout>0);
}

static void TestModes(void) {
 int i,slot,generation,alternate,command;
 bg_class_t cl=*BG_Class(0);
 CHECK(WP_M16==WP_TACTICAL_INSERTION+1 && WP_MASTERKEY==WP_M16+1 && WP_NUM_WEAPONS<=MAX_WEAPONS);
 CHECK(BG_BaseWeapon(WP_MASTERKEY)==WP_M16 && BG_AlternateWeapon(WP_M16)==WP_MASTERKEY);
 cl.slot[0]=WP_M16; CHECK(BG_ValidateClass(&cl));
 cl.slot[0]=WP_MASTERKEY; CHECK(!BG_ValidateClass(&cl));
 for(i=0;i<BG_ClassWeaponCount();i++) CHECK(BG_ClassWeapon(i)!=WP_MASTERKEY);
 CHECK(BG_WeaponFireLength(WP_M16)==0 && BG_WeaponFireLength(WP_MASTERKEY)>0);
 ModeReady(qfalse); BG_SetSlotWeapon(&ps,1,WP_M16);
 for(slot=0;slot<SLOT_COUNT;slot++) for(generation=0;generation<2;generation++) for(alternate=0;alternate<2;alternate++) {
  ps.slotFlags[slot]=generation ? BG_GUN_GENERATION : 0;
  command=BG_WeaponCommand(&ps,slot,alternate);
  CHECK(BG_IS_SLOT_COMMAND(command) && BG_RequestedSlot(&ps,command)==slot);
  CHECK(BG_SlotCommandCurrent(&ps,command));
  CHECK(BG_RequestedWeapon(&ps,command)==(alternate?WP_MASTERKEY:WP_M16));
  ps.slotFlags[slot]^=BG_GUN_GENERATION;
  CHECK(!BG_SlotCommandCurrent(&ps,command) && BG_RequestedWeapon(&ps,command)==WP_NONE);
 }
 ModeReady(qfalse);
 CHECK(ps.ammo[WP_M16]==30 && ps.ammo[WP_MASTERKEY]==4 && ps.ammoReserve[WP_MASTERKEY]==16);
 move.cmd.buttons=BUTTON_ATTACK; Step(1); move.cmd.buttons=0;
 CHECK(fired[WP_M16]==1 && ps.ammo[WP_M16]==29 && ps.ammo[WP_MASTERKEY]==4);
 CHECK(!(ps.stats[STAT_UNCHAMBERED]&(1<<WP_M16)));
 Run(M16_FIRE_MS);
 move.cmd.weapon=BG_WeaponCommand(&ps,0,qtrue); Step(1);
 CHECK(ps.weapon==WP_M16 && ps.weaponstate==WEAPON_DROPPING && ps.weaponTime==M16_DROP_MS);
 FinishSelection();
 CHECK(ps.weapon==WP_MASTERKEY && BG_ActiveSlotWeapon(&ps)==WP_MASTERKEY);
 CHECK(ps.stats[STAT_SLOT_PRIMARY]==WP_M16 && ps.stats[STAT_SLOT_SECONDARY]==WP_SHOTGUN);
 move.cmd.buttons=BUTTON_ATTACK; Step(1); move.cmd.buttons=0;
 CHECK(fired[WP_MASTERKEY]==1 && ps.ammo[WP_MASTERKEY]==3 && ps.ammo[WP_M16]==29);
 CHECK(ps.stats[STAT_UNCHAMBERED]&(1<<WP_MASTERKEY));
 Run(MASTERKEY_FIRE_MS);
 CHECK(!(ps.stats[STAT_UNCHAMBERED]&(1<<WP_MASTERKEY)));
 /* A melee returns to the mode left in hand; Y still changes inventory slots. */
 move.cmd.buttons=BUTTON_MELEE; Step(1); move.cmd.buttons=0;
 FinishSelection(); CHECK(ps.weapon==WP_MASTERKEY);
 move.cmd.weapon=BG_SlotCommand(&ps,1); FinishSelection();
 CHECK(ps.weapon==WP_SHOTGUN);
 move.cmd.weapon=BG_SlotCommand(&ps,0); FinishSelection();
 CHECK(ps.weapon==WP_MASTERKEY && ps.ammo[WP_MASTERKEY]==3 && ps.ammo[WP_M16]==29);
 /* Two mode presses cancel an unfinished drop through ordinary YY handling. */
 move.cmd.weapon=BG_WeaponCommand(&ps,0,qfalse); Step(1);
 CHECK(ps.weaponstate==WEAPON_DROPPING);
 move.cmd.weapon=BG_WeaponCommand(&ps,0,qtrue); move.cmd.buttons=BUTTON_ATTACK; Step(1);
 CHECK(fired[WP_MASTERKEY]==2 && ps.weaponstate==WEAPON_FIRING);
}

static void TestDuplicateInstances(void) {
 bg_weaponInstance_t gun={0}, copy;
 ModeReady(qtrue);
 ps.ammo[WP_M16]=17; ps.ammoReserve[WP_M16]=59;
 ps.ammo[WP_MASTERKEY]=3; ps.ammoReserve[WP_MASTERKEY]=9;
 ps.stats[STAT_UNCHAMBERED]|=1<<WP_MASTERKEY;
 gun.weapon=WP_M16; gun.ammo=23; gun.reserve=100; gun.altAmmo=1; gun.altReserve=4;
 BG_PutSlot(&ps,1,&gun);
 CHECK(ps.ammo[WP_MASTERKEY]==3 && ps.ammo[WP_M16]==17);
 BG_SelectSlot(&ps,1);
 CHECK(BG_ActiveSlotWeapon(&ps)==WP_M16);
 CHECK(ps.ammo[WP_MASTERKEY]==1 && ps.ammo[WP_M16]==23 && !(ps.stats[STAT_UNCHAMBERED]&(1<<WP_MASTERKEY)));
 ps.ammo[WP_M16]=22; ps.ammo[WP_MASTERKEY]=0;
 BG_SelectSlot(&ps,0);
 CHECK(BG_ActiveSlotWeapon(&ps)==WP_MASTERKEY && ps.ammo[WP_M16]==17 && ps.ammo[WP_MASTERKEY]==3);
 CHECK(ps.stats[STAT_UNCHAMBERED]&(1<<WP_MASTERKEY));
 BG_GetSlot(&ps,1,&copy);
 CHECK(copy.ammo==22 && copy.altAmmo==0 && copy.reserve==100 && copy.altReserve==4);
 CHECK(!(copy.flags&(BG_GUN_ALTERNATE|BG_GUN_ALT_UNCHAMBERED)));
 BG_GetSlot(&ps,0,&copy);
 CHECK(copy.ammo==17 && copy.altAmmo==3 && copy.reserve==59 && copy.altReserve==9);
 CHECK((copy.flags&(BG_GUN_ALTERNATE|BG_GUN_ALT_UNCHAMBERED))==(BG_GUN_ALTERNATE|BG_GUN_ALT_UNCHAMBERED));
}

static void TestNoteTransitions(void) {
 int offset;
 for(offset=-1;offset<=1;offset++) {
  ModeReady(qtrue);
  move.cmd.buttons=BUTTON_ATTACK; Step(1); move.cmd.buttons=0;
  Run(MASTERKEY_BOLT_CLOSE_MS+offset-1);
  move.cmd.weapon=BG_WeaponCommand(&ps,0,qfalse); Step(1);
  CHECK(ps.ammo[WP_MASTERKEY]==3 && ps.ammo[WP_M16]==30);
  if(offset==0) CHECK(ps.weapon==WP_M16 && ps.weaponstate==WEAPON_RAISING);
  else CHECK(ps.weapon==WP_MASTERKEY && ps.weaponstate==WEAPON_DROPPING && ps.weaponTime==MASTERKEY_DROP_MS);
  CHECK(!!(ps.stats[STAT_UNCHAMBERED]&(1<<WP_MASTERKEY))==(offset<=0));
  FinishSelection();
  move.cmd.weapon=BG_WeaponCommand(&ps,0,qtrue); FinishSelection(); Step(1);
  if(offset<=0) CHECK(ps.weaponstate==WEAPON_BOLTING && ps.weaponAnimTime==0);
  else CHECK(ps.weaponstate==WEAPON_READY);
 }
 for(offset=-1;offset<=1;offset++) {
  ModeReady(qtrue); ps.ammo[WP_MASTERKEY]=1;
  move.cmd.buttons=BUTTON_RELOAD; Step(1); move.cmd.buttons=0;
  Run(MASTERKEY_RELOAD_START_MS+MASTERKEY_ROUND_IN_MS+offset-1);
  move.cmd.weapon=BG_WeaponCommand(&ps,0,qfalse); Step(1);
  if(offset==0) CHECK(ps.weapon==WP_M16 && ps.weaponstate==WEAPON_RAISING);
  else CHECK(ps.weapon==WP_MASTERKEY && ps.weaponstate==WEAPON_DROPPING);
  CHECK(ps.ammo[WP_MASTERKEY]==(offset>0?2:1));
  CHECK(ps.ammoReserve[WP_MASTERKEY]==(offset>0?15:16));
  CHECK(ps.ammo[WP_M16]==30 && ps.ammoReserve[WP_M16]==120);
 }
 /* Rifle magazine has exactly the same early/exact/late ordering into the
  * attached mode; no special logic detects either transition. */
 for(offset=-1;offset<=1;offset++) {
  ModeReady(qfalse); ps.ammo[WP_M16]=7;
  move.cmd.buttons=BUTTON_RELOAD; Step(1); move.cmd.buttons=0;
  Run(M16_MAG_IN_MS+offset-1);
  move.cmd.weapon=BG_WeaponCommand(&ps,0,qtrue); Step(1);
  if(offset==0) CHECK(ps.weapon==WP_MASTERKEY && ps.weaponstate==WEAPON_RAISING);
  else CHECK(ps.weapon==WP_M16 && ps.weaponstate==WEAPON_DROPPING);
  CHECK(ps.ammo[WP_M16]==(offset>0?30:7));
  CHECK(ps.ammo[WP_MASTERKEY]==4 && ps.ammoReserve[WP_MASTERKEY]==16);
 }
 /* Fire interrupts shell loading through END, never immediately. A tap is
  * not queued, while a held trigger shoots after the close completes. */
 ModeReady(qtrue); ps.ammo[WP_MASTERKEY]=1;
 move.cmd.buttons=BUTTON_RELOAD; Step(1); move.cmd.buttons=0;
 Run(MASTERKEY_RELOAD_START_MS+1);
 move.cmd.buttons=BUTTON_ATTACK; Step(1); move.cmd.buttons=0;
 CHECK(ps.weaponstate==WEAPON_RELOADING && ps.weaponAnimSeq==RSEQ_END && !fired[WP_MASTERKEY]);
 Run(MASTERKEY_RELOAD_END_MS+1); CHECK(!fired[WP_MASTERKEY] && ps.weaponstate==WEAPON_READY);
}

static void TestWireAndReplay(void) {
 playerState_t from,decoded,predicted;
 entityState_t drop={0},baseline={0},received;
 bg_weaponInstance_t gun;
 byte data[MAX_MSGLEN]; msg_t msg;
 int i,number;
 ModeReady(qtrue); BG_SetSlotWeapon(&ps,1,WP_M16);
 ps.slotFlags[1]=BG_GUN_GENERATION|BG_GUN_ALTERNATE|BG_GUN_ALT_UNCHAMBERED;
 ps.slotAltAmmo[1]=2; ps.slotAltReserve[1]=7;
 move.cmd.buttons=BUTTON_ATTACK; Step(1); move.cmd.buttons=0;
 Run(MASTERKEY_BOLT_CLOSE_MS-1); from=ps;
 MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,NULL,&from);
 CHECK(!msg.overflowed); MSG_BeginReading(&msg); MSG_ReadDeltaPlayerstate(&msg,NULL,&decoded);
 CHECK(decoded.slotFlags[1]==ps.slotFlags[1] && decoded.slotAltAmmo[1]==2 && decoded.slotAltReserve[1]==7);
 CHECK(decoded.ammo[WP_MASTERKEY]==3 && decoded.stats[STAT_UNCHAMBERED]==ps.stats[STAT_UNCHAMBERED]);
 move.cmd.weapon=BG_WeaponCommand(&ps,0,qfalse); Step(1); predicted=ps;
 ps=decoded; Step(1);
 CHECK(!memcmp(&predicted,&ps,sizeof(ps)));
 CHECK(ps.weapon==WP_M16 && (ps.stats[STAT_UNCHAMBERED]&(1<<WP_MASTERKEY)));
 for(i=0;i<32;i++) {
  memset(&drop,0,sizeof(drop)); drop.number=MAX_CLIENTS+1; drop.eType=ET_ITEM;
  drop.modelindex=BG_FindItemForWeapon(WP_M16)-bg_itemlist; drop.modelindex2=1;
  drop.time2=17|(59<<16); drop.weaponAltAmmo=2; drop.weaponAltReserve=9;
  drop.otherEntityNum2=i;
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaEntity(&msg,&baseline,&drop,qtrue);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg); number=MSG_ReadBits(&msg,GENTITYNUM_BITS);
  MSG_ReadDeltaEntity(&msg,&baseline,&received,number);
  CHECK(received.weaponAltAmmo==2 && received.weaponAltReserve==9 && received.otherEntityNum2==i);
  CHECK(BG_WeaponItem(&received,&gun));
  CHECK(gun.weapon==WP_M16 && gun.ammo==17 && gun.reserve==59 && gun.altAmmo==2 && gun.altReserve==9);
  CHECK((gun.flags&(BG_GUN_ALTERNATE|BG_GUN_ALT_UNCHAMBERED))==(i&(BG_GUN_ALTERNATE|BG_GUN_ALT_UNCHAMBERED)));
 }
}

int main(void) {
 TestModes(); TestDuplicateInstances(); TestNoteTransitions(); TestWireAndReplay();
 printf("PASS: %d Masterkey mode/ammo/pump/note/wire checks\n",checks);
 return 0;
}
#endif
