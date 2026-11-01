/* Exercise the actual sight parser, transform and optic projection headlessly. */
#include "../../code/cgame/cg_sights.c"

cg_t cg;
cgs_t cgs;
vmCvar_t cg_sight, cg_drawGun;
static FILE *input;
static int checks, entityCount;
static refEntity_t rendered[4];
static const char *configOverride;
static int scopeDraws;
static float scopeWidth;

#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)
#define NEAR(a,b) CHECK(fabs((a)-(b)) < 0.002f)

void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL CG_Printf(const char *fmt, ...) { (void)fmt; }
void trap_SnapVector(float *v) { SnapVector(v); }
void QDECL Com_Error(int level, const char *fmt, ...) {
	va_list args;
	(void)level;
	va_start(args,fmt);
	vfprintf(stderr,fmt,args);
	va_end(args);
	exit(1);
}
void CG_RegisterWeapon(int weapon) { (void)weapon; }
int trap_FS_FOpenFile(const char *path, fileHandle_t *handle, fsMode_t mode) {
	char full[256];
	long size;
	(void)mode;
	if (configOverride) { *handle=1; return (int)strlen(configOverride); }
	Com_sprintf(full,sizeof(full),"assets/baseq3/%s",path);
	input=fopen(full,"rb");
	if (!input) { *handle=0; return -1; }
	fseek(input,0,SEEK_END);
	size=ftell(input);
	rewind(input);
	*handle=1;
	return (int)size;
}
void trap_FS_Read(void *buffer, int size, fileHandle_t handle) {
	(void)handle;
	if (configOverride) memcpy(buffer,configOverride,size);
	else CHECK(fread(buffer,1,size,input)==(size_t)size);
}
void trap_FS_FCloseFile(fileHandle_t handle) {
	(void)handle;
	if (!configOverride) fclose(input);
}
qhandle_t trap_R_RegisterModel(const char *name) { CHECK(name[0]); return 1; }
qhandle_t trap_R_RegisterShader(const char *name) { CHECK(name[0]); return 2; }
void trap_R_SetColor(const float *color) { (void)color; }
void CG_FillRect(float x,float y,float w,float h,const float *color) {
 (void)x; (void)y; (void)w; (void)h; CHECK(color[3]>=0 && color[3]<=1);
}
void CG_DrawPic(float x,float y,float w,float h,qhandle_t shader) {
 (void)x; (void)y; (void)h; CHECK(shader!=0); scopeDraws++; scopeWidth=w;
}
void trap_R_AddRefEntityToScene(const refEntity_t *entity) {
	CHECK(entityCount<4);
	rendered[entityCount++]=*entity;
}
int trap_R_LerpTag(orientation_t *tag, clipHandle_t model, int oldframe,
                  int frame, float fraction, const char *name) {
	(void)model; (void)oldframe; (void)frame; (void)fraction;
	CHECK(!strcmp(name,"Base"));
	AxisClear(tag->axis);
	VectorSet(tag->origin,12.299569f,-2.454050f,-3.123764f);
	return qtrue;
}

