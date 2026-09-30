/*
 * First-person sights. These transforms describe an eye in the model's idle
 * coordinates. They are applied to the whole animated model, never to its
 * gameplay state or to the clip clock. A moving/reloading picture can therefore
 * coexist with aim permission if prediction/presentation later disagree.
 */
#include "cg_local.h"

typedef struct {
	qboolean valid;
	vec3_t eye;
	vec3_t angles;
} sightPose_t;

typedef struct {
	sightPose_t iron, redDot;
	float inTime, outTime, fovScale, aperture;
	char mountTag[MAX_QPATH];
	int rootJoint;                 // parentless joint; matches mountTag
	vec3_t mountOrigin, mountAngles, lensOrigin;
	qhandle_t opticModel, reticleShader;
} weaponSights_t;

static weaponSights_t cg_sights[WP_NUM_WEAPONS];

static const weaponSights_t *CG_Sights_Def( int weapon ) {
	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) return NULL;
	return &cg_sights[weapon];
}

static const sightPose_t *CG_Sights_Pose( const weaponSights_t *def ) {
	if ( !def || !def->iron.valid ) return NULL;
	if ( cg_sight.integer == 1 && def->redDot.valid && def->opticModel ) {
		return &def->redDot;
	}
	return &def->iron;
}

static qboolean CG_Sights_Numbers( char **cursor, float *values, int count ) {
	int i;
	char *token;
	for ( i = 0; i < count; i++ ) {
		token = COM_ParseExt( cursor, qfalse );
		if ( !token[0] || !Q_isanumber( token ) ) return qfalse;
		values[i] = atof( token );
		// This also rejects NaN/infinity before they reach the renderer.
		if ( !(values[i] >= -4096.0f && values[i] <= 4096.0f) ) return qfalse;
	}
	return qtrue;
}

void CG_Sights_Register( int weapon, const char *path ) {
	weaponSights_t def;
	fileHandle_t file;
	char buffer[4096], optic[MAX_QPATH], reticle[MAX_QPATH];
	char *cursor, *token;
	int length;
	float values[7];
	qboolean valid = qtrue;

	if ( weapon <= WP_NONE || weapon >= WP_NUM_WEAPONS ) return;
	memset( &cg_sights[weapon], 0, sizeof(cg_sights[weapon]) );
	if ( !path || !path[0] ) return;
	length = trap_FS_FOpenFile( path, &file, FS_READ );
	if ( length < 0 ) return;
	if ( length >= sizeof(buffer) ) {
		trap_FS_FCloseFile( file );
		CG_Printf( "^3Sight config %s is too large\n", path );
		return;
	}
	trap_FS_Read( buffer, length, file );
	trap_FS_FCloseFile( file );
	buffer[length] = '\0';

	memset( &def, 0, sizeof(def) );
	def.inTime = 150.0f;
	def.outTime = 120.0f;
	def.fovScale = 0.72f;
	def.aperture = 0.78f;
	def.rootJoint = -1;
	optic[0] = reticle[0] = '\0';
	cursor = buffer;
	while ( valid && (token = COM_Parse( &cursor ))[0] ) {
		if ( !Q_stricmp( token, "iron" ) || !Q_stricmp( token, "reddot" ) ) {
			sightPose_t *pose = !Q_stricmp( token, "iron" ) ? &def.iron : &def.redDot;
			valid = CG_Sights_Numbers( &cursor, values, 6 );
			if ( !valid ) break;
			VectorCopy( values, pose->eye );
			VectorSet( pose->angles, values[3], values[4], values[5] );
			pose->valid = qtrue;
		} else if ( !Q_stricmp( token, "timing" ) ) {
			valid = CG_Sights_Numbers( &cursor, values, 2 );
			if ( !valid || values[0] <= 0 || values[1] <= 0 ) { valid = qfalse; break; }
			def.inTime = values[0];
			def.outTime = values[1];
		} else if ( !Q_stricmp( token, "fov" ) ) {
			valid = CG_Sights_Numbers( &cursor, values, 1 );
			if ( !valid || values[0] < 0.1f || values[0] > 1.0f ) { valid = qfalse; break; }
			def.fovScale = values[0];
		} else if ( !Q_stricmp( token, "lens" ) ) {
			valid = CG_Sights_Numbers( &cursor, values, 4 );
			if ( !valid || values[3] <= 0 ) { valid = qfalse; break; }
			VectorCopy( values, def.lensOrigin );
			def.aperture = values[3];
		} else if ( !Q_stricmp( token, "optic" ) || !Q_stricmp( token, "reticle" ) ) {
			char *name = !Q_stricmp( token, "optic" ) ? optic : reticle;
			token = COM_ParseExt( &cursor, qfalse );
			if ( !token[0] || strlen(token) >= MAX_QPATH ) { valid = qfalse; break; }
			Q_strncpyz( name, token, MAX_QPATH );
		} else if ( !Q_stricmp( token, "mount" ) ) {
			token = COM_ParseExt( &cursor, qfalse );
			if ( !token[0] || strlen(token) >= MAX_QPATH ) { valid = qfalse; break; }
			Q_strncpyz( def.mountTag, token, sizeof(def.mountTag) );
			valid = CG_Sights_Numbers( &cursor, values, 7 );
			if ( !valid || values[0] < 0 || values[0] >= MAX_MODEL_JOINTS ||
			     values[0] != (int)values[0] ) { valid = qfalse; break; }
			def.rootJoint = (int)values[0];
			VectorSet( def.mountOrigin, values[1], values[2], values[3] );
			VectorSet( def.mountAngles, values[4], values[5], values[6] );
		} else {
			valid = qfalse;
		}
	}
	if ( !valid || !def.iron.valid ||
	     (def.redDot.valid && (!optic[0] || !reticle[0] || def.rootJoint < 0)) ) {
		CG_Printf( "^3Invalid sight config %s; keeping hipfire presentation\n", path );
		return;
	}
	if ( def.redDot.valid ) {
		def.opticModel = trap_R_RegisterModel( optic );
		def.reticleShader = trap_R_RegisterShader( reticle );
		// A missing attachment falls back to the authored iron sights.
		if ( !def.opticModel || !def.reticleShader ) def.redDot.valid = qfalse;
	}
	cg_sights[weapon] = def;
}

