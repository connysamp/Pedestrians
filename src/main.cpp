#define HAVE_STDINT_H
#include "amx/amx.h"
#include "plugincommon.h"
#include <sampgdk.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <algorithm>
#include <vector>
#include <cmath>
#include <iostream>
#include <unordered_set>
#include <chrono>
#include <map>

using json = nlohmann::json;

typedef void(*logprintf_t)(const char* format, ...);
logprintf_t logprintf;

std::unordered_set<uint32_t> g_IgnoredNodes;
extern void *pAMXFunctions;

struct PedNode {
    int id;
    int area;
    float x, y, z;
    std::vector<uint32_t> links;
};
std::vector<PedNode> g_Nodes;

enum {
    WALK_PLAYER = 0, WALK_CIVI, WALK_GANG1, WALK_GANG2,
    WALK_OLD, WALK_FATOLD, WALK_FAT, WOMAN_WALKOLD,
    WOMAN_WALKFATOLD, WALK_SHUFFLE, WOMAN_WALKNORM,
    WOMAN_WALKBUSY, WOMAN_WALKPRO, WOMAN_WALKSEXY,
    WALK_DRUNK, WALK_WUZI
};

const char* NameAnimations[] = {
    "WALK_player", "WALK_civi", "WALK_gang1", "WALK_gang2",
    "WALK_old", "WALK_fatold", "WALK_fat", "WOMAN_walkold",
    "WOMAN_walkfatold", "WALK_shuffle", "WOMAN_walknorm",
    "WOMAN_walkbusy", "WOMAN_walkpro", "WOMAN_walksexy",
    "WALK_drunk", "WALK_Wuzi"
};

std::vector<std::pair<int, int>> g_MaleSkins;
std::vector<std::pair<int, int>> g_FemaleSkins;

uint64_t GetTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct WalkingActor {
    int actorid;
    int skin;
    bool inUse;
    bool isDead;
    uint32_t currentNodeId;
    uint32_t targetNodeId;
    uint32_t previousNodeId;
    float currentX, currentY, currentZ;
    float speed;
    float currentAngle;
    float targetAngle;
    float laneOffset;
    uint64_t lastUpdateTime;
    int walkStyle;
    uint64_t fallenTick;
    float health;
    int actionId;
    uint64_t actionTick;
    uint64_t panicTick;
    int state;
};

#define PED_STATE_WALKING 1
#define PED_STATE_PANIC 2
#define PED_STATE_FALLEN 3
#define PED_STATE_STOPPED 4
#define PED_STATE_DEAD 5

#define MAX_STREAM_ACTORS 500
WalkingActor g_Actors[MAX_STREAM_ACTORS];
uint64_t g_LastStreamTick = 0;
uint64_t g_LastPhysicsTick = 0;
std::vector<AMX*> g_AMX;

void SetPedState(int a, int newState) {
    if (g_Actors[a].state != newState) {
        int oldState = g_Actors[a].state;
        g_Actors[a].state = newState;
        for (AMX* amx : g_AMX) {
            int idx;
            if (!amx_FindPublic(amx, "OnPedestrianStateChange", &idx)) {
                amx_Push(amx, newState);
                amx_Push(amx, oldState);
                amx_Push(amx, g_Actors[a].actorid);
                cell ret;
                amx_Exec(amx, &ret, idx);
            }
        }
    }
}

void TriggerPedDeath(int a, int killerid, int reason) {
    if (g_Actors[a].isDead) return;
    g_Actors[a].isDead = true;
    g_Actors[a].speed = 0.0f;
    SetPedState(a, PED_STATE_DEAD);
    sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "CRACK", "crckdeth2", 4.1f, false, true, true, true, 0);
    
    for (AMX* amx : g_AMX) {
        int idx;
        if (!amx_FindPublic(amx, "OnPedestrianDeath", &idx)) {
            amx_Push(amx, reason);
            amx_Push(amx, killerid);
            amx_Push(amx, g_Actors[a].actorid);
            cell ret;
            amx_Exec(amx, &ret, idx);
        }
    }
}

