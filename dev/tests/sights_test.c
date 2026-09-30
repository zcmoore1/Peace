/* Exercise the actual sight parser, transform and optic projection headlessly. */
#include "../../code/cgame/cg_sights.c"

cg_t cg;
cgs_t cgs;
vmCvar_t cg_sight, cg_drawGun;
static FILE *input;
static int checks, entityCount;
static refEntity_t rendered[4];
static const char *configOverride;

#define CHECK(x) do { checks++; if (!(x)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)
#define NEAR(a,b) CHECK(fabs((a)-(b)) < 0.002f)

void QDECL Com_Printf(const char *fmt, ...) { (void)fmt; }
void QDECL CG_Printf(const char *fmt, ...) { (void)fmt; }
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
	NEAR(cg.adsFrac,0);
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
static void TestAlignment(void) {
	const vec3_t cameraAngles[]={{0,0,0},{70,143,10},{-75,285,-15}};
	const vec3_t rear={10.30f,-2.698f,-1.046f};
	const vec3_t front={24.38f,-2.711f,-1.196f};
	int i,j,s;
	playerState_t before;
	Reset();
	cg.adsFrac=1;
	before=cg.predictedPlayerState;
	for(s=0;s<2;s++) {
		const sightPose_t *pose;
		cg_sight.integer=s;
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
	cg_sight.integer=1;
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
	cg_sight.integer=0;
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
int main(void) {
	Load();
	TestInvalidConfigs();
	TestTransitions();
	TestAlignment();
	TestOptic();
	printf("PASS: sight config, transitions, alignment and optic projection (%d checks).\n",checks);
	return 0;
}
