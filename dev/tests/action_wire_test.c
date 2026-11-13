/* Verify the real playerstate delta format preserves all ten action bits, MW2's grenadeTimeLeft
 * and the spawn's move class, including a tap released while holstering, through prediction
 * corrections. Protocol 88: MW2's stance on the wire - the usercmd's stance bits, pm_flags'
 * PMF_PRONE and PMF_PRONEMOVE_OVERRIDDEN, the float eye and its leg, the prone body, prevCmd,
 * and an ET_PLAYER's stance flags, eye and prone body (BG_PlayerStateToEntityState). */
#include "../../code/qcommon/q_shared.h"
#include "../../code/qcommon/qcommon.h"
#include "../../code/game/bg_public.h"

static int checks;
cvar_t *cl_shownet;
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_Error(int level,const char *fmt,...) {
 va_list args; (void)level; va_start(args,fmt); vfprintf(stderr,fmt,args);
 va_end(args); exit(1);
}
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static playerState_t RoundTrip(playerState_t *from, playerState_t *to) {
 byte data[MAX_MSGLEN]; msg_t msg; playerState_t decoded;
 MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,from,to);
 CHECK(!msg.overflowed); MSG_BeginReading(&msg);
 MSG_ReadDeltaPlayerstate(&msg,from,&decoded);
 return decoded;
}
/* Protocol 88 (GAME_VERSION -17): MW2's stance in the playerstate. pm_flags is 18 bits
 * (PMF_PRONE 16, PMF_PRONEMOVE_OVERRIDDEN 17); the eye is a float; the leg's target,
 * elapsed ms and direction, the prone body's three angles and prevCmd are new fields. */
