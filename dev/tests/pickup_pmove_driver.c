/* Keep the real private weapon think in its own TU: Q3 headers intentionally
 * do not have include guards. No substitute state machine or gameplay hook. */
#include "../../code/game/bg_pmove.c"
void PickupWeaponStep(pmove_t *move, int msec) {
 pm=move; memset(&pml,0,sizeof(pml)); pml.msec=msec; PM_Weapon();
}
/* Where a note sits on a gun's clip, read from the game's own table. */
int PickupNoteTime(int weapon, int seq, int kind) {
 const bg_weaponNote_t *note=PM_SegNotes(weapon,seq);
 for(;note->note!=WNOTE_NONE;note++) if(note->note==kind) return note->time;
 return -1;
}