void ApplyDamage(int a, float amount, int killerid, int reason) {
    if (g_Actors[a].isDead) return;
    g_Actors[a].health -= amount;
    if (g_Actors[a].health <= 0.0f) {
        TriggerPedDeath(a, killerid, reason);
    } else {
        uint64_t now = GetTimeMs();
        if (g_Actors[a].fallenTick == 0 && g_Actors[a].actionId == 0) {
            sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", "sprint_panic", 4.1f, true, true, true, false, 0);
            g_Actors[a].speed = 5.0f;
            g_Actors[a].panicTick = now + 4000;
            SetPedState(a, PED_STATE_PANIC);
        }
    }
}

int FindNodeIndex(uint32_t id) {
    for (size_t i = 0; i < g_Nodes.size(); ++i) {
        if (g_Nodes[i].id == id) return i;
    }
    return -1;
}

int PickNextNode(int currentIdx, int previousNodeId) {
    const auto& links = g_Nodes[currentIdx].links;
    if (links.empty()) return g_Nodes[currentIdx].id;
    
    std::vector<int> validLinks;
    for (uint32_t linkId : links) {
        if ((int)linkId != previousNodeId && g_IgnoredNodes.count(linkId) == 0) {
            int linkIdx = FindNodeIndex(linkId);
            if (linkIdx != -1) validLinks.push_back(linkId);
        }
    }
    
    if (validLinks.empty()) {
        for (uint32_t linkId : links) {
            if (FindNodeIndex(linkId) != -1) return linkId;
        }
        return g_Nodes[currentIdx].id;
    }
    
    int r = rand() % validLinks.size();
    return validLinks[r];
}

bool LoadPedPaths() {
    std::ifstream file("scriptfiles/pedpaths.json");
    if (!file.is_open()) {
        logprintf("[Pedestrians] ERROR: scriptfiles/pedpaths.json not found!");
        return false;
    }
    try {
        json j; file >> j;
        for (auto& item : j) {
            PedNode node;
            node.id = item["id"]; node.area = item["area"];
            node.x = item["x"]; node.y = item["y"]; node.z = item["z"];
            if (item.contains("links") && item["links"].is_array()) {
                for (auto& l : item["links"]) node.links.push_back(l);
            }
            g_Nodes.push_back(node);
        }
        logprintf("[Pedestrians] Loaded %d pedestrian nodes from pedpaths.json", g_Nodes.size());
        return true;
    } catch (...) {
        logprintf("[Pedestrians] ERROR: pedpaths.json is corrupted or invalid format!");
        return false;
    }
}

void ApplyRightOffset(float fromX, float fromY, float toX, float toY, float offset, float& outX, float& outY) {
    float dx = toX - fromX;
    float dy = toY - fromY;
    float len = std::sqrt(dx*dx + dy*dy);
    if (len > 0.001f) {
        outX += (dy / len) * offset;
        outY += (-dx / len) * offset;
    }
}

void SetActorWalk(int slot, uint32_t startNode, float speed) {
    int startIdx = FindNodeIndex(startNode);
    if (startIdx == -1) return;

    g_Actors[slot].speed = speed;
    g_Actors[slot].currentNodeId = startNode;
    g_Actors[slot].previousNodeId = startNode;
    g_Actors[slot].currentX = g_Nodes[startIdx].x;
    g_Actors[slot].currentY = g_Nodes[startIdx].y;
    g_Actors[slot].currentZ = g_Nodes[startIdx].z;

    g_Actors[slot].targetNodeId = PickNextNode(startIdx, startNode);

    int targetIdx = FindNodeIndex(g_Actors[slot].targetNodeId);
    if (targetIdx != -1) {
        ApplyRightOffset(g_Nodes[startIdx].x, g_Nodes[startIdx].y, 
                         g_Nodes[targetIdx].x, g_Nodes[targetIdx].y, 
                         0.75f, g_Actors[slot].currentX, g_Actors[slot].currentY);

        float tx = g_Nodes[targetIdx].x;
        float ty = g_Nodes[targetIdx].y;
        ApplyRightOffset(g_Nodes[startIdx].x, g_Nodes[startIdx].y, 
                         g_Nodes[targetIdx].x, g_Nodes[targetIdx].y, 
                         0.75f, tx, ty);

        float dx = tx - g_Actors[slot].currentX;
        float ty_diff = ty - g_Actors[slot].currentY;
        g_Actors[slot].targetAngle = std::atan2(-dx, ty_diff) * (180.0f / 3.14159265f);
        g_Actors[slot].currentAngle = g_Actors[slot].targetAngle;
    }
}