static void Load(void) {
	configOverride=NULL;
	CG_Sights_Register(WP_SHOTGUN,"models/weapons2/spas12/view.sights");
	CHECK(cg_sights[WP_SHOTGUN].iron.valid);
	CHECK(cg_sights[WP_SHOTGUN].redDot.valid);
}
static void Reset(void) {
	memset(&cg,0,sizeof(cg));
	memset(&cg_sight,0,sizeof(cg_sight));
	cg_drawGun.integer=1;
	cgs.screenXScale=cgs.screenYScale=1;
	cg.time=1000;
	cg.predictedPlayerState.weapon=WP_SHOTGUN;
	cg.predictedPlayerState.stats[STAT_HEALTH]=100;
	cg.predictedPlayerState.pm_flags=PMF_ADS;
	cg.predictedPlayerState.weaponstate=WEAPON_READY;
	cg.predictedPlayerState.weaponAnimTime=-1;
	AxisClear(cg.refdef.viewaxis);
	cg.refdef.fov_x=65;
	CG_Sights_Update(&cg.predictedPlayerState);
}
static void Tick(int time) {
	cg.time+=time;
	CG_Sights_Update(&cg.predictedPlayerState);
}
static void TestTransitions(void) {
	int in=(int)cg_sights[WP_SHOTGUN].inTime;
	int out=(int)cg_sights[WP_SHOTGUN].outTime;
	float partial;
	Reset();
	Tick(in/2);
	NEAR(cg.adsFrac,(float)(in/2)/in);
	partial=cg.adsFrac;
	/* A second eye/render pass at the same serverTime must not advance. */
	CG_Sights_Update(&cg.predictedPlayerState);
	NEAR(cg.adsFrac,partial);
	cg.predictedPlayerState.pm_flags=0;
	Tick(out/4);
	CHECK(cg.adsFrac>0 && cg.adsFrac<partial);
	cg.predictedPlayerState.pm_flags=PMF_ADS;
	Tick(in);
	NEAR(cg.adsFrac,1);
	NEAR(CG_Sights_Fraction(),1);
	CHECK(CG_Sights_HideCrosshair());
	cg_drawGun.integer=0;
	CHECK(!CG_Sights_HideCrosshair());
	cg_drawGun.integer=1;
	cg.predictedPlayerState.pm_flags=0;
	Tick(out);
	NEAR(cg.adsFrac,0);
	CHECK(!CG_Sights_HideCrosshair());
	cg.predictedPlayerState.pm_flags=PMF_ADS;
	Tick(in);
	cg.predictedPlayerState.weapon=WP_MACHINEGUN;
	Tick(in);
	NEAR(cg.adsFrac,1); /* aim progress belongs to the player */
	CHECK(!CG_Sights_HideCrosshair());
	Tick(in);
	cg.predictedPlayerState.stats[STAT_HEALTH]=0;
	Tick(in);
	NEAR(cg.adsFrac,0);
	Reset();
	Tick(in);
	cg.time-=in*2;
	CG_Sights_Update(&cg.predictedPlayerState);
	NEAR(cg.adsFrac,0);
}

static void ModelPoint(const refEntity_t *hand, const vec3_t point, vec3_t out) {
	int i;
	VectorCopy(hand->origin,out);
	for(i=0;i<3;i++) VectorMA(out,point[i],hand->axis[i],out);
}

static void TestTeleportWithoutPrediction(void) {
	int in=(int)cg_sights[WP_SHOTGUN].inTime;
	Reset();
	cg.thisFrameTeleport=qtrue; /* interpolation never clears this flag */
	Tick(in);
	NEAR(cg.adsFrac,1);
	cg.predictedPlayerState.eFlags ^= EF_TELEPORT_BIT;
	Tick(1);
	NEAR(cg.adsFrac,0);
	Tick(in/2);
	CHECK(cg.adsFrac>0 && cg.adsFrac<1);
	Tick(in);
	NEAR(cg.adsFrac,1);
	/* A second teleport still resets, even though the global flag stayed set. */
	cg.predictedPlayerState.eFlags ^= EF_TELEPORT_BIT;
	Tick(1);
	NEAR(cg.adsFrac,0);
	Tick(in);
	NEAR(cg.adsFrac,1);
}
static void TestAlignment(void) {
	const vec3_t cameraAngles[]={{0,0,0},{70,143,10},{-75,285,-15}};
	const vec3_t rear={10.30f,-2.698f,-1.046f};
	const vec3_t front={24.38f,-2.711f,-1.196f};
	int i,j,s;
	playerState_t before;
	Reset();
	cg.adsFrac=1;
	for(s=0;s<2;s++) {
		const sightPose_t *pose;
		cg.predictedPlayerState.slotAttachments[SLOT_PRIMARY]=s;
		before=cg.predictedPlayerState;
		pose=CG_Sights_Pose(&cg_sights[WP_SHOTGUN]);
		for(i=0;i<3;i++) {
			refEntity_t hand;
			vec3_t eye, point, relative;
			memset(&hand,0,sizeof(hand));
			AnglesToAxis(cameraAngles[i],cg.refdef.viewaxis);
			VectorSet(cg.refdef.vieworg,100,-30,64);
			/* Arbitrary hip offsets/bob must disappear at full ADS. */
			VectorSet(hand.origin,17,99,-42);
			AxisClear(hand.axis);
			hand.frame=42; hand.oldframe=41; hand.backlerp=0.3f;
			CG_Sights_Apply(WP_SHOTGUN,&hand);
			ModelPoint(&hand,pose->eye,eye);
			for(j=0;j<3;j++) NEAR(eye[j],cg.refdef.vieworg[j]);
			for(j=0;j<3;j++) NEAR(VectorLength(hand.axis[j]),1);
			CHECK(hand.frame==42 && hand.oldframe==41);
			NEAR(hand.backlerp,0.3f);
			if(s==0) {
				ModelPoint(&hand,rear,point);
				VectorSubtract(point,cg.refdef.vieworg,relative);
				CHECK(fabs(DotProduct(relative,cg.refdef.viewaxis[1]))<0.02f);
				CHECK(fabs(DotProduct(relative,cg.refdef.viewaxis[2]))<0.02f);
				ModelPoint(&hand,front,point);
				VectorSubtract(point,cg.refdef.vieworg,relative);
				CHECK(fabs(DotProduct(relative,cg.refdef.viewaxis[1]))<0.02f);
				CHECK(fabs(DotProduct(relative,cg.refdef.viewaxis[2]))<0.02f);
			}
		}
	}
	CHECK(!memcmp(&before,&cg.predictedPlayerState,sizeof(before)));
}

