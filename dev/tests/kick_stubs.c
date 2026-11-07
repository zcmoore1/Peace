/* What CG_EntityEvent names for events the view kick test never raises
 * (impacts, trails, obituaries, item effects): reaching one fails the test.
 * Its own TU, with no prototypes: only the names matter to the linker. */
#include <stdio.h>
#include <stdlib.h>
#define UNUSED_CGAME(name) void name( void ) { fprintf( stderr, #name " is not part of this test\n" ); exit( 1 ); }
UNUSED_CGAME(CG_Beam) UNUSED_CGAME(CG_Bullet) UNUSED_CGAME(CG_CenterPrint) UNUSED_CGAME(CG_GibPlayer)
UNUSED_CGAME(CG_MissileHitPlayer) UNUSED_CGAME(CG_MissileHitWall) UNUSED_CGAME(CG_RailTrail)
UNUSED_CGAME(CG_ScorePlum) UNUSED_CGAME(CG_SetEntitySoundPosition) UNUSED_CGAME(CG_ShotgunFire)
UNUSED_CGAME(CG_SmokePuff) UNUSED_CGAME(CG_SpawnEffect) UNUSED_CGAME(CG_PointContents)
UNUSED_CGAME(CG_CustomSound) UNUSED_CGAME(CG_ConfigString)
#ifdef MISSIONPACK
UNUSED_CGAME(CG_InvulnerabilityImpact) UNUSED_CGAME(CG_InvulnerabilityJuiced) UNUSED_CGAME(CG_KamikazeEffect)
UNUSED_CGAME(CG_LightningBoltBeam) UNUSED_CGAME(CG_ObeliskExplode) UNUSED_CGAME(CG_ObeliskPain)
UNUSED_CGAME(CG_VoiceChatLocal)
#endif
