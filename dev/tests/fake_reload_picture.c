/* The real cg_anim.c, for what the viewmodel shows. Picture only. */
#include "../../code/cgame/cg_anim.c"
cg_t cg;
static FILE *input;
void QDECL CG_Printf(const char *fmt, ...) { (void)fmt; }
int trap_FS_FOpenFile(const char *path, fileHandle_t *handle, fsMode_t mode) {
	long size; (void)mode; input = fopen(path, "rb"); if (!input) { fprintf(stderr, "missing %s\n", path); exit(2); }
	fseek(input, 0, SEEK_END); size = ftell(input); rewind(input); *handle = 1; return size;
}
void trap_FS_Read(void *buffer, int size, fileHandle_t handle) { (void)handle; if (fread(buffer, 1, size, input) != (size_t)size) exit(3); }
void trap_FS_FCloseFile(fileHandle_t handle) { (void)handle; fclose(input); }
qboolean trap_R_BuildModelPose(qhandle_t model, int frame, int oldframe, float blend, modelPose_t *out) {
	(void)model; (void)frame; (void)oldframe; (void)blend; memset(out, 0, sizeof(*out)); return qtrue;
}
void trap_R_BlendModelPoses(modelPose_t *out, const modelPose_t *a, float wa, const modelPose_t *b, float wb) {
	(void)a; (void)wa; (void)b; (void)wb; memset(out, 0, sizeof(*out));
}
void FakeReloadPictureInit(void) {
	CG_WeapAnim_Init();
	CG_WeapAnim_RegisterClips(WP_SHOTGUN, 1, "assets/baseq3/models/weapons2/spas12/view.cfg");
	CG_WeapAnim_RegisterClips(WP_M40A3, 1, "assets/baseq3/models/weapons2/m40a3/view.cfg");
}
/* The base-layer clip on screen (WANIM_*), -1 for a weapon without clips. Fails on any ps write. */
int FakeReloadPicture(playerState_t *ps, int now, int msec) {
	playerState_t before = *ps; refEntity_t ent;
	cg.time = now; memset(&ent, 0, sizeof(ent)); ent.hModel = 1;
	if (!CG_WeapAnim_Apply(ps, &ent, msec)) return -1;
	if (memcmp(ps, &before, sizeof(*ps))) { fprintf(stderr, "FAIL: cgame wrote the playerState\n"); exit(1); }
	return cg_weapLayers[LAYER_BASE].weight > .5f ? cg_weapLayers[LAYER_BASE].clip : -1;
}
int FakeReloadIsReloadClip(int clip) { return clip >= WANIM_RELOAD_START && clip <= WANIM_RELOAD_END; }