void ProcessPhysics() {
    uint64_t now = GetTimeMs();
    

    static int playerLastTarget[MAX_PLAYERS] = {0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (sampgdk_IsPlayerConnected(i)) {
            int target = sampgdk_GetPlayerTargetActor(i);
            if (target != 0xFFFF && target != playerLastTarget[i]) {
                bool isPed = false;
                for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
                    if (g_Actors[a].inUse && g_Actors[a].actorid == target) {
                        isPed = true;
                        break;
                    }
                }
                if (isPed) {
                    for (AMX* amx : g_AMX) {
                        int idx;
                        if (!amx_FindPublic(amx, "OnPlayerAimPedestrian", &idx)) {
                            amx_Push(amx, target);
                            amx_Push(amx, i);
                            cell ret;
                            amx_Exec(amx, &ret, idx);
                        }
                    }
                }
            }
            playerLastTarget[i] = target;
        }
    }
    

    int drivingPlayers[MAX_PLAYERS];
    int drivingVehicles[MAX_PLAYERS];
    float vehX[MAX_PLAYERS], vehY[MAX_PLAYERS], vehZ[MAX_PLAYERS];
    float vehVX[MAX_PLAYERS], vehVY[MAX_PLAYERS];
    int drivingCount = 0;
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (sampgdk_IsPlayerConnected(i) && sampgdk_GetPlayerState(i) == 2) {
            int veh = sampgdk_GetPlayerVehicleID(i);
            float vx, vy, vz;
            sampgdk_GetVehicleVelocity(veh, &vx, &vy, &vz);
            if ((vx*vx + vy*vy) > 0.01f) {
                sampgdk_GetVehiclePos(veh, &vehX[drivingCount], &vehY[drivingCount], &vehZ[drivingCount]);
                vehVX[drivingCount] = vx;
                vehVY[drivingCount] = vy;
                drivingPlayers[drivingCount] = i;
                drivingVehicles[drivingCount] = veh;
                drivingCount++;
            }
        }
    }

    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (!g_Actors[a].inUse || g_Actors[a].isDead) continue;

        if (g_Actors[a].fallenTick > 0) {
            if (now - g_Actors[a].fallenTick > 2500) {
                g_Actors[a].fallenTick = 0;
                g_Actors[a].actionId = 1;
                g_Actors[a].actionTick = now;
                sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", "getup_front", 4.1f, false, false, false, false, 0);
            }
            g_Actors[a].lastUpdateTime = now;
            continue;
        }

        if (g_Actors[a].actionId == 1) {
            if (now - g_Actors[a].actionTick > 1500) {
                g_Actors[a].actionId = 0;
                if (g_Actors[a].panicTick > now) {
                    g_Actors[a].speed = 5.0f;
                    sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", "sprint_panic", 4.1f, true, true, true, false, 0);
                    SetPedState(a, PED_STATE_PANIC);
                } else {
                    g_Actors[a].speed = 1.2f;
                    sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", NameAnimations[g_Actors[a].walkStyle], 4.1f, true, true, true, false, 0);
                    SetPedState(a, PED_STATE_WALKING);
                }
            }
            g_Actors[a].lastUpdateTime = now;
            continue;
        }
        
        if (g_Actors[a].panicTick > 0 && g_Actors[a].panicTick <= now) {
            g_Actors[a].panicTick = 0;
            g_Actors[a].speed = 1.2f;
            sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", NameAnimations[g_Actors[a].walkStyle], 4.1f, true, true, true, false, 0);
            SetPedState(a, PED_STATE_WALKING);
        }

        float ax, ay, az;
        sampgdk_GetActorPos(g_Actors[a].actorid, &ax, &ay, &az);

        for (int v = 0; v < drivingCount; v++) {
            float dx = ax - vehX[v];
            float dy = ay - vehY[v];
            float distSq = dx*dx + dy*dy;
            
            if (distSq < 20.0f) {
                float vx = vehVX[v];
                float vy = vehVY[v];
                float speed = std::sqrt(vx*vx + vy*vy);
                

                float dot = dx * vx + dy * vy;
                if (dot > 0.0f) {

                    float cross = std::abs(dx * vy - dy * vx) / speed;
                    if (cross < 1.5f) {
                        g_Actors[a].speed = 0.0f;
                        sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", "KO_skid_front", 4.1f, false, true, true, true, 0);
                        g_Actors[a].fallenTick = now;
                        SetPedState(a, PED_STATE_FALLEN);
                        
                        ApplyDamage(a, 40.0f, drivingPlayers[v], 0);
                        
                        for(AMX* amx : g_AMX) {
                            int idx;
                            if(!amx_FindPublic(amx, "OnPedestrianHitByVeh", &idx)) {
                                amx_Push(amx, g_Actors[a].actorid);
                                amx_Push(amx, drivingVehicles[v]);
                                amx_Push(amx, drivingPlayers[v]);
                                cell ret;
                                amx_Exec(amx, &ret, idx);
                            }
                        }
                        break;
                    }
                }
            }
        }

        if (g_Actors[a].fallenTick > 0 || g_Actors[a].speed == 0.0f) continue;
        

        float dt = (now - g_Actors[a].lastUpdateTime) / 1000.0f;
        g_Actors[a].lastUpdateTime = now;

        int targetIdx = FindNodeIndex(g_Actors[a].targetNodeId);
        if (targetIdx == -1) continue;
        int currentIdx = FindNodeIndex(g_Actors[a].currentNodeId);

        float tx = g_Nodes[targetIdx].x;
        float ty = g_Nodes[targetIdx].y;
        float tz = g_Nodes[targetIdx].z;

        if (currentIdx != -1) {
            ApplyRightOffset(g_Nodes[currentIdx].x, g_Nodes[currentIdx].y,
                             g_Nodes[targetIdx].x, g_Nodes[targetIdx].y,
                             0.75f, tx, ty);
        }

        float dx = tx - g_Actors[a].currentX;
        float dy = ty - g_Actors[a].currentY;
        float dist2D = std::sqrt(dx*dx + dy*dy);
        if (dist2D == 0.0f) dist2D = 0.001f;
        float moveDist = g_Actors[a].speed * dt;

        float ratio = moveDist / dist2D;
        if (ratio > 1.0f) ratio = 1.0f;
        float dz = tz - g_Actors[a].currentZ;
        g_Actors[a].currentX += dx * ratio;
        g_Actors[a].currentY += dy * ratio;
        g_Actors[a].currentZ += dz * ratio;

        if (dist2D <= 1.0f || moveDist >= dist2D) {
            g_Actors[a].previousNodeId = g_Actors[a].currentNodeId;
            g_Actors[a].currentNodeId = g_Actors[a].targetNodeId;
            g_Actors[a].targetNodeId = PickNextNode(targetIdx, g_Actors[a].previousNodeId);

            int nextIdx = FindNodeIndex(g_Actors[a].targetNodeId);
            if (nextIdx != -1) {
                float ntx = g_Nodes[nextIdx].x;
                float nty = g_Nodes[nextIdx].y;
                ApplyRightOffset(g_Nodes[targetIdx].x, g_Nodes[targetIdx].y,
                                 g_Nodes[nextIdx].x, g_Nodes[nextIdx].y,
                                 0.75f, ntx, nty);

                float ndx = ntx - g_Actors[a].currentX;
                float ndy = nty - g_Actors[a].currentY;
                g_Actors[a].targetAngle = std::atan2(-ndx, ndy) * (180.0f / 3.14159265f);
            }
        }

        float diff = g_Actors[a].targetAngle - g_Actors[a].currentAngle;
        while(diff <= -180.0f) diff += 360.0f;
        while(diff > 180.0f) diff -= 360.0f;
        g_Actors[a].currentAngle += (diff * 0.15f);
        while(g_Actors[a].currentAngle >= 360.0f) g_Actors[a].currentAngle -= 360.0f;
        while(g_Actors[a].currentAngle < 0.0f) g_Actors[a].currentAngle += 360.0f;

        sampgdk_SetActorFacingAngle(g_Actors[a].actorid, g_Actors[a].currentAngle);
        sampgdk_SetActorPos(g_Actors[a].actorid, g_Actors[a].currentX, g_Actors[a].currentY, g_Actors[a].currentZ + 0.9f);
    }
}

