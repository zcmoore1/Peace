/* Exercise the real engine recorder and snapshot builder with two disjoint
 * visibility areas. Replay must be identical for every lobby recipient. */
#include "../../code/server/sv_snapshot.c"

server_t sv;
serverStatic_t svs;
static cvar_t maxclients;
cvar_t *sv_maxclients=&maxclients;
static sharedEntity_t entities[MAX_GENTITIES];
static playerState_t players[MAX_CLIENTS];
static client_t clients[4];
static entityState_t packetEntities[4096];
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)

void QDECL Com_Error(int code,const char *fmt,...) { (void)code; (void)fmt; abort(); }
void QDECL Com_Printf(const char *fmt,...) { (void)fmt; }
void QDECL Com_DPrintf(const char *fmt,...) { (void)fmt; }
sharedEntity_t *SV_GentityNum(int n) { return &entities[n]; }
playerState_t *SV_GameClientNum(int n) { return &players[n]; }
svEntity_t *SV_SvEntityForGentity(sharedEntity_t *e) { return &sv.svEntities[e-entities]; }
int CM_PointLeafnum(const vec3_t p) { return p[0] >= 500 ? 1 : 0; }
int CM_LeafArea(int leaf) { return leaf; }
int CM_LeafCluster(int leaf) { return leaf; }
qboolean CM_AreasConnected(int a,int b) { return a==b; }
byte *CM_ClusterPVS(int cluster) { static byte pvs[2]={1,2}; return &pvs[cluster]; }
int CM_WriteAreaBits(byte *bits,int area) { bits[0]|=1<<area; return 1; }

static void Reset(void) {
 int i;
 SV_ReplayReset(qfalse);
 memset(&sv,0,sizeof(sv)); memset(&svs,0,sizeof(svs));
 memset(entities,0,sizeof(entities)); memset(players,0,sizeof(players));
 memset(clients,0,sizeof(clients));
 sv.state=SS_GAME; sv.num_entities=7; maxclients.integer=4;
 svs.clients=clients; svs.snapshotEntities=packetEntities;
 svs.numSnapshotEntities=ARRAY_LEN(packetEntities);
 for(i=0;i<sv.num_entities;i++) {
  entities[i].s.number=i; entities[i].r.linked=qtrue;
  sv.svEntities[i].numClusters=1;
 }
 for(i=0;i<4;i++) {
  clients[i].state=CS_ACTIVE; clients[i].gentity=&entities[i];
  players[i].clientNum=i; players[i].viewheight=DEFAULT_VIEWHEIGHT;
  players[i].weapon=WP_SHOTGUN; players[i].weaponstate=WEAPON_READY;
  players[i].stats[STAT_HEALTH]=100;
 }
 /* A distant survivor and spectator cannot see the victim in live play. */
 players[2].origin[0]=players[3].origin[0]=1000;
 players[3].persistant[PERS_TEAM]=TEAM_SPECTATOR;
 for(i=2;i<=3;i++) {
  sv.svEntities[i].areanum=sv.svEntities[i].areanum2=1;
  sv.svEntities[i].clusternums[0]=1;
 }
 entities[4].r.svFlags=SVF_NOCLIENT;
 entities[5].r.svFlags=SVF_SINGLECLIENT; entities[5].r.singleClient=1;
 entities[6].r.svFlags=SVF_BROADCAST;
 entities[6].s.eType=ET_MISSILE;
 entities[6].s.pos.trType=TR_GRAVITY; entities[6].s.pos.trTime=1200;
 entities[6].s.time=123; /* overloaded duration/parameter, not an absolute clock */
 SV_ReplayReset(qtrue);
}

