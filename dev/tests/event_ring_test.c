/* MW2's event rings end to end, through the real code. A player's commands
 * run the real Pmove (fake_reload_pmove.c's floor) and, after each, the real
 * BG_PlayerStateToEntityState, as ClientThink_real does (MW2 493873). Each
 * server frame's entity goes through the real network codec (msg.c), and a
 * client plays it through the real CG_SetNextSnap, CG_TransitionSnapshot,
 * CG_TransitionEntity and CG_ResetEntity (cg_snapshot.c), CG_CheckEvents and
 * CG_EntityEvent (cg_event.c), CG_FireWeapon and CG_PrepOffhand
 * (cg_weapons.c); its own player's and the killcam's playerstate through the
 * real CG_TransitionPlayerState and CG_CheckPlayerstateEvents
 * (cg_playerstate.c). Only the renderer, the sound system's playback and the
 * cgame's effects are stubbed: a sound is a name, and what plays is logged.
 *
 * Checked: the entity ring carries every event since the last snapshot, up to
 * four, oldest first, each with its own parm (MW2's CG_CheckEvents 4c33f0),
 * across the 11-bit wrap; an opponent's shot plays the gun it names though
 * the entity holds the throwing knife by then (MW2's es.weapon stays the gun);
 * the press's serverOnly and singleClient events never reach anyone else; the
 * pullback plays from the weapon's data for others and not in the player's
 * own view (MW2's data has no first-person one); the switch sets the HUD's
 * equipped offhand and the ADS reset leaves a toggled aim, for the player
 * alone and never in a killcam, and a new viewed player or life takes the
 * equipped offhand from the playerstate (MW2's snapshot transition); an entity seen again after EVENT_VALID_MSEC
 * plays what its ring still holds, one seen just before plays only what is
 * new, and a player's entity, reset, plays nothing its ring already held. And the killcam's ring arithmetic, which emerges: a shot and the
 * throwing knife's press in one recorded frame leave the archived playerstate
 * no shot to play (MW2's four events overwrite it), while a frame between them
 * keeps it.
 *
 * MW2's stance events (EV_STANCE_FORCE_STAND/CROUCH/PRONE, EV_STANCE_INVALID,
 * singleClient): the player's own resets the client's stance through
 * _peaceSetStance (MW2's CL_SetStance, owner's decision), never in a killcam,
 * a demo or a followed view; no entity carries them; EV_STANCE_INVALID arms
 * MW2's invalid-command hint, which a same-type event never restamps. */
#include "../../code/cgame/cg_snapshot.c"

void FakeReloadPmove( pmove_t *move );
void CG_CheckPlayerstateEvents( playerState_t *ps, playerState_t *ops );
void WireEntity( const entityState_t *from, const entityState_t *to, entityState_t *out );
void WirePlayerstate( const playerState_t *from, const playerState_t *to, playerState_t *out );

cg_t cg;
cgs_t cgs;
centity_t cg_entities[MAX_GENTITIES];
weaponInfo_t cg_weapons[MAX_WEAPONS];
itemInfo_t cg_items[MAX_ITEMS];
vmCvar_t cg_brassTime, cg_debugEvents, cg_drawGun, cg_footsteps, cg_noProjectileTrail, cg_nopredict,
	cg_oldPlasma, cg_oldRail, cg_oldRocket, cg_railTrailTime, cg_synchronousClients, cg_tracerChance,
	cg_tracerLength, cg_tracerWidth, cg_showmiss, cg_smoothClients, cg_viewsize, cg_thirdPerson;

static int checks, warnings;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( int code, const char *fmt, ... ) {
	va_list a; (void)code; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); abort();
}
void QDECL CG_Error( const char *fmt, ... ) { va_list a; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); abort(); }
/* MW2's "Event %s just for client %i was sent to other clients" is the only print expected */
void QDECL CG_Printf( const char *fmt, ... ) { if ( strstr( fmt, "just for client" ) ) warnings++; }
void trap_SnapVector( float *v );
/* the view kick's hand-off to the client (cg_kick.c): nothing here reads it */
void trap_SetUserCmdKickAngles( float pitch, float yaw, float roll ) { (void)pitch; (void)yaw; (void)roll; }