static void TestOptic(void) {
	refEntity_t hand;
	modelPose_t pose;
	Reset();
	cg.predictedPlayerState.slotAttachments[SLOT_PRIMARY]=BG_ATTACHMENT_REDDOT;
	cg.adsFrac=1;
	memset(&hand,0,sizeof(hand));
	AxisClear(hand.axis);
	CG_Sights_Apply(WP_SHOTGUN,&hand);
	entityCount=0;
	CG_Sights_AddOptic(WP_SHOTGUN,&hand);
	CHECK(entityCount==2);
	CHECK(rendered[0].reType==RT_MODEL && rendered[1].reType==RT_SPRITE);
	NEAR(rendered[1].origin[1],0);
	NEAR(rendered[1].origin[2],0);
	CHECK(rendered[1].origin[0]>4); /* lens stays in front of the near plane */
	/* Blended GL2 pose and GL1/QVM frame tag must mount at the same point. */
	memset(&pose,0,sizeof(pose));
	pose.numJoints=1;
	pose.joints[0].rotate[3]=1;
	VectorSet(pose.joints[0].scale,1,1,1);
	VectorSet(pose.joints[0].translate,12.299569f,-2.454050f,-3.123764f);
	hand.pose=&pose;
	entityCount=0;
	CG_Sights_AddOptic(WP_SHOTGUN,&hand);
	CHECK(entityCount==2);
	NEAR(rendered[0].origin[1],0);
	NEAR(rendered[0].origin[2],0);
	/* Moving animation moves the housing and takes the reticle off the lens.
	   No on-screen replacement dot may appear outside the aperture. */
	pose.joints[0].translate[1]+=cg_sights[WP_SHOTGUN].aperture*2;
	entityCount=0;
	CG_Sights_AddOptic(WP_SHOTGUN,&hand);
	CHECK(entityCount==1);
	cg.predictedPlayerState.slotAttachments[SLOT_PRIMARY]=0;
	entityCount=0;
	CG_Sights_AddOptic(WP_SHOTGUN,&hand);
	CHECK(entityCount==0);
}

