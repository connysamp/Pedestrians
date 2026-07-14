#if defined _pedestrians_included
	#endinput
#endif
#define _pedestrians_included

#include <a_samp>

enum
{
	WALK_PLAYER = 0,
	WALK_CIVI,
	WALK_GANG1,
	WALK_GANG2,
	WALK_OLD,
	WALK_FATOLD,
	WALK_FAT,
	WOMAN_WALKOLD,
	WOMAN_WALKFATOLD,
	WALK_SHUFFLE,
	WOMAN_WALKNORM,
	WOMAN_WALKBUSY,
	WOMAN_WALKPRO,
	WOMAN_WALKSEXY,
	WALK_DRUNK,
	WALK_WUZI
};

new MaleSkins[][2] = {
	{1, WALK_OLD}, {2, WALK_CIVI}, {7, WALK_CIVI}, {14, WALK_CIVI}, {15, WALK_CIVI},
	{18, WALK_GANG1}, {20, WALK_GANG2}, {21, WALK_GANG1}, {22, WALK_GANG2}, {23, WALK_CIVI},
	{24, WALK_FAT}, {25, WALK_GANG1}, {26, WALK_CIVI}, {27, WALK_CIVI}, {28, WALK_GANG2},
	{29, WALK_GANG1}, {30, WALK_GANG2}, {32, WALK_FAT}, {33, WALK_CIVI}, {34, WALK_CIVI}, {35, WALK_OLD}
};

new FemaleSkins[][2] = {
	{9, WOMAN_WALKNORM}, {12, WOMAN_WALKBUSY}, {13, WOMAN_WALKNORM}, {31, WOMAN_WALKNORM},
	{40, WOMAN_WALKNORM}, {41, WOMAN_WALKNORM}, {54, WOMAN_WALKNORM}, {55, WOMAN_WALKBUSY},
	{56, WOMAN_WALKNORM}, {64, WOMAN_WALKPRO}, {65, WOMAN_WALKNORM}, {69, WOMAN_WALKNORM},
	{75, WOMAN_WALKPRO}, {76, WOMAN_WALKBUSY}, {77, WOMAN_WALKOLD}, {85, WOMAN_WALKSEXY},
	{87, WOMAN_WALKBUSY}, {88, WOMAN_WALKOLD}, {89, WOMAN_WALKOLD}, {90, WOMAN_WALKBUSY},
	{92, WOMAN_WALKSEXY}
};

#define PED_STATE_WALKING 1
#define PED_STATE_PANIC 2
#define PED_STATE_FALLEN 3
#define PED_STATE_STOPPED 4
#define PED_STATE_DEAD 5

native Path_AddMaleSkin(skinid, walkstyle);
native Path_AddFemaleSkin(skinid, walkstyle);
native Path_IgnoreNode(nodeid);

native PrintIgnoredNodesCount();
native SetPedestrianHealth(actorid, Float:health);
native Float:GetPedestrianHealth(actorid);
native SetPedestrianSpeed(actorid, Float:speed);
native MakePedestrianPanic(actorid, duration_ms);
native IsPedestrianPanicking(actorid);
native SetPedestrianSkin(actorid, skinid);
native GetPedestrianNode(actorid);
native GetClosestPedestrianID(playerid);
native GetPedestrianSex(actorid);
native StopPedestrian(actorid);
native ResumePedestrian(actorid);
native ApplyPedestrianAnim(actorid, animlib[], animname[], Float:fDelta, loop, lockx, locky, freeze, time);
native IsPlayerInFrontOfPed(playerid, actorid);

forward OnPedestrianGetDamage(pedestrianid, playerid, type);
forward OnPedestrianHitByVeh(playerid, vehicleid, actorid);
forward OnPlayerAimPedestrian(playerid, pedestrianid);
forward OnPedestrianStateChange(actorid, oldstate, newstate);
forward OnPedestrianDeath(actorid, killerid, reason);

stock InitPedestrians()
{
	for(new i=0; i<sizeof(MaleSkins); i++) {
		Path_AddMaleSkin(MaleSkins[i][0], MaleSkins[i][1]);
	}
	for(new i=0; i<sizeof(FemaleSkins); i++) {
		Path_AddFemaleSkin(FemaleSkins[i][0], FemaleSkins[i][1]);
	}
	print("[Pedestrians] PAWN Configuration loaded.");
	PrintIgnoredNodesCount();
}

stock IgnorePedestrianNode(nodeid)
{
	Path_IgnoreNode(nodeid);
}