/* ---- sounds: a handle is a name; what is started is logged ---- */
static char soundNames[64][MAX_QPATH];
static int numSoundNames, played[64], playedEntity[64], numPlayed;
sfxHandle_t trap_S_RegisterSound( const char *name, qboolean compressed ) {
	int i; (void)compressed;
	for ( i = 1; i <= numSoundNames; i++ ) if ( !strcmp( soundNames[i], name ) ) return i;
	CHECK( numSoundNames + 1 < 64 ); Q_strncpyz( soundNames[++numSoundNames], name, MAX_QPATH );
	return numSoundNames;
}
void trap_S_StartSound( vec3_t origin, int entityNum, int channel, sfxHandle_t sfx ) {
	(void)origin; (void)channel; CHECK( numPlayed < 64 );
	playedEntity[numPlayed] = entityNum; played[numPlayed++] = sfx;
}
static const char *Played( int i ) { return i < numPlayed ? soundNames[played[i]] : ""; }
/* the files there: only the frag's MW2 pullback */
static const char *presentFile = "sound/weapons/offhand/weap_fraggrenade_pin.wav";
int trap_FS_FOpenFile( const char *path, fileHandle_t *f, fsMode_t mode ) {
	(void)mode; *f = !strcmp( path, presentFile ); return *f ? 1000 : -1;
}
void trap_FS_FCloseFile( fileHandle_t f ) { CHECK( f == 1 ); }
static char lastCommand[64];
void trap_SendConsoleCommand( const char *text ) { Q_strncpyz( lastCommand, text, sizeof( lastCommand ) ); }

/* ---- the renderer and what CG_RegisterWeapon names ---- */
qhandle_t trap_R_RegisterModel( const char *name ) { (void)name; return 1; }
qhandle_t trap_R_RegisterShader( const char *name ) { (void)name; return 1; }
void trap_R_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) { (void)model; VectorClear( mins ); VectorClear( maxs ); }
void CG_WeapAnim_RegisterClips( int weapon, qhandle_t model, const char *path ) { (void)weapon; (void)model; (void)path; }
void CG_Sights_Register( int weapon, const char *path ) { (void)weapon; (void)path; }

/* ---- the rest of the cgame: nothing here may reach it ---- */
qboolean CG_Killcam_Active( void ) { return cg.snap && ( cg.snap->snapFlags & SNAPFLAG_REPLAY ); }
void CG_Killcam_Transition( const snapshot_t *previous, const snapshot_t *next ) { (void)previous; (void)next; }
void CG_ExecuteNewServerCommands( int latestSequence ) { (void)latestSequence; }
void CG_BuildSolidList( void ) { }
void CG_ResetPlayerEntity( centity_t *cent ) { (void)cent; }
void CG_SetEntitySoundPosition( centity_t *cent ) { (void)cent; }
void CG_AddBufferedSound( sfxHandle_t sfx ) { (void)sfx; abort(); }
localEntity_t *CG_AllocLocalEntity( void ) { abort(); return NULL; }
void CG_Beam( centity_t *cent ) { (void)cent; abort(); }
void CG_Bleed( vec3_t origin, int entityNum ) { (void)origin; (void)entityNum; abort(); }
void CG_BubbleTrail( vec3_t start, vec3_t end, float spacing ) { (void)start; (void)end; (void)spacing; abort(); }
void CG_CenterPrint( const char *str, int y, int charWidth ) { (void)str; (void)y; (void)charWidth; abort(); }
const char *CG_ConfigString( int index ) { (void)index; abort(); return ""; }
sfxHandle_t CG_CustomSound( int clientNum, const char *soundName ) { (void)clientNum; (void)soundName; abort(); return 0; }
void CG_GibPlayer( vec3_t playerOrigin ) { (void)playerOrigin; abort(); }
void CG_ImpactMark( qhandle_t m, const vec3_t o, const vec3_t d, float r, float re, float g, float b, float a,
	qboolean alphaFade, float radius, qboolean temporary ) {
	(void)m; (void)o; (void)d; (void)r; (void)re; (void)g; (void)b; (void)a; (void)alphaFade; (void)radius; (void)temporary; abort();
}
localEntity_t *CG_MakeExplosion( vec3_t origin, vec3_t dir, qhandle_t hModel, qhandle_t shader, int msec, qboolean isSprite ) {
	(void)origin; (void)dir; (void)hModel; (void)shader; (void)msec; (void)isSprite; abort(); return NULL;
}
void CG_ParticleExplosion( char *animStr, vec3_t origin, vec3_t vel, int duration, int sizeStart, int sizeEnd ) {
	(void)animStr; (void)origin; (void)vel; (void)duration; (void)sizeStart; (void)sizeEnd; abort();
}
/* A hitscan fire event draws its shot (client-side bullets) into an empty world. */
int CG_PointContents( const vec3_t point, int passEntityNum ) { (void)point; (void)passEntityNum; return 0; }
void CG_ScorePlum( int client, vec3_t org, int score ) { (void)client; (void)org; (void)score; abort(); }
localEntity_t *CG_SmokePuff( const vec3_t p, const vec3_t vel, float radius, float r, float g, float b, float a,
	float duration, int startTime, int fadeInTime, int leFlags, qhandle_t hShader ) {
	(void)p; (void)vel; (void)radius; (void)r; (void)g; (void)b; (void)a; (void)duration; (void)startTime;
	(void)fadeInTime; (void)leFlags; (void)hShader;
	{ static localEntity_t puff; return &puff; }
}
void CG_SpawnEffect( vec3_t org ) { (void)org; abort(); }
void CG_Trace( trace_t *result, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
	int skipNumber, int mask ) {
	(void)mins; (void)maxs; (void)skipNumber; (void)mask;
	memset( result, 0, sizeof( *result ) ); result->fraction = 1; result->entityNum = ENTITYNUM_NONE;
	VectorCopy( end, result->endpos ); (void)start;
}
/* cg_predict.c's other players' hit volumes: no one is in this empty world */
void CG_ShotHitVolumes( trace_t *tr, const vec3_t start, const vec3_t end, int skipNumber ) {
	(void)tr; (void)start; (void)end; (void)skipNumber;
}
void trap_CM_BoxTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs,
	clipHandle_t model, int brushmask ) {
	(void)results; (void)start; (void)end; (void)mins; (void)maxs; (void)model; (void)brushmask; abort();
}
clipHandle_t trap_CM_TempBoxModel( const vec3_t mins, const vec3_t maxs ) { (void)mins; (void)maxs; return 0; }
void trap_CM_TransformedBoxTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins,
	const vec3_t maxs, clipHandle_t model, int brushmask, const vec3_t origin, const vec3_t angles ) {
	(void)start; (void)mins; (void)maxs; (void)model; (void)brushmask; (void)origin; (void)angles;
	memset( results, 0, sizeof( *results ) ); results->fraction = 1; results->entityNum = ENTITYNUM_NONE;
	VectorCopy( end, results->endpos );
}
void trap_R_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts ) { (void)hShader; (void)numVerts; (void)verts; }
void trap_R_AddRefEntityToScene( const refEntity_t *re ) { (void)re; abort(); }
void trap_S_StopLoopingSound( int entityNum ) { (void)entityNum; abort(); }
void trap_S_StartLocalSound( sfxHandle_t sfx, int channelNum ) { (void)sfx; (void)channelNum; abort(); }
#ifdef MISSIONPACK
vmCvar_t cg_cameraOrbit, cg_singlePlayerActive;
void CG_KamikazeEffect( vec3_t org ) { (void)org; abort(); }
void CG_ObeliskExplode( vec3_t org, int entityNum ) { (void)org; (void)entityNum; abort(); }
void CG_ObeliskPain( vec3_t org ) { (void)org; abort(); }
void CG_InvulnerabilityImpact( vec3_t org, vec3_t angles ) { (void)org; (void)angles; abort(); }
void CG_InvulnerabilityJuiced( vec3_t org ) { (void)org; abort(); }
void CG_LightningBoltBeam( vec3_t start, vec3_t end ) { (void)start; (void)end; abort(); }
void CG_VoiceChatLocal( int mode, qboolean voiceOnly, int clientNum, int color, const char *cmd ) {
	(void)mode; (void)voiceOnly; (void)clientNum; (void)color; (void)cmd; abort();
}
#endif