static void TestInvalidConfigs(void) {
	const char *bad[]={
		"iron 0 0\n", "iron 0 0 0 0 0 0\nfov 0\n",
		"iron 0 0 0 0 0 0\ntiming 0 100\n",
		"iron nan 0 0 0 0 0\n", "iron 0 0 0 0 0 0\nunknown 1\n",
		"iron 0 0 0 0 0 0\nreddot 0 0 0 0 0 0\n"
	};
	int i;
	for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
		configOverride=bad[i];
		CG_Sights_Register(WP_SHOTGUN,"test");
		CHECK(!cg_sights[WP_SHOTGUN].iron.valid);
	}
	Load();
	CG_Sights_Register(WP_SHOTGUN,NULL);
	CHECK(!cg_sights[WP_SHOTGUN].iron.valid);
	Load();
}
static void PrepareAimSwap(float fraction) {
	Reset();
	Tick((int)cg_sights[WP_SHOTGUN].inTime);
	cg.adsFrac=fraction;
	cg.predictedPlayerState.weapon=WP_M40A3;
	cg.predictedPlayerState.stats[STAT_ACTIVE_SLOT]=SLOT_SECONDARY;
	cg.predictedPlayerState.weaponstate=WEAPON_RAISING;
}

static void TestAimPermission(void) {
	const int blocked[]={WEAPON_RAISING,WEAPON_DROPPING,WEAPON_RELOADING,
		WEAPON_LADDER};
	const int sprintStates[]={WEAPON_SPRINT_IN,WEAPON_SPRINTING,WEAPON_SPRINT_OUT};
	int i, draws;
	playerState_t before;
	CG_Sights_Register(WP_M40A3,"models/weapons2/m40a3/view.sights");
	PrepareAimSwap(1);
	before=cg.predictedPlayerState;
	CG_Sights_Update(&cg.predictedPlayerState);
	NEAR(cg.adsFrac,1);
	CHECK(CG_Sights_HideViewModel() && CG_Sights_HideCrosshair());
	NEAR(1+CG_Sights_Fraction()*(CG_Sights_FovScale(WP_M40A3)-1),.26f);
	draws=scopeDraws; CG_Sights_DrawScope(); CHECK(scopeDraws==draws+1);
	CHECK(!memcmp(&before,&cg.predictedPlayerState,sizeof(before)));
	// Advancing render time does not expire aim. Only simulation permission
	// does: another eye or frame of the same state can still show the scope.
	Tick(1); Tick(37); NEAR(cg.adsFrac,1);
	cg.predictedPlayerState.pm_flags=0;
	CG_Sights_Update(&cg.predictedPlayerState); /* no render-time advance */
	NEAR(cg.adsFrac,0);
	CHECK(!CG_Sights_HideViewModel() && !CG_Sights_HideCrosshair());
	CG_Sights_DrawScope(); CHECK(scopeDraws==draws+1);
	NEAR(1+CG_Sights_Fraction()*(CG_Sights_FovScale(WP_M40A3)-1),1);

	// Releasing the button while aiming remains physically possible preserves
	// the usual exit curve, including its scope fade and matching FOV/model.
	PrepareAimSwap(1);
	cg.predictedPlayerState.weaponstate=WEAPON_READY;
	CG_Sights_Update(&cg.predictedPlayerState);
	cg.predictedPlayerState.pm_flags=0;
	Tick((int)cg_sights[WP_M40A3].outTime/10);
	NEAR(cg.adsFrac,.9f);
	draws=scopeDraws; CG_Sights_DrawScope(); CHECK(scopeDraws==draws+1);
	Tick((int)cg_sights[WP_M40A3].outTime);
	NEAR(cg.adsFrac,0);
	cg.predictedPlayerState.pm_flags=PMF_ADS;
	Tick((int)cg_sights[WP_M40A3].inTime/2); NEAR(cg.adsFrac,.5f);
	Tick((int)cg_sights[WP_M40A3].inTime/2); NEAR(cg.adsFrac,1);

	// No invented aim progress, including partial ADS and same-base copies.
	PrepareAimSwap(0); CG_Sights_Update(&cg.predictedPlayerState); NEAR(cg.adsFrac,0);
	PrepareAimSwap(.5f); CG_Sights_Update(&cg.predictedPlayerState); NEAR(cg.adsFrac,.5f);
	draws=scopeDraws; CG_Sights_DrawScope(); CHECK(scopeDraws==draws);
	Reset(); Tick((int)cg_sights[WP_SHOTGUN].inTime);
	cg.predictedPlayerState.stats[STAT_ACTIVE_SLOT]=SLOT_SECONDARY;
	cg.predictedPlayerState.slotAttachments[SLOT_SECONDARY]=BG_ATTACHMENT_REDDOT;
	CG_Sights_Update(&cg.predictedPlayerState); NEAR(cg.adsFrac,1);
	cg.predictedPlayerState.weapon=WP_M40A3;
	cg.predictedPlayerState.slotAttachments[SLOT_SECONDARY]=0;
	CG_Sights_Update(&cg.predictedPlayerState); NEAR(cg.adsFrac,1);

	for(i=0;i<ARRAY_LEN(blocked)+5;i++) {
		PrepareAimSwap(1);
		cg.predictedPlayerState.pm_flags=0;
		cg.predictedPlayerState.weaponstate=WEAPON_READY;
		if(i<ARRAY_LEN(blocked)) cg.predictedPlayerState.weaponstate=blocked[i];
		else if(i==ARRAY_LEN(blocked)) cg.predictedPlayerState.weaponAction=WA_MELEE;
		else if(i==ARRAY_LEN(blocked)+1) cg.predictedPlayerState.stats[STAT_LADDER]=LADDER_ATTACHED;
		else if(i==ARRAY_LEN(blocked)+2) cg.predictedPlayerState.weapon=WP_KNIFE;
		else if(i==ARRAY_LEN(blocked)+3) cg.predictedPlayerState.weapon=WP_NONE;
		else cg.predictedPlayerState.pm_flags=PMF_SPRINTING;
		CHECK(!BG_CanAimDownSights(&cg.predictedPlayerState));
		CG_Sights_Update(&cg.predictedPlayerState); NEAR(cg.adsFrac,0);
	}
	// Current dev permits aiming as soon as ADS ends sprint movement, even
	// while its carry/exit animation still runs. Do not restore the old state
	// blacklist when sharing the physical permission check with presentation.
	for(i=0;i<ARRAY_LEN(sprintStates);i++) {
		PrepareAimSwap(1);
		cg.predictedPlayerState.pm_flags=0;
		cg.predictedPlayerState.weaponstate=sprintStates[i];
		CHECK(BG_CanAimDownSights(&cg.predictedPlayerState));
		Tick((int)cg_sights[WP_M40A3].outTime/10); NEAR(cg.adsFrac,.9f);
	}
	// Camera discontinuities win even when incoming ADS permission is live.
	for(i=0;i<7;i++) {
		PrepareAimSwap(1);
		switch(i) {
		case 0: cg.predictedPlayerState.eFlags^=EF_TELEPORT_BIT; break;
		case 1: cg.predictedPlayerState.clientNum++; break;
		case 2: cg.predictedPlayerState.stats[STAT_HEALTH]=0; break;
		case 3: cg.renderingThirdPerson=qtrue; break;
		case 4: cg.predictedPlayerState.pm_type=PM_INTERMISSION; break;
		case 5: cg.predictedPlayerState.persistant[PERS_TEAM]=TEAM_SPECTATOR; break;
		case 6: cg.time=cg.adsTime-1; break;
		}
		CG_Sights_Update(&cg.predictedPlayerState); NEAR(cg.adsFrac,0);
	}
}