void ProcessStreaming() {

    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (!g_Actors[a].inUse || g_Actors[a].isDead) continue;
        
        bool nearPlayer = false;
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (!IsPlayerConnected(p)) continue;
            float px, py, pz;
            GetPlayerPos(p, &px, &py, &pz);
            float dx = g_Actors[a].currentX - px;
            float dy = g_Actors[a].currentY - py;
            if (dx*dx + dy*dy < 48400.0f) {
                nearPlayer = true;
                break;
            }
        }
        
        if (!nearPlayer) {
            SetActorPos(g_Actors[a].actorid, 0.0f, 0.0f, -1000.0f);
            g_Actors[a].inUse = false;
        }
    }
    

    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!IsPlayerConnected(p) || GetPlayerState(p) == 0) continue;
        float px, py, pz;
        GetPlayerPos(p, &px, &py, &pz);
        
        int actorsNear = 0;
        for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
            if (!g_Actors[a].inUse) continue;
            float dx = g_Actors[a].currentX - px;
            float dy = g_Actors[a].currentY - py;
            if (dx*dx + dy*dy < 14400.0f) actorsNear++;
        }
        
        if (actorsNear < 20) {
            float vx, vy, vz;
            GetPlayerVelocity(p, &vx, &vy, &vz);
            float speed = std::sqrt(vx*vx + vy*vy);
            
            float searchX = px;
            float searchY = py;
            if (speed > 0.05f) {
                float aheadMeters = std::min(speed * 250.0f, 150.0f);
                searchX += (vx / speed) * aheadMeters;
                searchY += (vy / speed) * aheadMeters;
            }
            
            std::vector<int> foundNodes;
            for (const auto& node : g_Nodes) {
                float dx = node.x - searchX;
                float dy = node.y - searchY;
                if (dx*dx + dy*dy <= 90000.0f) foundNodes.push_back(node.id);
            }
            if (foundNodes.size() > 1) {
                std::shuffle(foundNodes.begin(), foundNodes.end(), std::mt19937(std::random_device()()));
            }
            
            for (int nid : foundNodes) {
                if (actorsNear >= 20) break;
                int idx = FindNodeIndex(nid);
                if (idx == -1) continue;
                
                float nx = g_Nodes[idx].x;
                float ny = g_Nodes[idx].y;
                float nz = g_Nodes[idx].z;
                
                float dx = nx - px;
                float dy = ny - py;
                if (dx*dx + dy*dy < 1225.0f) continue;
                
                bool occupied = false;
                for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
                    if (g_Actors[a].inUse) {
                        float adx = nx - g_Actors[a].currentX;
                        float ady = ny - g_Actors[a].currentY;
                        if (adx*adx + ady*ady < 625.0f) { occupied = true; break; }
                    }
                }
                if (occupied) continue;
                
                int slot = -1;
                for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
                    if (!g_Actors[a].inUse) { slot = a; break; }
                }
                if (slot == -1) break;
                
                int skin = 2, walkStyle = 1;
                if (rand() % 2 == 0 && !g_MaleSkins.empty()) {
                    auto& s = g_MaleSkins[rand() % g_MaleSkins.size()];
                    skin = s.first; walkStyle = s.second;
                } else if (!g_FemaleSkins.empty()) {
                    auto& s = g_FemaleSkins[rand() % g_FemaleSkins.size()];
                    skin = s.first; walkStyle = s.second;
                }
                
                sampgdk_DestroyActor(g_Actors[slot].actorid);
                AMX_NATIVE ca_raycast = sampgdk_FindNative("CA_RayCastLine");
                if (ca_raycast != NULL) {
                    float dummyX = 0.0f, dummyY = 0.0f;
                    sampgdk_InvokeNative(ca_raycast, "ffffffRRR", nx, ny, 700.0f, nx, ny, -1000.0f, &dummyX, &dummyY, &nz);
                }
                g_Actors[slot].actorid = sampgdk_CreateActor(skin, nx, ny, nz + 0.9f, 0.0f);
                g_Actors[slot].skin = skin;
                sampgdk_SetActorInvulnerable(g_Actors[slot].actorid, false);
                SetActorVirtualWorld(g_Actors[slot].actorid, GetPlayerVirtualWorld(p));
                
                ApplyActorAnimation(g_Actors[slot].actorid, "PED", NameAnimations[walkStyle], 4.1f, true, true, true, false, 0);
                
                g_Actors[slot].inUse = true;
                g_Actors[slot].isDead = false;
                g_Actors[slot].walkStyle = walkStyle;
                g_Actors[slot].fallenTick = 0;
                g_Actors[slot].health = 100.0f;
                g_Actors[slot].state = PED_STATE_WALKING;
                g_Actors[slot].panicTick = 0;
                g_Actors[slot].actionId = 0;
                SetActorWalk(slot, nid, 1.2f);
                
                actorsNear++;
            }
        }
    }
}

