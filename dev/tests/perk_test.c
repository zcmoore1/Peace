/* Perks exercise the real shared class, movement and weapon code. */
#include "../../code/game/bg_pmove.c"
static playerState_t ps;
static pmove_t move;
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int level,const char *fmt,...) {
 va_list args; (void)level; va_start(args,fmt); vfprintf(stderr,fmt,args); va_end(args); exit(1);
}
void trap_SnapVector(float *v) { SnapVector(v); }
static void Ready(int weapon,int perk) {
 bg_class_t cl=*BG_Class(0);
 memset(&ps,0,sizeof(ps)); memset(&move,0,sizeof(move)); memset(&pml,0,sizeof(pml));
 cl.slot[0]=weapon; cl.slot[1]=weapon==WP_MACHINEGUN ? WP_SHOTGUN : WP_MACHINEGUN;
 cl.perk[0]=perk; BG_ApplyClass(&ps,&cl); ps.stats[STAT_HEALTH]=100;
 move.ps=&ps; move.cmd.weapon=weapon; pm=&move;
}
static void Step(int ms) { pml.msec=ms; PM_Weapon(); }
static void TestCatalog(void) {
 bg_class_t cl,copy,before; char text[BG_CLASS_STRING_SIZE]; int a,b,c,valid,i;
 CHECK(!strcmp(BG_Perk(PERK_NO_FALL_DAMAGE)->name,"9 Lives"));
 CHECK(!strcmp(BG_Perk(PERK_SILENT_FOOTSTEPS)->name,"Inaudible"));
 CHECK(!strcmp(BG_Perk(PERK_EXPEDITIOUS)->name,"Expeditious"));
 for(a=0;a<BG_PERK_COUNT;a++) for(b=0;b<BG_PERK_COUNT;b++) for(c=0;c<BG_PERK_COUNT;c++) {
  cl=*BG_Class(0); cl.perk[0]=a; cl.perk[1]=b; cl.perk[2]=c;
  valid=!(a && (a==b || a==c)) && !(b && b==c);
  CHECK(BG_ValidateClass(&cl)==valid); if(!valid)continue;
  CHECK(BG_SerializeClass(&cl,text,sizeof(text)) && BG_ParseClass(text,&copy));
  CHECK(!memcmp(&cl,&copy,sizeof(cl)));
  BG_ApplyClass(&ps,&copy);
  for(i=1;i<BG_PERK_COUNT;i++) CHECK(BG_HasPerk(&ps,i)==(i==a || i==b || i==c));
  CHECK(fabs(BG_MovementScale(&ps)-((a==PERK_EXPEDITIOUS || b==PERK_EXPEDITIOUS || c==PERK_EXPEDITIOUS) ? 1.1f : 1.0f))<.0001f);
  CHECK(BG_AttachmentLimit(&ps)==((a==PERK_SECOND_NATURE || b==PERK_SECOND_NATURE || c==PERK_SECOND_NATURE) ? 2 : 1));
  BG_ApplyLoadout(&ps,0); CHECK(ps.perks==0);
 }
 Com_sprintf(text,sizeof(text),"1|%i|%i|%i|2|%i|2|0|0|0|Legacy",WP_MACHINEGUN,WP_SHOTGUN,WP_FRAG,WP_FLASH);
 CHECK(BG_ParseClass(text,&cl));
 CHECK(cl.attachments[0]==-1 && cl.attachments[1]==-1);
 cl.attachments[0]=BG_ATTACHMENT_REDDOT; cl.attachments[1]=0;
 CHECK(BG_SerializeClass(&cl,text,sizeof(text)) && BG_ParseClass(text,&copy));
 CHECK(copy.attachments[0]==BG_ATTACHMENT_REDDOT && copy.attachments[1]==0);
 BG_ApplyClass(&ps,&copy); CHECK(ps.slotAttachments[0]==BG_ATTACHMENT_REDDOT && ps.slotAttachments[1]==0);
 cl.perk[0]=BG_PERK_COUNT; CHECK(!BG_ValidateClass(&cl));
 cl.perk[0]=-1; CHECK(!BG_ValidateClass(&cl));
 cl.perk[0]=PERK_SECOND_NATURE; cl.attachments[0]=2;
 CHECK(!BG_ValidateClass(&cl)); /* extra capacity cannot invent unsupported attachments */
 cl=*BG_Class(1); cl.attachments[0]=BG_ATTACHMENT_REDDOT; CHECK(!BG_ValidateClass(&cl));
 before=copy;
 Com_sprintf(text,sizeof(text),"2|%i|%i|%i|2|%i|2|1|1|0|0|0|Duplicate",WP_MACHINEGUN,WP_SHOTGUN,WP_FRAG,WP_FLASH);
 CHECK(!BG_ParseClass(text,&copy));
 CHECK(!memcmp(&before,&copy,sizeof(copy)));
 CHECK(BG_AttachmentCount(3)==2 && BG_AttachmentCount(0)==0);
 CHECK(!BG_HasPerk(&ps,-1) && !BG_HasPerk(&ps,0) && !BG_HasPerk(&ps,BG_PERK_COUNT));
}
static int Reload(int weapon,int perk,int step) {
 int total=0,stock,elapsed=0;
 Ready(weapon,perk); ps.ammo[weapon]=0; stock=ps.ammoReserve[weapon]; PM_BeginReload();
 while(ps.weaponstate==WEAPON_RELOADING && total<30000) {
  Step(step); total+=step;
  CHECK(ps.ammo[weapon]+ps.ammoReserve[weapon]==stock);
  CHECK(ps.ammo[weapon]>=0 && ps.ammo[weapon]<=BG_WeaponMagSize(weapon));
  elapsed=ps.weaponAnimTime;
 }
 CHECK(total<30000 && ps.weaponstate==WEAPON_READY && elapsed==-1);
 CHECK(ps.ammo[weapon]==BG_WeaponMagSize(weapon)); return total;
}
static void TestReload(void) {
 const int steps[]={1,7,16,33};
 int i,j,weapon,base,fast,seat,mode,perk;
 const bg_weaponNote_t *n;
 for(i=0;i<BG_ClassWeaponCount();i++) {
  weapon=BG_ClassWeapon(i);
  base=Reload(weapon,PERK_NONE,1); fast=Reload(weapon,PERK_QUICK_RELOAD,1);
  CHECK(abs(base-2*fast)<=BG_WeaponMagSize(weapon)+RSEQ_COUNT);
  for(j=1;j<ARRAY_LEN(steps);j++) Reload(weapon,PERK_QUICK_RELOAD,steps[j]);
  for(perk=PERK_NONE;perk<=PERK_QUICK_RELOAD;perk++) for(mode=0;mode<3;mode++) {
   Ready(weapon,perk); ps.ammo[weapon]=1; PM_BeginReload();
   ps.weaponAnimSeq=RSEQ_LOOP; n=PM_SegNotes(weapon,RSEQ_LOOP);
   while(n->note!=WNOTE_NONE && n->note!=WNOTE_MAG_IN)n++;
   CHECK(n->note==WNOTE_MAG_IN); seat=BG_ReloadTime(&ps,n->time); CHECK(seat>0);
   ps.weaponAnimTime=seat-1; ps.weaponTime=BG_ReloadSegmentTime(&ps,RSEQ_LOOP);
   if(mode==0)move.cmd.weapon=ps.stats[STAT_SLOT_SECONDARY];
   else if(mode==1)ps.pm_flags|=PMF_SPRINTING;
   else ps.stats[STAT_LADDER]=LADDER_ATTACHED;
   Step(1);
   CHECK(ps.weaponTime==0 || ps.weaponstate==WEAPON_RAISING);
   CHECK(ps.ammo[weapon]==1); /* the same finish-before-fill ordering */
   if(mode==0)CHECK(ps.weapon==ps.stats[STAT_SLOT_SECONDARY]);
   if(mode==1)CHECK(ps.weaponstate==WEAPON_SPRINTING);
   if(mode==2)CHECK(ps.weaponstate==WEAPON_LADDER);
  }
  Ready(weapon,PERK_QUICK_RELOAD);
  move.cmd.weapon=ps.stats[STAT_SLOT_SECONDARY]; Step(1);
  CHECK(ps.weaponTime==BG_WeaponDropTime(weapon));
  move.cmd.weapon=weapon; Step(1); CHECK(ps.weaponstate==WEAPON_READY && ps.weaponTime==0);
 }
 /* Fire-cycle notes are not reload notes even on bolt/pump weapons. */
 for(i=0;i<2;i++) {
  weapon=i ? WP_M40A3 : WP_SHOTGUN; Ready(weapon,PERK_QUICK_RELOAD);
  ps.weaponstate=WEAPON_BOLTING; ps.weaponAnimSeq=ASEQ_FIRE;
  ps.stats[STAT_UNCHAMBERED]=1<<weapon;
  for(n=PM_SegNotes(weapon,ASEQ_FIRE);n->note!=WNOTE_BOLT_CLOSED && n->note!=WNOTE_NONE;n++);
  CHECK(n->note==WNOTE_BOLT_CLOSED);
  ps.weaponAnimTime=n->time/2; Step(1); CHECK(ps.stats[STAT_UNCHAMBERED]&(1<<weapon));
  ps.weaponAnimTime=n->time-1; Step(1); CHECK(!(ps.stats[STAT_UNCHAMBERED]&(1<<weapon)));
 }
}
static void TestFootsteps(void) {
 int silent,water,metal,seq,bob=0;
 for(water=0;water<3;water++) for(metal=0;metal<2;metal++) for(silent=0;silent<2;silent++) {
  Ready(WP_MACHINEGUN,silent ? PERK_SILENT_FOOTSTEPS : PERK_NONE);
  ps.groundEntityNum=ENTITYNUM_WORLD; ps.velocity[0]=200; move.cmd.forwardmove=127;
  ps.speed=320;	/* g_speed: MW2's cycle rate is speed over the posture's most */
  move.waterlevel=water; pml.groundTrace.surfaceFlags=metal ? SURF_METALSTEPS : 0;
  ps.bobCycle=63; pml.msec=10; seq=ps.eventSequence; PM_Footsteps();
  CHECK(ps.eventSequence-seq==(silent ? 0 : 1));
  if(!silent)bob=ps.bobCycle; else CHECK(ps.bobCycle==bob);
 }
 Ready(WP_MACHINEGUN,PERK_SILENT_FOOTSTEPS);
 seq=ps.eventSequence; PM_AddEvent(EV_FOOTSPLASH); PM_AddEvent(EV_FOOTWADE); PM_AddEvent(EV_SWIM);
 CHECK(ps.eventSequence==seq);
 PM_AddEvent(EV_FIRE_WEAPON); CHECK(ps.eventSequence==seq+1);
 PM_AddEvent(EV_FALL_FAR); CHECK(ps.eventSequence==seq+2); /* never suppress the damage event */
 Ready(WP_MACHINEGUN,PERK_SILENT_FOOTSTEPS); ps.gravity=800; pml.previous_velocity[2]=-200;
 PM_CrashLand(); CHECK(ps.eventSequence==0 && ps.legsTimer==TIMER_LAND);
}
static void ClearTrace(trace_t *tr,const vec3_t start,const vec3_t mins,
 const vec3_t maxs,const vec3_t end,int pass,int mask) {
 (void)start;(void)mins;(void)maxs;(void)pass;(void)mask;
 memset(tr,0,sizeof(*tr));tr->fraction=1;VectorCopy(end,tr->endpos);
 tr->entityNum=ENTITYNUM_NONE;
}
static int NoContents(const vec3_t point,int pass) { (void)point;(void)pass;return 0; }
static void MovementReady(int perk) {
 Ready(WP_MACHINEGUN,perk);ps.speed=320;ps.gravity=800;
 move.trace=ClearTrace;move.pointcontents=NoContents;move.cmd.forwardmove=127;
 pml.frametime=.01f;pml.msec=10;
 VectorSet(pml.forward,1,0,0);VectorSet(pml.right,0,-1,0);
 VectorSet(pml.groundTrace.plane.normal,0,0,1);
 pml.walking=pml.groundPlane=qtrue;ps.groundEntityNum=ENTITYNUM_WORLD;
}
static void TestMovement(void) {
 const int flags[]={0,PMF_SPRINTING,PMF_ADS};
 int i,j,boost,mode;
 float base,fast,normalSpeed=0,jump=0,scale;
 vec3_t falling={0,0,0};
 CHECK(BG_MovementScale(NULL)==1.0f);
 MovementReady(PERK_NONE);
 for(i=0;i<ARRAY_LEN(flags);i++) {
  ps.pm_flags=flags[i];ps.perks=0;base=PM_CmdScale(&move.cmd);
  ps.perks=1<<(PERK_EXPEDITIOUS-1);fast=PM_CmdScale(&move.cmd);
  CHECK(fabs(fast-base*1.1f)<.0001f);
  for(j=0;j<1000;j++) { CHECK(PM_CmdScale(&move.cmd)==fast);CHECK(ps.speed==320); }
  /* Diagonal input retains normalization; the perk is not per-axis. */
  move.cmd.rightmove=127;scale=PM_CmdScale(&move.cmd);
  CHECK(fabs(scale*sqrt(2.0f)-fast)<.0001f);move.cmd.rightmove=0;
 }
 move.cmd.forwardmove=0;CHECK(PM_CmdScale(&move.cmd)==0);
 /* Exercise real accelerators and caps, not just the scaling helper. */
 for(mode=0;mode<8;mode++) for(boost=0;boost<2;boost++) {
  MovementReady(boost ? PERK_EXPEDITIOUS : PERK_NONE);
  if(mode==1)ps.pm_flags=PMF_SPRINTING;
  if(mode==2)ps.pm_flags=PMF_ADS;
  if(mode==3)ps.pm_flags=PMF_DUCKED;
  if(mode>=4 && mode<=6)move.waterlevel=mode-3;
  if(mode>=6)pml.walking=pml.groundPlane=qfalse;
  if(mode==7)VectorSet(pml.ladderNormal,-1,0,0);
  for(j=0;j<300;j++) {
   if(mode==7)PM_LadderMove();else if(mode==6)PM_WaterMove();else PM_WalkMove();
   CHECK(ps.speed==320 && ps.gravity==800);
   CHECK(ps.weaponstate==WEAPON_READY && ps.weaponTime==0);
  }
  scale=VectorLength(ps.velocity);
  if(!boost)normalSpeed=scale;else CHECK(fabs(scale-normalSpeed*1.1f)<.01f);
 }
 /* The jump impulse and gravity are not speed multipliers. With no movement
  * input, an existing falling/knockback velocity is also unchanged. */
 for(boost=0;boost<2;boost++) {
  MovementReady(boost ? PERK_EXPEDITIOUS : PERK_NONE);move.cmd.upmove=127;
  CHECK(PM_CheckJump());CHECK(ps.velocity[2]==JUMP_VELOCITY);
  PM_AirMove();if(!boost)jump=ps.velocity[2];else CHECK(ps.velocity[2]==jump);
  MovementReady(boost ? PERK_EXPEDITIOUS : PERK_NONE);
  move.cmd.forwardmove=0;pml.walking=pml.groundPlane=qfalse;
  VectorSet(ps.velocity,100,200,-300);PM_AirMove();
  if(!boost)VectorCopy(ps.velocity,falling);else CHECK(VectorCompare(ps.velocity,falling));
  CHECK(BG_ReloadTime(&ps,1234)==1234);
 }
}
int main(void) {
 TestCatalog(); TestReload(); TestFootsteps(); TestMovement();
 printf("PASS: %d perk catalog, persistence, timing, cancel, footstep and movement checks\n",checks);return 0;
}