/* ---- the server side: a player, its commands, its entity ---- */
#define VIEWER 0
#define SHOOTER 1
#define FRAME_MSEC 50	/* sv_fps 20 */
#define CMD_MSEC 8
static playerState_t shooter;
static entityState_t shooterEntity, shooterSent;
static usercmd_t shooterCmd;
static snapshot_t snaps[2];
static int current, serverTime;
static playerState_t viewer;

static void Shooter( int lethal ) {
	bg_class_t cl = *BG_Class( 0 );
	memset( &shooter, 0, sizeof( shooter ) ); memset( &shooterEntity, 0, sizeof( shooterEntity ) );
	memset( &shooterCmd, 0, sizeof( shooterCmd ) ); memset( &shooterSent, 0, sizeof( shooterSent ) );
	cl.slot[0] = WP_SHOTGUN; cl.slot[1] = WP_MACHINEGUN; cl.lethal = lethal; cl.lethalCount = BG_ClassEquipmentCount( lethal );
	CHECK( BG_ValidateClass( &cl ) );
	BG_ApplyClass( &shooter, &cl );
	shooter.clientNum = SHOOTER; shooter.stats[STAT_HEALTH] = 100; shooter.pm_type = PM_NORMAL;
	shooter.weapon = WP_SHOTGUN; shooter.weaponstate = WEAPON_READY;
	shooter.viewheight = DEFAULT_VIEWHEIGHT; shooter.origin[2] = 24.125f; shooter.groundEntityNum = ENTITYNUM_WORLD;
	shooter.speed = BG_PLAYER_SPEED; shooter.gravity = 800; shooter.commandTime = serverTime;
	shooterCmd.serverTime = serverTime; shooterCmd.weapon = WP_SHOTGUN;
	BG_PlayerStateToEntityStateExtraPolate( &shooter, &shooterEntity, shooter.commandTime, qtrue );
}
/* one command, as the server's ClientThink_real runs it: Pmove, then the entity */
static void Command( int buttons ) {
	pmove_t pm;
	shooterCmd.serverTime += CMD_MSEC; shooterCmd.buttons = buttons;
	memset( &pm, 0, sizeof( pm ) ); pm.ps = &shooter; pm.cmd = shooterCmd;
	FakeReloadPmove( &pm );
	BG_PlayerStateToEntityStateExtraPolate( &shooter, &shooterEntity, shooter.commandTime, qtrue );
}
static void Commands( int buttons, int msec ) { int t; for ( t = 0; t < msec; t += CMD_MSEC ) Command( buttons ); }

