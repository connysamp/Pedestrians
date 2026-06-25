#include <a_samp>
#include <pedestrians>

#pragma dynamic 20000

main()
{
	print(" Pedestrians Test Gamemode Loaded");
}

public OnGameModeInit()
{
	InitPedestrians();
	
	// Ignoring pedestrian nodes (If u got smthing mapped)
	IgnorePedestrianNode(3016528);
	IgnorePedestrianNode(3016545);
	
	SetGameModeText("Pedestrians");
	AddPlayerClass(0, 1958.3783, 1343.1572, 15.3746, 269.1425, 0, 0, 0, 0, 0, 0);
	return 1;
}

public OnPlayerKeyStateChange(playerid, newkeys, oldkeys)
{
	return 1;
}

public OnPedestrianDeath(pedestrianid, killerid, weaponid)
{
    new str[128];
    format(str, sizeof(str), "%d killed Pedestrian ID %d with Gun ID %d!", killerid, pedestrianid, weaponid);
    SendClientMessage(killerid, -1, str);

    GivePlayerMoney(killerid, 500);
    
    return 1;
}

public OnPedestrianGetDamage(pedestrianid, playerid, type)
{
	StopPedestrian(pedestrianid);
	
	new str[128];
	new nodeid = GetPedestrianNode(pedestrianid);
	format(str, sizeof(str), "You shot pedestrian ID %d, type: %d (Target Node: %d)", pedestrianid, type, nodeid);
	SendClientMessage(playerid, -1, str);
	
	ApplyPedestrianAnim(pedestrianid, "PED", "IDLE_chat", 4.1, 1, 0, 0, 0, 0);
	return 1;
}

forward OnPlayerAimPedestrian(playerid, pedestrianid);
public OnPlayerAimPedestrian(playerid, pedestrianid)
{
	StopPedestrian(pedestrianid);
	ApplyPedestrianAnim(pedestrianid, "SHOP", "SHP_Rob_HandsUp", 4.1, 0, 0, 0, 1, 0);
	
	new str[128];
	format(str, sizeof(str), "You aimed at pedestrian ID %d! They put their hands up.", pedestrianid);
	SendClientMessage(playerid, -1, str);
	return 1;
}

public OnPedestrianHitByVeh(playerid, pedestrianid)
{
	new str[128];
	format(str, sizeof(str), "You ran over pedestrian ID %d!", pedestrianid);
	SendClientMessage(playerid, -1, str);
	return 1;
}

public OnPlayerConnect(playerid)
{
	EnablePlayerCameraTarget(playerid, 1);
	SpawnPlayer(playerid);
	return 1;
}

public OnPlayerSpawn(playerid)
{
	SetPlayerPos(playerid, 1270.84, 178.07, 19.51);
	return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
	if(strcmp(cmdtext, "/vspawn", true) == 0)
	{
		new Float:x, Float:y, Float:z, Float:a;
		GetPlayerPos(playerid, x, y, z);
		GetPlayerFacingAngle(playerid, a);
		
		new vehid = CreateVehicle(411, x, y, z, a, 1, 1, -1);
		PutPlayerInVehicle(playerid, vehid, 0);
		
		SendClientMessage(playerid, -1, "Infernus spawned!");
		return 1;
	}
	
	return 0;
}
