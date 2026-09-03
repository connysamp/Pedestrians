# SA-MP Pedestrians

A comprehensive system for SA-MP and open.mp that populates your server with ambient pedestrians. It uses a custom C++ plugin and a PAWN include (`pedestrians.inc`) to manage pedestrian movement, spawning, zone skins, and player interactions.

## Why Actors Instead of NPCs?
Using SA-MP Actors offers key advantages over traditional NPCs:
* **Zero Player Slots Consumed:** Actors do not take up server player slots or connection slots.
* **Lightweight & Scalable:** The system dynamically streams pedestrians only around active players using a fast C++ plugin.
* **Dynamic Reactions:** Pedestrians react when shot at, aimed at, or hit by vehicles.

## Core Features
* **Global Pool Spawning:** Pedestrians are dynamically created near active players and recycled to save memory.
* **Optional ColAndreas:** Support for ColAndreas to calculate terrain height.
* **GTA SA Zone Skins:** Integrates San Andreas zones (`zone_skins.json`) to spawn area-appropriate skins (countryside skins in rural areas, suits in Las Vegas, etc.).
* **Interior Toggle:** Enable or disable pedestrian spawning inside interiors.
* **Interaction AI:** Pedestrians react when shot, punched, aimed at with a gun, or hit by cars.
* **Ignored Nodes:** Exclude specific nodes at startup so pedestrians avoid custom mapped objects or areas.

---

## Installation

1. Place `pedestrians.dll` in your `plugins/` folder and add `pedestrians` to `server.cfg`.
2. Set `ackslimit 10000` in `server.cfg`.
3. Copy `pedpaths.json` and `zone_skins.json` into your `scriptfiles/` folder.
4. Copy `pedestrians.inc` into `pawno/include/`.
5. Include it in your gamemode:
   ```pawn
   #include <pedestrians>
   ```

---

## PAWN Configuration

```pawn
#define PED_USE_COLANDREAS true
```
* Set to `true` to enable ColAndreas height calculations, or `false` to disable.

```pawn
#define PED_MAX_POOL 500
```
* Maximum total actors streamed in the server pool (Default: `500`).

```pawn
#define PED_MAX_PER_PLAYER 12
```
* Maximum pedestrians spawned around each player (Default: `12`).

```pawn
#define PED_SPAWN_DENSITY 1.0
```
* Spawn density multiplier applied to zone limits (Default: `1.0`). Set higher (e.g. `1.5`) for denser crowds or lower (e.g. `0.5`) for fewer pedestrians.

```pawn
#define PED_STREAM_DISTANCE 220.0
```
* Distance around players within which pedestrians are spawned and kept active (Default: `220.0`).

---

## PAWN Functions

```pawn
native InitPedestrians();
```
* Initializes the pedestrian system under `OnGameModeInit()` using configured `#define` defaults.

```pawn
native Path_SetMaxActorsInPool(count);
native Path_GetMaxActorsInPool();
```
* Sets or retrieves the total maximum streamed actors in the pool (Default: `500`).

```pawn
native Path_SetMaxActorsPerPlayer(count);
native Path_GetMaxActorsPerPlayer();
```
* Sets or retrieves the maximum pedestrians near any single player (Default: `12`).

```pawn
native Path_SetSpawnDensity(Float:density);
native Float:Path_GetSpawnDensity();
```
* Sets or retrieves the spawn density multiplier (Default: `1.0`).

```pawn
native Path_SetStreamingDistance(Float:distance);
native Float:Path_GetStreamingDistance();
```
* Sets or retrieves the streaming / despawn distance around players (Default: `220.0`).

```pawn
native Path_SetUseColAndreas(bool:use);
```
* Enables or disables ColAndreas height calculation at runtime.

```pawn
native TogglePedInInterior(bool:toggle);
```
* Enables or disables pedestrian spawning inside interiors.

```pawn
native IgnorePedestrianNode(nodeid);
```
* Excludes a specific node so pedestrians will not walk on it.

```pawn
native GetPedestrianNode(pedestrianid);
```
* Returns the target node ID the pedestrian is walking towards.

```pawn
native StopPedestrian(pedestrianid);
```
* Stops pedestrian movement.

```pawn
native ResumePedestrian(pedestrianid);
```
* Resumes pedestrian walking.

```pawn
native ApplyPedestrianAnim(pedestrianid, animlib[], animname[], Float:fDelta, loop, lockx, locky, freeze, time);
```
* Applies an animation to a pedestrian actor.

```pawn
native GetClosestPedestrianID(playerid);
```
* Returns the ID of the pedestrian closest to a player.

```pawn
native SetPedestrianHealth(actorid, Float:health);
```
* Sets pedestrian health.

```pawn
native Float:GetPedestrianHealth(actorid);
```
* Returns pedestrian health.

```pawn
native SetPedestrianSpeed(actorid, Float:speed);
```
* Sets pedestrian speed.

```pawn
native MakePedestrianPanic(actorid, duration_ms);
```
* Forces a pedestrian to panic and run for a duration in milliseconds.

```pawn
native IsPedestrianPanicking(actorid);
```
* Returns 1 if panicking, 0 otherwise.

```pawn
native SetPedestrianSkin(actorid, skinid);
```
* Changes a pedestrian's skin ID.

```pawn
native IsPlayerInFrontOfPed(playerid, actorid);
```
* Checks if a player is in front of the pedestrian.

---

## Callbacks

### OnPedestrianStateChange(actorid, oldstate, newstate)
Called when a pedestrian changes state:
* `1`: `PED_STATE_WALKING`
* `2`: `PED_STATE_PANIC`
* `3`: `PED_STATE_FALLEN`
* `4`: `PED_STATE_STOPPED`
* `5`: `PED_STATE_DEAD`

### OnPedestrianDeath(actorid, killerid, reason)
Called when a pedestrian dies.

### OnPedestrianGetDamage(pedestrianid, playerid, type)
Called when a pedestrian takes damage.

### OnPlayerAimPedestrian(playerid, pedestrianid)
Called when a player aims at a pedestrian with a weapon.

### OnPedestrianHitByVeh(playerid, vehicleid, actorid)
Called when a vehicle hits a pedestrian.

---

## Building from Source

### Windows (MSVC 32-bit)

```cmd
mkdir build
cd build
cmake .. -A Win32
cmake --build . --config Release
```

### Linux (GCC 32-bit)

```bash
mkdir build_linux
cd build_linux
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32
make
```
