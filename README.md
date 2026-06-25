# SA-MP Pedestrians & ActorPaths

A comprehensive system for SA-MP (San Andreas Multiplayer) that populates your server with intelligent pedestrians. It utilizes a highly optimized C++ plugin combined with a PAWN script (pedestrians.inc) to handle pathfinding, spawning, and interaction.

## Why Actors Instead of NPCs?
The biggest advantage of this system is that it uses SA-MP "Actors" instead of standard SA-MP "NPCs". 
* **Zero Player Slots Consumed:** Traditional NPCs take up valuable server player slots and require individual connection instances. Actors do not use player slots.
* **Extreme Performance & Scalability:** Standard NPCs require individual PAWN scripts and pre-recorded playback files, putting heavy strain on the server if you want hundreds of them. This system uses a highly optimized C++ plugin to parse thousands of nodes and dynamically stream Actors only around active players (Global Pool System).
* **Dynamic Interaction:** Traditional NPCs follow static paths. Our Actors react dynamically to their environment: they can raise their hands if aimed at, stop walking, and sprint away in panic if they take damage or hear gunshots.

## Core Features
* **Global Pool Spawning:** Pedestrians are created only when players are nearby and are intelligently recycled to save memory.
* **C++ Pathfinding:** Extreme fluidity and performance. The C++ plugin loads thousands of nodes (pedpaths.json) and moves pedestrians in the background.
* **Custom Walk Styles:** Each pedestrian is assigned the correct walking animation based on their skin ID (e.g., gangsters, elderly, women, civilians). This is fully configurable via 2D arrays.
* **Interaction and Panic AI:** Pedestrians react when shot or punched, and can be targeted with a weapon (OnPlayerAimPedestrian).
* **Ignored Nodes:** Ability to exclude specific nodes at server load to prevent pedestrians from walking onto custom mapped areas.
* **Network Optimization:** Angle caching and a balanced physical timer frequency prevent the "ackslimit" warning typical of heavy NPC servers.
* **Predictive Spawning & Anti-Clustering:** The system calculates player speed and direction using vector math to dynamically spawn pedestrians up to 150m ahead on their path, ensuring towns are populated naturally without nodes being clustered together.
* **Cross-Platform:** The C++ plugin compiles to both `.dll` for Windows and `.so` (32-bit) for Linux SA-MP servers.
* **open.mp Compatible:** The project does not rely on memory hacking or hooking. It uses standard AMX natives, making it 100% fully compatible with open.mp (Open Multiplayer) right out of the box.

---

## Installation

1. Place "pedestrians.dll" inside the "plugins/" directory and append "pedestrians" to your "server.cfg".
2. Add "ackslimit 10000" to your "server.cfg" to prevent network warnings caused by rapid actor position updates.
3. Place "pedpaths.json" inside the "scriptfiles/" directory.
4. Place "pedestrians.inc" inside the "pawno/include/" directory.
5. Open your gamemode and include the file:
   ```pawn
   #include <pedestrians>
   ```

---

## PAWN Functions

### InitPedestrians()
Initializes the system. Must be called under OnGameModeInit().
```pawn
public OnGameModeInit() {
    InitPedestrians();
    return 1;
}
```

### IgnorePedestrianNode(nodeid)
Ignores a specific node so pedestrians will never walk on it. Great if you have custom maps placed on existing sidewalks. Must be called under OnGameModeInit().
```pawn
IgnorePedestrianNode(3016528);
IgnorePedestrianNode(3016545);
```

### GetPedestrianNode(pedestrianid)
Returns the ID of the destination node the pedestrian is currently walking towards.

### StopPedestrian(pedestrianid)
Immediately stops the pedestrian (sets speed to 0.0).

### ResumePedestrian(pedestrianid)
Resumes the pedestrian's walk towards their target node (restores speed to 1.2).

