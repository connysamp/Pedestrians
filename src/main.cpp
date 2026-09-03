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
#include <unordered_map>
#include <chrono>
#include <map>

using json = nlohmann::json;

typedef void(*logprintf_t)(const char* format, ...);
logprintf_t logprintf;

bool g_UseColAndreas = false;
bool g_AllowPedsInInteriors = false;
static AMX_NATIVE g_NativeRayCastLine = NULL;

#define MAX_STREAM_ACTORS 500

int g_MaxActorsInPool = 500;
int g_MaxActorsPerPlayer = 12;
float g_SpawnDensity = 1.0f;
float g_StreamingDistance = 220.0f;

std::unordered_set<uint32_t> g_IgnoredNodes;
extern void *pAMXFunctions;

struct PedNode {
    int id;
    int area;
    float x, y, z;
    std::vector<uint32_t> links;
};

std::vector<PedNode> g_Nodes;
std::unordered_map<uint32_t, int> g_NodeIdToIndex;

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
std::unordered_map<int, int> g_SkinWalkStyles;

uint64_t GetTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

#define PED_STATE_WALKING 1
#define PED_STATE_PANIC 2
#define PED_STATE_FALLEN 3
#define PED_STATE_STOPPED 4
#define PED_STATE_DEAD 5

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
    uint64_t lastUpdateTime;
    int walkStyle;
    uint64_t fallenTick;
    float health;
    int actionId;
    uint64_t actionTick;
    uint64_t panicTick;
    int state;
};

WalkingActor g_Actors[MAX_STREAM_ACTORS];
uint64_t g_LastStreamTick = 0;
uint64_t g_LastPhysicsTick = 0;
std::vector<AMX*> g_AMX;

struct ZoneSkin {
    std::string name;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    int maxPeds;
    std::vector<int> skins[4];
};

std::vector<ZoneSkin> g_Zones;
std::vector<int> g_DefaultSkins;
int g_ServerHour = 12;
int g_ServerMinute = 0;

bool IsFemaleSkin(int skinId) {
    for (const auto& s : g_FemaleSkins) {
        if (s.first == skinId) return true;
    }
    for (const auto& s : g_MaleSkins) {
        if (s.first == skinId) return false;
    }
    auto it = g_SkinWalkStyles.find(skinId);
    if (it != g_SkinWalkStyles.end()) {
        int ws = it->second;
        if (ws == WOMAN_WALKOLD || ws == WOMAN_WALKFATOLD || ws == WOMAN_WALKNORM ||
            ws == WOMAN_WALKBUSY || ws == WOMAN_WALKPRO || ws == WOMAN_WALKSEXY) {
            return true;
        }
    }
    return false;
}

bool LoadZoneSkins() {
    std::ifstream file("scriptfiles/zone_skins.json");
    if (!file.is_open()) {
        file.open("zone_skins.json");
        if (!file.is_open()) {
            logprintf("[Pedestrians] WARNING: Could not open zone_skins.json in scriptfiles/ or root directory.");
            return false;
        }
    }
    
    try {
        json j;
        file >> j;
        
        g_DefaultSkins.clear();
        if (j.contains("default_skins") && j["default_skins"].is_array()) {
            for (auto& s : j["default_skins"]) {
                g_DefaultSkins.push_back(s.get<int>());
            }
        }
        if (g_DefaultSkins.empty()) {
            g_DefaultSkins = {7, 10, 14, 15, 17, 19, 20, 21, 22, 23, 24, 25, 28, 29};
        }
        
        g_SkinWalkStyles.clear();
        if (j.contains("skin_walkstyles") && j["skin_walkstyles"].is_object()) {
            for (auto& [key, val] : j["skin_walkstyles"].items()) {
                int skinId = std::stoi(key);
                int wsIndex = val.get<int>();
                g_SkinWalkStyles[skinId] = wsIndex;
            }
        }
        
        g_Zones.clear();
        if (j.contains("zones") && j["zones"].is_array()) {
            for (auto& z : j["zones"]) {
                ZoneSkin zs;
                zs.name = z.value("name", "");
                zs.minX = z["min"][0].get<float>();
                zs.minY = z["min"][1].get<float>();
                zs.minZ = z["min"][2].get<float>();
                zs.maxX = z["max"][0].get<float>();
                zs.maxY = z["max"][1].get<float>();
                zs.maxZ = z["max"][2].get<float>();
                zs.maxPeds = z.value("max_peds", 12);
                
                if (z.contains("skins") && z["skins"].is_object()) {
                    auto& sk = z["skins"];
                    if (sk.contains("morning")) for (auto& s : sk["morning"]) zs.skins[0].push_back(s.get<int>());
                    if (sk.contains("day")) for (auto& s : sk["day"]) zs.skins[1].push_back(s.get<int>());
                    if (sk.contains("evening")) for (auto& s : sk["evening"]) zs.skins[2].push_back(s.get<int>());
                    if (sk.contains("night")) for (auto& s : sk["night"]) zs.skins[3].push_back(s.get<int>());
                }
                g_Zones.push_back(zs);
            }
        }
        logprintf("[Pedestrians] Loaded %d zones, %d skin walkstyles, %d default skins",
            (int)g_Zones.size(), (int)g_SkinWalkStyles.size(), (int)g_DefaultSkins.size());
        return true;
    } catch (const std::exception& e) {
        logprintf("[Pedestrians] ERROR parsing zone_skins.json: %s", e.what());
        return false;
    }
}