float CG_Sights_Fraction( void ) {
	// Smooth endpoints, with one reversible linear clock underneath.
	return cg.adsFrac * cg.adsFrac * (3.0f - 2.0f * cg.adsFrac);
}

float CG_Sights_FovScale( int weapon ) {
	const weaponSights_t *def = CG_Sights_Def( weapon );
	return CG_Sights_Pose( def ) ? def->fovScale : 0.72f;
}

void CG_Sights_Update( const playerState_t *ps ) {
	const weaponSights_t *def;
	float target, duration, step;
	int elapsed = cg.time - cg.adsTime;
	int sight = cg_sight.integer == 1 ? 1 : 0;

	if ( ps->weapon > WP_NONE && ps->weapon < WP_NUM_WEAPONS ) {
		CG_RegisterWeapon( ps->weapon );
	}
	def = CG_Sights_Def( ps->weapon );
	if ( !cg.adsTime || elapsed < 0 || cg.adsWeapon != ps->weapon ||
	     cg.adsClientNum != ps->clientNum || cg.adsSight != sight ||
	     cg.thisFrameTeleport || cg.renderingThirdPerson ||
	     ps->stats[STAT_HEALTH] <= 0 || ps->pm_type == PM_INTERMISSION ||
	     ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		cg.adsFrac = 0.0f;
		elapsed = 0;
	}
	cg.adsTime = cg.time;
	cg.adsWeapon = ps->weapon;
	cg.adsClientNum = ps->clientNum;
	cg.adsSight = sight;

	// Gameplay supplies permission. In particular, no visual reload-layer
	// check belongs here: a real reload is denied by PM_CheckADS in pmove.
	target = (ps->pm_flags & PMF_ADS) ? 1.0f : 0.0f;
	duration = def && def->iron.valid ?
	           (target > cg.adsFrac ? def->inTime : def->outTime) :
	           (target > cg.adsFrac ? 150.0f : 120.0f);
	step = elapsed / duration;
	if ( cg.adsFrac < target ) cg.adsFrac = MIN( target, cg.adsFrac + step );
	else if ( cg.adsFrac > target ) cg.adsFrac = MAX( target, cg.adsFrac - step );
}

qboolean CG_Sights_HideCrosshair( void ) {
	return cg_drawGun.integer && !cg.testGun && !cg.renderingThirdPerson &&
	       CG_Sights_Pose( CG_Sights_Def( cg.predictedPlayerState.weapon ) ) &&
	       cg.adsFrac > 0.05f;
}

void CG_Sights_Apply( int weapon, refEntity_t *hand ) {
	const sightPose_t *pose = CG_Sights_Pose( CG_Sights_Def( weapon ) );
	vec3_t localAxis[3], aimAxis[3], aimOrigin;
	float blend = CG_Sights_Fraction();
	int i, j;

	if ( !pose || blend <= 0.0f ) return;
	AnglesToAxis( pose->angles, localAxis );
	MatrixMultiply( localAxis, cg.refdef.viewaxis, aimAxis );
	VectorCopy( cg.refdef.vieworg, aimOrigin );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( aimOrigin, -pose->eye[i], aimAxis[i], aimOrigin );
	}
	for ( i = 0; i < 3; i++ ) {
		hand->origin[i] += blend * (aimOrigin[i] - hand->origin[i]);
		for ( j = 0; j < 3; j++ ) {
			hand->axis[i][j] += blend * (aimAxis[i][j] - hand->axis[i][j]);
		}
	}
	// Keep an orthonormal basis during the blend; do not squash the model.
	VectorNormalize( hand->axis[0] );
	CrossProduct( hand->axis[0], hand->axis[1], hand->axis[2] );
	VectorNormalize( hand->axis[2] );
	CrossProduct( hand->axis[2], hand->axis[0], hand->axis[1] );
}