PLUGIN_EXPORT void PLUGIN_CALL ProcessTick() {
    sampgdk::ProcessTick();
    
    uint64_t now = GetTimeMs();
    if (now - g_LastPhysicsTick >= 80) {
        g_LastPhysicsTick = now;
        ProcessPhysics();
    }
    
    if (now - g_LastStreamTick >= 1000) {
        g_LastStreamTick = now;
        ProcessStreaming();
    }
}

PLUGIN_EXPORT bool PLUGIN_CALL OnGameModeInit() {
    if (!LoadPedPaths()) {
        logprintf("[Pedestrians] WARNING: Nodes not loaded! Pedestrians will NOT spawn!");
    }
    for (int i = 0; i < MAX_STREAM_ACTORS; i++) {
        g_Actors[i].actorid = CreateActor(101, 0.0f, 0.0f, -1000.0f, 0.0f);
        SetActorInvulnerable(g_Actors[i].actorid, false);
        g_Actors[i].inUse = false;
    }
    return true;
}

PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerWeaponShot(int playerid, int weaponid, int hittype, int hitid, float fX, float fY, float fZ) {
    float px, py, pz;
    sampgdk_GetPlayerPos(playerid, &px, &py, &pz);
    
    uint64_t now = GetTimeMs();
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (!g_Actors[a].inUse || g_Actors[a].isDead) continue;
        float dx = g_Actors[a].currentX - px;
        float dy = g_Actors[a].currentY - py;
        if (dx*dx + dy*dy < 2500.0f) {
            if (g_Actors[a].fallenTick == 0 && g_Actors[a].actionId == 0) {
                if (g_Actors[a].panicTick == 0) {
                    sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", "sprint_panic", 4.1f, true, true, true, false, 0);
                    g_Actors[a].speed = 5.0f;
                    SetPedState(a, PED_STATE_PANIC);
                }
            }
            g_Actors[a].panicTick = now + 4000;
        }
    }
    return true;
}

