#include <a_samp>
#include <pedestrians>

#pragma dynamic 20000

main()
{
    print("pedestrians testing server loaded");
}

public OnGameModeInit()
{
    IgnorePedestrianNode(3016528);
    IgnorePedestrianNode(3016545);
    
    InitPedestrians();
    
    SetGameModeText("testing");
    AddPlayerClass(0, 1958.3783, 1343.1572, 15.3746, 269.1425, 0, 0, 0, 0, 0, 0);
    return 1;
}

public OnPlayerConnect(playerid)
{
    EnablePlayerCameraTarget(playerid, 1);
    return 1;
}

public OnPedestrianStateChange(actorid, oldstate, newstate)
{
    new string[128];
    format(string, sizeof(string), "DEBUG: Pedestrian %d changed state from %d to %d.", actorid, oldstate, newstate);
    SendClientMessageToAll(-1, string);
    return 1;
}

public OnPedestrianDeath(actorid, killerid, reason)
{
    new string[128];
    format(string, sizeof(string), "DEBUG: Pedestrian %d died. Killer: %d, Reason: %d.", actorid, killerid, reason);
    SendClientMessageToAll(-1, string);
    return 1;
}

public OnPedestrianGetDamage(pedestrianid, playerid, type)
{
    new string[128];
    format(string, sizeof(string), "DEBUG: Pedestrian %d was hurt by player %d. Weapon type: %d.", pedestrianid, playerid, type);
    SendClientMessageToAll(-1, string);
    return 1;
}

public OnPedestrianHitByVeh(playerid, vehicleid, actorid)
{
    new string[128];
    format(string, sizeof(string), "DEBUG: Pedestrian %d was hit by vehicle %d driven by %d.", actorid, vehicleid, playerid);
    SendClientMessageToAll(-1, string);
    return 1;
}

public OnPlayerAimPedestrian(playerid, pedestrianid)
{
    new string[128];
    format(string, sizeof(string), "DEBUG: Player %d is aiming at pedestrian %d.", playerid, pedestrianid);
    SendClientMessageToAll(-1, string);
    return 1;
}

public OnPlayerCommandText(playerid, cmdtext[])
{
    if(strcmp(cmdtext, "/pedinfo", true) == 0)
    {
        new pedid = GetClosestPedestrianID(playerid);
        if(pedid == -1)
        {
            SendClientMessage(playerid, -1, "No pedestrians nearby.");
            return 1;
        }
        
        new sex = GetPedestrianSex(pedid);
        new Float:hp = GetPedestrianHealth(pedid);
        new node = GetPedestrianNode(pedid);
        new front = IsPlayerInFrontOfPed(playerid, pedid);
        new panic = IsPedestrianPanicking(pedid);
        
        new string[256];
        format(string, sizeof(string), "Pedestrian ID: %d | Sex: %d | Health: %.1f | Target Node: %d | In Front: %d | Panicking: %d", pedid, sex, hp, node, front, panic);
        SendClientMessage(playerid, -1, string);
        return 1;
    }
    
    if(strcmp(cmdtext, "/stopped", true) == 0)
    {
        new pedid = GetClosestPedestrianID(playerid);
        if(pedid != -1)
        {
            StopPedestrian(pedid);
            SendClientMessage(playerid, -1, "You stopped the closest pedestrian.");
        }
        return 1;
    }

    if(strcmp(cmdtext, "/resumeped", true) == 0)
    {
        new pedid = GetClosestPedestrianID(playerid);
        if(pedid != -1)
        {
            ResumePedestrian(pedid);
            SendClientMessage(playerid, -1, "You resumed the closest pedestrian's movement.");
        }
        return 1;
    }

    if(strcmp(cmdtext, "/killped", true) == 0)
    {
        new pedid = GetClosestPedestrianID(playerid);
        if(pedid != -1)
        {
            SetPedestrianHealth(pedid, 0.0);
            SendClientMessage(playerid, -1, "You killed the closest pedestrian.");
        }
        return 1;
    }

    if(strcmp(cmdtext, "/panicped", true) == 0)
    {
        new pedid = GetClosestPedestrianID(playerid);
        if(pedid != -1)
        {
            MakePedestrianPanic(pedid, 5000);
            SendClientMessage(playerid, -1, "You forced the closest pedestrian to panic for 5 seconds.");
        }
        return 1;
    }

    if(strcmp(cmdtext, "/speedped", true) == 0)
    {
        new pedid = GetClosestPedestrianID(playerid);
        if(pedid != -1)
        {
            SetPedestrianSpeed(pedid, 0.5);
            SendClientMessage(playerid, -1, "You slowed down the closest pedestrian to 0.5 speed.");
        }
        return 1;
    }

    if(strcmp(cmdtext, "/skinped", true) == 0)
    {
        new pedid = GetClosestPedestrianID(playerid);
        if(pedid != -1)
        {
            SetPedestrianSkin(pedid, 2);
            SendClientMessage(playerid, -1, "You changed the skin of the closest pedestrian to Maccer (ID: 2).");
        }
        return 1;
    }

    if(strcmp(cmdtext, "/healped", true) == 0)
    {
        new pedid = GetClosestPedestrianID(playerid);
        if(pedid != -1)
        {
            SetPedestrianHealth(pedid, 100.0);
            SendClientMessage(playerid, -1, "You fully healed the closest pedestrian.");
        }
        return 1;
    }
    
    return 0;
}