static void TestStanceWire(void) {
 playerState_t from,to,decoded; int i,j,k;
 const float eyes[]={ (float)MINS_Z, (float)DEAD_VIEWHEIGHT, (float)PRONE_VIEWHEIGHT, 0.0f,
  (float)CROUCH_VIEWHEIGHT, 35.73f, (float)DEFAULT_VIEWHEIGHT, -0.4375f, 21.0f/3.0f };
 const int targets[]={ 0, BG_EYE_DEAD, BG_EYE_PRONE, BG_EYE_CROUCH, BG_EYE_STAND };
 const int legMs[]={ 0, 1, BG_PRONE_LERP_MS/2, BG_PRONE_LERP_MS-1, (1<<PS_EYE_LEG_BITS)-1 };
 const float angles[]={ 0.0f, -0.5f, 123.456f, 359.99f, -BG_PRONE_PITCH_CAP, BG_PRONE_YAW_CAP, 1e-3f };
 const int moves[]={ -128, -127, -1, 0, 1, 64, 127 };
 CHECK(PMF_PRONE==(1<<16) && PMF_PRONEMOVE_OVERRIDDEN==(1<<17));
 CHECK(PMF_PENDING_MAG==(1<<15));	/* the old top bit, so the two new ones are the next */
 CHECK(BG_PRONE_LERP_MS<(1<<PS_EYE_LEG_BITS));	/* a running leg's elapsed ms always fits */
 CHECK(BG_EYE_STAND<256 && BG_EYE_DEAD>0);	/* targets fit their 8 unsigned bits; 0 is MW2's "never set" */
 CHECK(BG_EYE_PRONE==PRONE_VIEWHEIGHT-MINS_Z && BG_EYE_CROUCH==CROUCH_VIEWHEIGHT-MINS_Z);
 /* every pm_flags bit, alone and with all the others, set and cleared */
 for(i=0;i<18;i++) {
  memset(&from,0,sizeof(from)); to=from; to.pm_flags=1<<i;
  decoded=RoundTrip(&from,&to); CHECK(decoded.pm_flags==(1<<i));
  from.pm_flags=(1<<18)-1; to.pm_flags=((1<<18)-1)&~(1<<i);
  decoded=RoundTrip(&from,&to); CHECK(decoded.pm_flags==to.pm_flags);
  decoded=RoundTrip(&to,&from); CHECK(decoded.pm_flags==(1<<18)-1);
 }
 /* the float eye: Peace's eyes, MW2's never-set sentinel (MINS_Z), mid-leg fractions */
 for(i=0;i<ARRAY_LEN(eyes);i++) for(j=0;j<ARRAY_LEN(eyes);j++) {
  memset(&from,0,sizeof(from)); to=from; from.viewheight=eyes[i]; to.viewheight=eyes[j];
  decoded=RoundTrip(&from,&to); CHECK(decoded.viewheight==eyes[j]);
 }
 /* the leg: every target from every other, its ms at both ends, both directions */
 for(i=0;i<ARRAY_LEN(targets);i++) for(j=0;j<ARRAY_LEN(targets);j++) for(k=0;k<ARRAY_LEN(legMs);k++) {
  memset(&from,0,sizeof(from)); to=from;
  from.viewHeightTarget=targets[i]; to.viewHeightTarget=targets[j];
  from.viewHeightLerpTarget=targets[j]; to.viewHeightLerpTarget=targets[i];
  from.viewHeightLerpTime=legMs[ARRAY_LEN(legMs)-1-k]; to.viewHeightLerpTime=legMs[k];
  from.viewHeightLerpDown=k&1; to.viewHeightLerpDown=(k+i)&1;
  decoded=RoundTrip(&from,&to);
  CHECK(decoded.viewHeightTarget==targets[j] && decoded.viewHeightLerpTarget==targets[i]);
  CHECK(decoded.viewHeightLerpTime==legMs[k] && decoded.viewHeightLerpDown==((k+i)&1));
 }
 /* the prone body's angles, full floats, each alone */
 for(i=0;i<ARRAY_LEN(angles);i++) for(j=0;j<ARRAY_LEN(angles);j++) {
  memset(&from,0,sizeof(from)); to=from;
  to.proneDirection=angles[i]; to.proneDirectionPitch=angles[j]; to.proneTorsoPitch=angles[(i+j)%ARRAY_LEN(angles)];
  decoded=RoundTrip(&from,&to);
  CHECK(decoded.proneDirection==to.proneDirection && decoded.proneDirectionPitch==to.proneDirectionPitch);
  CHECK(decoded.proneTorsoPitch==to.proneTorsoPitch);
  from=to; memset(&to,0,sizeof(to)); decoded=RoundTrip(&from,&to);
  CHECK(decoded.proneDirection==0 && decoded.proneDirectionPitch==0 && decoded.proneTorsoPitch==0);
 }
 /* prevCmd: every move byte and the aim, packed, sent and read back by the macros */
 for(i=0;i<ARRAY_LEN(moves);i++) for(j=0;j<ARRAY_LEN(moves);j++) for(k=0;k<2;k++) {
  usercmd_t cmd; memset(&cmd,0,sizeof(cmd));
  cmd.forwardmove=(signed char)moves[i]; cmd.rightmove=(signed char)moves[j];
  cmd.buttons=(k ? BUTTON_ADS : 0)|BUTTON_ATTACK|BUTTON_STANCE_MASK|BUTTON_SPRINT;
  memset(&from,0,sizeof(from)); to=from; to.prevCmd=BG_PrevCmdPack(&cmd);
  CHECK(to.prevCmd>=0 && to.prevCmd<(1<<17));	/* the netfield's 17 bits */
  decoded=RoundTrip(&from,&to); CHECK(decoded.prevCmd==to.prevCmd);
  CHECK(BG_PREVCMD_FORWARD(&decoded)==cmd.forwardmove && BG_PREVCMD_RIGHT(&decoded)==cmd.rightmove);
  CHECK(BG_PREVCMD_ADS(&decoded)==k);
 }
 /* everything at once, from a full state back to nothing */
 memset(&from,0,sizeof(from)); to=from;
 to.pm_flags=PMF_PRONE|PMF_PRONEMOVE_OVERRIDDEN|PMF_ADS; to.viewheight=-12.6875f;
 to.viewHeightTarget=BG_EYE_PRONE; to.viewHeightLerpTarget=BG_EYE_PRONE; to.viewHeightLerpTime=BG_PRONE_LERP_MS-1;
 to.viewHeightLerpDown=1; to.proneDirection=271.5f; to.proneDirectionPitch=-7.25f; to.proneTorsoPitch=3.5f;
 to.prevCmd=BG_PREVCMD_ADS_BIT|0x7f;
 decoded=RoundTrip(&from,&to); CHECK(!memcmp(&decoded,&to,sizeof(to)));
 decoded=RoundTrip(&to,&from); CHECK(!memcmp(&decoded,&from,sizeof(from)));
}
/* The stance bits ride in usercmd_t.buttons (32 bits on the wire): bits 22..24, above
 * BUTTON_USE_RELOAD, every combination with the other buttons. */
