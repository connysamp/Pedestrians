#if defined _pedestrians_included
	#endinput
#endif
#define _pedestrians_included

#include <a_samp>

#define MAX_STREAM_ACTORS 500

new StreamActors[MAX_STREAM_ACTORS];
new bool:ActorInUse[MAX_STREAM_ACTORS];
new Float:LastActorAngle[1000];
new ActorFallenTick[MAX_STREAM_ACTORS];
new ActorWalkStyle[MAX_STREAM_ACTORS];
new ActorTargetNode[MAX_STREAM_ACTORS];

forward OnPedestrianHitByVeh(playerid, pedestrianid);

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

static const NameAnimations[][] =
{
    "WALK_player", "WALK_civi", "WALK_gang1", "WALK_gang2",
    "WALK_old", "WALK_fatold", "WALK_fat", "WOMAN_walkold",
    "WOMAN_walkfatold", "WALK_shuffle", "WOMAN_walknorm",
    "WOMAN_walkbusy", "WOMAN_walkpro", "WOMAN_walksexy",
    "WALK_drunk", "WALK_Wuzi"
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

new StreamTimer;
new bool:BenchmarkMode = false;

// Native declarations for the C++ plugin
native Path_GetNodesInRange(Float:x, Float:y, Float:z, Float:radius, result[], max_size);
native Path_GetNodePos(node_id, &Float:x, &Float:y, &Float:z);
native Path_SetActorWalk(actorid, start_node_id, Float:speed);
native Path_StopActorWalk(actorid);
native Path_ProcessActors(actors[], Float:xs[], Float:ys[], Float:zs[], Float:angles[], &count, max_size);
native Path_SetActorSpeed(actorid, Float:speed);
native Path_IgnoreNode(nodeid);
native Path_GetActorNode(actorid);
forward OnPedestrianGetDamage(pedestrianid, playerid, type);

stock GetClosestPedestrianID(playerid)
{
	if(!IsPlayerConnected(playerid)) return -1;
	new Float:px, Float:py, Float:pz;
	GetPlayerPos(playerid, px, py, pz);
	
	new closest = -1;
	new Float:minDist = 99999.0;
	
	for(new a=0; a<MAX_STREAM_ACTORS; a++)
	{
		if(!ActorInUse[a]) continue;
		new Float:ax, Float:ay, Float:az;
		GetActorPos(StreamActors[a], ax, ay, az);
		new Float:dist = floatsqroot(floatpower(ax - px, 2.0) + floatpower(ay - py, 2.0) + floatpower(az - pz, 2.0));
		if(dist < minDist) {
			minDist = dist;
			closest = StreamActors[a];
		}
	}
	return closest;
}

stock StopPedestrian(pedestrianid)
{
	Path_SetActorSpeed(pedestrianid, 0.0);
}

stock ResumePedestrian(pedestrianid)
{
	Path_SetActorSpeed(pedestrianid, 1.2);
}

stock IgnorePedestrianNode(nodeid)
{
	Path_IgnoreNode(nodeid);
}

stock GetPedestrianNode(pedestrianid)
{
	return Path_GetActorNode(pedestrianid);
}

// Helper function removed in favor of 2D arrays
stock ApplyPedestrianAnim(pedestrianid, animlib[], animname[], Float:fDelta, loop, lockx, locky, freeze, time)
{
	ApplyActorAnimation(pedestrianid, animlib, animname, fDelta, loop, lockx, locky, freeze, time);
}

stock InitPedestrians()
{
	for(new i=0; i<MAX_STREAM_ACTORS; i++)
	{
		// Spawn far away under the map
		StreamActors[i] = CreateActor(101, 0.0, 0.0, -1000.0, 0.0);
		SetActorInvulnerable(StreamActors[i], false);
		ActorInUse[i] = false;
	}
	
	// Timer principali
	SetTimer("ActorPhysicsTimer", 80, true);
	StreamTimer = SetTimer("StreamActorsTimer", 1000, true);
	
	print("[Pedestrians] Initialized in PAWN.");
}

forward StreamActorsTimer();
public StreamActorsTimer()
{
	// 1. Despawn actors that are too far from ALL players
	for(new a=0; a<MAX_STREAM_ACTORS; a++)
	{
		if(!ActorInUse[a]) continue;
		
		new bool:nearPlayer = false;
		for(new p=0; p<MAX_PLAYERS; p++)
		{
			if(!IsPlayerConnected(p)) continue;
			new Float:px, Float:py, Float:pz;
			GetPlayerPos(p, px, py, pz);
			
			new Float:ax, Float:ay, Float:az;
			GetActorPos(StreamActors[a], ax, ay, az);
			
			new Float:dx = ax - px;
			new Float:dy = ay - py;
			if(floatpower(dx, 2.0) + floatpower(dy, 2.0) < 220.0 * 220.0) { // Keep them active up to 220m
				nearPlayer = true;
				break;
			}
		}
		
		if(!nearPlayer) {
			SetActorPos(StreamActors[a], 0.0, 0.0, -1000.0);
			Path_StopActorWalk(StreamActors[a]);
			ActorInUse[a] = false;
		}
	}
	
	// 2. Spawn actors near players if density is low
	for(new i=0; i<MAX_PLAYERS; i++)
	{
		if(!IsPlayerConnected(i)) continue;
		if(GetPlayerState(i) == PLAYER_STATE_NONE || GetPlayerState(i) == PLAYER_STATE_WASTED || GetPlayerState(i) == PLAYER_STATE_SPECTATING) continue;
		
		new Float:px, Float:py, Float:pz;
		GetPlayerPos(i, px, py, pz);
		
		new actorsNear = 0;
		for(new a=0; a<MAX_STREAM_ACTORS; a++)
		{
			if(!ActorInUse[a]) continue;
			new Float:ax, Float:ay, Float:az;
			GetActorPos(StreamActors[a], ax, ay, az);
			new Float:dx = ax - px;
			new Float:dy = ay - py;
			if(floatpower(dx, 2.0) + floatpower(dy, 2.0) < 120.0 * 120.0) actorsNear++; // Density checked in 120m radius
		}
		
		new targetActors = 20;
		
		if (actorsNear < targetActors)
		{
			// Predictive Spawning: project the search center forward based on velocity
			new Float:vx, Float:vy, Float:vz;
			GetPlayerVelocity(i, vx, vy, vz);
			
			new Float:searchX = px;
			new Float:searchY = py;
			new Float:speed = floatsqroot(vx*vx + vy*vy);
			
			// If moving fast enough to predict
			if (speed > 0.05)
			{
				new Float:aheadMeters = speed * 250.0; // Math projection factor
				if (aheadMeters > 150.0) aheadMeters = 150.0; // Cap to 150m ahead
				
				// Normalize direction and multiply by distance
				new Float:dirX = vx / speed;
				new Float:dirY = vy / speed;
				
				searchX += dirX * aheadMeters;
				searchY += dirY * aheadMeters;
			}
			
			new nodes[200];
			new count = Path_GetNodesInRange(searchX, searchY, pz, 300.0, nodes, sizeof(nodes));
			
			for(new n=0; n<count && actorsNear < targetActors; n++)
			{
				new Float:nx, Float:ny, Float:nz;
				if(Path_GetNodePos(nodes[n], nx, ny, nz))
				{
					// Distance check so we don't spawn them right in the player's face
					new Float:dist = floatsqroot(floatpower(nx - px, 2.0) + floatpower(ny - py, 2.0));
					if (dist < 35.0) continue; // Spawna appena fuori dal campo visivo immediato
					
					// Anti-ammucchiamento: controlliamo se c'è già un pedone vicinissimo a questo nodo
					new bool:nodeOccupied = false;
					for(new a=0; a<MAX_STREAM_ACTORS; a++) {
						if(ActorInUse[a]) {
							new Float:ax, Float:ay, Float:az;
							GetActorPos(StreamActors[a], ax, ay, az);
							if(floatpower(nx - ax, 2.0) + floatpower(ny - ay, 2.0) < 25.0 * 25.0) {
								nodeOccupied = true;
								break;
							}
						}
					}
					if(nodeOccupied) continue; // Salta questo nodo, è già pieno!
					
					new slot = -1;
					for(new a=0; a<MAX_STREAM_ACTORS; a++) {
						if(!ActorInUse[a]) {
							slot = a;
							break;
						}
					}
					if(slot == -1) break;
					
					new skin, walkStyle;
					if(random(2) == 0) {
						new idx = random(sizeof(MaleSkins));
						skin = MaleSkins[idx][0];
						walkStyle = MaleSkins[idx][1];
					} else {
						new idx = random(sizeof(FemaleSkins));
						skin = FemaleSkins[idx][0];
						walkStyle = FemaleSkins[idx][1];
					}
					
					DestroyActor(StreamActors[slot]);
					StreamActors[slot] = CreateActor(skin, nx, ny, nz + 1.0, 0.0);
					SetActorInvulnerable(StreamActors[slot], false);
					SetActorVirtualWorld(StreamActors[slot], GetPlayerVirtualWorld(i));
					
					new walkAnim[32];
					format(walkAnim, sizeof(walkAnim), "%s", NameAnimations[walkStyle]);
					ApplyActorAnimation(StreamActors[slot], "PED", walkAnim, 4.1, 1, 1, 1, 0, 0);
					
					ActorInUse[slot] = true;
					ActorWalkStyle[slot] = walkStyle;
					ActorFallenTick[slot] = 0;
					
					Path_SetActorWalk(StreamActors[slot], nodes[n], 1.2);
					actorsNear++;
				}
			}
		}
	}
	return 1;
}

forward ActorPhysicsTimer();
public ActorPhysicsTimer()
{
	new tick = GetTickCount();
	
	// Check vehicle collisions and fallen recovery
	for(new a=0; a<MAX_STREAM_ACTORS; a++) {
		if(!ActorInUse[a]) continue;
		
		if (ActorFallenTick[a] > 0) {
			if (tick - ActorFallenTick[a] > 2500) { // 2.5 seconds on the ground
				ActorFallenTick[a] = 0;
				ApplyActorAnimation(StreamActors[a], "PED", "getup_front", 4.1, 0, 0, 0, 0, 0); // Alzati
				SetTimerEx("ResumeActorWalk", 1500, false, "i", a); // Riprendi a camminare quasi subito
			}
			continue;
		}
		
		new Float:ax, Float:ay, Float:az;
		GetActorPos(StreamActors[a], ax, ay, az);
		
		for(new i=0; i<MAX_PLAYERS; i++) {
			if(!IsPlayerConnected(i) || GetPlayerState(i) != PLAYER_STATE_DRIVER) continue;
			
			new veh = GetPlayerVehicleID(i);
			new Float:vx, Float:vy, Float:vz;
			GetVehicleVelocity(veh, vx, vy, vz);
			new Float:speed = floatsqroot(vx*vx + vy*vy);
			
			if (speed > 0.1) // Moving fast enough
			{
				new Float:px, Float:py, Float:pz;
				GetVehiclePos(veh, px, py, pz);
				
				if (floatpower(ax - px, 2.0) + floatpower(ay - py, 2.0) < 3.5 * 3.5) { // Hit by vehicle
					ActorTargetNode[a] = Path_GetActorNode(StreamActors[a]); // Salva rotta originale
					Path_StopActorWalk(StreamActors[a]); // Ferma in C++
					ApplyActorAnimation(StreamActors[a], "PED", "KO_skid_front", 4.1, 0, 1, 1, 1, 0); // Cadi
					ActorFallenTick[a] = tick;
					
					CallLocalFunction("OnPedestrianHitByVeh", "ii", i, StreamActors[a]);
					break; // Salta altri veicoli
				}
			}
		}
	}
	
	new actors[MAX_STREAM_ACTORS];
	new Float:xs[MAX_STREAM_ACTORS];
	new Float:ys[MAX_STREAM_ACTORS];
	new Float:zs[MAX_STREAM_ACTORS];
	new Float:angles[MAX_STREAM_ACTORS];
	new count;
	
	Path_ProcessActors(actors, xs, ys, zs, angles, count, sizeof(actors));
	
	for(new i=0; i<count; i++)
	{
		SetActorPos(actors[i], xs[i], ys[i], zs[i] + 1.0);
		if(floatabs(angles[i] - LastActorAngle[actors[i]]) > 0.5)
		{
			SetActorFacingAngle(actors[i], angles[i]);
			LastActorAngle[actors[i]] = angles[i];
		}
	}
	
	if (BenchmarkMode)
	{
		new time = GetTickCount() - tick;
		if (time > 0) printf("[Pedestrians Benchmark] Processed %d actors in %d ms.", count, time);
	}
	return 1;
}

forward ResumeActorWalk(slot);
public ResumeActorWalk(slot)
{
	if(!ActorInUse[slot] || ActorFallenTick[slot] > 0) return;
	
	new Float:ax, Float:ay, Float:az;
	GetActorPos(StreamActors[slot], ax, ay, az);
	
	if(ActorTargetNode[slot] != 0) {
		Path_SetActorWalk(StreamActors[slot], ActorTargetNode[slot], 1.2);
	}
	
	new walkAnim[32];
	format(walkAnim, sizeof(walkAnim), "%s", NameAnimations[ActorWalkStyle[slot]]);
	ApplyActorAnimation(StreamActors[slot], "PED", walkAnim, 4.1, 1, 1, 1, 0, 0);
}

public OnPlayerWeaponShot(playerid, weaponid, hittype, hitid, Float:fX, Float:fY, Float:fZ)
{
	// Panic AI logic
	new Float:px, Float:py, Float:pz;
	GetPlayerPos(playerid, px, py, pz);
	
	for(new a=0; a<MAX_STREAM_ACTORS; a++)
	{
		if(!ActorInUse[a]) continue;
		new Float:ax, Float:ay, Float:az;
		GetActorPos(StreamActors[a], ax, ay, az);
		
		new Float:dx = ax - px;
		new Float:dy = ay - py;
		if(floatpower(dx, 2.0) + floatpower(dy, 2.0) < 50.0 * 50.0) // Within 50m of gunshot
		{
			ApplyActorAnimation(StreamActors[a], "PED", "sprint_panic", 4.1, 1, 1, 1, 0, 0);
			Path_SetActorSpeed(StreamActors[a], 5.0); // Fast sprint speed
		}
	}
	
	#if defined ped_OnPlayerWeaponShot
		return ped_OnPlayerWeaponShot(playerid, weaponid, hittype, hitid, fX, fY, fZ);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerWeaponShot
	#undef OnPlayerWeaponShot
#else
	#define _ALS_OnPlayerWeaponShot
#endif
#define OnPlayerWeaponShot ped_OnPlayerWeaponShot
#if defined ped_OnPlayerWeaponShot
	forward ped_OnPlayerWeaponShot(playerid, weaponid, hittype, hitid, Float:fX, Float:fY, Float:fZ);
#endif

public OnPlayerGiveDamageActor(playerid, damaged_actorid, Float:amount, weaponid, bodypart)
{
	new bool:isPed = false;
	for(new a=0; a<MAX_STREAM_ACTORS; a++) {
		if(ActorInUse[a] && StreamActors[a] == damaged_actorid) {
			isPed = true;
			break;
		}
	}
	
	if(isPed) {
		new type = (weaponid == 0) ? 0 : 1;
		CallLocalFunction("OnPedestrianGetDamage", "iii", damaged_actorid, playerid, type);
	}
	
	#if defined ped_OnPlayerGiveDamageActor
		return ped_OnPlayerGiveDamageActor(playerid, damaged_actorid, amount, weaponid, bodypart);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerGiveDamageActor
	#undef OnPlayerGiveDamageActor
#else
	#define _ALS_OnPlayerGiveDamageActor
#endif
#define OnPlayerGiveDamageActor ped_OnPlayerGiveDamageActor
#if defined ped_OnPlayerGiveDamageActor
	forward ped_OnPlayerGiveDamageActor(playerid, damaged_actorid, Float:amount, weaponid, bodypart);
#endif

new PlayerTargetedPedestrian[MAX_PLAYERS] = {-1, ...};
forward OnPlayerAimPedestrian(playerid, pedestrianid);

public OnPlayerUpdate(playerid)
{
	new targetActor = GetPlayerTargetActor(playerid);
	if(targetActor != INVALID_ACTOR_ID)
	{
		new bool:isPed = false;
		for(new a=0; a<MAX_STREAM_ACTORS; a++) {
			if(ActorInUse[a] && StreamActors[a] == targetActor) {
				isPed = true;
				break;
			}
		}
		
		if(isPed)
		{
			if(PlayerTargetedPedestrian[playerid] != targetActor)
			{
				PlayerTargetedPedestrian[playerid] = targetActor;
				CallLocalFunction("OnPlayerAimPedestrian", "ii", playerid, targetActor);
			}
		}
		else
		{
			PlayerTargetedPedestrian[playerid] = -1;
		}
	}
	else
	{
		if (PlayerTargetedPedestrian[playerid] != -1) {
			PlayerTargetedPedestrian[playerid] = -1;
		}
	}

	#if defined ped_OnPlayerUpdate
		return ped_OnPlayerUpdate(playerid);
	#else
		return 1;
	#endif
}
#if defined _ALS_OnPlayerUpdate
	#undef OnPlayerUpdate
#else
	#define _ALS_OnPlayerUpdate
#endif
#define OnPlayerUpdate ped_OnPlayerUpdate
#if defined ped_OnPlayerUpdate
	forward ped_OnPlayerUpdate(playerid);
#endif