int GetTimeSlot() {
    if (g_ServerHour >= 5 && g_ServerHour < 12) return 0;
    if (g_ServerHour >= 12 && g_ServerHour < 18) return 1;
    if (g_ServerHour >= 18 && g_ServerHour < 22) return 2;
    return 3;
}

int GetMaxPedsForPosition(float x, float y, float z) {
    for (const auto& zone : g_Zones) {
        if (x >= zone.minX && x <= zone.maxX &&
            y >= zone.minY && y <= zone.maxY) {
            return zone.maxPeds;
        }
    }
    return 10;
}

int GetSkinForPosition(float x, float y, float z) {
    static std::mt19937 rng(std::random_device{}());
    int timeSlot = GetTimeSlot();
    
    std::unordered_set<int> nearbySkins;
    for (int a = 0; a < g_MaxActorsInPool; a++) {
        if (g_Actors[a].inUse) {
            float adx = g_Actors[a].currentX - x;
            float ady = g_Actors[a].currentY - y;
            if (adx*adx + ady*ady <= 10000.0f) {
                nearbySkins.insert(g_Actors[a].skin);
            }
        }
    }

    for (const auto& zone : g_Zones) {
        if (x >= zone.minX && x <= zone.maxX &&
            y >= zone.minY && y <= zone.maxY) {
            
            bool isBeachZone = (zone.name == "SANTA_MARIA_BEACH_CUSTOM" || zone.name.find("BEACH") != std::string::npos);
            if (isBeachZone) {
                std::uniform_int_distribution<int> roll(0, 99);
                if (roll(rng) >= 40) {
                    continue;
                }
            }

            const auto& skins = zone.skins[timeSlot];
            if (!skins.empty()) {
                std::vector<int> uniqueSkins;
                for (int s : skins) {
                    if (nearbySkins.find(s) == nearbySkins.end()) {
                        uniqueSkins.push_back(s);
                    }
                }
                if (!uniqueSkins.empty()) {
                    std::uniform_int_distribution<size_t> dist(0, uniqueSkins.size() - 1);
                    return uniqueSkins[dist(rng)];
                }
                std::uniform_int_distribution<size_t> dist(0, skins.size() - 1);
                return skins[dist(rng)];
            }
        }
    }
    if (!g_DefaultSkins.empty()) {
        std::uniform_int_distribution<size_t> dist(0, g_DefaultSkins.size() - 1);
        return g_DefaultSkins[dist(rng)];
    }
    return 7;
}

int GetWalkStyleForSkin(int skinId) {
    auto it = g_SkinWalkStyles.find(skinId);
    if (it != g_SkinWalkStyles.end()) {
        return it->second;
    }
    return 1;
}

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

