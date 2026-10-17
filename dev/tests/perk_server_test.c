/* Drive the real authoritative event-to-damage boundary. */
#include "../../code/game/g_active.c"
level_locals_t level;
vmCvar_t g_dmflags,g_gametype;
static int damageCalls,damageTotal,shots,checks;
#define CHECK(x) do { checks++; if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
void G_Damage(gentity_t *t,gentity_t *i,gentity_t *a,vec3_t d,vec3_t p,int damage,int flags,int mod) {
 (void)t;(void)i;(void)a;(void)d;(void)p;(void)flags;CHECK(mod==MOD_FALLING);damageCalls++;damageTotal+=damage;
}
void FireWeapon(gentity_t *e) { (void)e;shots++; }
gitem_t *BG_FindItemForPowerup(powerup_t pw) { (void)pw;return NULL; }
gitem_t *BG_FindItem(const char *name) { (void)name;return NULL; }
gentity_t *Drop_Item(gentity_t *e,gitem_t *i,float angle) { (void)i;(void)angle;return e; }
gentity_t *SelectSpawnPoint(vec3_t avoid,vec3_t origin,vec3_t angles,qboolean bot) {
 (void)avoid;(void)origin;(void)angles;(void)bot;return NULL;
}
void TeleportPlayer(gentity_t *e,vec3_t o,vec3_t a) { (void)e;(void)o;(void)a; }
void G_StartKamikaze(gentity_t *e) { (void)e; }
void DropPortalSource(gentity_t *e) { (void)e; }
void DropPortalDestination(gentity_t *e) { (void)e; }
int main(void) {
 gentity_t ent;gclient_t client;int mask,event;
 memset(&ent,0,sizeof(ent));memset(&client,0,sizeof(client));ent.client=&client;ent.s.eType=ET_PLAYER;
 for(mask=0;mask<(1<<PS_PERK_BITS);mask++)for(event=EV_FALL_MEDIUM;event<=EV_FALL_FAR;event++) {
  client.ps.perks=mask;client.ps.events[0]=event;client.ps.eventSequence=1;damageCalls=damageTotal=0;
  g_dmflags.integer=0;ClientEvents(&ent,0);
  CHECK(damageCalls==(BG_HasPerk(&client.ps,PERK_NO_FALL_DAMAGE) ? 0 : 1));
  if(damageCalls)CHECK(damageTotal==(event==EV_FALL_FAR ? 10 : 5));
  g_dmflags.integer=DF_NO_FALLING;damageCalls=0;ClientEvents(&ent,0);CHECK(!damageCalls);
  client.ps.events[0]=EV_FIRE_WEAPON;shots=0;ClientEvents(&ent,0);CHECK(shots==1);
 }
 printf("PASS: %d authoritative fall-damage perk checks\n",checks);return 0;
}