cell AMX_NATIVE_CALL n_Path_AddMaleSkin(AMX* amx, cell* params) {
    g_MaleSkins.push_back({params[1], params[2]});
    return 1;
}

cell AMX_NATIVE_CALL n_Path_AddFemaleSkin(AMX* amx, cell* params) {
    g_FemaleSkins.push_back({params[1], params[2]});
    return 1;
}



cell AMX_NATIVE_CALL native_SetPedestrianHealth(AMX *amx, cell *params) {
    int actorid = params[1];
    float health = amx_ctof(params[2]);
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            g_Actors[a].health = health;
            if (health <= 0.0f) {
                TriggerPedDeath(a, INVALID_PLAYER_ID, 0);
            }
            return 1;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_GetPedestrianHealth(AMX *amx, cell *params) {
    int actorid = params[1];
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            return amx_ftoc(g_Actors[a].health);
        }
    }
    float zero = 0.0f;
    return amx_ftoc(zero);
}

cell AMX_NATIVE_CALL native_SetPedestrianSpeed(AMX *amx, cell *params) {
    int actorid = params[1];
    float speed = amx_ctof(params[2]);
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            if (g_Actors[a].isDead) return 0;
            g_Actors[a].speed = speed;
            return 1;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_MakePedestrianPanic(AMX *amx, cell *params) {
    int actorid = params[1];
    int duration_ms = params[2];
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            if (g_Actors[a].isDead) return 0;
            sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", "sprint_panic", 4.1f, true, true, true, false, 0);
            g_Actors[a].speed = 5.0f;
            g_Actors[a].panicTick = GetTimeMs() + duration_ms;
            SetPedState(a, PED_STATE_PANIC);
            return 1;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_IsPedestrianPanicking(AMX *amx, cell *params) {
    int actorid = params[1];
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            return (g_Actors[a].panicTick > GetTimeMs()) ? 1 : 0;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_SetPedestrianSkin(AMX *amx, cell *params) {
    int actorid = params[1];
    int skinid = params[2];
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            float ax, ay, az;
            sampgdk_GetActorPos(actorid, &ax, &ay, &az);
            sampgdk_DestroyActor(actorid);
            g_Actors[a].actorid = sampgdk_CreateActor(skinid, ax, ay, az, 0.0f);
            g_Actors[a].skin = skinid;
            sampgdk_SetActorInvulnerable(g_Actors[a].actorid, false);

            if (g_Actors[a].isDead) {
                sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "CRACK", "crckdeth2", 4.1f, false, true, true, true, 0);
            } else if (g_Actors[a].state == PED_STATE_WALKING) {
                sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", NameAnimations[g_Actors[a].walkStyle], 4.1f, true, true, true, false, 0);
            } else if (g_Actors[a].state == PED_STATE_PANIC) {
                sampgdk_ApplyActorAnimation(g_Actors[a].actorid, "PED", "sprint_panic", 4.1f, true, true, true, false, 0);
            }
            return 1;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_GetPedestrianNode(AMX *amx, cell *params) {
    int actorid = params[1];
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            return g_Actors[a].targetNodeId;
        }
    }
    return -1;
}

cell AMX_NATIVE_CALL native_PrintIgnoredNodesCount(AMX *amx, cell *params) {
    sampgdk_logprintf("[Pedestrians] Ignoring %zu nodes.", g_IgnoredNodes.size());
    return 1;
}

cell AMX_NATIVE_CALL native_GetClosestPedestrianID(AMX *amx, cell *params) {
    int playerid = params[1];
    float px, py, pz;
    sampgdk_GetPlayerPos(playerid, &px, &py, &pz);
    int closestId = -1;
    float closestDist = 9999999.0f;
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (!g_Actors[a].inUse) continue;
        float ax, ay, az;
        sampgdk_GetActorPos(g_Actors[a].actorid, &ax, &ay, &az);
        float dx = ax - px; float dy = ay - py; float dz = az - pz;
        float dist = dx*dx + dy*dy + dz*dz;
        if (dist < closestDist) {
            closestDist = dist;
            closestId = g_Actors[a].actorid;
        }
    }
    return closestId;
}