static clientSnapshot_t *Build(int n) {
 SV_BuildClientSnapshot(&clients[n]);
 return &clients[n].frames[0];
}
static qboolean Has(clientSnapshot_t *snap,int n) {
 int i;
 for(i=0;i<snap->num_entities;i++) if(packetEntities[(snap->first_entity+i)%ARRAY_LEN(packetEntities)].number==n) return qtrue;
 return qfalse;
}
/* A server frame of play: the killer moves, aims and works the gun. */
static void Play(int t) {
 sv.time=t; players[0].commandTime=t;
 players[0].origin[1]=t*.01f;
 players[0].viewangles[1]=t*.03f;
 players[0].weaponAnimTime=t%1000; players[0].weaponTime=1000-t%1000;
 players[0].pm_flags=PMF_ADS;
 players[0].moveClass=MOVECLASS_ROCKETLAUNCHER;	/* spawn-owned, not a clock */
 players[0].powerups[PW_QUAD]=15000;
 players[0].powerups[PW_REDFLAG]=INT_MAX;
 entities[1].s.pos.trBase[1]=t*.02f;
 SV_RecordReplayFrame();
}
/* S&D's round-winning killcam (g_snd.c): asked for the frame after the death,
 * MW2's window around it (4s before, 1.95s after), played 5.05s after it. Play
 * and recording go on to the window's end; the clip then plays, pinned. */
static void TestLobby(int step) {
 int t,n,i,end,offset,head,count,death,last=0;
 clientSnapshot_t expected,*snap;
 entityState_t expectedEntities[MAX_SNAPSHOT_ENTITIES];
 playerState_t live;
 Reset();
 for(t=1000;t<=10000;t+=step) Play(t);
 CHECK(replay.count>2 && replay.count<=REPLAY_FRAMES);
 CHECK(SV_ReplayFrame(0)->time <= sv.time-KILLCAM_REWIND);
 CHECK(!Has(Build(2),1) && !Has(Build(3),1));
 death=sv.time; Play(sv.time+step);
 end=SV_ReplayStart(0,death-KILLCAM_REWIND,death+KILLCAM_LINGER,death+KILLCAM_ROUND_DELAY);
 CHECK(replay.beginTime==death+KILLCAM_ROUND_DELAY && replay.toTime==death+KILLCAM_LINGER);
 CHECK(end==replay.beginTime+replay.toTime-replay.fromTime && end>sv.time);
 CHECK(replay.fromTime>=death-KILLCAM_REWIND);
 CHECK(replay.fromTime<death-KILLCAM_REWIND+step+REPLAY_SAMPLE_MSEC);
 CHECK(!SV_ReplayStart(1,sv.time-1000,sv.time+200,sv.time+1000)); /* no competing replay */
 /* until beginTime everyone gets the live game, and the window keeps recording */
 while(sv.time+step<replay.beginTime) {
  Play(sv.time+step);
  CHECK(!Build(0)->snapFlags && !Build(3)->snapFlags && Build(3)->ps.clientNum==3);
 }
 CHECK(SV_ReplayFrame(replay.count-1)->time<=replay.toTime);
 CHECK(SV_ReplayFrame(replay.count-1)->time>replay.toTime-step-REPLAY_SAMPLE_MSEC);
 head=replay.head; count=replay.count; offset=replay.beginTime-replay.fromTime;
 for(t=replay.beginTime;t<end;t+=step) {
  sv.time=t; live=players[0]; SV_RecordReplayFrame();
  CHECK(replay.head==head && replay.count==count); /* pinned history */
  expected=*Build(0);
  CHECK(expected.snapFlags==SNAPFLAG_REPLAY && expected.ps.clientNum==0);
  CHECK(expected.serverTime<=sv.time && expected.serverTime>=replay.beginTime);
  /* constant offset at game speed: the frame's own time, shifted */
  CHECK(expected.ps.commandTime==expected.serverTime && expected.ps.origin[1]==(expected.serverTime-offset)*.01f);
  if(t==replay.beginTime) CHECK(expected.serverTime==replay.beginTime); /* the window's first frame is kept */
  last=expected.serverTime-offset;
  CHECK(expected.ps.pm_flags&PMF_ADS);
  CHECK(expected.ps.moveClass==MOVECLASS_ROCKETLAUNCHER);	/* never rebased */
  CHECK(expected.ps.powerups[PW_QUAD]==15000+offset);
  CHECK(expected.ps.powerups[PW_REDFLAG]==INT_MAX);
  CHECK(expected.ps.weaponAnimTime+expected.ps.weaponTime==1000);
  CHECK(Has(&expected,1) && Has(&expected,6));
  CHECK(!Has(&expected,0) && !Has(&expected,2) && !Has(&expected,4) && !Has(&expected,5));
  for(i=0;i<expected.num_entities;i++) {
   expectedEntities[i]=packetEntities[(expected.first_entity+i)%ARRAY_LEN(packetEntities)];
   if(expectedEntities[i].number==6) {
    CHECK(expectedEntities[i].pos.trTime==1200+offset && expectedEntities[i].time==123);
   }
  }
  for(n=1;n<4;n++) {
   snap=Build(n);
   CHECK(snap->serverTime==expected.serverTime && snap->snapFlags==expected.snapFlags);
   CHECK(!memcmp(&snap->ps,&expected.ps,sizeof(snap->ps)));
   CHECK(!memcmp(snap->areabits,expected.areabits,sizeof(snap->areabits)));
   CHECK(snap->num_entities==expected.num_entities);
   for(i=0;i<snap->num_entities;i++) CHECK(!memcmp(&expectedEntities[i],
    &packetEntities[(snap->first_entity+i)%ARRAY_LEN(packetEntities)],sizeof(entityState_t)));
  }
  CHECK(!memcmp(&live,&players[0],sizeof(live))); /* never edits simulation */
 }
 /* it showed play up to the window's end, after the death */
 CHECK(last>death+KILLCAM_LINGER-step-REPLAY_SAMPLE_MSEC && last<=death+KILLCAM_LINGER);
 sv.time=end; CHECK(!Build(3)->snapFlags && Build(3)->ps.clientNum==3);
 SV_ReplayReset(qtrue); CHECK(!replay.count && !replay.endTime && replay.frames);
 CHECK(!Build(0)->snapFlags); /* map/round restart cancels playback */
}