/* ---- the client: snapshots in, through the real transitions ---- */
static void Fresh( void ) {
	memset( cg_entities, 0, sizeof( cg_entities ) ); memset( cg_weapons, 0, sizeof( cg_weapons ) );
	memset( &cg, 0, sizeof( cg ) ); memset( &cgs, 0, sizeof( cgs ) ); memset( snaps, 0, sizeof( snaps ) );
	memset( &viewer, 0, sizeof( viewer ) );
	numPlayed = 0; lastCommand[0] = 0; warnings = 0; current = 0;
	serverTime = 10000;
	viewer.clientNum = VIEWER; viewer.stats[STAT_HEALTH] = 100;
	cg.clientNum = VIEWER;
	snaps[0].serverTime = serverTime; snaps[0].ps = viewer;
	cg.snap = &snaps[0]; cg.time = serverTime;
}
/* the server's next frame: the entities in it, through the codec, and the
 * client's transition to it */
static void Snapshot( const entityState_t *e, int count, int snapFlags, const playerState_t *ps ) {
	snapshot_t *next = &snaps[current ^ 1];
	int i;
	memset( next, 0, sizeof( *next ) );
	serverTime += FRAME_MSEC;
	next->serverTime = serverTime; next->snapFlags = snapFlags; next->ps = ps ? *ps : viewer;
	for ( i = 0; i < count; i++ ) next->entities[i] = e[i];
	next->numEntities = count;
	CG_SetNextSnap( next );
	cg.time = serverTime;
	CG_TransitionSnapshot();
	current ^= 1;
}
/* the shooter's entity as this frame's snapshot carries it */
static void ShooterSnapshot( void ) {
	entityState_t sent;
	WireEntity( &shooterSent, &shooterEntity, &sent );
	shooterSent = shooterEntity;
	CHECK( !sent.event );	/* the server never sends one */
	Snapshot( &sent, 1, 0, NULL );
}
static int PlayedName( const char *name ) {
	int i, n = 0;
	for ( i = 0; i < numPlayed; i++ ) if ( !strcmp( Played( i ), name ) ) n++;
	return n;
}

static const char *SpasFire = "sound/weapons/shotgun/sshotf1b.wav";

/* An opponent shoots, then pulls the throwing knife on the very next
 * command, both in one server frame. The viewer hears the SPAS: the shot
 * rides the ring with its gun, though the entity holds the knife by then.
 * Nothing of the press but the pullback reaches it, and the knife has none. */
static void TestShotThenKnife( void ) {
	int i;
	Fresh(); Shooter( WP_THROWING_KNIFE ); ShooterSnapshot();
	CHECK( !numPlayed && cg_entities[SHOOTER].currentValid );
	Commands( 0, 100 ); ShooterSnapshot(); CHECK( !numPlayed );
	Command( BUTTON_ATTACK ); Command( BUTTON_LETHAL );
	CHECK( shooter.weapon == WP_THROWING_KNIFE && shooterEntity.weapon == WP_THROWING_KNIFE );
	ShooterSnapshot();
	CHECK( numPlayed == 1 && !strcmp( Played( 0 ), SpasFire ) && playedEntity[0] == SHOOTER );
	CHECK( cg_entities[SHOOTER].currentState.weapon == WP_THROWING_KNIFE );
	CHECK( cg_entities[SHOOTER].previousEventSequence == cg_entities[SHOOTER].currentState.eventSequence );
	CHECK( !warnings && !lastCommand[0] && !cg.equippedOffhand );
	/* a frame with nothing new plays nothing again */
	ShooterSnapshot(); CHECK( numPlayed == 1 );
	/* the entity's ring holds the shot and the pullback only */
	for ( i = 0; i < MAX_ENTITY_EVENTS; i++ ) {
		int ev = cg_entities[SHOOTER].currentState.events[i];
		CHECK( ev != EV_SWITCH_OFFHAND && ev != EV_WEAPON_SWITCH_STARTED && ev != EV_RESET_ADS );
	}
}

