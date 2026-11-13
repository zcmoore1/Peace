/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
// bg_misc.c -- both games misc functions, all completely stateless

#include "../qcommon/q_shared.h"
#include "bg_public.h"

/*QUAKED item_***** ( 0 0 0 ) (-16 -16 -16) (16 16 16) suspended
DO NOT USE THIS CLASS, IT JUST HOLDS GENERAL INFORMATION.
The suspended flag will allow items to hang in the air, otherwise they are dropped to the next surface.

If an item is the target of another entity, it will not spawn in until fired.

An item fires all of its targets when it is picked up.  If the toucher can't carry it, the targets won't be fired.

"notfree" if set to 1, don't spawn in free for all games
"notteam" if set to 1, don't spawn in team games
"notsingle" if set to 1, don't spawn in single player games
"wait"	override the default wait before respawning.  -1 = never respawn automatically, which can be used with targeted spawning.
"random" random number of plus or minus seconds varied from the respawn time
"count" override quantity or duration on most items.
*/

gitem_t	bg_itemlist[] = 
{
	{
		NULL,
		NULL,
		{ NULL,
		NULL,
		NULL, NULL} ,
/* icon */		NULL,
/* pickup */	NULL,
		0,
		0,
		0,
/* precache */ "",
/* sounds */ ""
	},	// leave index 0 alone

	//
	// ARMOR
	//

/*QUAKED item_armor_shard (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_armor_shard", 
		"sound/misc/ar1_pkup.wav",
		{ "models/powerups/armor/shard.md3", 
		"models/powerups/armor/shard_sphere.md3",
		NULL, NULL} ,
/* icon */		"icons/iconr_shard",
/* pickup */	"Armor Shard",
		5,
		IT_ARMOR,
		0,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_armor_combat (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_armor_combat", 
		"sound/misc/ar2_pkup.wav",
        { "models/powerups/armor/armor_yel.md3",
		NULL, NULL, NULL},
/* icon */		"icons/iconr_yellow",
/* pickup */	"Armor",
		50,
		IT_ARMOR,
		0,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_armor_body (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_armor_body", 
		"sound/misc/ar2_pkup.wav",
        { "models/powerups/armor/armor_red.md3",
		NULL, NULL, NULL},
/* icon */		"icons/iconr_red",
/* pickup */	"Heavy Armor",
		100,
		IT_ARMOR,
		0,
/* precache */ "",
/* sounds */ ""
	},

	//
	// health
	//
/*QUAKED item_health_small (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_health_small",
		"sound/items/s_health.wav",
        { "models/powerups/health/small_cross.md3", 
		"models/powerups/health/small_sphere.md3", 
		NULL, NULL },
/* icon */		"icons/iconh_green",
/* pickup */	"5 Health",
		5,
		IT_HEALTH,
		0,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_health (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_health",
		"sound/items/n_health.wav",
        { "models/powerups/health/medium_cross.md3", 
		"models/powerups/health/medium_sphere.md3", 
		NULL, NULL },
/* icon */		"icons/iconh_yellow",
/* pickup */	"25 Health",
		25,
		IT_HEALTH,
		0,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_health_large (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_health_large",
		"sound/items/l_health.wav",
        { "models/powerups/health/large_cross.md3", 
		"models/powerups/health/large_sphere.md3", 
		NULL, NULL },
/* icon */		"icons/iconh_red",
/* pickup */	"50 Health",
		50,
		IT_HEALTH,
		0,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_health_mega (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_health_mega",
		"sound/items/m_health.wav",
        { "models/powerups/health/mega_cross.md3", 
		"models/powerups/health/mega_sphere.md3", 
		NULL, NULL },
/* icon */		"icons/iconh_mega",
/* pickup */	"Mega Health",
		100,
		IT_HEALTH,
		0,
/* precache */ "",
/* sounds */ ""
	},


	//
	// WEAPONS 
	//

/*QUAKED weapon_gauntlet (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_gauntlet", 
		"sound/misc/w_pkup.wav",
        { "models/weapons2/gauntlet/gauntlet.md3",
		NULL, NULL, NULL},
/* icon */		"icons/iconw_gauntlet",
/* pickup */	"Gauntlet",
		0,
		IT_WEAPON,
		WP_GAUNTLET,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_knife (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
Melee. Never spawns in a map - the entry exists because every weapon in
STAT_WEAPONS must resolve to an item: CG_RegisterWeapon CG_Error()s otherwise,
and the weapon-select HUD walks every owned bit.
*/
	{
		"weapon_knife",
		"sound/misc/w_pkup.wav",
        { "models/weapons2/throwingknife/knife.md3",
		NULL, NULL, NULL},
/* icon */		"icons/peace_throwingknife",
/* pickup */	"Knife",
		0,
		IT_WEAPON,
		WP_KNIFE,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_frag (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
Lethal equipment. Placeholder visuals until real assets land.
*/
	{
		"weapon_frag",
		"sound/misc/w_pkup.wav",
        { "models/weapons2/grenadel/grenadel.md3",
		NULL, NULL, NULL},
/* icon */		"icons/iconw_grenade",
/* pickup */	"Frag Grenade",
		0,
		IT_WEAPON,
		WP_FRAG,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_flash (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
Tactical equipment. Placeholder visuals until real assets land.
*/
	{
		"weapon_flash",
		"sound/misc/w_pkup.wav",
        { "models/weapons2/grenadel/grenadel.md3",
		NULL, NULL, NULL},
/* icon */		"icons/iconw_grenade",
/* pickup */	"Flashbang",
		0,
		IT_WEAPON,
		WP_FLASH,
/* precache */ "",
/* sounds */ ""
	},

/* Held lethal equipment, not a primary/secondary slot. */
	{
		"weapon_semtex", "sound/misc/w_pkup.wav",
		{ "models/weapons2/semtex/semtex.md3", NULL, NULL, NULL },
		"icons/peace_semtex", "Semtex", 0, IT_WEAPON, WP_SEMTEX, "", ""
	},

/* Lethal equipment: throwing knife, with original Peace art. */
	{
		"weapon_throwingknife", "sound/misc/w_pkup.wav",
		{ "models/weapons2/throwingknife/knife.md3", NULL, NULL, NULL },
		"icons/peace_throwingknife", "Throwing Knife", 0, IT_WEAPON, WP_THROWING_KNIFE, "", ""
	},

/*QUAKED weapon_shotgun (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_shotgun", 
		"sound/misc/w_pkup.wav",
        { "models/weapons2/shotgun/shotgun.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_shotgun",
/* pickup */	"Shotgun",
		10,
		IT_WEAPON,
		WP_SHOTGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_machinegun (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_machinegun", 
		"sound/misc/w_pkup.wav",
        { "models/weapons2/machinegun/machinegun.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_machinegun",
/* pickup */	"Machinegun",
		40,
		IT_WEAPON,
		WP_MACHINEGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_grenadelauncher (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_grenadelauncher",
		"sound/misc/w_pkup.wav",
        { "models/weapons2/grenadel/grenadel.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_grenade",
/* pickup */	"Grenade Launcher",
		10,
		IT_WEAPON,
		WP_GRENADE_LAUNCHER,
/* precache */ "",
/* sounds */ "sound/weapons/grenade/hgrenb1a.wav sound/weapons/grenade/hgrenb2a.wav"
	},

/*QUAKED weapon_rocketlauncher (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_rocketlauncher",
		"sound/misc/w_pkup.wav",
        { "models/weapons2/rocketl/rocketl.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_rocket",
/* pickup */	"Rocket Launcher",
		10,
		IT_WEAPON,
		WP_ROCKET_LAUNCHER,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_lightning (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_lightning", 
		"sound/misc/w_pkup.wav",
        { "models/weapons2/lightning/lightning.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_lightning",
/* pickup */	"Lightning Gun",
		100,
		IT_WEAPON,
		WP_LIGHTNING,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_railgun (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_railgun", 
		"sound/misc/w_pkup.wav",
        { "models/weapons2/railgun/railgun.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_railgun",
/* pickup */	"Railgun",
		10,
		IT_WEAPON,
		WP_RAILGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_plasmagun (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_plasmagun", 
		"sound/misc/w_pkup.wav",
        { "models/weapons2/plasma/plasma.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_plasma",
/* pickup */	"Plasma Gun",
		50,
		IT_WEAPON,
		WP_PLASMAGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_bfg (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_bfg",
		"sound/misc/w_pkup.wav",
        { "models/weapons2/bfg/bfg.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_bfg",
/* pickup */	"BFG10K",
		20,
		IT_WEAPON,
		WP_BFG,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_grapplinghook (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_grapplinghook",
		"sound/misc/w_pkup.wav",
        { "models/weapons2/grapple/grapple.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_grapple",
/* pickup */	"Grappling Hook",
		0,
		IT_WEAPON,
		WP_GRAPPLING_HOOK,
/* precache */ "",
/* sounds */ ""
	},

	//
	// AMMO ITEMS
	//

/*QUAKED ammo_shells (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_shells",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/shotgunam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_shotgun",
/* pickup */	"Shells",
		10,
		IT_AMMO,
		WP_SHOTGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_bullets (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_bullets",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/machinegunam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_machinegun",
/* pickup */	"Bullets",
		50,
		IT_AMMO,
		WP_MACHINEGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_grenades (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_grenades",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/grenadeam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_grenade",
/* pickup */	"Grenades",
		5,
		IT_AMMO,
		WP_GRENADE_LAUNCHER,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_cells (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_cells",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/plasmaam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_plasma",
/* pickup */	"Cells",
		30,
		IT_AMMO,
		WP_PLASMAGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_lightning (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_lightning",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/lightningam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_lightning",
/* pickup */	"Lightning",
		60,
		IT_AMMO,
		WP_LIGHTNING,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_rockets (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_rockets",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/rocketam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_rocket",
/* pickup */	"Rockets",
		5,
		IT_AMMO,
		WP_ROCKET_LAUNCHER,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_slugs (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_slugs",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/railgunam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_railgun",
/* pickup */	"Slugs",
		10,
		IT_AMMO,
		WP_RAILGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_bfg (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_bfg",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/bfgam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_bfg",
/* pickup */	"Bfg Ammo",
		15,
		IT_AMMO,
		WP_BFG,
/* precache */ "",
/* sounds */ ""
	},

	//
	// HOLDABLE ITEMS
	//
/*QUAKED holdable_teleporter (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"holdable_teleporter", 
		"sound/items/holdable.wav",
        { "models/powerups/holdable/teleporter.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/teleporter",
/* pickup */	"Personal Teleporter",
		60,
		IT_HOLDABLE,
		HI_TELEPORTER,
/* precache */ "",
/* sounds */ ""
	},
/*QUAKED holdable_medkit (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"holdable_medkit", 
		"sound/items/holdable.wav",
        { 
		"models/powerups/holdable/medkit.md3", 
		"models/powerups/holdable/medkit_sphere.md3",
		NULL, NULL},
/* icon */		"icons/medkit",
/* pickup */	"Medkit",
		60,
		IT_HOLDABLE,
		HI_MEDKIT,
/* precache */ "",
/* sounds */ "sound/items/use_medkit.wav"
	},

	//
	// POWERUP ITEMS
	//
/*QUAKED item_quad (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_quad", 
		"sound/items/quaddamage.wav",
        { "models/powerups/instant/quad.md3", 
        "models/powerups/instant/quad_ring.md3",
		NULL, NULL },
/* icon */		"icons/quad",
/* pickup */	"Quad Damage",
		30,
		IT_POWERUP,
		PW_QUAD,
/* precache */ "",
/* sounds */ "sound/items/damage2.wav sound/items/damage3.wav"
	},

/*QUAKED item_enviro (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_enviro",
		"sound/items/protect.wav",
        { "models/powerups/instant/enviro.md3", 
		"models/powerups/instant/enviro_ring.md3", 
		NULL, NULL },
/* icon */		"icons/envirosuit",
/* pickup */	"Battle Suit",
		30,
		IT_POWERUP,
		PW_BATTLESUIT,
/* precache */ "",
/* sounds */ "sound/items/airout.wav sound/items/protect3.wav"
	},

/*QUAKED item_haste (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_haste",
		"sound/items/haste.wav",
        { "models/powerups/instant/haste.md3", 
		"models/powerups/instant/haste_ring.md3", 
		NULL, NULL },
/* icon */		"icons/haste",
/* pickup */	"Speed",
		30,
		IT_POWERUP,
		PW_HASTE,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_invis (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_invis",
		"sound/items/invisibility.wav",
        { "models/powerups/instant/invis.md3", 
		"models/powerups/instant/invis_ring.md3", 
		NULL, NULL },
/* icon */		"icons/invis",
/* pickup */	"Invisibility",
		30,
		IT_POWERUP,
		PW_INVIS,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_regen (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_regen",
		"sound/items/regeneration.wav",
        { "models/powerups/instant/regen.md3", 
		"models/powerups/instant/regen_ring.md3", 
		NULL, NULL },
/* icon */		"icons/regen",
/* pickup */	"Regeneration",
		30,
		IT_POWERUP,
		PW_REGEN,
/* precache */ "",
/* sounds */ "sound/items/regen.wav"
	},

/*QUAKED item_flight (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"item_flight",
		"sound/items/flight.wav",
        { "models/powerups/instant/flight.md3", 
		"models/powerups/instant/flight_ring.md3", 
		NULL, NULL },
/* icon */		"icons/flight",
/* pickup */	"Flight",
		60,
		IT_POWERUP,
		PW_FLIGHT,
/* precache */ "",
/* sounds */ "sound/items/flight.wav"
	},

/*QUAKED team_CTF_redflag (1 0 0) (-16 -16 -16) (16 16 16)
Only in CTF games
*/
	{
		"team_CTF_redflag",
		NULL,
        { "models/flags/r_flag.md3",
		NULL, NULL, NULL },
/* icon */		"icons/iconf_red1",
/* pickup */	"Red Flag",
		0,
		IT_TEAM,
		PW_REDFLAG,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED team_CTF_blueflag (0 0 1) (-16 -16 -16) (16 16 16)
Only in CTF games
*/
	{
		"team_CTF_blueflag",
		NULL,
        { "models/flags/b_flag.md3",
		NULL, NULL, NULL },
/* icon */		"icons/iconf_blu1",
/* pickup */	"Blue Flag",
		0,
		IT_TEAM,
		PW_BLUEFLAG,
/* precache */ "",
/* sounds */ ""
	},

#ifdef MISSIONPACK
/*QUAKED holdable_kamikaze (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"holdable_kamikaze", 
		"sound/items/holdable.wav",
        { "models/powerups/kamikazi.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/kamikaze",
/* pickup */	"Kamikaze",
		60,
		IT_HOLDABLE,
		HI_KAMIKAZE,
/* precache */ "",
/* sounds */ "sound/items/kamikazerespawn.wav"
	},

/*QUAKED holdable_portal (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"holdable_portal", 
		"sound/items/holdable.wav",
        { "models/powerups/holdable/porter.md3",
		NULL, NULL, NULL},
/* icon */		"icons/portal",
/* pickup */	"Portal",
		60,
		IT_HOLDABLE,
		HI_PORTAL,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED holdable_invulnerability (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"holdable_invulnerability", 
		"sound/items/holdable.wav",
        { "models/powerups/holdable/invulnerability.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/invulnerability",
/* pickup */	"Invulnerability",
		60,
		IT_HOLDABLE,
		HI_INVULNERABILITY,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_nails (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_nails",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/nailgunam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_nailgun",
/* pickup */	"Nails",
		20,
		IT_AMMO,
		WP_NAILGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_mines (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_mines",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/proxmineam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_proxlauncher",
/* pickup */	"Proximity Mines",
		10,
		IT_AMMO,
		WP_PROX_LAUNCHER,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED ammo_belt (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"ammo_belt",
		"sound/misc/am_pkup.wav",
        { "models/powerups/ammo/chaingunam.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/icona_chaingun",
/* pickup */	"Chaingun Belt",
		100,
		IT_AMMO,
		WP_CHAINGUN,
/* precache */ "",
/* sounds */ ""
	},

	//
	// PERSISTANT POWERUP ITEMS
	//
/*QUAKED item_scout (.3 .3 1) (-16 -16 -16) (16 16 16) suspended redTeam blueTeam
*/
	{
		"item_scout",
		"sound/items/scout.wav",
        { "models/powerups/scout.md3", 
		NULL, NULL, NULL },
/* icon */		"icons/scout",
/* pickup */	"Scout",
		30,
		IT_PERSISTANT_POWERUP,
		PW_SCOUT,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_guard (.3 .3 1) (-16 -16 -16) (16 16 16) suspended redTeam blueTeam
*/
	{
		"item_guard",
		"sound/items/guard.wav",
        { "models/powerups/guard.md3", 
		NULL, NULL, NULL },
/* icon */		"icons/guard",
/* pickup */	"Guard",
		30,
		IT_PERSISTANT_POWERUP,
		PW_GUARD,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_doubler (.3 .3 1) (-16 -16 -16) (16 16 16) suspended redTeam blueTeam
*/
	{
		"item_doubler",
		"sound/items/doubler.wav",
        { "models/powerups/doubler.md3", 
		NULL, NULL, NULL },
/* icon */		"icons/doubler",
/* pickup */	"Doubler",
		30,
		IT_PERSISTANT_POWERUP,
		PW_DOUBLER,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED item_doubler (.3 .3 1) (-16 -16 -16) (16 16 16) suspended redTeam blueTeam
*/
	{
		"item_ammoregen",
		"sound/items/ammoregen.wav",
        { "models/powerups/ammo.md3",
		NULL, NULL, NULL },
/* icon */		"icons/ammo_regen",
/* pickup */	"Ammo Regen",
		30,
		IT_PERSISTANT_POWERUP,
		PW_AMMOREGEN,
/* precache */ "",
/* sounds */ ""
	},

	/*QUAKED team_CTF_neutralflag (0 0 1) (-16 -16 -16) (16 16 16)
Only in One Flag CTF games
*/
	{
		"team_CTF_neutralflag",
		NULL,
        { "models/flags/n_flag.md3",
		NULL, NULL, NULL },
/* icon */		"icons/iconf_neutral1",
/* pickup */	"Neutral Flag",
		0,
		IT_TEAM,
		PW_NEUTRALFLAG,
/* precache */ "",
/* sounds */ ""
	},

	{
		"item_redcube",
		"sound/misc/am_pkup.wav",
        { "models/powerups/orb/r_orb.md3",
		NULL, NULL, NULL },
/* icon */		"icons/iconh_rorb",
/* pickup */	"Red Cube",
		0,
		IT_TEAM,
		0,
/* precache */ "",
/* sounds */ ""
	},

	{
		"item_bluecube",
		"sound/misc/am_pkup.wav",
        { "models/powerups/orb/b_orb.md3",
		NULL, NULL, NULL },
/* icon */		"icons/iconh_borb",
/* pickup */	"Blue Cube",
		0,
		IT_TEAM,
		0,
/* precache */ "",
/* sounds */ ""
	},
/*QUAKED weapon_nailgun (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_nailgun", 
		"sound/misc/w_pkup.wav",
        { "models/weapons/nailgun/nailgun.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_nailgun",
/* pickup */	"Nailgun",
		10,
		IT_WEAPON,
		WP_NAILGUN,
/* precache */ "",
/* sounds */ ""
	},

/*QUAKED weapon_prox_launcher (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_prox_launcher", 
		"sound/misc/w_pkup.wav",
        { "models/weapons/proxmine/proxmine.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_proxlauncher",
/* pickup */	"Prox Launcher",
		5,
		IT_WEAPON,
		WP_PROX_LAUNCHER,
/* precache */ "",
/* sounds */ "sound/weapons/proxmine/wstbtick.wav "
			"sound/weapons/proxmine/wstbactv.wav "
			"sound/weapons/proxmine/wstbimpl.wav "
			"sound/weapons/proxmine/wstbimpm.wav "
			"sound/weapons/proxmine/wstbimpd.wav "
			"sound/weapons/proxmine/wstbactv.wav"
	},

/*QUAKED weapon_chaingun (.3 .3 1) (-16 -16 -16) (16 16 16) suspended
*/
	{
		"weapon_chaingun", 
		"sound/misc/w_pkup.wav",
        { "models/weapons/vulcan/vulcan.md3", 
		NULL, NULL, NULL},
/* icon */		"icons/iconw_chaingun",
/* pickup */	"Chaingun",
		80,
		IT_WEAPON,
		WP_CHAINGUN,
/* precache */ "",
/* sounds */ "sound/weapons/vulcan/wvulwind.wav"
	},
#endif

	// end of list marker
	{
		"weapon_m40a3", "sound/misc/w_pkup.wav",
		{ "models/weapons2/m40a3/world.md3", NULL, NULL, NULL },
		"icons/peace_m40a3", "M300 Praetorian", 5, IT_WEAPON, WP_M40A3, "", ""
	},
	/* Lethal equipment: MW2's Tactical Insertion (flare_mp). Appended so no
	 * item index moves. Placeholder art: the flash's grenade model and icon. */
	{
		"weapon_tacticalinsertion", "sound/misc/w_pkup.wav",
		{ "models/weapons2/grenadel/grenadel.md3", NULL, NULL, NULL },
		"icons/iconw_grenade", "Tactical Insertion", 0, IT_WEAPON, WP_TACTICAL_INSERTION, "", ""
	},
	{NULL}
};

int		bg_numItems = ARRAY_LEN( bg_itemlist ) - 1;


/*
==============
BG_FindItemForPowerup
==============
*/
gitem_t	*BG_FindItemForPowerup( powerup_t pw ) {
	int		i;

	for ( i = 0 ; i < bg_numItems ; i++ ) {
		if ( (bg_itemlist[i].giType == IT_POWERUP || 
					bg_itemlist[i].giType == IT_TEAM ||
					bg_itemlist[i].giType == IT_PERSISTANT_POWERUP) && 
			bg_itemlist[i].giTag == pw ) {
			return &bg_itemlist[i];
		}
	}

	return NULL;
}


/*
==============
BG_FindItemForHoldable
==============
*/
gitem_t	*BG_FindItemForHoldable( holdable_t pw ) {
	int		i;

	for ( i = 0 ; i < bg_numItems ; i++ ) {
		if ( bg_itemlist[i].giType == IT_HOLDABLE && bg_itemlist[i].giTag == pw ) {
			return &bg_itemlist[i];
		}
	}

	Com_Error( ERR_DROP, "HoldableItem not found" );

	return NULL;
}


/*
===============
BG_FindItemForWeapon

===============
*/
gitem_t	*BG_FindItemForWeapon( weapon_t weapon ) {
	gitem_t	*it;
	
	for ( it = bg_itemlist + 1 ; it->classname ; it++) {
		if ( it->giType == IT_WEAPON && it->giTag == weapon ) {
			return it;
		}
	}

	Com_Error( ERR_DROP, "Couldn't find item for weapon %i", weapon);
	return NULL;
}

/*
===============
BG_FindItem

===============
*/
gitem_t	*BG_FindItem( const char *pickupName ) {
	gitem_t	*it;
	
	for ( it = bg_itemlist + 1 ; it->classname ; it++ ) {
		if ( !Q_stricmp( it->pickup_name, pickupName ) )
			return it;
	}

	return NULL;
}

/*
============
BG_PlayerTouchesItem

Items can be picked up without actually touching their physical bounds to make
grabbing them easier
============
*/
qboolean	BG_PlayerTouchesItem( playerState_t *ps, entityState_t *item, int atTime ) {
	vec3_t		origin;

	BG_EvaluateTrajectory( &item->pos, atTime, origin );

	// we are ignoring ducked differences here
	if ( ps->origin[0] - origin[0] > 44
		|| ps->origin[0] - origin[0] < -50
		|| ps->origin[1] - origin[1] > 36
		|| ps->origin[1] - origin[1] < -36
		|| ps->origin[2] - origin[2] > 36
		|| ps->origin[2] - origin[2] < -36 ) {
		return qfalse;
	}

	return qtrue;
}



/*
================
BG_CanItemBeGrabbed

Returns false if the item should not be picked up.
This needs to be the same for client side prediction and server use.
================
*/
qboolean BG_CanItemBeGrabbed( int gametype, const entityState_t *ent, const playerState_t *ps ) {
	gitem_t	*item;
#ifdef MISSIONPACK
	int		upperBound;
#endif

	if ( ent->modelindex < 1 || ent->modelindex >= bg_numItems ) {
		Com_Error( ERR_DROP, "BG_CanItemBeGrabbed: index out of range" );
	}

	item = &bg_itemlist[ent->modelindex];

	switch( item->giType ) {
	case IT_WEAPON:
		return qtrue;	// weapons are always picked up

	case IT_AMMO:
		if ( ps->ammo[ item->giTag ] >= 200 ) {
			return qfalse;		// can't hold any more
		}
		return qtrue;

	case IT_ARMOR:
#ifdef MISSIONPACK
		if( bg_itemlist[ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_SCOUT ) {
			return qfalse;
		}

		// we also clamp armor to the maxhealth for handicapping
		if( bg_itemlist[ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD ) {
			upperBound = ps->stats[STAT_MAX_HEALTH];
		}
		else {
			upperBound = ps->stats[STAT_MAX_HEALTH] * 2;
		}

		if ( ps->stats[STAT_ARMOR] >= upperBound ) {
			return qfalse;
		}
#else
		if ( ps->stats[STAT_ARMOR] >= ps->stats[STAT_MAX_HEALTH] * 2 ) {
			return qfalse;
		}
#endif
		return qtrue;

	case IT_HEALTH:
		// small and mega healths will go over the max, otherwise
		// don't pick up if already at max
#ifdef MISSIONPACK
		if( bg_itemlist[ps->stats[STAT_PERSISTANT_POWERUP]].giTag == PW_GUARD ) {
		}
		else
#endif
		if ( item->quantity == 5 || item->quantity == 100 ) {
			if ( ps->stats[STAT_HEALTH] >= ps->stats[STAT_MAX_HEALTH] * 2 ) {
				return qfalse;
			}
			return qtrue;
		}

		if ( ps->stats[STAT_HEALTH] >= ps->stats[STAT_MAX_HEALTH] ) {
			return qfalse;
		}
		return qtrue;

	case IT_POWERUP:
		return qtrue;	// powerups are always picked up

#ifdef MISSIONPACK
	case IT_PERSISTANT_POWERUP:
		// can only hold one item at a time
		if ( ps->stats[STAT_PERSISTANT_POWERUP] ) {
			return qfalse;
		}

		// check team only
		if( ( ent->generic1 & 2 ) && ( ps->persistant[PERS_TEAM] != TEAM_RED ) ) {
			return qfalse;
		}
		if( ( ent->generic1 & 4 ) && ( ps->persistant[PERS_TEAM] != TEAM_BLUE ) ) {
			return qfalse;
		}

		return qtrue;
#endif

	case IT_TEAM: // team items, such as flags
#ifdef MISSIONPACK		
		if( gametype == GT_1FCTF ) {
			// neutral flag can always be picked up
			if( item->giTag == PW_NEUTRALFLAG ) {
				return qtrue;
			}
			if (ps->persistant[PERS_TEAM] == TEAM_RED) {
				if (item->giTag == PW_BLUEFLAG  && ps->powerups[PW_NEUTRALFLAG] ) {
					return qtrue;
				}
			} else if (ps->persistant[PERS_TEAM] == TEAM_BLUE) {
				if (item->giTag == PW_REDFLAG  && ps->powerups[PW_NEUTRALFLAG] ) {
					return qtrue;
				}
			}
		}
#endif
		if( gametype == GT_CTF ) {
			// ent->modelindex2 is non-zero on items if they are dropped
			// we need to know this because we can pick up our dropped flag (and return it)
			// but we can't pick up our flag at base
			if (ps->persistant[PERS_TEAM] == TEAM_RED) {
				if (item->giTag == PW_BLUEFLAG ||
					(item->giTag == PW_REDFLAG && ent->modelindex2) ||
					(item->giTag == PW_REDFLAG && ps->powerups[PW_BLUEFLAG]) )
					return qtrue;
			} else if (ps->persistant[PERS_TEAM] == TEAM_BLUE) {
				if (item->giTag == PW_REDFLAG ||
					(item->giTag == PW_BLUEFLAG && ent->modelindex2) ||
					(item->giTag == PW_BLUEFLAG && ps->powerups[PW_REDFLAG]) )
					return qtrue;
			}
		}

#ifdef MISSIONPACK
		if( gametype == GT_HARVESTER ) {
			return qtrue;
		}
#endif
		return qfalse;

	case IT_HOLDABLE:
		// can only hold one item at a time
		if ( ps->stats[STAT_HOLDABLE_ITEM] ) {
			return qfalse;
		}
		return qtrue;

        case IT_BAD:
            Com_Error( ERR_DROP, "BG_CanItemBeGrabbed: IT_BAD" );
        default:
#ifndef Q3_VM
#ifndef NDEBUG
          Com_Printf("BG_CanItemBeGrabbed: unknown enum %d\n", item->giType );
#endif
#endif
         break;
	}

	return qfalse;
}

//======================================================================

/*
================
BG_EvaluateTrajectory

================
*/
void BG_EvaluateTrajectory( const trajectory_t *tr, int atTime, vec3_t result ) {
	float		deltaTime;
	float		phase;

	switch( tr->trType ) {
	case TR_STATIONARY:
	case TR_INTERPOLATE:
		VectorCopy( tr->trBase, result );
		break;
	case TR_LINEAR:
		deltaTime = ( atTime - tr->trTime ) * 0.001;	// milliseconds to seconds
		VectorMA( tr->trBase, deltaTime, tr->trDelta, result );
		break;
	case TR_SINE:
		deltaTime = ( atTime - tr->trTime ) / (float) tr->trDuration;
		phase = sin( deltaTime * M_PI * 2 );
		VectorMA( tr->trBase, phase, tr->trDelta, result );
		break;
	case TR_LINEAR_STOP:
		if ( atTime > tr->trTime + tr->trDuration ) {
			atTime = tr->trTime + tr->trDuration;
		}
		deltaTime = ( atTime - tr->trTime ) * 0.001;	// milliseconds to seconds
		if ( deltaTime < 0 ) {
			deltaTime = 0;
		}
		VectorMA( tr->trBase, deltaTime, tr->trDelta, result );
		break;
	case TR_GRAVITY:
		deltaTime = ( atTime - tr->trTime ) * 0.001;	// milliseconds to seconds
		VectorMA( tr->trBase, deltaTime, tr->trDelta, result );
		result[2] -= 0.5 * DEFAULT_GRAVITY * deltaTime * deltaTime;		// FIXME: local gravity...
		break;
	default:
		Com_Error( ERR_DROP, "BG_EvaluateTrajectory: unknown trType: %i", tr->trType );
		break;
	}
}

/*
================
BG_EvaluateTrajectoryDelta

For determining velocity at a given time
================
*/
void BG_EvaluateTrajectoryDelta( const trajectory_t *tr, int atTime, vec3_t result ) {
	float	deltaTime;
	float	phase;

	switch( tr->trType ) {
	case TR_STATIONARY:
	case TR_INTERPOLATE:
		VectorClear( result );
		break;
	case TR_LINEAR:
		VectorCopy( tr->trDelta, result );
		break;
	case TR_SINE:
		deltaTime = ( atTime - tr->trTime ) / (float) tr->trDuration;
		phase = cos( deltaTime * M_PI * 2 );	// derivative of sin = cos
		phase *= 0.5;
		VectorScale( tr->trDelta, phase, result );
		break;
	case TR_LINEAR_STOP:
		if ( atTime > tr->trTime + tr->trDuration ) {
			VectorClear( result );
			return;
		}
		VectorCopy( tr->trDelta, result );
		break;
	case TR_GRAVITY:
		deltaTime = ( atTime - tr->trTime ) * 0.001;	// milliseconds to seconds
		VectorCopy( tr->trDelta, result );
		result[2] -= DEFAULT_GRAVITY * deltaTime;		// FIXME: local gravity...
		break;
	default:
		Com_Error( ERR_DROP, "BG_EvaluateTrajectoryDelta: unknown trType: %i", tr->trType );
		break;
	}
}

char *eventnames[] = {
	"EV_NONE",

	"EV_FOOTSTEP",
	"EV_FOOTSTEP_METAL",
	"EV_FOOTSPLASH",
	"EV_FOOTWADE",
	"EV_SWIM",

	"EV_STEP_4",
	"EV_STEP_8",
	"EV_STEP_12",
	"EV_STEP_16",

	"EV_FALL_SHORT",
	"EV_FALL_MEDIUM",
	"EV_FALL_FAR",

	"EV_JUMP_PAD",			// boing sound at origin", jump sound on player

	"EV_JUMP",
	"EV_WATER_TOUCH",	// foot touches
	"EV_WATER_LEAVE",	// foot leaves
	"EV_WATER_UNDER",	// head touches
	"EV_WATER_CLEAR",	// head leaves

	"EV_ITEM_PICKUP",			// normal item pickups are predictable
	"EV_GLOBAL_ITEM_PICKUP",	// powerup / team sounds are broadcast to everyone

	"EV_NOAMMO",
	"EV_CHANGE_WEAPON",
	"EV_FIRE_WEAPON",
	"EV_RELOAD",
	"EV_WEAPON_NOTETRACK",

	"EV_USE_ITEM0",
	"EV_USE_ITEM1",
	"EV_USE_ITEM2",
	"EV_USE_ITEM3",
	"EV_USE_ITEM4",
	"EV_USE_ITEM5",
	"EV_USE_ITEM6",
	"EV_USE_ITEM7",
	"EV_USE_ITEM8",
	"EV_USE_ITEM9",
	"EV_USE_ITEM10",
	"EV_USE_ITEM11",
	"EV_USE_ITEM12",
	"EV_USE_ITEM13",
	"EV_USE_ITEM14",
	"EV_USE_ITEM15",

	"EV_ITEM_RESPAWN",
	"EV_ITEM_POP",
	"EV_PLAYER_TELEPORT_IN",
	"EV_PLAYER_TELEPORT_OUT",

	"EV_GRENADE_BOUNCE",		// eventParm will be the soundindex

	"EV_GENERAL_SOUND",
	"EV_GLOBAL_SOUND",		// no attenuation
	"EV_GLOBAL_TEAM_SOUND",

	"EV_BULLET_HIT_FLESH",
	"EV_BULLET_HIT_WALL",

	"EV_MISSILE_HIT",
	"EV_MISSILE_MISS",
	"EV_MISSILE_MISS_METAL",
	"EV_RAILTRAIL",
	"EV_SHOTGUN",
	"EV_BULLET",				// otherEntity is the shooter

	"EV_PAIN",
	"EV_DEATH1",
	"EV_DEATH2",
	"EV_DEATH3",
	"EV_OBITUARY",

	"EV_POWERUP_QUAD",
	"EV_POWERUP_BATTLESUIT",
	"EV_POWERUP_REGEN",

	"EV_GIB_PLAYER",			// gib a previously living player
	"EV_SCOREPLUM",			// score plum

//#ifdef MISSIONPACK
	"EV_PROXIMITY_MINE_STICK",
	"EV_PROXIMITY_MINE_TRIGGER",
	"EV_KAMIKAZE",			// kamikaze explodes
	"EV_OBELISKEXPLODE",		// obelisk explodes
	"EV_OBELISKPAIN",		// obelisk pain
	"EV_INVUL_IMPACT",		// invulnerability sphere impact
	"EV_JUICED",				// invulnerability juiced effect
	"EV_LIGHTNINGBOLT",		// lightning bolt bounced of invulnerability sphere
//#endif

	"EV_DEBUG_LINE",
	"EV_STOPLOOPINGSOUND",
	"EV_TAUNT",
	"EV_TAUNT_YES",
	"EV_TAUNT_NO",
	"EV_TAUNT_FOLLOWME",
	"EV_TAUNT_GETFLAG",
	"EV_TAUNT_GUARDBASE",
	"EV_TAUNT_PATROL",

	"EV_STEP_DOWN_4",
	"EV_STEP_DOWN_8",

	"EV_GRENADE_SUICIDE",

	"EV_SWITCH_OFFHAND",
	"EV_WEAPON_SWITCH_STARTED",
	"EV_RESET_ADS",
	"EV_PREP_OFFHAND",

	"EV_STANCE_FORCE_STAND",
	"EV_STANCE_FORCE_CROUCH",
	"EV_STANCE_FORCE_PRONE",
	"EV_STANCE_INVALID"

};
// one name per event, and every event number fits the rings' 8 bits
typedef char eventNamesMatchEvents[(ARRAY_LEN( eventnames ) == EV_STANCE_INVALID + 1 && EV_STANCE_INVALID < 256) ? 1 : -1];

/*
===============
BG_AddPredictableEventToPlayerstate

Handles the sequence numbers
===============
*/

void	trap_Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize );

void BG_AddPredictableEventToPlayerstate( int newEvent, int eventParm, playerState_t *ps ) {

#ifdef _DEBUG
	{
		char buf[256];
		trap_Cvar_VariableStringBuffer("showevents", buf, sizeof(buf));
		if ( atof(buf) != 0 ) {
#ifdef QAGAME
			Com_Printf(" game event svt %5d -> %5d: num = %20s parm %d\n", ps->pmove_framecount/*ps->commandTime*/, ps->eventSequence, eventnames[newEvent], eventParm);
#else
			Com_Printf("Cgame event svt %5d -> %5d: num = %20s parm %d\n", ps->pmove_framecount/*ps->commandTime*/, ps->eventSequence, eventnames[newEvent], eventParm);
#endif
		}
	}
#endif
	ps->events[ps->eventSequence & (MAX_PS_EVENTS-1)] = newEvent;
	ps->eventParms[ps->eventSequence & (MAX_PS_EVENTS-1)] = eventParm;
	ps->eventSequence++;
}

/*
===============
BG_AddEntityEvent

MW2's (0x4405d0): an event on an entity's own ring. Its sequence wraps at 11
bits, as MW2's does.
===============
*/
void BG_AddEntityEvent( entityState_t *es, int event, int eventParm ) {
	if ( !event ) {
		return;
	}
	es->events[es->eventSequence & (MAX_ENTITY_EVENTS-1)] = event;
	es->eventParms[es->eventSequence & (MAX_ENTITY_EVENTS-1)] = eventParm;
	es->eventSequence = ( es->eventSequence + 1 ) & ES_EVENT_SEQUENCE_MASK;
}

/*
===============
BG_EventReachesOthers

MW2's BG_PlayerStateToEntityState copies a player's events onto its entity
(56eab0), skipping two lists. Events only the server reacts to (0x739b38):
EV_RELOAD_ADDAMMO, EV_RELOAD_START_NOTIFY, EV_WEAPON_SWITCH_STARTED,
EV_OFFHAND_END_NOTIFY and the four EV_SV_FIRE_WEAPON. Events only the player's
own client plays (0x739af0): the four stance corrections, EV_SWITCH_OFFHAND,
both EV_EMPTY_OFFHAND, EV_RESET_ADS, EV_MELEE_BLOOD, the five hint events and
three EV_BULLET_HIT_CLIENT. Peace has a counterpart only for the offhand
press's three below and the four stance events (MW2's 6..9, the list's first
four). Its one shot event, EV_FIRE_WEAPON, is both MW2's
EV_SV_FIRE_WEAPON (the shooter's own view and server) and the EV_FIRE_WEAPON
MW2's server sends everyone else, so it is on neither list.
===============
*/
static const int bg_serverOnlyEvents[] = { EV_WEAPON_SWITCH_STARTED };
static const int bg_singleClientEvents[] = { EV_STANCE_FORCE_STAND, EV_STANCE_FORCE_CROUCH,
	EV_STANCE_FORCE_PRONE, EV_STANCE_INVALID, EV_SWITCH_OFFHAND, EV_RESET_ADS };

qboolean BG_EventReachesOthers( int event ) {
	int i;
	if ( !event ) {
		return qfalse;
	}
	for ( i = 0; i < ARRAY_LEN( bg_serverOnlyEvents ); i++ ) {
		if ( bg_serverOnlyEvents[i] == event ) return qfalse;
	}
	for ( i = 0; i < ARRAY_LEN( bg_singleClientEvents ); i++ ) {
		if ( bg_singleClientEvents[i] == event ) return qfalse;
	}
	return qtrue;
}

/*
===============
BG_PlayerEventsToEntity

MW2's copy of the predictable ring onto the entity's (56eab0): every event
since the last copy, oldest first, except the ones that never reach anyone
else. Run after each command and at each frame's end, so a command's last
MAX_PS_EVENTS events are copied. A command Pmove chops into several steps
can raise more: MW2's stance events can come every step (EV_STANCE_INVALID
while a prone turn stays blocked, EV_STANCE_FORCE_STAND on a ladder with a
crouch or prone bit, a FORCE event until the client's stance is reset), so a
shot followed by four of them in one chopped command falls out of the ring,
as it does of MW2's four slots (and of ClientEvents, which reads the same
last four). Nothing special-cases it; stance_test.c measures it.
===============
*/
static void BG_PlayerEventsToEntity( playerState_t *ps, entityState_t *s ) {
	int i;
	if ( ps->entityEventSequence < ps->eventSequence - MAX_PS_EVENTS ) {
		ps->entityEventSequence = ps->eventSequence - MAX_PS_EVENTS;
	}
	for ( i = ps->entityEventSequence; i < ps->eventSequence; i++ ) {
		int event = ps->events[i & (MAX_PS_EVENTS-1)];
		if ( BG_EventReachesOthers( event ) ) {
			BG_AddEntityEvent( s, event, ps->eventParms[i & (MAX_PS_EVENTS-1)] );
		}
	}
	ps->entityEventSequence = ps->eventSequence;
}

/*
========================
BG_TouchJumpPad
========================
*/
void BG_TouchJumpPad( playerState_t *ps, entityState_t *jumppad ) {
	vec3_t	angles;
	float p;
	int effectNum;

	// spectators don't use jump pads
	if ( ps->pm_type != PM_NORMAL ) {
		return;
	}

	// flying characters don't hit bounce pads
	if ( ps->powerups[PW_FLIGHT] ) {
		return;
	}

	// if we didn't hit this same jumppad the previous frame
	// then don't play the event sound again if we are in a fat trigger
	if ( ps->jumppad_ent != jumppad->number ) {

		vectoangles( jumppad->origin2, angles);
		p = fabs( AngleNormalize180( angles[PITCH] ) );
		if( p < 45 ) {
			effectNum = 0;
		} else {
			effectNum = 1;
		}
		BG_AddPredictableEventToPlayerstate( EV_JUMP_PAD, effectNum, ps );
	}
	// remember hitting this jumppad this frame
	ps->jumppad_ent = jumppad->number;
	ps->jumppad_frame = ps->pmove_framecount;
	// give the player the velocity from the jumppad
	VectorCopy( jumppad->origin2, ps->velocity );
}

/*
========================
BG_PlayerStanceToEntity

MW2 mirrors the stance into its eFlags (EF_CROUCHING, EF_PRONE, written with
the flags at 0x571017-0x571102), and the entity is how everyone else knows it.
Peace writes them here only, from the flags, so playerState_t.eFlags never
holds them. The eye, unsnapped, rides in origin2[2], which no ET_PLAYER used:
others' drawn bullets start where the server's damage trace did, and the eye
is what the bullet-only hit volumes reach up to (BG_PlayerHitVolumes). The
prone body's yaw and pitch ride in origin2[0] and origin2[1] while prone.
========================
*/
static void BG_PlayerStanceToEntity( const playerState_t *ps, entityState_t *s ) {
	s->eFlags &= ~( EF_CROUCHING | EF_PRONE );
	if ( ps->pm_flags & PMF_DUCKED ) {
		s->eFlags |= EF_CROUCHING;
	}
	if ( ps->pm_flags & PMF_PRONE ) {
		s->eFlags |= EF_PRONE;
		s->origin2[0] = ps->proneDirection;
		s->origin2[1] = ps->proneDirectionPitch;
	} else {
		s->origin2[0] = 0;
		s->origin2[1] = 0;
	}
	s->origin2[2] = ps->viewheight;
}

/*
========================
BG_PlayerStateToEntityState

This is done after each set of usercmd_t on the server,
and after local prediction on the client
========================
*/
void BG_PlayerStateToEntityState( playerState_t *ps, entityState_t *s, qboolean snap ) {
	int		i;

	if ( ps->pm_type == PM_INTERMISSION || ps->pm_type == PM_SPECTATOR ) {
		s->eType = ET_INVISIBLE;
	} else if ( ps->stats[STAT_HEALTH] <= GIB_HEALTH ) {
		s->eType = ET_INVISIBLE;
	} else {
		s->eType = ET_PLAYER;
	}

	s->number = ps->clientNum;

	s->pos.trType = TR_INTERPOLATE;
	VectorCopy( ps->origin, s->pos.trBase );
	if ( snap ) {
		SnapVector( s->pos.trBase );
	}
	// set the trDelta for flag direction
	VectorCopy( ps->velocity, s->pos.trDelta );

	s->apos.trType = TR_INTERPOLATE;
	VectorCopy( ps->viewangles, s->apos.trBase );
	if ( snap ) {
		SnapVector( s->apos.trBase );
	}

	s->angles2[YAW] = ps->movementDir;
	s->legsAnim = ps->legsAnim;
	s->torsoAnim = ps->torsoAnim;
	s->clientNum = ps->clientNum;		// ET_PLAYER looks here instead of at number
										// so corpses can also reference the proper config
	s->eFlags = ps->eFlags;
	if ( ps->stats[STAT_HEALTH] <= 0 ) {
		s->eFlags |= EF_DEAD;
	} else {
		s->eFlags &= ~EF_DEAD;
	}

	// The external event is the player's own; G_AddEvent puts it on the ring.
	BG_PlayerEventsToEntity( ps, s );

	s->weapon = ps->weapon;
	s->groundEntityNum = ps->groundEntityNum;

	s->powerups = 0;
	for ( i = 0 ; i < MAX_POWERUPS ; i++ ) {
		if ( ps->powerups[ i ] ) {
			s->powerups |= 1 << i;
		}
	}

	s->loopSound = ps->loopSound;
	s->generic1 = ps->generic1;

	BG_PlayerStanceToEntity( ps, s );
}

/*
========================
BG_PlayerStateToEntityStateExtraPolate

This is done after each set of usercmd_t on the server,
and after local prediction on the client
========================
*/
void BG_PlayerStateToEntityStateExtraPolate( playerState_t *ps, entityState_t *s, int time, qboolean snap ) {
	int		i;

	if ( ps->pm_type == PM_INTERMISSION || ps->pm_type == PM_SPECTATOR ) {
		s->eType = ET_INVISIBLE;
	} else if ( ps->stats[STAT_HEALTH] <= GIB_HEALTH ) {
		s->eType = ET_INVISIBLE;
	} else {
		s->eType = ET_PLAYER;
	}

	s->number = ps->clientNum;

	s->pos.trType = TR_LINEAR_STOP;
	VectorCopy( ps->origin, s->pos.trBase );
	if ( snap ) {
		SnapVector( s->pos.trBase );
	}
	// set the trDelta for flag direction and linear prediction
	VectorCopy( ps->velocity, s->pos.trDelta );
	// set the time for linear prediction
	s->pos.trTime = time;
	// set maximum extra polation time
	s->pos.trDuration = 50; // 1000 / sv_fps (default = 20)

	s->apos.trType = TR_INTERPOLATE;
	VectorCopy( ps->viewangles, s->apos.trBase );
	if ( snap ) {
		SnapVector( s->apos.trBase );
	}

	s->angles2[YAW] = ps->movementDir;
	s->legsAnim = ps->legsAnim;
	s->torsoAnim = ps->torsoAnim;
	s->clientNum = ps->clientNum;		// ET_PLAYER looks here instead of at number
										// so corpses can also reference the proper config
	s->eFlags = ps->eFlags;
	if ( ps->stats[STAT_HEALTH] <= 0 ) {
		s->eFlags |= EF_DEAD;
	} else {
		s->eFlags &= ~EF_DEAD;
	}

	BG_PlayerEventsToEntity( ps, s );

	s->weapon = ps->weapon;
	s->groundEntityNum = ps->groundEntityNum;

	s->powerups = 0;
	for ( i = 0 ; i < MAX_POWERUPS ; i++ ) {
		if ( ps->powerups[ i ] ) {
			s->powerups |= 1 << i;
		}
	}

	s->loopSound = ps->loopSound;
	s->generic1 = ps->generic1;

	BG_PlayerStanceToEntity( ps, s );
}

/*
=============================================================================

CLIENT-SIDE BULLETS

MW2's sv_clientSideBullets: the server traces a hitscan shot for its damage
and sends no impact or tracer for it; every client traces the same shot again
from its fire event and draws them (Bullet_Fire 0x4402c0 on the server, the
cgame's 0x59fdc0 from CG_FireWeapon 4fb2df). Both sides build the shot here,
from the same numbers.

The randomness is MW2's: a shot is seeded with the time of the command that
fired it (Bullet_Fire's gameTime, the cgame's predictedPlayerState.commandTime),
stirred once by BG_srand (0x410720), then drawn from with BG_random
(0x44c590), one shot's bullets or pellets from one seed. The shapes are the
ones Peace's guns already had: the bullet's random angle and two random
lengths scaled by BG_WeaponSpread, and the shotgun's fixed square of
DEFAULT_SHOTGUN_COUNT pellets. So a shot hurts what it hurt before; only where
its randomness comes from changed, and the client can now repeat it.

=============================================================================
*/

/*
================
BG_BulletSeed

MW2's BG_srand: four steps of its generator over the shot's command time.
================
*/
int BG_BulletSeed( int gameTime ) {
	unsigned int seed = (unsigned int)gameTime;
	int i;

	for ( i = 0; i < 4; i++ ) {
		seed = seed * 214013u + 2531011u;
	}
	return (int)seed;
}

/*
================
BG_BulletRandom

MW2's BG_random: the next step's top 15 bits, in [0, 1).
================
*/
float BG_BulletRandom( int *seed ) {
	unsigned int next = (unsigned int)*seed * 214013u + 2531011u;

	*seed = (int)next;
	return (float)( next >> 17 ) * ( 1.0f / 32768.0f );
}

float BG_BulletCrandom( int *seed ) {
	return 2.0f * ( BG_BulletRandom( seed ) - 0.5f );
}

/*
================
BG_ShotMuzzle

Where a player's shot starts: the snapped origin the server's entity carries,
up to the eye, 14 along the aim, snapped (the server's CalcMuzzlePoint). The
eye is a float: MW2's eye curve passes through fractions.
================
*/
void BG_ShotMuzzle( const vec3_t base, float viewheight, const vec3_t forward, vec3_t muzzle ) {
	VectorCopy( base, muzzle );
	muzzle[2] += viewheight;
	VectorMA( muzzle, 14, forward, muzzle );
	SnapVector( muzzle );
}

/*
================
BG_PlayerShotBase

The base BG_ShotMuzzle starts from, as BG_PlayerStateToEntityState snaps it
into the entity the server's FireWeapon reads.
================
*/
void BG_PlayerShotBase( const playerState_t *ps, vec3_t base ) {
	VectorCopy( ps->origin, base );
	SnapVector( base );
}

/*
================
BG_BulletEnd

One bullet's end: a random angle around the aim and a random length along
each of its two sides, at most spread * 16 off at BG_BULLET_RANGE.
================
*/
void BG_BulletEnd( int *seed, float spread, const vec3_t muzzle, const vec3_t forward,
	const vec3_t right, const vec3_t up, vec3_t end ) {
	float	angle, r, u;

	angle = BG_BulletRandom( seed ) * M_PI * 2.0f;
	u = sin( angle ) * BG_BulletCrandom( seed ) * spread * 16;
	r = cos( angle ) * BG_BulletCrandom( seed ) * spread * 16;
	VectorMA( muzzle, BG_BULLET_RANGE, forward, end );
	VectorMA( end, r, right, end );
	VectorMA( end, u, up, end );
}

/*
================
BG_ShotgunAxes

The pattern's axes: the aim scaled to 4096 and snapped, as the shotgun's
blast always carried it, with a side and an up derived from it alone.
================
*/
void BG_ShotgunAxes( const vec3_t aim, vec3_t forward, vec3_t right, vec3_t up ) {
	vec3_t	scaled;

	VectorScale( aim, 4096, scaled );
	SnapVector( scaled );
	VectorNormalize2( scaled, forward );
	PerpendicularVector( right, forward );
	CrossProduct( forward, right, up );
}

/*
================
BG_ShotgunPelletEnd

The next pellet's end in the fixed square pattern.
================
*/
void BG_ShotgunPelletEnd( int *seed, const vec3_t origin, const vec3_t forward,
	const vec3_t right, const vec3_t up, vec3_t end ) {
	float	r, u;

	r = BG_BulletCrandom( seed ) * DEFAULT_SHOTGUN_SPREAD * 16;
	u = BG_BulletCrandom( seed ) * DEFAULT_SHOTGUN_SPREAD * 16;
	VectorMA( origin, BG_BULLET_RANGE, forward, end );
	VectorMA( end, r, right, end );
	VectorMA( end, u, up, end );
}

/*
=============================================================================

MW2'S STANCE

The shared half of MW2's stance button and prone (dev/STANCE.md): the box
rule, the spawn eye, the previous command pmove keeps for the crawl and aim
rules, and the bullet-only hit volumes the server's damage traces and every
client's drawn bullets test alike.

=============================================================================
*/

/*
================
BG_AngleNormalize360, BG_AngleNormalize180, BG_AngleDelta

MW2's angle arithmetic (AngleNormalize360 0x438DC0, AngleDelta 0x446CA0 and
the same pattern inlined for AngleNormalize180): a fraction of a turn, its
whole turns taken off by floor, back to degrees. Exact to a float, where
q_math's versions round to a 65536th of a turn. MW2's prone view, its pitch
and its room check turn on these.
================
*/
#define BG_INV_360	0.0027777778450399637	// MW2's 1/360, a float's (0x700240)

float BG_AngleNormalize360( float angle ) {
	float	turns = (float)( angle * BG_INV_360 );
	float	whole = (float)floor( turns );

	return (float)( ( (double)turns - whole ) * 360.0 );
}

float BG_AngleNormalize180( float angle ) {
	float	turns = (float)( angle * BG_INV_360 );
	float	whole = (float)floor( (float)( turns + 0.5 ) );

	return (float)( ( (double)turns - whole ) * 360.0 );
}

float BG_AngleDelta( float angle1, float angle2 ) {
	return BG_AngleNormalize180( angle1 - angle2 );
}

/*
================
BG_PlayerBox

The one box rule, from the flags pmove keeps (they follow MW2's
viewHeightTarget): dead, prone, crouched or standing. A spectator stands.
Pmove's invulnerability sphere is its own.
================
*/
void BG_PlayerBox( const playerState_t *ps, vec3_t mins, vec3_t maxs ) {
	VectorSet( mins, -PLAYER_WIDTH, -PLAYER_WIDTH, MINS_Z );
	VectorSet( maxs, PLAYER_WIDTH, PLAYER_WIDTH, DEFAULT_HEIGHT );
	if ( ps->pm_type == PM_SPECTATOR ) {
		return;
	}
	if ( ps->pm_type == PM_DEAD ) {
		maxs[2] = DEAD_HEIGHT;
	} else if ( ps->pm_flags & PMF_PRONE ) {
		maxs[2] = PRONE_HEIGHT;
	} else if ( ps->pm_flags & PMF_DUCKED ) {
		maxs[2] = CROUCH_HEIGHT;
	}
}

/*
================
BG_LegsAnimStance

Another player's stance as their legs animation shows it. PM_Footsteps
changes the legs with no move input only below xyspeed 5, so it can lag the
stance while a player slides; EF_CROUCHING and EF_PRONE are exact.
================
*/
int BG_LegsAnimStance( int legsAnim ) {
	switch ( legsAnim & ~ANIM_TOGGLEBIT ) {
	case LEGS_IDLEPR:
	case LEGS_CRAWL:
	case LEGS_CRAWLB:
		return BG_STANCE_PRONE;
	case LEGS_IDLECR:
	case LEGS_WALKCR:
	case LEGS_BACKCR:
		return BG_STANCE_CROUCH;
	default:
		return BG_STANCE_STAND;
	}
}

/*
================
BG_WeaponBlocksProne

MW2's blocksProne (WeaponDef+0x66F), read by PlayerProneAllowed (0x570780).
Only riotshield_mp sets it (the IW4 dump); Peace has no such weapon.
================
*/
qboolean BG_WeaponBlocksProne( int weapon ) {
	(void)weapon;
	return qfalse;
}

/*
================
BG_SpawnStance

MW2's spawn (0x47DB25-0x47DB68): the eye current and target standing, no
leg running. Only the stance flags are cleared: PMF_RESPAWNED and
PMF_TIME_KNOCKBACK, which ClientSpawn sets beside it, are left alone.
================
*/
void BG_SpawnStance( playerState_t *ps ) {
	ps->viewheight = DEFAULT_VIEWHEIGHT;
	ps->viewHeightTarget = BG_EYE_STAND;
	ps->viewHeightLerpTarget = 0;
	ps->viewHeightLerpTime = 0;
	ps->viewHeightLerpDown = 0;
	ps->pm_flags &= ~( PMF_DUCKED | PMF_PRONE | PMF_PRONEMOVE_OVERRIDDEN );
	ps->proneDirection = 0;
	ps->proneDirectionPitch = 0;
	ps->proneTorsoPitch = 0;
}

/*
================
BG_PrevCmdPack

playerState_t.prevCmd, MW2's pmove oldcmd as far as prone reads it: the
command's forwardmove and rightmove as bytes, and its aim button.
================
*/
int BG_PrevCmdPack( const usercmd_t *cmd ) {
	return ( cmd->forwardmove & 0xff ) | ( ( cmd->rightmove & 0xff ) << 8 ) |
		( ( cmd->buttons & BUTTON_ADS ) ? BG_PREVCMD_ADS_BIT : 0 );
}

/*
================
BG_PlayerHitVolumes

The volumes a bullet or pellet can hit a player in (bg_public.h has the
contract): Peace's approximation of MW2's posed skeleton (0x746A08,
0x4C8190), a placeholder, from what the target's entity carries.
  - The move box, always first.
  - Over the box: while the eye (origin2[2]) is above the box top, as on a
    downward stance leg (the box drops at the leg's start, the eye follows
    MW2's curve), the box's footprint from its top up to the eye plus
    BG_HIT_HEAD_ALLOWANCE, the box's own reach above its eye. A player who
    can fire over cover can be shot over it.
  - The prone body: what MW2's prone room check traces (BG_CheckProneValid,
    in bg_pmove.c, with the same BG_PRONE_* numbers), from the origin to
    BG_PRONE_FEET_DIST behind it along the body's yaw (origin2[0]) and pitch
    (origin2[1]), BG_PRONE_TRACE_HALF either side, from the feet to the
    prone box's top.
A corpse is its box alone.
================
*/
int BG_PlayerHitVolumes( const entityState_t *es, const vec3_t origin, const vec3_t mins,
	const vec3_t maxs, bgHitVolume_t vols[BG_MAX_HIT_VOLUMES] ) {
	bgHitVolume_t	*v = &vols[0];
	int				i, count = 1;
	float			eye, top;
	vec3_t			angles, forward, right, up;

	v->type = HITVOL_BOX;
	for ( i = 0; i < 3; i++ ) {
		v->center[i] = origin[i] + 0.5f * ( mins[i] + maxs[i] );
		v->halfSize[i] = 0.5f * ( maxs[i] - mins[i] );
	}
	AxisClear( v->axis );
	if ( es->eFlags & EF_DEAD ) {
		return count;
	}

	eye = es->origin2[2];
	if ( eye > maxs[2] ) {
		v = &vols[count++];
		top = eye + BG_HIT_HEAD_ALLOWANCE;
		v->type = HITVOL_OVER_BOX;
		v->center[0] = origin[0] + 0.5f * ( mins[0] + maxs[0] );
		v->center[1] = origin[1] + 0.5f * ( mins[1] + maxs[1] );
		v->center[2] = origin[2] + 0.5f * ( maxs[2] + top );
		v->halfSize[0] = 0.5f * ( maxs[0] - mins[0] );
		v->halfSize[1] = 0.5f * ( maxs[1] - mins[1] );
		v->halfSize[2] = 0.5f * ( top - maxs[2] );
		AxisClear( v->axis );
	}

	if ( es->eFlags & EF_PRONE ) {
		v = &vols[count++];
		v->type = HITVOL_PRONE_BODY;
		VectorSet( angles, es->origin2[1], es->origin2[0], 0 );
		AngleVectors( angles, forward, right, up );
		VectorNegate( forward, v->axis[0] );	// toward the feet
		VectorCopy( right, v->axis[1] );
		VectorCopy( up, v->axis[2] );
		VectorCopy( origin, v->center );
		VectorMA( v->center, 0.5f * BG_PRONE_FEET_DIST, v->axis[0], v->center );
		VectorMA( v->center, 0.5f * ( MINS_Z + PRONE_HEIGHT ), v->axis[2], v->center );
		VectorSet( v->halfSize, 0.5f * BG_PRONE_FEET_DIST, BG_PRONE_TRACE_HALF,
			0.5f * ( PRONE_HEIGHT - MINS_Z ) );
	}
	return count;
}

/*
================
BG_HitVolumeTrace

Where the segment start->end first enters a volume: the slab test along each
of its axes. The fraction is along the whole segment, as a trace's is. A
segment that only touches a face hits it. A start inside (or on a face) is
the engine's box trace's (CM_TraceThroughBrush): a segment that gets out
passes (the engine marks it startsolid and keeps the fraction), so a bullet
fired from inside someone's volume, as from a muzzle over another prone
player's legs, goes on; only a segment that never leaves is a hit, at 0, its
normal facing back along it (zero for a segment with no length), as the
engine's all-solid trace is.
================
*/
qboolean BG_HitVolumeTrace( const bgHitVolume_t *vol, const vec3_t start, const vec3_t end,
	float *fraction, vec3_t normal ) {
	vec3_t	rel, dir;
	float	enter, leave, s, d, t0, t1, t, side, enterSide;
	int		i, face;

	VectorSubtract( start, vol->center, rel );
	VectorSubtract( end, start, dir );
	enter = 0.0f;
	leave = 1.0f;
	face = -1;
	enterSide = 0.0f;
	for ( i = 0; i < 3; i++ ) {
		s = DotProduct( rel, vol->axis[i] );
		d = DotProduct( dir, vol->axis[i] );
		if ( d == 0.0f ) {
			// parallel to this pair of faces: inside them or never
			if ( s < -vol->halfSize[i] || s > vol->halfSize[i] ) {
				return qfalse;
			}
			continue;
		}
		t0 = ( -vol->halfSize[i] - s ) / d;
		t1 = ( vol->halfSize[i] - s ) / d;
		side = -1.0f;	// moving along the axis, it comes in through the near (-) face
		if ( t0 > t1 ) {
			t = t0; t0 = t1; t1 = t;
			side = 1.0f;
		}
		if ( t0 > enter ) {
			enter = t0;
			face = i;
			enterSide = side;
		}
		if ( t1 < leave ) {
			leave = t1;
		}
		if ( enter > leave ) {
			return qfalse;
		}
	}
	if ( face < 0 && leave < 1.0f ) {
		return qfalse;	// it starts inside and gets out: the engine's startsolid, no hit
	}
	*fraction = enter;
	if ( face >= 0 ) {
		VectorScale( vol->axis[face], enterSide, normal );
	} else {
		VectorNegate( dir, normal );
		VectorNormalize( normal );
	}
	return qtrue;
}