static void TestBoundaries(void) {
 int t,first;
 clientSnapshot_t *snap;
 Reset(); CHECK(!SV_ReplayStart(0,0,100,5000));
 for(t=1000;t<=1100;t+=25) { sv.time=t; SV_RecordReplayFrame(); }
 CHECK(!SV_ReplayStart(-1,0,1850,5000)); CHECK(!SV_ReplayStart(4,0,1850,5000));
 CHECK(!SV_ReplayStart(0,1200,1100,5000));
 CHECK(!SV_ReplayStart(0,0,1100+KILLCAM_LINGER+1,8000)); /* the window ends too far ahead */
 CHECK(!SV_ReplayStart(0,0,1099,5000)); /* ... or has ended already */
 CHECK(!SV_ReplayStart(0,1100-REPLAY_HISTORY_MSEC,1100,5000)); /* longer than the history */
 CHECK(!SV_ReplayStart(0,0,1850,1850+REPLAY_SAMPLE_MSEC-1)); /* playback before its last sample */
 CHECK(SV_ReplayStart(0,-4000,1850,1850+REPLAY_SAMPLE_MSEC)); CHECK(replay.fromTime==1000);
 first=replay.beginTime;
 /* Follow state / disconnect cannot transfer the killer's camera to a teammate. */
 players[0].clientNum=2; players[0].origin[0]=1000;
 for(t=1125;t<=1850;t+=25) { sv.time=t; SV_RecordReplayFrame(); }
 sv.time=first+700; snap=Build(3);
 CHECK(snap->ps.clientNum==0 && snap->ps.origin[0]==0 && Has(snap,1));
 /* Arrival during playback needs no prior client history. */
 clients[3].netchan.outgoingSequence=2;
 SV_BuildClientSnapshot(&clients[3]); snap=&clients[3].frames[2];
 CHECK(snap->snapFlags==SNAPFLAG_REPLAY && Has(snap,1));
 SV_ReplayReset(qfalse); CHECK(!replay.frames && !replay.count && !replay.endTime);
}