static void TestStanceUsercmd(void) {
 usercmd_t from,to,decoded; byte data[MAX_MSGLEN]; msg_t msg; int i,key;
 CHECK(BUTTON_PRONE==(1<<22) && BUTTON_CROUCH==(1<<23) && BUTTON_TEMP_STANCE==(1<<24));
 CHECK(BUTTON_USE_RELOAD==(1<<21) && !(BUTTON_STANCE_MASK&((1<<22)-1)));
 for(key=0;key<2;key++) for(i=0;i<16;i++) {
  memset(&from,0,sizeof(from)); to=from; to.serverTime=1000+i;
  to.buttons=(i&1 ? BUTTON_PRONE : 0)|(i&2 ? BUTTON_CROUCH : 0)|(i&4 ? BUTTON_TEMP_STANCE : 0)
   |(i&8 ? BUTTON_ATTACK|BUTTON_ADS|BUTTON_USE_RELOAD|BUTTON_SPRINT : 0);
  to.forwardmove=127; to.rightmove=-127; to.upmove=(i&8) ? -127 : 0;
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaUsercmdKey(&msg,key*0x1234,&from,&to);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg);
  MSG_ReadDeltaUsercmdKey(&msg,key*0x1234,&from,&decoded);
  CHECK(decoded.buttons==to.buttons && decoded.upmove==to.upmove && decoded.forwardmove==to.forwardmove);
  CHECK((decoded.buttons&BUTTON_STANCE_MASK)==(to.buttons&BUTTON_STANCE_MASK));
 }
}
/* An ET_PLAYER entity (BG_PlayerStateToEntityState and ...ExtraPolate) carries MW2's stance
 * (EF_CROUCHING, EF_PRONE from the flags, never from ps.eFlags), the eye unsnapped in
 * origin2[2], and while prone the body's yaw and pitch in origin2[0..1]; all of it survives
 * the entity delta. These are what others' drawn bullets and the bullet-only hit volumes
 * read (BG_PlayerHitVolumes). */