static void ClearWorldTrace(trace_t *tr,const vec3_t start,const vec3_t mins,
	const vec3_t maxs,const vec3_t end,int pass,int mask) {
	(void)start;(void)mins;(void)maxs;(void)pass;(void)mask;
	memset(tr,0,sizeof(*tr));tr->fraction=1;tr->entityNum=ENTITYNUM_NONE;
	VectorCopy(end,tr->endpos);
}
static int NoWorldContents(const vec3_t point,int pass) { (void)point;(void)pass;return 0; }
static void MoveAim(pmove_t *move,int msec,qboolean present) {
	playerState_t before;
	move->cmd.serverTime=move->ps->commandTime+msec;
	Pmove(move);
	cg.time=move->ps->commandTime;
	if(present) {
		before=*move->ps;
		CG_Sights_Update(move->ps);
		CHECK(!memcmp(&before,move->ps,sizeof(before)));
	}
}
static void BeginAimedShot(pmove_t *move) {
	playerState_t *ps=&cg.predictedPlayerState;
	Reset(); Tick((int)cg_sights[WP_SHOTGUN].inTime);
	BG_SetSlotWeapon(ps,SLOT_PRIMARY,WP_SHOTGUN);
	BG_SetSlotWeapon(ps,SLOT_SECONDARY,WP_M40A3);
	ps->slotFlags[SLOT_PRIMARY]=ps->slotFlags[SLOT_SECONDARY]=0;
	ps->slotAttachments[SLOT_PRIMARY]=BG_ATTACHMENT_REDDOT;
	ps->weapon=WP_SHOTGUN;ps->weaponstate=WEAPON_READY;ps->weaponTime=0;
	ps->weaponAnimTime=-1;ps->commandTime=cg.time;ps->speed=320;
	ps->ammo[WP_SHOTGUN]=BG_WeaponMagSize(WP_SHOTGUN);
	ps->groundEntityNum=ENTITYNUM_NONE;
	memset(move,0,sizeof(*move));move->ps=ps;
	move->trace=ClearWorldTrace;move->pointcontents=NoWorldContents;
	move->tracemask=MASK_PLAYERSOLID;move->cmd.weapon=BG_SlotCommand(ps,SLOT_PRIMARY);
	move->cmd.buttons=BUTTON_ADS|BUTTON_ATTACK;
	MoveAim(move,1,qtrue);
	CHECK(ps->weaponstate==WEAPON_FIRING);
	move->cmd.buttons=BUTTON_ADS;
}

