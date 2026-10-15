/* Keep the real private weapon think in its own TU: Q3 headers intentionally
 * do not have include guards. No substitute state machine or gameplay hook. */
#include "../../code/game/bg_pmove.c"
void PickupWeaponStep(pmove_t *move, int msec) {
 pm=move; memset(&pml,0,sizeof(pml)); pml.msec=msec; PM_Weapon();
}