/* A frag: the press raises nothing for anyone else; the pullback, when the
 * quick drop is over, plays the frag's world pullback from its data. */
static void TestFragPullback( void ) {
	int t;
	Fresh(); Shooter( WP_FRAG ); ShooterSnapshot(); Commands( 0, 100 ); ShooterSnapshot();
	Command( BUTTON_LETHAL ); ShooterSnapshot(); CHECK( !numPlayed );
	for ( t = 0; t < 2000 && shooter.weapon != WP_FRAG; t += CMD_MSEC ) Command( BUTTON_LETHAL );
	CHECK( shooter.weapon == WP_FRAG );
	ShooterSnapshot();
	CHECK( numPlayed == 1 && !strcmp( Played( 0 ), presentFile ) && playedEntity[0] == SHOOTER );
	CHECK( cg_weapons[WP_FRAG].pullbackSound && !cg_weapons[WP_FRAG].pullbackSoundPlayer );
	CHECK( !warnings );
}

/* The ring on its own: up to four a snapshot, oldest first, each its own
 * parm; a fifth is the oldest lost; the 11-bit wrap; and an entity seen
 * again. Every event is put on the entity by the real BG_AddEntityEvent. */
static void TestCapacity( void ) {
	entityState_t mover;
	char name[MAX_QPATH];
	int i, k;
	Fresh();
	for ( i = 1; i <= 9; i++ ) {
		Com_sprintf( name, sizeof( name ), "sound/test/%i.wav", i );
		cgs.gameSounds[i] = trap_S_RegisterSound( name, qfalse );
	}
	memset( &mover, 0, sizeof( mover ) ); mover.number = MAX_CLIENTS + 5; mover.eType = ET_GENERAL;
	Snapshot( &mover, 1, 0, NULL );
	for ( k = 0; k < 2; k++ ) {
		/* four since the last snapshot; then five, across the 11-bit wrap,
		 * on an entity first seen at sequence 2046 */
		int count = 4 + k, first;
		if ( k ) {
			memset( &mover, 0, sizeof( mover ) ); mover.number = MAX_CLIENTS + 6; mover.eType = ET_GENERAL;
			mover.eventSequence = ES_EVENT_SEQUENCE_MASK - 1;
			Snapshot( &mover, 1, 0, NULL );
		}
		first = numPlayed;
		for ( i = 1; i <= count; i++ ) BG_AddEntityEvent( &mover, EV_GENERAL_SOUND, i );
		Snapshot( &mover, 1, 0, NULL );
		CHECK( numPlayed - first == 4 );
		for ( i = 0; i < 4; i++ ) {
			Com_sprintf( name, sizeof( name ), "sound/test/%i.wav", count - 3 + i );
			CHECK( !strcmp( Played( first + i ), name ) && playedEntity[first + i] == mover.number );
		}
	}
	CHECK( mover.eventSequence == 3 );	/* 2046, 2047, 0, 1, 2 and on */
	/* out of sight longer than EVENT_VALID_MSEC: what the ring still holds
	 * plays when it is seen again (the server empties older slots,
	 * G_AgeEntityEvents); an emptied slot plays nothing */
	numPlayed = 0;
	Snapshot( NULL, 0, 0, NULL ); serverTime += EVENT_VALID_MSEC;
	mover.events[0] = mover.eventParms[0] = 0;	/* the old one the server emptied */
	BG_AddEntityEvent( &mover, EV_GENERAL_SOUND, 9 );
	Snapshot( &mover, 1, 0, NULL );
	CHECK( numPlayed == 3 && !strcmp( Played( 0 ), "sound/test/4.wav" ) && !strcmp( Played( 2 ), "sound/test/9.wav" ) );
	/* seen a frame ago, then teleported: only what is new plays */
	numPlayed = 0;
	BG_AddEntityEvent( &mover, EV_GENERAL_SOUND, 7 ); mover.eFlags ^= EF_TELEPORT_BIT;
	Snapshot( &mover, 1, 0, NULL );
	CHECK( numPlayed == 1 && !strcmp( Played( 0 ), "sound/test/7.wav" ) );
	/* a ring that was reset (a new entity in the slot, a body) plays nothing
	 * until its next event */
	numPlayed = 0; mover.eventSequence = 0; memset( mover.events, 0, sizeof( mover.events ) );
	Snapshot( &mover, 1, 0, NULL ); CHECK( !numPlayed && !cg_entities[mover.number].previousEventSequence );
	BG_AddEntityEvent( &mover, EV_GENERAL_SOUND, 2 ); Snapshot( &mover, 1, 0, NULL );
	CHECK( numPlayed == 1 && !strcmp( Played( 0 ), "sound/test/2.wav" ) );
}