static void TestScopeThroughPmove(void) {
	pmove_t move;
	playerState_t *ps=&cg.predictedPlayerState;
	bg_weaponInstance_t original;
	int closed,offset,t,draws;
	// Measure the shipped close note through real gameplay, not a copied time.
	BeginAimedShot(&move);
	for(t=0;t<BG_WeaponFireLength(WP_SHOTGUN) &&
		(ps->stats[STAT_UNCHAMBERED] & (1<<WP_SHOTGUN));t++) MoveAim(&move,1,qfalse);
	CHECK(!(ps->stats[STAT_UNCHAMBERED] & (1<<WP_SHOTGUN)));
	closed=ps->weaponAnimTime;
	CHECK(closed>1 && closed<BG_WeaponFireLength(WP_SHOTGUN));
	for(offset=-1;offset<=1;offset++) {
		BeginAimedShot(&move);
		for(t=0;t<closed-1+offset;t++) MoveAim(&move,1,qtrue);
		move.cmd.weapon=BG_SlotCommand(ps,SLOT_SECONDARY);
		MoveAim(&move,1,qtrue);
		for(t=0;ps->weapon==WP_SHOTGUN && t<=BG_WeaponDropTime(WP_SHOTGUN);t++) MoveAim(&move,1,qtrue);
		CHECK(ps->weapon==WP_M40A3 && ps->weaponstate==WEAPON_RAISING);
		CHECK(!!(ps->pm_flags & PMF_ADS)==(offset==0));
		NEAR(cg.adsFrac,offset==0 ? 1 : 0);
		draws=scopeDraws;CG_Sights_DrawScope();CHECK(scopeDraws==draws+(offset==0));
		BG_GetSlot(ps,SLOT_PRIMARY,&original);
		CHECK(!!(original.flags & BG_GUN_UNCHAMBERED)==(offset<=0));
		MoveAim(&move,1,qtrue);
		CHECK(!(ps->pm_flags & PMF_ADS));NEAR(cg.adsFrac,0);
		draws=scopeDraws;CG_Sights_DrawScope();CHECK(scopeDraws==draws);
	}
	// Two simulation updates before drawing: the transient was never rendered.
	// There is no event or presentation latch to replay it after ADS is revoked.
	BeginAimedShot(&move);
	for(t=0;t<closed-1;t++) MoveAim(&move,1,qtrue);
	move.cmd.weapon=BG_SlotCommand(ps,SLOT_SECONDARY);
	MoveAim(&move,1,qfalse);CHECK(ps->weapon==WP_M40A3 && (ps->pm_flags & PMF_ADS));
	MoveAim(&move,1,qtrue);CHECK(!(ps->pm_flags & PMF_ADS));NEAR(cg.adsFrac,0);
	draws=scopeDraws;CG_Sights_DrawScope();CHECK(scopeDraws==draws);
}