static void TestStanceEntity(void) {
 const int flags[4]={ 0, PMF_DUCKED, PMF_PRONE, PMF_PRONE|PMF_PRONEMOVE_OVERRIDDEN };
 const float eyes[4]={ (float)DEFAULT_VIEWHEIGHT, 35.73f, -12.6875f, (float)PRONE_VIEWHEIGHT };
 int i,way,dead;
 /* above every older flag (EF_TEAMVOTED is the top one), apart, inside the entity's 22 bits */
 CHECK(!((EF_CROUCHING|EF_PRONE)&(EF_TEAMVOTED*2-1)) && EF_CROUCHING!=EF_PRONE && (EF_CROUCHING|EF_PRONE)<(1<<22));
 for(way=0;way<2;way++) for(dead=0;dead<2;dead++) for(i=0;i<4;i++) {
  playerState_t ps; entityState_t es,base,decoded; byte data[MAX_MSGLEN]; msg_t msg;
  memset(&ps,0,sizeof(ps)); memset(&es,0,sizeof(es)); memset(&base,0,sizeof(base));
  ps.clientNum=3; ps.pm_type=dead ? PM_DEAD : PM_NORMAL; ps.stats[STAT_HEALTH]=dead ? 0 : 100;
  ps.origin[0]=10.25f; ps.origin[1]=-3.5f; ps.origin[2]=24.125f;
  ps.pm_flags=flags[i]|PMF_ADS; ps.viewheight=eyes[i];
  ps.proneDirection=271.5f; ps.proneDirectionPitch=-7.25f; ps.proneTorsoPitch=3.5f;
  ps.eFlags=(i==0) ? EF_CROUCHING|EF_PRONE : 0;	/* never the playerstate's: overwritten */
  es.origin2[0]=es.origin2[1]=99;	/* a stale entity: overwritten */
  if(way) BG_PlayerStateToEntityStateExtraPolate(&ps,&es,5000,qtrue);
  else BG_PlayerStateToEntityState(&ps,&es,qtrue);
  CHECK(es.eType==ET_PLAYER && es.origin2[2]==eyes[i]);	/* unsnapped */
  CHECK(es.pos.trBase[2]==24.0f);	/* the origin is snapped as before */
  CHECK(!!(es.eFlags&EF_CROUCHING)==!!(flags[i]&PMF_DUCKED) && !!(es.eFlags&EF_PRONE)==!!(flags[i]&PMF_PRONE));
  CHECK(!!(es.eFlags&EF_DEAD)==dead);
  if(flags[i]&PMF_PRONE) CHECK(es.origin2[0]==ps.proneDirection && es.origin2[1]==ps.proneDirectionPitch);
  else CHECK(es.origin2[0]==0 && es.origin2[1]==0);
  es.number=3;
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaEntity(&msg,&base,&es,qtrue);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg);
  memset(&decoded,0,sizeof(decoded));
  MSG_ReadDeltaEntity(&msg,&base,&decoded,MSG_ReadBits(&msg,GENTITYNUM_BITS));
  CHECK(decoded.number==3 && decoded.eFlags==es.eFlags);
  CHECK(decoded.origin2[0]==es.origin2[0] && decoded.origin2[1]==es.origin2[1] && decoded.origin2[2]==es.origin2[2]);
 }
}
int main(void) {
 playerState_t from,to,decoded;
 byte data[MAX_MSGLEN]; msg_t msg; int i,j;
 for(i=0;i<1024;i++) for(j=0;j<1024;j++) {
  memset(&from,0,sizeof(from)); from.weaponAction=i; from.perks=i&((1<<PS_PERK_BITS)-1);
  from.moveClass=i%MOVECLASS_COUNT;
  to=from; to.weaponAction=j; to.perks=j&((1<<PS_PERK_BITS)-1);
  to.moveClass=j%MOVECLASS_COUNT;	/* every move class, from every other */
  to.ammo[3]=7; to.ammoReserve[3]=24;
  to.stats[PS_STAT_WEAPONS]=(1<<14)|(1<<17)|(1<<(MAX_WEAPONS-1));
  to.stats[PS_STAT_UNCHAMBERED]=(1<<15)|(1<<18)|(1<<(MAX_WEAPONS-1));
  to.ammo[17]=2; to.ammo[MAX_WEAPONS-1]=3; to.ammoReserve[MAX_WEAPONS-1]=99;
  to.weapon=3; to.weaponstate=2; to.weaponTime=37;
  to.weaponDraw=j&1; to.slotAmmo[0]=7; to.slotAmmo[1]=2;
  to.slotReserve[0]=29; to.slotReserve[1]=40;
  to.slotAttachments[0]=i&1; to.slotAttachments[1]=j&1;
  to.slotFlags[0]=i&7; to.slotFlags[1]=j&7;	/* with the generation bit */
  { const int hints[4]={0,37,ENTITYNUM_WORLD,ENTITYNUM_NONE}; to.cursorHintEntIndex=hints[(i+j)&3]; }
  from.cursorHint=i%CURSOR_HINT_COUNT; to.cursorHint=j%CURSOR_HINT_COUNT;	/* every hint type, from every other */
  to.pickupTarget=1023; to.pickupSerial=2147483000; to.pickupTime=1000;
  to.pickupSlot=j&1; to.pickupHeld=j&3;
  from.weapFlags=i&3; to.weapFlags=j&3;	/* PWF_USE_RELOAD and PWF_SPRINT_RELEASE */
  /* MW2's jump and sprint state: an airborne jump (-1), the longest landing
   * slowdown, the full jump wait, a fractional take-off height, the meter. */
  to.jumpLand=(j&1) ? -1 : BG_JUMP_LAND_TIME; to.jumpWait=(i&1) ? BG_JUMP_WAIT : 0;
  to.jumpOriginZ=-123.375f; to.sprintUsed=(j&2) ? BG_SPRINT_TIME : 0;
  /* MW2's grenadeTimeLeft: none, cooked out (-1), every offhand's whole
   * fuse, a cooked frag part way, from every other */
  { const int fuses[8]={0,-1,3500,1500,2000,BG_THROWING_KNIFE_LIFE,50,1};
    from.grenadeTimeLeft=fuses[i&7]; to.grenadeTimeLeft=fuses[j&7]; }
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&from,&to);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg);
  MSG_ReadDeltaPlayerstate(&msg,&from,&decoded);
  CHECK(decoded.weaponAction==j);
  CHECK(decoded.perks==to.perks);
  CHECK(decoded.moveClass==to.moveClass);
  CHECK(decoded.ammo[3]==7 && decoded.ammoReserve[3]==24);
  CHECK(decoded.stats[PS_STAT_WEAPONS]==to.stats[PS_STAT_WEAPONS]);
  CHECK(decoded.stats[PS_STAT_UNCHAMBERED]==to.stats[PS_STAT_UNCHAMBERED]);
  CHECK(decoded.ammo[17]==2 && decoded.ammo[MAX_WEAPONS-1]==3 && decoded.ammoReserve[MAX_WEAPONS-1]==99);
  CHECK(decoded.weapon==3 && decoded.weaponstate==2 && decoded.weaponTime==37);
  CHECK(decoded.weaponDraw==to.weaponDraw && decoded.slotAmmo[0]==7 && decoded.slotAmmo[1]==2);
  CHECK(decoded.slotReserve[0]==29 && decoded.slotReserve[1]==40);
  CHECK(decoded.slotFlags[0]==to.slotFlags[0] && decoded.slotFlags[1]==to.slotFlags[1]);
  CHECK(decoded.slotAttachments[0]==to.slotAttachments[0] && decoded.slotAttachments[1]==to.slotAttachments[1]);
  CHECK(decoded.pickupTarget==1023 && decoded.pickupSerial==to.pickupSerial && decoded.pickupTime==1000);
  CHECK(decoded.pickupSlot==to.pickupSlot && decoded.pickupHeld==to.pickupHeld);
  CHECK(decoded.weapFlags==to.weapFlags);
  CHECK(decoded.jumpLand==to.jumpLand && decoded.jumpWait==to.jumpWait);
  CHECK(decoded.jumpOriginZ==to.jumpOriginZ && decoded.sprintUsed==to.sprintUsed);
  CHECK(decoded.cursorHintEntIndex==to.cursorHintEntIndex);
  CHECK(decoded.cursorHint==to.cursorHint);
  CHECK(decoded.grenadeTimeLeft==to.grenadeTimeLeft);
 }
 /* the use hint's six types in PS_CURSOR_HINT_BITS (3, protocol 84): both
  * Tactical Insertion types, the top one included, each from every other.
  * Protocol 85 adds grenadeTimeLeft; protocol 87 adds moveClass. */
 CHECK(CURSOR_HINT_COUNT==6 && PS_CURSOR_HINT_BITS==3 && CURSOR_HINT_INSERTION_ENEMY==5 && PROTOCOL_VERSION==88);
 /* moveClass is MW2's nine weapon classes in PS_MOVECLASS_BITS (4): every one,
  * the last included, from every other, and alone in a delta. */
 CHECK(MOVECLASS_COUNT==9 && MOVECLASS_COUNT<=(1<<PS_MOVECLASS_BITS));
 for(i=0;i<MOVECLASS_COUNT;i++) for(j=0;j<MOVECLASS_COUNT;j++) {
  memset(&from,0,sizeof(from)); to=from; from.moveClass=i; to.moveClass=j;
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&from,&to);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg);
  MSG_ReadDeltaPlayerstate(&msg,&from,&decoded);
  CHECK(decoded.moveClass==j && decoded.perks==0 && decoded.weaponAction==0);
 }
 for(i=0;i<CURSOR_HINT_COUNT;i++) for(j=0;j<CURSOR_HINT_COUNT;j++) {
  memset(&from,0,sizeof(from)); to=from; from.cursorHint=i; to.cursorHint=j;
  to.cursorHintEntIndex=MAX_CLIENTS+j;
  MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&from,&to);
  CHECK(!msg.overflowed); MSG_BeginReading(&msg);
  MSG_ReadDeltaPlayerstate(&msg,&from,&decoded);
  CHECK(decoded.cursorHint==j && decoded.cursorHintEntIndex==MAX_CLIENTS+j);
 }
 /* grenadeTimeLeft is MW2's signed 16 bits (0x742550): every fuse an offhand
  * has fits (the throwing knife's 30000 the longest), as do both ends */
 { const int fuses[6]={-32768,-1,0,1,BG_THROWING_KNIFE_LIFE,32767};
   CHECK(BG_THROWING_KNIFE_LIFE<32768);
   for(i=0;i<6;i++) for(j=0;j<6;j++) {
    memset(&from,0,sizeof(from)); to=from; from.grenadeTimeLeft=fuses[i]; to.grenadeTimeLeft=fuses[j];
    MSG_Init(&msg,data,sizeof(data)); MSG_WriteDeltaPlayerstate(&msg,&from,&to);
    CHECK(!msg.overflowed); MSG_BeginReading(&msg);
    MSG_ReadDeltaPlayerstate(&msg,&from,&decoded);
    CHECK(decoded.grenadeTimeLeft==fuses[j]);
   } }
 TestStanceWire();
 TestStanceUsercmd();
 TestStanceEntity();
 printf("PASS: %d action wire checks\n",checks); return 0;
}