### ApplyPedestrianAnim(pedestrianid, animlib[], animname[], Float:fDelta, loop, lockx, locky, freeze, time)
Easily applies a specific animation to the pedestrian (wrapper for ApplyActorAnimation).

### GetClosestPedestrianID(playerid)
Returns the ID of the pedestrian (actor) closest to a specific player.

---

## Callbacks

### OnPedestrianGetDamage(pedestrianid, playerid, type)
Called when a player hits a pedestrian.
* type 0: Melee / Fist damage
* type 1: Firearm damage

**Usage Example:**
```pawn
public OnPedestrianGetDamage(pedestrianid, playerid, type)
{
    StopPedestrian(pedestrianid);
    
    new str[128];
    new nodeid = GetPedestrianNode(pedestrianid);
    format(str, sizeof(str), "You hit pedestrian ID %d, type: %d (Target Node: %d)", pedestrianid, type, nodeid);
    SendClientMessage(playerid, -1, str);
    
    // Make them react and talk
    ApplyPedestrianAnim(pedestrianid, "PED", "IDLE_chat", 4.1, 1, 0, 0, 0, 0);
    return 1;
}
```

### OnPlayerAimPedestrian(playerid, pedestrianid)
Called (only once per aim) when the player aims at a pedestrian with a firearm.
*(Note: Requires EnablePlayerCameraTarget(playerid, 1); under OnPlayerConnect to function).*

**Usage Example:**
```pawn
public OnPlayerAimPedestrian(playerid, pedestrianid)
{
    StopPedestrian(pedestrianid);
    ApplyPedestrianAnim(pedestrianid, "SHOP", "SHP_Rob_HandsUp", 4.1, 0, 0, 0, 1, 0);
    
    SendClientMessage(playerid, -1, "You aimed at the pedestrian! They raised their hands.");
    return 1;
}
```

### OnPedestrianHitByVeh(playerid, pedestrianid)
Called when a pedestrian is run over by a player's vehicle. By default, the pedestrian will fall to the ground for a few seconds, stand back up, and seamlessly resume their original route.

**Usage Example:**
```pawn
public OnPedestrianHitByVeh(playerid, pedestrianid)
{
    new str[128];
    format(str, sizeof(str), "You ran over pedestrian ID %d!", pedestrianid);
    SendClientMessage(playerid, -1, str);
    return 1;
}
```

---

## Configuration (Skins and Walk Styles)

Inside "pedestrians.inc" you will find an enum containing all walk styles and two configurable 2D arrays (MaleSkins and FemaleSkins).
To add new skins, simply insert {SKIN_ID, WALK_STYLE} into the respective array and the pedestrian will automatically adopt that walking animation upon spawning.

```pawn
new MaleSkins[][2] = {
	{1, WALK_OLD}, 
	{2, WALK_CIVI},
	{18, WALK_GANG1},
	{24, WALK_FAT}
};
```

---

## Building from Source

This project uses CMake to generate the build files. The plugin must be compiled as a **32-bit (x86)** shared library because the SA-MP and open.mp server executables are 32-bit.

### Windows (MSVC)
1. Install [CMake](https://cmake.org/) and Visual Studio (with C++ build tools).
2. Open a terminal or PowerShell in the repository root.
3. Generate the build files:
   ```cmd
   mkdir build
   cd build
   cmake .. -A Win32
   ```
4. Compile the plugin:
   ```cmd
   cmake --build . --config Release
   ```
5. You will find `pedestrians.dll` in the `build/Release/` directory.

### Linux (GCC)
1. Install CMake and the 32-bit compiler libraries (e.g., `g++-multilib` on Ubuntu/Debian).
   ```bash
   sudo apt-get install cmake g++-multilib
   ```
2. Open a terminal in the repository root.
3. Generate the build files and compile:
   ```bash
   mkdir build_linux
   cd build_linux
   cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32
   make
   ```
4. You will find `libpedestrians.so` in the `build_linux/` directory. (Rename it to `pedestrians.so` in your server plugins).