/* A player's entity is MW2's exception (CG_ResetEntity's ET_PLAYER case,
 * 5951f4): reset, its ring counts as played. Out of view for a frame, or for
 * longer than a slot lives, it comes back with a shot it fired meanwhile and
 * plays none of it (a follower saw that shot through the playerstate); the
 * next shot plays. */
static void TestPlayerSeenAgain( void ) {
	int away;
	for ( away = 0; away < 2; away++ ) {
		Fresh(); Shooter( WP_THROWING_KNIFE ); ShooterSnapshot(); Commands( 0, 100 ); ShooterSnapshot();
		CHECK( !numPlayed );
		Snapshot( NULL, 0, 0, NULL );
		if ( away ) serverTime += EVENT_VALID_MSEC;
		Command( BUTTON_ATTACK );
		ShooterSnapshot();
		CHECK( !numPlayed && cg_entities[SHOOTER].previousEventSequence == cg_entities[SHOOTER].currentState.eventSequence );
		Commands( 0, 1500 ); ShooterSnapshot();
		numPlayed = 0; Command( BUTTON_ATTACK ); ShooterSnapshot();
		CHECK( numPlayed == 1 && !strcmp( Played( 0 ), SpasFire ) && playedEntity[0] == SHOOTER );
	}
}

/* The player's own view, and the killcam's: the playerstate's ring through
 * the real CG_CheckPlayerstateEvents. */
/* the predicted entity is the playerstate's, as CG_AddPacketEntities makes it each frame */
static void PlayerEvents( playerState_t *ps, playerState_t *ops ) {
	BG_PlayerStateToEntityState( ops, &cg.predictedPlayerEntity.currentState, qfalse );
	CG_CheckPlayerstateEvents( ps, ops );
}
static void TestOwnView( void ) {
	playerState_t before, shot, knife, wired;
	Fresh(); Shooter( WP_THROWING_KNIFE ); Commands( 0, 100 );
	/* the shooter is this client */
	cg.clientNum = SHOOTER; snaps[0].ps = shooter; cg.snap = &snaps[0];
	before = shooter; Command( BUTTON_ATTACK ); shot = shooter; Command( BUTTON_LETHAL ); knife = shooter;
	/* the four press events and the shot travel in the playerstate */
	WirePlayerstate( &before, &knife, &wired );
	CHECK( !memcmp( wired.events, knife.events, sizeof( wired.events ) ) && !memcmp( wired.eventParms, knife.eventParms, sizeof( wired.eventParms ) ) );
	CHECK( wired.eventSequence == knife.eventSequence );
	/* live, the client checks its state every frame: the shot, then the press */
	snaps[0].ps = knife;
	PlayerEvents( &shot, &before );
	CHECK( numPlayed == 1 && !strcmp( Played( 0 ), SpasFire ) && playedEntity[0] == SHOOTER );
	PlayerEvents( &knife, &shot );
	CHECK( numPlayed == 1 );	/* the knife has no pullback, in either view */
	CHECK( cg.equippedOffhand == WP_THROWING_KNIFE && !strcmp( lastCommand, "leaveads\n" ) && !warnings );
	/* a frag's pullback has no first-person sound (MW2's pullbackSoundPlayer is empty) */
	Fresh(); Shooter( WP_FRAG ); Commands( 0, 100 ); cg.clientNum = SHOOTER;
	before = shooter; Command( BUTTON_LETHAL );
	while ( shooter.weapon != WP_FRAG ) Command( BUTTON_LETHAL );
	snaps[0].ps = shooter; cg.snap = &snaps[0];
	PlayerEvents( &shooter, &before );
	CHECK( !numPlayed && cg.equippedOffhand == WP_FRAG && !strcmp( lastCommand, "leaveads\n" ) );
	/* someone else's playerstate (following them): the HUD follows, the
	 * viewer's own aim is not theirs to leave */
	Fresh(); Shooter( WP_THROWING_KNIFE ); Commands( 0, 100 ); cg.clientNum = VIEWER;
	before = shooter; Command( BUTTON_LETHAL ); snaps[0].ps = shooter; cg.snap = &snaps[0];
	PlayerEvents( &shooter, &before );
	CHECK( cg.equippedOffhand == WP_THROWING_KNIFE && !lastCommand[0] && !warnings );
}
/* The killcam replays one archived playerstate a 50ms frame. With the shot
 * and the knife's press in one frame, the shot's slot is the fourth event
 * back plus one: the loop never visits it, so nothing of the shot plays. A
 * frame between them keeps it. Nothing here knows either. */