static void TestPmoveAimInputs(void) {
	pmove_t move;
	playerState_t *ps=&cg.predictedPlayerState;
	int weapon, state;
	const int sprintStates[]={WEAPON_SPRINT_IN,WEAPON_SPRINTING,WEAPON_SPRINT_OUT};
	for(weapon=0;weapon<2;weapon++) {
		BeginAimedShot(&move);
		if(weapon) BG_SelectSlot(ps,SLOT_SECONDARY);
		ps->weaponstate=WEAPON_READY;ps->weaponTime=0;ps->weaponAnimTime=-1;
		ps->stats[STAT_UNCHAMBERED]=0;
		move.cmd.weapon=BG_SlotCommand(ps,weapon ? SLOT_SECONDARY : SLOT_PRIMARY);
		move.cmd.buttons=BUTTON_ADS|BUTTON_SPRINT;
		MoveAim(&move,1,qtrue);
		// The held sprint key blocks an unscoped gun but serves as the scope's
		// breath input. Sharing physical permission must retain this input rule.
		CHECK(!!(ps->pm_flags & PMF_ADS)==weapon);
		move.cmd.buttons=BUTTON_ADS;
		MoveAim(&move,1,qtrue);CHECK(ps->pm_flags & PMF_ADS);
	}
	for(state=0;state<ARRAY_LEN(sprintStates);state++) {
		BeginAimedShot(&move);
		ps->weaponstate=sprintStates[state];ps->weaponTime=BG_WeaponSprintOutTime(ps->weapon);
		ps->weaponAnimTime=-1;ps->stats[STAT_UNCHAMBERED]=0;
		ps->pm_flags|=PMF_SPRINTING;
		move.cmd.buttons=BUTTON_ADS;move.cmd.forwardmove=127;
		MoveAim(&move,1,qtrue);
		CHECK(!(ps->pm_flags & PMF_SPRINTING) && (ps->pm_flags & PMF_ADS));
		CHECK(ps->weaponstate==WEAPON_SPRINT_OUT);NEAR(cg.adsFrac,1);
	}
}

int main(void) {
	Load();
	TestInvalidConfigs();
	TestTransitions();
	TestTeleportWithoutPrediction();
	TestAlignment();
	TestOptic();
	TestAimPermission();
	TestScopeThroughPmove();
	TestPmoveAimInputs();
	scopeDraws=0;
	Reset();
	CG_Sights_Register(WP_M40A3,"models/weapons2/m40a3/view.sights");
	cg.predictedPlayerState.weapon=WP_M40A3;
	CG_Sights_Update(&cg.predictedPlayerState);
	Tick((int)cg_sights[WP_M40A3].inTime);
	NEAR(cg.adsFrac,1);
	CHECK(CG_Sights_HideCrosshair());
	NEAR(CG_Sights_FovScale(WP_M40A3),.26f);
	cgs.screenXScale=3; cgs.screenYScale=2.25f;
	CG_Sights_DrawScope(); CHECK(scopeDraws==1); NEAR(scopeWidth*3,1080);
	CHECK(CG_Sights_HideViewModel());
	cg.adsFrac=.5f; CG_Sights_DrawScope(); CHECK(scopeDraws==1);
	CHECK(!CG_Sights_HideViewModel());
	cg.adsFrac=1; cg.renderingThirdPerson=qtrue; CG_Sights_DrawScope(); CHECK(scopeDraws==1);
	printf("PASS: sight config, transitions, alignment and optic projection (%d checks).\n",checks);
	return 0;
}