/* The renderer's tag syscall samples frames, not entity.pose. Use the same
 * blended root transform as the gun on GL2; GL1/QVM use the named frame tag.
 * The configured joint must be parentless, so its local pose is model space.
 */
static qboolean CG_Sights_Root( const weaponSights_t *def,
                               const refEntity_t *hand, orientation_t *root ) {
	if ( hand->pose && def->rootJoint < hand->pose->numJoints ) {
		const modelJointTransform_t *joint = &hand->pose->joints[def->rootJoint];
		float x = joint->rotate[0], y = joint->rotate[1];
		float z = joint->rotate[2], w = joint->rotate[3];
		int i;
		VectorCopy( joint->translate, root->origin );
		VectorSet( root->axis[0], 1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w) );
		VectorSet( root->axis[1], 2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w) );
		VectorSet( root->axis[2], 2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y) );
		for ( i = 0; i < 3; i++ ) VectorScale( root->axis[i], joint->scale[i], root->axis[i] );
		return qtrue;
	}
	return trap_R_LerpTag( root, hand->hModel, hand->oldframe, hand->frame,
	                      1.0f - hand->backlerp, def->mountTag );
}

void CG_Sights_AddOptic( int weapon, const refEntity_t *hand ) {
	const weaponSights_t *def = CG_Sights_Def( weapon );
	orientation_t root;
	refEntity_t optic, dot;
	vec3_t rootAxis[3], mountAxis[3], plane, relative, hit;
	float denominator, distance, horizontal, vertical;
	int i;

	if ( !def || CG_Sights_Pose( def ) != &def->redDot ||
	     !CG_Sights_Root( def, hand, &root ) ) return;

	memset( &optic, 0, sizeof(optic) );
	optic.hModel = def->opticModel;
	optic.renderfx = hand->renderfx;
	VectorCopy( cg.refdef.vieworg, optic.lightingOrigin );
	optic.renderfx |= RF_LIGHTING_ORIGIN;
	MatrixMultiply( root.axis, ((refEntity_t *)hand)->axis, rootAxis );
	VectorCopy( hand->origin, optic.origin );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( optic.origin, root.origin[i], hand->axis[i], optic.origin );
		VectorMA( optic.origin, def->mountOrigin[i], rootAxis[i], optic.origin );
	}
	AnglesToAxis( def->mountAngles, mountAxis );
	MatrixMultiply( mountAxis, rootAxis, optic.axis );
	trap_R_AddRefEntityToScene( &optic );

	// Collimated aiming point: intersect the camera's aim ray with the lens.
	// It is visible only inside the real opening, and the housing depth-tests
	// it. A reload/recoil that moves the optic out of view also loses the dot.
	if ( cg.adsFrac < 0.85f ) return;
	VectorCopy( optic.origin, plane );
	for ( i = 0; i < 3; i++ ) VectorMA( plane, def->lensOrigin[i], optic.axis[i], plane );
	denominator = DotProduct( cg.refdef.viewaxis[0], optic.axis[0] );
	if ( denominator <= 0.1f ) return;
	VectorSubtract( plane, cg.refdef.vieworg, relative );
	distance = DotProduct( relative, optic.axis[0] ) / denominator;
	if ( distance <= 0.0f ) return;
	VectorMA( cg.refdef.vieworg, distance, cg.refdef.viewaxis[0], hit );
	VectorSubtract( hit, plane, relative );
	horizontal = DotProduct( relative, optic.axis[1] );
	vertical = DotProduct( relative, optic.axis[2] );
	if ( horizontal*horizontal + vertical*vertical > def->aperture*def->aperture ) return;

	memset( &dot, 0, sizeof(dot) );
	dot.reType = RT_SPRITE;
	dot.renderfx = RF_FIRST_PERSON | RF_DEPTHHACK;
	dot.customShader = def->reticleShader;
	VectorCopy( hit, dot.origin );
	dot.radius = distance * tan( cg.refdef.fov_x * M_PI / 360.0f ) * 0.002f;
	dot.shaderRGBA[0] = 255;
	dot.shaderRGBA[1] = 32;
	dot.shaderRGBA[2] = 16;
	dot.shaderRGBA[3] = (byte)(255.0f * CG_Sights_Fraction());
	trap_R_AddRefEntityToScene( &dot );
}
