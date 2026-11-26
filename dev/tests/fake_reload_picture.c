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
	CG_WeapAnim_RegisterClips(WP_M16, 1, "assets/baseq3/models/weapons2/m16/view.cfg");
	CG_WeapAnim_RegisterClips(WP_G19, 1, "assets/baseq3/models/weapons2/g19/view.cfg");
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
/* The move layer drawn this frame (a draw, a drop or a cancel's return): is it
 * the drop clip, a draw clip, easing home; where it is in its clip and how much
 * of the picture it is; and the gun's whole drop clip, in clip ms. */
void FakeReloadMove(int weapon, int *drop, int *draw, int *ret, float *time, float *weight, float *dropLength) {
	const animLayer_t *move = &cg_weapLayers[LAYER_MOVE];
	*drop = move->clip == WANIM_DROP; *draw = CG_WeapAnim_IsDrawClip(move->clip); *ret = move->fit == MOVE_FIT_RETURN;
	*time = move->time; *weight = move->weight;
	*dropLength = weapon > WP_NONE && weapon < WP_NUM_WEAPONS ? CG_WeapAnim_ClipLength(&bg_weapAnimDefs[weapon], WANIM_DROP) : 0;
}
/* Every run starts with no picture of its own (CG_WeapAnim_Reset). */
void FakeReloadPictureReset(void) { CG_WeapAnim_Reset(); }
