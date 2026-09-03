#if defined _pedestrians_included
	#endinput
#endif
#define _pedestrians_included

#include <a_samp>

#define PED_STATE_WALKING 1
#define PED_STATE_PANIC 2
#define PED_STATE_FALLEN 3
#define PED_STATE_STOPPED 4
#define PED_STATE_DEAD 5

#define DEFAULT_PED_MAX_POOL            500
#define DEFAULT_PED_MAX_PER_PLAYER      12
#define DEFAULT_PED_SPAWN_DENSITY       1.0
#define DEFAULT_PED_STREAM_DISTANCE     220.0

#if !defined PED_USE_COLANDREAS
	#define PED_USE_COLANDREAS true
#endif

#if !defined PED_MAX_POOL
	#define PED_MAX_POOL DEFAULT_PED_MAX_POOL
#endif

#if !defined PED_MAX_PER_PLAYER
	#define PED_MAX_PER_PLAYER DEFAULT_PED_MAX_PER_PLAYER
#endif

#if !defined PED_SPAWN_DENSITY
	#define PED_SPAWN_DENSITY DEFAULT_PED_SPAWN_DENSITY
#endif

#if !defined PED_STREAM_DISTANCE
	#define PED_STREAM_DISTANCE DEFAULT_PED_STREAM_DISTANCE
#endif

native Path_SetUseColAndreas(bool:use);
native TogglePedInInterior(bool:toggle);
native Path_SetTime(hour, minute);

native Path_SetMaxActorsInPool(count);
native Path_GetMaxActorsInPool();

native Path_SetMaxActorsPerPlayer(count);
native Path_GetMaxActorsPerPlayer();

native Path_SetSpawnDensity(Float:density);
native Float:Path_GetSpawnDensity();

native Path_SetStreamingDistance(Float:distance);
native Float:Path_GetStreamingDistance();

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
#if PED_USE_COLANDREAS
	if(!Path_SetUseColAndreas(true))
	{
		print("[Pedestrians] WARNING: PED_USE_COLANDREAS is set to true but ColAndreas plugin is missing.");
		print("[Pedestrians] Falling back to default coordinate calculation method.");
	}
#else
	Path_SetUseColAndreas(false);
#endif
	
	TogglePedInInterior(false);
	Path_SetMaxActorsInPool(PED_MAX_POOL);
	Path_SetMaxActorsPerPlayer(PED_MAX_PER_PLAYER);
	Path_SetSpawnDensity(PED_SPAWN_DENSITY);
	Path_SetStreamingDistance(PED_STREAM_DISTANCE);
}

stock IgnorePedestrianNode(nodeid)
{
	Path_IgnoreNode(nodeid);
}