inline int FindNodeIndex(uint32_t id) {
    auto it = g_NodeIdToIndex.find(id);
    if (it != g_NodeIdToIndex.end()) {
        return it->second;
    }
    return -1;
}

int PickNextNode(int currentIdx, int previousNodeId) {
    if (currentIdx < 0 || (size_t)currentIdx >= g_Nodes.size()) return -1;
    const auto& links = g_Nodes[currentIdx].links;
    if (links.empty()) return g_Nodes[currentIdx].id;
    
    std::vector<int> validLinks;
    for (uint32_t linkId : links) {
        if ((int)linkId != previousNodeId && g_IgnoredNodes.count(linkId) == 0) {
            int linkIdx = FindNodeIndex(linkId);
            if (linkIdx != -1) validLinks.push_back((int)linkId);
        }
    }
    
    if (validLinks.empty()) {
        for (uint32_t linkId : links) {
            if (FindNodeIndex(linkId) != -1) return (int)linkId;
        }
        return g_Nodes[currentIdx].id;
    }
    
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, validLinks.size() - 1);
    return validLinks[dist(rng)];
}

bool LoadPedPaths() {
    std::ifstream file("scriptfiles/pedpaths.json");
    if (!file.is_open()) {
        file.open("pedpaths.json");
        if (!file.is_open()) {
            logprintf("[Pedestrians] ERROR: pedpaths.json not found in scriptfiles/ or root directory!");
            return false;
        }
    }
    try {
        json j;
        file >> j;
        g_Nodes.clear();
        g_NodeIdToIndex.clear();
        if (j.is_array()) {
            g_Nodes.reserve(j.size());
            g_NodeIdToIndex.reserve(j.size());
            for (auto& item : j) {
                PedNode node;
                node.id = item["id"].get<int>();
                node.area = item.value("area", 0);
                node.x = item["x"].get<float>();
                node.y = item["y"].get<float>();
                node.z = item["z"].get<float>();
                if (item.contains("links") && item["links"].is_array()) {
                    for (auto& l : item["links"]) {
                        node.links.push_back(l.get<uint32_t>());
                    }
                }
                int idx = (int)g_Nodes.size();
                g_NodeIdToIndex[node.id] = idx;
                g_Nodes.push_back(node);
            }
        }
        logprintf("[Pedestrians] Loaded %d pedestrian nodes from pedpaths.json", (int)g_Nodes.size());
        return true;
    } catch (const std::exception& e) {
        logprintf("[Pedestrians] ERROR parsing pedpaths.json: %s", e.what());
        return false;
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
    g_Actors[slot].lastUpdateTime = GetTimeMs();

    g_Actors[slot].targetNodeId = PickNextNode(startIdx, startNode);

    int targetIdx = FindNodeIndex(g_Actors[slot].targetNodeId);
    if (targetIdx != -1) {
        float dx = g_Nodes[targetIdx].x - g_Actors[slot].currentX;
        float dy = g_Nodes[targetIdx].y - g_Actors[slot].currentY;
        g_Actors[slot].targetAngle = std::atan2(-dx, dy) * (180.0f / 3.14159265f);
        g_Actors[slot].currentAngle = g_Actors[slot].targetAngle;
    }
}

bool RayCastTerrain(float x, float y, float currentZ, float targetZ, float& outZ) {
    if (!g_UseColAndreas) return false;
    if (g_NativeRayCastLine == NULL) {
        g_NativeRayCastLine = sampgdk_FindNative("CA_RayCastLine");
        if (g_NativeRayCastLine == NULL) {
            g_UseColAndreas = false;
            return false;
        }
    }
    cell dummyXc = 0, dummyYc = 0, caZc = 0;
    cell ret = sampgdk_InvokeNative(g_NativeRayCastLine, "ffffffRRR",
        x, y, currentZ + 3.0f,
        x, y, currentZ - 4.0f,
        &dummyXc, &dummyYc, &caZc);
    if (ret != 0) {
        float caZ = amx_ctof(caZc);
        if (targetZ == 0.0f || std::abs(caZ - targetZ) <= 3.5f) {
            outZ = caZ;
            return true;
        }
    }
    return false;
}

void ProcessPhysics() {
    uint64_t now = GetTimeMs();

    static int playerLastTarget[MAX_PLAYERS] = {0};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (sampgdk_IsPlayerConnected(i)) {
            int target = sampgdk_GetPlayerTargetActor(i);
            if (target != 0xFFFF && target != playerLastTarget[i]) {
                bool isPed = false;
                for (int a = 0; a < g_MaxActorsInPool; a++) {
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

    for (int a = 0; a < g_MaxActorsInPool; a++) {
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

        float ax = g_Actors[a].currentX;
        float ay = g_Actors[a].currentY;
        float az = g_Actors[a].currentZ;

        for (int v = 0; v < drivingCount; v++) {
            float dx = ax - vehX[v];
            float dy = ay - vehY[v];
            float dz = std::abs(az - vehZ[v]);
            float distSq = dx*dx + dy*dy;
            
            if (dz <= 3.0f && distSq < 20.0f) {
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
                        
                        for (AMX* amx : g_AMX) {
                            int idx;
                            if (!amx_FindPublic(amx, "OnPedestrianHitByVeh", &idx)) {
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

        if (g_Actors[a].fallenTick > 0) continue;
        if (g_Actors[a].speed == 0.0f) {
            float finalZ = g_Actors[a].currentZ + 0.9f;
            float rayZ = g_Actors[a].currentZ;
            if (RayCastTerrain(g_Actors[a].currentX, g_Actors[a].currentY, g_Actors[a].currentZ, 0.0f, rayZ)) {
                g_Actors[a].currentZ = rayZ;
                finalZ = rayZ + 0.9f;
            }
            sampgdk_SetActorPos(g_Actors[a].actorid, g_Actors[a].currentX, g_Actors[a].currentY, finalZ);
            continue;
        }

        float dt = (now - g_Actors[a].lastUpdateTime) / 1000.0f;
        g_Actors[a].lastUpdateTime = now;

        int targetIdx = FindNodeIndex(g_Actors[a].targetNodeId);
        if (targetIdx == -1) continue;

        float tx = g_Nodes[targetIdx].x;
        float ty = g_Nodes[targetIdx].y;
        float tz = g_Nodes[targetIdx].z;

        float dx = tx - g_Actors[a].currentX;
        float dy = ty - g_Actors[a].currentY;
        float dist2D = std::sqrt(dx * dx + dy * dy);

        if (dist2D <= 1.8f) {
            g_Actors[a].previousNodeId = g_Actors[a].currentNodeId;
            g_Actors[a].currentNodeId = g_Actors[a].targetNodeId;
            g_Actors[a].targetNodeId = PickNextNode(targetIdx, g_Actors[a].previousNodeId);

            targetIdx = FindNodeIndex(g_Actors[a].targetNodeId);
            if (targetIdx != -1) {
                tx = g_Nodes[targetIdx].x;
                ty = g_Nodes[targetIdx].y;
                tz = g_Nodes[targetIdx].z;
                dx = tx - g_Actors[a].currentX;
                dy = ty - g_Actors[a].currentY;
                dist2D = std::sqrt(dx * dx + dy * dy);
            }
        }

        if (dist2D < 0.001f) dist2D = 0.001f;

        float desiredAngle = std::atan2(-dx, dy) * (180.0f / 3.14159265f);
        float diff = desiredAngle - g_Actors[a].currentAngle;
        while(diff <= -180.0f) diff += 360.0f;
        while(diff > 180.0f) diff -= 360.0f;

        float baseTurnRate = (g_Actors[a].state == PED_STATE_PANIC) ? 250.0f : 120.0f;
        float maxTurnRate = baseTurnRate * dt;
        if (diff > maxTurnRate) diff = maxTurnRate;
        if (diff < -maxTurnRate) diff = -maxTurnRate;

        g_Actors[a].currentAngle += diff;
        while(g_Actors[a].currentAngle >= 360.0f) g_Actors[a].currentAngle -= 360.0f;
        while(g_Actors[a].currentAngle < 0.0f) g_Actors[a].currentAngle += 360.0f;

        float currentSpeed = g_Actors[a].speed;
        if (std::abs(diff) > 40.0f * dt) {
            currentSpeed *= 0.8f;
        }

        float rad = -g_Actors[a].currentAngle * (3.14159265f / 180.0f);
        float fwdX = std::sin(rad);
        float fwdY = std::cos(rad);

        float moveDist = currentSpeed * dt;
        g_Actors[a].currentX += fwdX * moveDist;
        g_Actors[a].currentY += fwdY * moveDist;

        float dz = tz - g_Actors[a].currentZ;
        g_Actors[a].currentZ += dz * std::min(1.0f, dt * 2.5f);

        float finalZ = g_Actors[a].currentZ + 0.9f;
        float rayZ = g_Actors[a].currentZ;
        if (RayCastTerrain(g_Actors[a].currentX, g_Actors[a].currentY, g_Actors[a].currentZ, tz, rayZ)) {
            g_Actors[a].currentZ = rayZ;
            finalZ = rayZ + 0.9f;
        }

        sampgdk_SetActorFacingAngle(g_Actors[a].actorid, g_Actors[a].currentAngle);
        sampgdk_SetActorPos(g_Actors[a].actorid, g_Actors[a].currentX, g_Actors[a].currentY, finalZ);
    }
}

void ProcessStreaming() {
    float streamDistSq = g_StreamingDistance * g_StreamingDistance;

    for (int a = 0; a < g_MaxActorsInPool; a++) {
        if (!g_Actors[a].inUse || g_Actors[a].isDead) continue;
        
        bool nearPlayer = false;
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (!sampgdk_IsPlayerConnected(p)) continue;
            float px, py, pz;
            sampgdk_GetPlayerPos(p, &px, &py, &pz);
            float dx = g_Actors[a].currentX - px;
            float dy = g_Actors[a].currentY - py;
            if (dx*dx + dy*dy < streamDistSq) {
                nearPlayer = true;
                break;
            }
        }
        
        if (!nearPlayer) {
            sampgdk_SetActorPos(g_Actors[a].actorid, 0.0f, 0.0f, -1000.0f);
            g_Actors[a].inUse = false;
        }
    }

    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!sampgdk_IsPlayerConnected(p) || sampgdk_GetPlayerState(p) == 0) continue;
        if (!g_AllowPedsInInteriors && sampgdk_GetPlayerInterior(p) != 0) continue;

        float px, py, pz;
        sampgdk_GetPlayerPos(p, &px, &py, &pz);
        
        int maxZonePeds = int(GetMaxPedsForPosition(px, py, pz) * g_SpawnDensity);
        if (maxZonePeds > g_MaxActorsPerPlayer) maxZonePeds = g_MaxActorsPerPlayer;
        if (maxZonePeds < 1) maxZonePeds = 1;

        int actorsNear = 0;
        for (int a = 0; a < g_MaxActorsInPool; a++) {
            if (!g_Actors[a].inUse) continue;
            float dx = g_Actors[a].currentX - px;
            float dy = g_Actors[a].currentY - py;
            if (dx*dx + dy*dy < 14400.0f) actorsNear++;
        }

        if (actorsNear < maxZonePeds) {
            float vx, vy, vz;
            sampgdk_GetPlayerVelocity(p, &vx, &vy, &vz);
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
                if (std::abs(dx) <= 300.0f && std::abs(dy) <= 300.0f) {
                    if (dx*dx + dy*dy <= 90000.0f) {
                        foundNodes.push_back(node.id);
                    }
                }
            }
            if (foundNodes.size() > 1) {
                static std::mt19937 rng(std::random_device{}());
                std::shuffle(foundNodes.begin(), foundNodes.end(), rng);
            }
            
            for (int nid : foundNodes) {
                if (actorsNear >= maxZonePeds) break;
                int idx = FindNodeIndex(nid);
                if (idx == -1) continue;
                
                float nx = g_Nodes[idx].x;
                float ny = g_Nodes[idx].y;
                float nz = g_Nodes[idx].z;
                
                float dx = nx - px;
                float dy = ny - py;
                if (dx*dx + dy*dy < 1225.0f) continue;
                
                bool occupied = false;
                for (int a = 0; a < g_MaxActorsInPool; a++) {
                    if (g_Actors[a].inUse) {
                        float adx = nx - g_Actors[a].currentX;
                        float ady = ny - g_Actors[a].currentY;
                        if (adx*adx + ady*ady < 625.0f) {
                            occupied = true;
                            break;
                        }
                    }
                }
                if (occupied) continue;
                
                int slot = -1;
                for (int a = 0; a < g_MaxActorsInPool; a++) {
                    if (!g_Actors[a].inUse) {
                        slot = a;
                        break;
                    }
                }
                if (slot == -1) break;
                
                int skin = GetSkinForPosition(nx, ny, nz);
                int walkStyle = GetWalkStyleForSkin(skin);
                
                if (g_Actors[slot].actorid != 0 && g_Actors[slot].actorid != 0xFFFF) {
                    sampgdk_DestroyActor(g_Actors[slot].actorid);
                }
                
                float rayZ = nz;
                if (RayCastTerrain(nx, ny, nz, nz, rayZ)) {
                    nz = rayZ;
                }
                
                float spawnZ = nz;
                if (!g_UseColAndreas) spawnZ += 0.9f;

                int newActor = sampgdk_CreateActor(skin, nx, ny, spawnZ, 0.0f);
                if (newActor == 0xFFFF || newActor < 0) continue;

                g_Actors[slot].actorid = newActor;
                g_Actors[slot].currentZ = nz;
                g_Actors[slot].skin = skin;
                sampgdk_SetActorInvulnerable(g_Actors[slot].actorid, false);
                sampgdk_SetActorVirtualWorld(g_Actors[slot].actorid, sampgdk_GetPlayerVirtualWorld(p));
                
                sampgdk_ApplyActorAnimation(g_Actors[slot].actorid, "PED", NameAnimations[walkStyle], 4.1f, true, true, true, false, 0);
                
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
    LoadZoneSkins();
    if (!LoadPedPaths()) {
        logprintf("[Pedestrians] WARNING: Nodes not loaded! Pedestrians will NOT spawn!");
    }
    for (int i = 0; i < MAX_STREAM_ACTORS; i++) {
        g_Actors[i].actorid = sampgdk_CreateActor(101, 0.0f, 0.0f, -1000.0f, 0.0f);
        if (g_Actors[i].actorid != 0xFFFF && g_Actors[i].actorid >= 0) {
            sampgdk_SetActorInvulnerable(g_Actors[i].actorid, false);
        }
        g_Actors[i].inUse = false;
    }
    logprintf("[Pedestrians] Configuration: MaxPool=%d, MaxPerPlayer=%d, Density=%.2f, StreamDist=%.1f",
        g_MaxActorsInPool, g_MaxActorsPerPlayer, g_SpawnDensity, g_StreamingDistance);
    return true;
}

PLUGIN_EXPORT bool PLUGIN_CALL OnPlayerWeaponShot(int playerid, int weaponid, int hittype, int hitid, float fX, float fY, float fZ) {
    float px, py, pz;
    sampgdk_GetPlayerPos(playerid, &px, &py, &pz);
    
    uint64_t now = GetTimeMs();
    for (int a = 0; a < g_MaxActorsInPool; a++) {
        if (!g_Actors[a].inUse || g_Actors[a].isDead) continue;
        float dx = g_Actors[a].currentX - px;
        float dy = g_Actors[a].currentY - py;
        float dz = std::abs(g_Actors[a].currentZ - pz);
        if (dz < 20.0f && (dx*dx + dy*dy < 2500.0f)) {
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
    int skinId = (int)params[1];
    int walkStyle = (int)params[2];
    g_MaleSkins.push_back({skinId, walkStyle});
    g_SkinWalkStyles[skinId] = walkStyle;
    return 1;
}

cell AMX_NATIVE_CALL n_Path_AddFemaleSkin(AMX* amx, cell* params) {
    int skinId = (int)params[1];
    int walkStyle = (int)params[2];
    g_FemaleSkins.push_back({skinId, walkStyle});
    g_SkinWalkStyles[skinId] = walkStyle;
    return 1;
}

cell AMX_NATIVE_CALL n_Path_SetTime(AMX* amx, cell* params) {
    g_ServerHour = (int)params[1];
    g_ServerMinute = (int)params[2];
    return 1;
}

cell AMX_NATIVE_CALL n_Path_SetMaxActorsInPool(AMX* amx, cell* params) {
    int count = (int)params[1];
    if (count < 1) count = 1;
    if (count > MAX_STREAM_ACTORS) count = MAX_STREAM_ACTORS;
    g_MaxActorsInPool = count;
    return 1;
}

cell AMX_NATIVE_CALL n_Path_GetMaxActorsInPool(AMX* amx, cell* params) {
    return (cell)g_MaxActorsInPool;
}

cell AMX_NATIVE_CALL n_Path_SetMaxActorsPerPlayer(AMX* amx, cell* params) {
    int count = (int)params[1];
    if (count < 1) count = 1;
    if (count > MAX_STREAM_ACTORS) count = MAX_STREAM_ACTORS;
    g_MaxActorsPerPlayer = count;
    return 1;
}

cell AMX_NATIVE_CALL n_Path_GetMaxActorsPerPlayer(AMX* amx, cell* params) {
    return (cell)g_MaxActorsPerPlayer;
}

cell AMX_NATIVE_CALL n_Path_SetSpawnDensity(AMX* amx, cell* params) {
    float density = amx_ctof(params[1]);
    if (density < 0.0f) density = 0.0f;
    if (density > 5.0f) density = 5.0f;
    g_SpawnDensity = density;
    return 1;
}

cell AMX_NATIVE_CALL n_Path_GetSpawnDensity(AMX* amx, cell* params) {
    return amx_ftoc(g_SpawnDensity);
}

cell AMX_NATIVE_CALL n_Path_SetStreamingDistance(AMX* amx, cell* params) {
    float dist = amx_ctof(params[1]);
    if (dist < 30.0f) dist = 30.0f;
    if (dist > 600.0f) dist = 600.0f;
    g_StreamingDistance = dist;
    return 1;
}

cell AMX_NATIVE_CALL n_Path_GetStreamingDistance(AMX* amx, cell* params) {
    return amx_ftoc(g_StreamingDistance);
}

cell AMX_NATIVE_CALL native_SetPedestrianHealth(AMX *amx, cell *params) {
    int actorid = params[1];
    float health = amx_ctof(params[2]);
    for (int a = 0; a < g_MaxActorsInPool; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            g_Actors[a].health = health;
            if (health <= 0.0f) {
                TriggerPedDeath(a, 0xFFFF, 0);
            }
            return 1;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_GetPedestrianHealth(AMX *amx, cell *params) {
    int actorid = params[1];
    for (int a = 0; a < g_MaxActorsInPool; a++) {
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
    for (int a = 0; a < g_MaxActorsInPool; a++) {
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
    for (int a = 0; a < g_MaxActorsInPool; a++) {
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
    for (int a = 0; a < g_MaxActorsInPool; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            return (g_Actors[a].panicTick > GetTimeMs()) ? 1 : 0;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_SetPedestrianSkin(AMX *amx, cell *params) {
    int actorid = params[1];
    int skinid = params[2];
    for (int a = 0; a < g_MaxActorsInPool; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            float ax, ay, az;
            sampgdk_GetActorPos(actorid, &ax, &ay, &az);
            sampgdk_DestroyActor(actorid);
            g_Actors[a].actorid = sampgdk_CreateActor(skinid, ax, ay, az, 0.0f);
            g_Actors[a].skin = skinid;
            g_Actors[a].walkStyle = GetWalkStyleForSkin(skinid);
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
    for (int a = 0; a < g_MaxActorsInPool; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            return g_Actors[a].targetNodeId;
        }
    }
    return -1;
}

cell AMX_NATIVE_CALL native_PrintIgnoredNodesCount(AMX *amx, cell *params) {
    logprintf("[Pedestrians] Ignoring %u nodes.", (unsigned int)g_IgnoredNodes.size());
    return 1;
}

cell AMX_NATIVE_CALL native_GetClosestPedestrianID(AMX *amx, cell *params) {
    int playerid = params[1];
    float px, py, pz;
    sampgdk_GetPlayerPos(playerid, &px, &py, &pz);
    int closestId = -1;
    float closestDist = 9999999.0f;
    for (int a = 0; a < g_MaxActorsInPool; a++) {
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
    for (int a = 0; a < g_MaxActorsInPool; a++) {
        if (g_Actors[a].inUse && g_Actors[a].actorid == actorid) {
            return IsFemaleSkin(g_Actors[a].skin) ? 2 : 1;
        }
    }
    return 0;
}

cell AMX_NATIVE_CALL native_StopPedestrian(AMX *amx, cell *params) {
    int actorid = params[1];
    for (int a = 0; a < g_MaxActorsInPool; a++) {
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
    for (int a = 0; a < g_MaxActorsInPool; a++) {
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
    
    for (int a = 0; a < g_MaxActorsInPool; a++) {
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

cell AMX_NATIVE_CALL n_TogglePedInInterior(AMX* amx, cell* params) {
    g_AllowPedsInInteriors = (params[1] != 0);
    return 1;
}

cell AMX_NATIVE_CALL n_Path_SetUseColAndreas(AMX* amx, cell* params) {
    bool req = (params[1] != 0);
    if (req) {
        g_NativeRayCastLine = sampgdk_FindNative("CA_RayCastLine");
        if (g_NativeRayCastLine != NULL) {
            g_UseColAndreas = true;
            return 1;
        } else {
            g_UseColAndreas = false;
            return 0;
        }
    } else {
        g_UseColAndreas = false;
        g_NativeRayCastLine = NULL;
        return 1;
    }
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
    bool loop = (params[5] != 0);
    bool lockx = (params[6] != 0);
    bool locky = (params[7] != 0);
    bool freeze = (params[8] != 0);
    int time = params[9];
    
    sampgdk_ApplyActorAnimation(actorid, animlib, animname, fDelta, loop, lockx, locky, freeze, time);
    return 1;
}

AMX_NATIVE_INFO PluginNatives[] = {
    {"TogglePedInInterior", n_TogglePedInInterior},
    {"Path_SetUseColAndreas", n_Path_SetUseColAndreas},
    {"Path_SetTime", n_Path_SetTime},
    {"Path_SetMaxActorsInPool", n_Path_SetMaxActorsInPool},
    {"Path_GetMaxActorsInPool", n_Path_GetMaxActorsInPool},
    {"Path_SetMaxActorsPerPlayer", n_Path_SetMaxActorsPerPlayer},
    {"Path_GetMaxActorsPerPlayer", n_Path_GetMaxActorsPerPlayer},
    {"Path_SetSpawnDensity", n_Path_SetSpawnDensity},
    {"Path_GetSpawnDensity", n_Path_GetSpawnDensity},
    {"Path_SetStreamingDistance", n_Path_SetStreamingDistance},
    {"Path_GetStreamingDistance", n_Path_GetStreamingDistance},
    {"Path_AddMaleSkin", n_Path_AddMaleSkin},
    {"Path_AddFemaleSkin", n_Path_AddFemaleSkin},
    {"Path_IgnoreNode", native_IgnorePedestrianNode},
    {"IgnorePedestrianNode", native_IgnorePedestrianNode},
    {"GetClosestPedestrianID", native_GetClosestPedestrianID},
    {"GetPedestrianSex", native_GetPedestrianSex},
    {"StopPedestrian", native_StopPedestrian},
    {"ResumePedestrian", native_ResumePedestrian},
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
    logprintf("[Pedestrians] Plugin loaded successfully!");
    return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload() {
    for (int i = 0; i < MAX_STREAM_ACTORS; i++) {
        if (g_Actors[i].inUse || (g_Actors[i].actorid != 0 && g_Actors[i].actorid != 0xFFFF)) {
            sampgdk_DestroyActor(g_Actors[i].actorid);
            g_Actors[i].inUse = false;
            g_Actors[i].actorid = 0;
        }
    }
    g_Nodes.clear();
    g_NodeIdToIndex.clear();
    g_Zones.clear();
    g_AMX.clear();
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
    for (int a = 0; a < g_MaxActorsInPool; a++) {
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