/* MW2's snapshot rate (SV_UserinfoChanged 0x401a8a; SV_SendMessageToClient
 * 0x48ff84, 0x490033): a loopback or LAN client gets a snapshot every server
 * frame; any other one every 100ms while the server is an online game
 * (onlinegame, default on), else every 50ms. The client's `snaps` has no say. */
static cvar_t onlineGame, lanForceRate, timescale;
cvar_t *sv_onlineGame=&onlineGame, *sv_lanForceRate=&lanForceRate, *com_timescale=&timescale;
qboolean Sys_IsLANAddress(netadr_t adr) { return adr.type==NA_IP && adr.ip[0]==192 && adr.ip[1]==168; }
static void TestSnapshotRate(void) {
 static const int frames[]={ 50, 25, 8 };	/* sv_fps 20 (MW2's), 40, 125 */
 client_t c; int f, kind, online, t, sent, last;
 timescale.value=1; lanForceRate.integer=1;
 onlineGame.integer=1; CHECK(SV_ClientSnapshotMsec()==100);
 onlineGame.integer=0; CHECK(SV_ClientSnapshotMsec()==50);
 for(f=0;f<3;f++) for(kind=0;kind<3;kind++) for(online=0;online<2;online++) {
  memset(&svs,0,sizeof(svs)); memset(&c,0,sizeof(c));
  onlineGame.integer=online;
  c.snapshotMsec=SV_ClientSnapshotMsec();	/* what SV_UserinfoChanged stores */
  if(kind==0) c.netchan.remoteAddress.type=NA_LOOPBACK;
  else { c.netchan.remoteAddress.type=NA_IP; c.netchan.remoteAddress.ip[0]=kind==1 ? 192 : 81; c.netchan.remoteAddress.ip[1]=168; }
  CHECK(SV_SnapshotEveryFrame(&c)==(kind<2));
  for(sent=0,last=-1,t=1000;t<=3000;t+=frames[f]) {
   svs.time=t;
   if(SV_SnapshotDue(&c)) {	/* SV_SendClientMessages sends, and stamps the time */
    if(last>=0) CHECK(t-last==(kind<2 ? frames[f] : ((online ? 100 : 50)+frames[f]-1)/frames[f]*frames[f]));
    last=c.lastSnapshotTime=t; sent++;
   }
  }
  CHECK(sent>1);
 }
 /* a bot is sent nothing; its AI reads a snapshot built every frame */
 memset(&c,0,sizeof(c)); c.netchan.remoteAddress.type=NA_BOT; onlineGame.integer=1;
 CHECK(SV_SnapshotEveryFrame(&c));
 /* sv_lanForceRate 0 treats a LAN client as any other */
 memset(&c,0,sizeof(c)); onlineGame.integer=1; lanForceRate.integer=0; c.snapshotMsec=SV_ClientSnapshotMsec();
 c.netchan.remoteAddress.type=NA_IP; c.netchan.remoteAddress.ip[0]=192; c.netchan.remoteAddress.ip[1]=168;
 CHECK(!SV_SnapshotEveryFrame(&c));
 svs.time=c.lastSnapshotTime=1000; svs.time+=50; CHECK(!SV_SnapshotDue(&c)); svs.time+=50; CHECK(SV_SnapshotDue(&c));
 lanForceRate.integer=1;
}

int main(void) {
 TestLobby(8); TestLobby(25); TestLobby(50); TestLobby(100); TestBoundaries(); TestSnapshotRate();
 printf("PASS: %i replay visibility, timing, lobby, isolation, reset and snapshot rate checks\n",checks);
 return 0;
}