cell AMX_NATIVE_CALL native_GetPedestrianSex(AMX *amx, cell *params) {
    int actorid = params[1];
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            for (auto s : g_FemaleSkins) {
                if (s.first == g_Actors[a].skin) return 2;
            }
            return 1;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_StopPedestrian(AMX *amx, cell *params) {
    int actorid = params[1];
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            if (g_Actors[a].isDead) return 0;
            g_Actors[a].speed = 0.0f;
            sampgdk_ClearActorAnimations(actorid);
            SetPedState(a, PED_STATE_STOPPED);
            return 1;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_ResumePedestrian(AMX *amx, cell *params) {
    int actorid = params[1];
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            if (g_Actors[a].isDead) return 0;
            g_Actors[a].speed = 1.2f;
            g_Actors[a].lastUpdateTime = GetTimeMs();
            sampgdk_ApplyActorAnimation(actorid, "PED", NameAnimations[g_Actors[a].walkStyle], 4.1f, true, true, true, false, 0);
            SetPedState(a, PED_STATE_WALKING);
            return 1;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_IgnorePedestrianNode(AMX *amx, cell *params) {
    g_IgnoredNodes.insert((uint32_t)params[1]);
    return 1;
}

cell AMX_NATIVE_CALL native_IsPlayerInFrontOfPed(AMX *amx, cell *params) {
    int playerid = params[1];
    int actorid = params[2];
    
    float px, py, pz;
    sampgdk_GetPlayerPos(playerid, &px, &py, &pz);
    
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            float ax, ay, az;
            sampgdk_GetActorPos(actorid, &ax, &ay, &az);
            
            float dx = px - ax;
            float dy = py - ay;
            float dz = pz - az;
            float distSq = dx*dx + dy*dy + dz*dz;
            
            if (distSq > 400.0f) return 0;
            
            float pedDirX = std::sin(-g_Actors[a].currentAngle * 3.14159265f / 180.0f);
            float pedDirY = std::cos(-g_Actors[a].currentAngle * 3.14159265f / 180.0f);
            
            float dist2D = std::sqrt(dx*dx + dy*dy);
            if (dist2D < 0.01f) return 1;
            
            float dot = (dx / dist2D) * pedDirX + (dy / dist2D) * pedDirY;
            if (dot > 0.0f) return 1;
            return 0;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_ApplyPedestrianAnim(AMX *amx, cell *params) {
    int actorid = params[1];
    char animlib[64];
    char animname[64];
    cell *physAddr;
    int len;
    
    amx_GetAddr(amx, params[2], &physAddr);
    amx_StrLen(physAddr, &len);
    amx_GetString(animlib, physAddr, 0, sizeof(animlib));
    
    amx_GetAddr(amx, params[3], &physAddr);
    amx_StrLen(physAddr, &len);
    amx_GetString(animname, physAddr, 0, sizeof(animname));
    
    float fDelta = amx_ctof(params[4]);
    bool loop = params[5];
    bool lockx = params[6];
    bool locky = params[7];
    bool freeze = params[8];
    int time = params[9];
    
    sampgdk_ApplyActorAnimation(actorid, animlib, animname, fDelta, loop, lockx, locky, freeze, time);
    return 1;
}

AMX_NATIVE_INFO PluginNatives[] = {
    {"Path_AddMaleSkin", n_Path_AddMaleSkin},
    {"Path_AddFemaleSkin", n_Path_AddFemaleSkin},
    {"GetClosestPedestrianID", native_GetClosestPedestrianID},
    {"GetPedestrianSex", native_GetPedestrianSex},
    {"StopPedestrian", native_StopPedestrian},
    {"ResumePedestrian", native_ResumePedestrian},
    {"IgnorePedestrianNode", native_IgnorePedestrianNode},
    {"ApplyPedestrianAnim", native_ApplyPedestrianAnim},
    {"IsPlayerInFrontOfPed", native_IsPlayerInFrontOfPed},
    {"SetPedestrianHealth", native_SetPedestrianHealth},
    {"GetPedestrianHealth", native_GetPedestrianHealth},
    {"SetPedestrianSpeed", native_SetPedestrianSpeed},
    {"MakePedestrianPanic", native_MakePedestrianPanic},
    {"IsPedestrianPanicking", native_IsPedestrianPanicking},
    {"SetPedestrianSkin", native_SetPedestrianSkin},
    {"GetPedestrianNode", native_GetPedestrianNode},
    {"PrintIgnoredNodesCount", native_PrintIgnoredNodesCount},
    {0, 0}
};

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports() {
    return sampgdk::Supports() | SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES | SUPPORTS_PROCESS_TICK;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void **ppData) {
    pAMXFunctions = ppData[PLUGIN_DATA_AMX_EXPORTS];
    logprintf = (logprintf_t)ppData[PLUGIN_DATA_LOGPRINTF];
    
    sampgdk::Load(ppData);
    logprintf(">> Pedestrians plugin by conny loaded!");
    return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload() {
    sampgdk::Unload();
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX *amx) {
    g_AMX.push_back(amx);
    return amx_Register(amx, PluginNatives, -1);
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX *amx) {
    for (auto it = g_AMX.begin(); it != g_AMX.end(); ++it) {
        if (*it == amx) {
            g_AMX.erase(it);
            break;
        }
    }
    return AMX_ERR_NONE;
}

PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerGiveDamageActor(int playerid, int damaged_actorid, float amount, int weaponid, int bodypart) {
    for (int a = 0; a < MAX_STREAM_ACTORS; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == damaged_actorid) {
            ApplyDamage(a, amount, playerid, weaponid);
            break;
        }
    }
    for (AMX* amx : g_AMX) {
        int idx;
        if (!amx_FindPublic(amx, "OnPedestrianGetDamage", &idx)) {
            amx_Push(amx, weaponid);
            amx_Push(amx, playerid);
            amx_Push(amx, damaged_actorid);
            cell ret;
            amx_Exec(amx, &ret, idx);
        }
    }
    return true;
}