static void TestKillcam( void ) {
	playerState_t before, shot, knife;
	int split;
	for ( split = 0; split < 2; split++ ) {
		Fresh(); Shooter( WP_THROWING_KNIFE ); Commands( 0, 100 );
		before = shooter; Command( BUTTON_ATTACK ); shot = shooter; Command( BUTTON_LETHAL ); knife = shooter;
		snaps[0].ps = before; snaps[0].snapFlags = SNAPFLAG_REPLAY; cg.snap = &snaps[0];
		if ( split ) Snapshot( NULL, 0, SNAPFLAG_REPLAY, &shot );
		Snapshot( NULL, 0, SNAPFLAG_REPLAY, &knife );
		CHECK( PlayedName( SpasFire ) == split );
		CHECK( cg.equippedOffhand == WP_THROWING_KNIFE && !lastCommand[0] );	/* never the viewer's aim */
	}
}

/* MW2's snapshot transition takes the HUD's equipped offhand from the
 * playerstate whenever the viewed player or their life changes, never in a
 * demo (4507cd-450811, 0x472ff0), and drops the view kick with it. The
 * killer's press in a killcam, or a followed player's, plays on the HUD
 * there, and does not stay on the viewer's own HUD afterward. */
static void TestEquippedOffhandReset( void ) {
	playerState_t before, knife;
	Fresh(); Shooter( WP_THROWING_KNIFE ); Commands( 0, 100 );
	before = shooter; Command( BUTTON_LETHAL ); knife = shooter;
	CHECK( knife.weapon == WP_THROWING_KNIFE );
	/* the killcam: the cut to the killer's view, then the killer's press */
	cg.equippedOffhand = WP_FLASH;	/* the viewer's own last press */
	VectorSet( cg.kickAngles, -3, 1, -0.5f ); VectorSet( cg.kickAVel, -40, 10, -5 );
	Snapshot( NULL, 0, SNAPFLAG_REPLAY, &before );
	CHECK( !cg.equippedOffhand && VectorCompare( cg.kickAngles, vec3_origin ) && VectorCompare( cg.kickAVel, vec3_origin ) );
	Snapshot( NULL, 0, SNAPFLAG_REPLAY, &knife );
	CHECK( cg.equippedOffhand == WP_THROWING_KNIFE );
	/* back to the viewer's own view: theirs again, no offhand out */
	Snapshot( NULL, 0, 0, NULL );
	CHECK( !cg.equippedOffhand );
	/* following someone with the knife already out: from their playerstate */
	knife.pm_flags |= PMF_FOLLOW;
	Snapshot( NULL, 0, 0, &knife );
	CHECK( cg.equippedOffhand == WP_THROWING_KNIFE );
	Snapshot( NULL, 0, 0, NULL );
	/* the viewer's own new life */
	cg.equippedOffhand = WP_FLASH; Snapshot( NULL, 0, 0, NULL ); CHECK( cg.equippedOffhand == WP_FLASH );
	viewer.persistant[PERS_SPAWN_COUNT]++;
	Snapshot( NULL, 0, 0, NULL );
	CHECK( !cg.equippedOffhand );
	/* a demo keeps it, as MW2's does */
	cg.demoPlayback = qtrue; cg.equippedOffhand = WP_FLASH;
	Snapshot( NULL, 0, 0, &knife );
	CHECK( cg.equippedOffhand == WP_FLASH );
	cg.demoPlayback = qfalse;
	CHECK( !warnings );
}

/* The stance events, raised on the player's playerstate as pmove raises them
 * (BG_AddPredictableEventToPlayerstate): what the player's own cgame does,
 * what a demo, the killcam or a followed view does (nothing), and that the
 * entity never carries one. */
