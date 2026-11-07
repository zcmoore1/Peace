/* What CL_CgameSystemCalls names for the traps the view kick's QVM boundary
 * test never makes (renderer, sound, collision, files, keys, commands):
 * reaching one fails the test. Its own TU, with no prototypes: only the
 * names matter to the linker. */
#include <stdio.h>
#include <stdlib.h>
#define UNUSED_ENGINE(name) void name( void ) { fprintf( stderr, #name " is not part of this test\n" ); exit( 1 ); }
UNUSED_ENGINE(CIN_DrawCinematic) UNUSED_ENGINE(CIN_PlayCinematic) UNUSED_ENGINE(CIN_RunCinematic)
UNUSED_ENGINE(CIN_SetExtents) UNUSED_ENGINE(CIN_StopCinematic)
UNUSED_ENGINE(CL_AddRefEntityFromVM) UNUSED_ENGINE(CL_AddReliableCommand) UNUSED_ENGINE(CL_SystemInfoChanged)
UNUSED_ENGINE(CM_BoxTrace) UNUSED_ENGINE(CM_InlineModel) UNUSED_ENGINE(CM_LoadMap) UNUSED_ENGINE(CM_NumInlineModels)
UNUSED_ENGINE(CM_PointContents) UNUSED_ENGINE(CM_TempBoxModel) UNUSED_ENGINE(CM_TransformedBoxTrace)
UNUSED_ENGINE(CM_TransformedPointContents)
UNUSED_ENGINE(Cbuf_AddText) UNUSED_ENGINE(Cmd_Argc) UNUSED_ENGINE(Cmd_ArgsBuffer) UNUSED_ENGINE(Cmd_ArgsFrom)
UNUSED_ENGINE(Cmd_Argv) UNUSED_ENGINE(Cmd_ArgvBuffer) UNUSED_ENGINE(Cmd_RemoveCommandSafe) UNUSED_ENGINE(Cmd_TokenizeString)
UNUSED_ENGINE(Com_RealTime) UNUSED_ENGINE(Con_ClearNotify) UNUSED_ENGINE(Con_Close)
UNUSED_ENGINE(Cvar_Register) UNUSED_ENGINE(Cvar_SetSafe) UNUSED_ENGINE(Cvar_Update) UNUSED_ENGINE(Cvar_VariableStringBuffer)
UNUSED_ENGINE(FS_FCloseFile) UNUSED_ENGINE(FS_FOpenFileByMode) UNUSED_ENGINE(FS_Read) UNUSED_ENGINE(FS_Seek) UNUSED_ENGINE(FS_Write)
UNUSED_ENGINE(Key_GetBindingName) UNUSED_ENGINE(Key_GetCatcher) UNUSED_ENGINE(Key_GetKey) UNUSED_ENGINE(Key_IsDown)
UNUSED_ENGINE(Key_SetCatcher)
UNUSED_ENGINE(SCR_UpdateScreen)
UNUSED_ENGINE(S_AddLoopingSound) UNUSED_ENGINE(S_AddRealLoopingSound) UNUSED_ENGINE(S_ClearLoopingSounds)
UNUSED_ENGINE(S_RegisterSound) UNUSED_ENGINE(S_Respatialize) UNUSED_ENGINE(S_StartBackgroundTrack)
UNUSED_ENGINE(S_StartLocalSound) UNUSED_ENGINE(S_StartSound) UNUSED_ENGINE(S_StopBackgroundTrack)
UNUSED_ENGINE(S_StopLoopingSound) UNUSED_ENGINE(S_UpdateEntityPosition)
UNUSED_ENGINE(Sys_Milliseconds) UNUSED_ENGINE(qsnapvectorsse)