static void StanceEvent( int event, int parm ) {
	playerState_t before = shooter;
	BG_AddPredictableEventToPlayerstate( event, parm, &shooter );
	BG_PlayerStateToEntityStateExtraPolate( &shooter, &shooterEntity, shooter.commandTime, qtrue );
	snaps[0].ps = shooter; cg.snap = &snaps[0];
	lastCommand[0] = 0;
	PlayerEvents( &shooter, &before );
}
static void TestStanceEvents( void ) {
	static const struct { int event, stance; } force[] = {
		{ EV_STANCE_FORCE_STAND, BG_STANCE_STAND }, { EV_STANCE_FORCE_CROUCH, BG_STANCE_CROUCH },
		{ EV_STANCE_FORCE_PRONE, BG_STANCE_PRONE } };
	char expect[64];
	int i, parm, view, stamp;
	for ( i = 0; i < ARRAY_LEN( force ); i++ ) for ( parm = BG_STANCE_PARM_FORCED; parm <= BG_STANCE_PARM_PRONE_BLOCKED; parm++ ) {
		/* the player's own: the stance reset, and no hint (MW2's FORCE_PRONE
		 * hint needs a weapon that blocks prone, which Peace has none of) */
		Fresh(); Shooter( WP_FRAG ); Commands( 0, 100 ); cg.clientNum = SHOOTER;
		CHECK( !BG_WeaponBlocksProne( shooter.weapon ) );
		StanceEvent( force[i].event, parm );
		Com_sprintf( expect, sizeof( expect ), "_peaceSetStance %i\n", force[i].stance );
		CHECK( !strcmp( lastCommand, expect ) && !cg.invalidCmdHintType && !warnings && !numPlayed );
		/* never anyone else's input: a followed player, the killcam, a demo */
		for ( view = 0; view < 3; view++ ) {
			Fresh(); Shooter( WP_FRAG ); Commands( 0, 100 );
			cg.clientNum = view == 0 ? VIEWER : SHOOTER;
			if ( view == 1 ) snaps[0].snapFlags = SNAPFLAG_REPLAY;
			if ( view == 2 ) cg.demoPlayback = qtrue;
			StanceEvent( force[i].event, parm );
			CHECK( !lastCommand[0] && !cg.invalidCmdHintType && !warnings );
			cg.demoPlayback = qfalse;
		}
	}
	/* singleClient: the entity's ring never carries one, so no one else plays it */
	Fresh(); Shooter( WP_FRAG ); ShooterSnapshot(); Commands( 0, 100 ); ShooterSnapshot();
	for ( i = 0; i < ARRAY_LEN( force ); i++ ) {
		BG_AddPredictableEventToPlayerstate( force[i].event, BG_STANCE_PARM_STAND_BLOCKED, &shooter );
		Command( 0 );
	}
	BG_AddPredictableEventToPlayerstate( EV_STANCE_INVALID, BG_STANCE_PARM_PRONE_BLOCKED, &shooter );
	Command( 0 );
	ShooterSnapshot();
	for ( i = 0; i < MAX_ENTITY_EVENTS; i++ ) {
		int ev = cg_entities[SHOOTER].currentState.events[i];
		CHECK( ev < EV_STANCE_FORCE_STAND || ev > EV_STANCE_INVALID );
	}
	CHECK( !lastCommand[0] && !warnings && !cg.invalidCmdHintType );
	/* EV_STANCE_INVALID arms MW2's "Prone Blocked" (CG_SetInvalidCmdHint
	 * 0x587230): stamped once; the same type again leaves the stamp; the weapon
	 * is stored every time; another type restamps */
	Fresh(); Shooter( WP_FRAG ); Commands( 0, 100 ); cg.clientNum = SHOOTER;
	StanceEvent( EV_STANCE_INVALID, BG_STANCE_PARM_PRONE_BLOCKED );
	CHECK( cg.invalidCmdHintType == CG_INVALID_HINT_PRONE_BLOCKED && cg.invalidCmdHintTime == cg.time );
	CHECK( cg.invalidCmdHintWeapon == shooter.weapon && !lastCommand[0] && !warnings );
	stamp = cg.time;
	cg.time += CG_INVALID_HINT_MS / 2;
	shooter.weapon = WP_SHOTGUN;
	StanceEvent( EV_STANCE_INVALID, BG_STANCE_PARM_PRONE_BLOCKED );
	CHECK( cg.invalidCmdHintTime == stamp && cg.invalidCmdHintWeapon == WP_SHOTGUN );
	cg.time += 8;
	CG_SetInvalidCmdHint( WP_MACHINEGUN, BG_STANCE_PARM_STAND_BLOCKED );
	CHECK( cg.invalidCmdHintType == CG_INVALID_HINT_STAND_BLOCKED && cg.invalidCmdHintTime == cg.time );
	CG_SetInvalidCmdHint( WP_SHOTGUN, BG_STANCE_PARM_CROUCH_BLOCKED );
	CHECK( cg.invalidCmdHintType == CG_INVALID_HINT_CROUCH_BLOCKED && cg.invalidCmdHintWeapon == WP_SHOTGUN );
	/* a parm with no hint stores the weapon and arms nothing */
	stamp = cg.invalidCmdHintTime; cg.time += 8;
	CG_SetInvalidCmdHint( WP_M40A3, BG_STANCE_PARM_FORCED );
	CHECK( cg.invalidCmdHintType == CG_INVALID_HINT_CROUCH_BLOCKED && cg.invalidCmdHintTime == stamp &&
		cg.invalidCmdHintWeapon == WP_M40A3 );
	/* followed, or in the killcam: no hint */
	Fresh(); Shooter( WP_FRAG ); Commands( 0, 100 ); cg.clientNum = VIEWER;
	StanceEvent( EV_STANCE_INVALID, BG_STANCE_PARM_PRONE_BLOCKED );
	CHECK( !cg.invalidCmdHintType && !warnings );
}

int main( void ) {
	TestShotThenKnife();
	TestFragPullback();
	TestCapacity();
	TestPlayerSeenAgain();
	TestOwnView();
	TestKillcam();
	TestEquippedOffhandReset();
	TestStanceEvents();
	printf( "PASS: %d event ring checks\n", checks );
	return 0;
}
