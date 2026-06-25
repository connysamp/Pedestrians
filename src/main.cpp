#define HAVE_STDINT_H
#include "amx/amx.h"
#include "plugincommon.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <algorithm>
#include <vector>
#include <cmath>
#include <iostream>
#include <unordered_set>

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

bool LoadPedPaths() {
    std::ifstream file("scriptfiles/pedpaths.json");
    if (!file.is_open()) {
        logprintf("[Pedestrians] Missing paths file!");
        return false;
    }
    
    try {
        json j;
        file >> j;
        
        for (auto& item : j) {
            PedNode node;
            node.id = item["id"];
            node.area = item["area"];
            node.x = item["x"];
            node.y = item["y"];
            node.z = item["z"];
            if (item.contains("links") && item["links"].is_array()) {
                for (auto& l : item["links"]) {
                    node.links.push_back(l);
                }
            }
            g_Nodes.push_back(node);
        }
        
        logprintf("[Pedestrians] Successfully loaded %d pedestrian nodes.", (int)g_Nodes.size());
        return true;
    } catch (const std::exception& e) {
        logprintf("[Pedestrians] ERROR parsing JSON: %s", e.what());
        return false;
    }
}

cell AMX_NATIVE_CALL n_Path_GetNodesInRange(AMX* amx, cell* params) {
    float x = amx_ctof(params[1]);
    float y = amx_ctof(params[2]);
    float z = amx_ctof(params[3]);
    float radius = amx_ctof(params[4]);
    cell* result_ptr = NULL;
    amx_GetAddr(amx, params[5], &result_ptr);
    int max_size = params[6];
    
    float r2 = radius * radius;
    std::vector<int> foundNodes;
    
    for (const auto& node : g_Nodes) {
        float dx = node.x - x;
        float dy = node.y - y;
        
        if ((dx*dx + dy*dy) <= r2) {
            foundNodes.push_back(node.id);
        }
    }
    
    if (foundNodes.size() > 1) {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(foundNodes.begin(), foundNodes.end(), g);
    }
    
    int count = 0;
    for(size_t i = 0; i < foundNodes.size() && count < max_size; ++i) {
        result_ptr[count] = foundNodes[i];
        count++;
    }
    
    return count;
}

cell AMX_NATIVE_CALL n_Path_GetNodePos(AMX* amx, cell* params) {
    int id = params[1];
    
    for (const auto& node : g_Nodes) {
        if (node.id == id) {
            cell* x_ptr; amx_GetAddr(amx, params[2], &x_ptr); *x_ptr = amx_ftoc(node.x);
            cell* y_ptr; amx_GetAddr(amx, params[3], &y_ptr); *y_ptr = amx_ftoc(node.y);
            cell* z_ptr; amx_GetAddr(amx, params[4], &z_ptr); *z_ptr = amx_ftoc(node.z);
            return 1;
        }
    }
    return 0;
}

#include <chrono>
#include <map>
#include <cstdlib>

uint64_t GetTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct WalkingActor {
    int actorid;
    uint32_t currentNodeId;
    uint32_t targetNodeId;
    uint32_t previousNodeId;
    float currentX, currentY, currentZ;
    float speed;
    float currentAngle;
    float targetAngle;
    float laneOffset;
    uint64_t lastUpdateTime;
};

std::map<int, WalkingActor> g_WalkingActors;

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
            if (linkIdx != -1) {
                validLinks.push_back(linkId);
            }
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

cell AMX_NATIVE_CALL n_Path_SetActorWalk(AMX* amx, cell* params) {
    int actorid = params[1];
    uint32_t startNode = params[2];
    float speed = amx_ctof(params[3]);
    
    int startIdx = FindNodeIndex(startNode);
    if (startIdx == -1) return 0;
    
    WalkingActor wa;
    wa.actorid = actorid;
    wa.currentNodeId = startNode;
    wa.previousNodeId = startNode;
    wa.currentX = g_Nodes[startIdx].x;
    wa.currentY = g_Nodes[startIdx].y;
    wa.currentZ = g_Nodes[startIdx].z;
    wa.speed = speed;
    wa.laneOffset = ((float)rand() / (float)RAND_MAX) * 3.0f - 1.5f;
    wa.lastUpdateTime = GetTimeMs();
    
    wa.targetNodeId = PickNextNode(startIdx, startNode);
    
    wa.currentAngle = 0.0f;
    wa.targetAngle = 0.0f;
    int targetIdx = FindNodeIndex(wa.targetNodeId);
    if (targetIdx != -1) {
        float dx = g_Nodes[targetIdx].x - wa.currentX;
        float dy = g_Nodes[targetIdx].y - wa.currentY;
        wa.targetAngle = atan2(dy, dx) * (180.0f / 3.14159265f) - 90.0f;
        wa.currentAngle = wa.targetAngle;
    }
    
    g_WalkingActors[actorid] = wa;
    return 1;
}

cell AMX_NATIVE_CALL n_Path_StopActorWalk(AMX* amx, cell* params) {
    int actorid = params[1];
    g_WalkingActors.erase(actorid);
    return 1;
}

cell AMX_NATIVE_CALL n_Path_SetActorSpeed(AMX* amx, cell* params) {
    int actorid = params[1];
    float speed = amx_ctof(params[2]);
    
    if (g_WalkingActors.count(actorid) > 0) {
        g_WalkingActors[actorid].speed = speed;
        return 1;
    }
    return 0;
}

cell AMX_NATIVE_CALL n_Path_IgnoreNode(AMX* amx, cell* params) {
    g_IgnoredNodes.insert((uint32_t)params[1]);
    return 1;
}

cell AMX_NATIVE_CALL n_Path_GetActorNode(AMX* amx, cell* params) {
    int actorid = params[1];
    if (g_WalkingActors.count(actorid) > 0) {
        return g_WalkingActors[actorid].targetNodeId;
    }
    return -1;
}

cell AMX_NATIVE_CALL n_Path_ProcessActors(AMX* amx, cell* params) {
    cell* actors_ptr; amx_GetAddr(amx, params[1], &actors_ptr);
    cell* xs_ptr; amx_GetAddr(amx, params[2], &xs_ptr);
    cell* ys_ptr; amx_GetAddr(amx, params[3], &ys_ptr);
    cell* zs_ptr; amx_GetAddr(amx, params[4], &zs_ptr);
    cell* angles_ptr; amx_GetAddr(amx, params[5], &angles_ptr);
    cell* count_ptr; amx_GetAddr(amx, params[6], &count_ptr);
    int max_size = params[7];
    
    int count = 0;
    uint64_t now = GetTimeMs();
    
    for (auto it = g_WalkingActors.begin(); it != g_WalkingActors.end(); ++it) {
        if (count >= max_size) break;
        
        WalkingActor& wa = it->second;
        float dt = (now - wa.lastUpdateTime) / 1000.0f;
        wa.lastUpdateTime = now;
        
        int targetIdx = FindNodeIndex(wa.targetNodeId);
        if (targetIdx == -1) continue;
        
        float tx = g_Nodes[targetIdx].x;
        float ty = g_Nodes[targetIdx].y;
        float tz = g_Nodes[targetIdx].z;
        
        int currentIdx = FindNodeIndex(wa.currentNodeId);
        float ndx = g_Nodes[targetIdx].x - (currentIdx != -1 ? g_Nodes[currentIdx].x : wa.currentX);
        float ndy = g_Nodes[targetIdx].y - (currentIdx != -1 ? g_Nodes[currentIdx].y : wa.currentY);
        float ndist = std::sqrt(ndx*ndx + ndy*ndy);
        if(ndist > 0.001f) {
            float px = -(ndy / ndist);
            float py = (ndx / ndist);
            tx += px * wa.laneOffset;
            ty += py * wa.laneOffset;
        }
        
        float dx = tx - wa.currentX;
        float dy = ty - wa.currentY;
        float dz = tz - wa.currentZ;
        
        float dist2D = std::sqrt(dx*dx + dy*dy);
        if (dist2D == 0.0f) dist2D = 0.001f;
        float moveDist = wa.speed * dt;
        
        if (moveDist >= dist2D) {
            wa.currentX = tx;
            wa.currentY = ty;
            wa.currentZ = tz;
            wa.previousNodeId = wa.currentNodeId;
            wa.currentNodeId = wa.targetNodeId;
            
            wa.targetNodeId = PickNextNode(targetIdx, wa.previousNodeId);
            
            int nextIdx = FindNodeIndex(wa.targetNodeId);
            if (nextIdx != -1) {
                float ndx = g_Nodes[nextIdx].x - wa.currentX;
                float ndy = g_Nodes[nextIdx].y - wa.currentY;
                wa.targetAngle = atan2(ndy, ndx) * (180.0f / 3.14159265f) - 90.0f;
            }
            
        } else {
            float ratio = moveDist / dist2D;
            wa.currentX += dx * ratio;
            wa.currentY += dy * ratio;
            wa.currentZ += dz * ratio;
        }
        
        float diff = wa.targetAngle - wa.currentAngle;
        while(diff <= -180.0f) diff += 360.0f;
        while(diff > 180.0f) diff -= 360.0f;
        
        wa.currentAngle += (diff * 0.15f);
        
        while(wa.currentAngle < 0.0f) wa.currentAngle += 360.0f;
        while(wa.currentAngle >= 360.0f) wa.currentAngle -= 360.0f;
        
        actors_ptr[count] = wa.actorid;
        xs_ptr[count] = amx_ftoc(wa.currentX);
        ys_ptr[count] = amx_ftoc(wa.currentY);
        zs_ptr[count] = amx_ftoc(wa.currentZ);
        angles_ptr[count] = amx_ftoc(wa.currentAngle);
        count++;
    }
    
    *count_ptr = count;
    return 1;
}

AMX_NATIVE_INFO PluginNatives[] = {
    { "Path_GetNodesInRange", n_Path_GetNodesInRange },
    {"Path_GetNodePos", n_Path_GetNodePos},
    {"Path_SetActorWalk", n_Path_SetActorWalk},
    {"Path_StopActorWalk", n_Path_StopActorWalk},
    {"Path_ProcessActors", n_Path_ProcessActors},
    {"Path_SetActorSpeed", n_Path_SetActorSpeed},
    {"Path_IgnoreNode", n_Path_IgnoreNode},
    {"Path_GetActorNode", n_Path_GetActorNode},
    { 0, 0 }
};

PLUGIN_EXPORT unsigned int PLUGIN_CALL Supports() {
    return SUPPORTS_VERSION | SUPPORTS_AMX_NATIVES;
}

PLUGIN_EXPORT bool PLUGIN_CALL Load(void **ppData) {
    pAMXFunctions = ppData[PLUGIN_DATA_AMX_EXPORTS];
    logprintf = (logprintf_t)ppData[PLUGIN_DATA_LOGPRINTF];
        logprintf("-------------------------------------------");
        logprintf(" [Pedestrians] Plugin loaded! (by conny)");
        logprintf("-------------------------------------------");
    
    LoadPedPaths();
    
    return true;
}

PLUGIN_EXPORT void PLUGIN_CALL Unload() {
    logprintf("ActorPaths Plugin unloaded.");
}

PLUGIN_EXPORT int PLUGIN_CALL AmxLoad(AMX *amx) {
    return amx_Register(amx, PluginNatives, -1);
}

PLUGIN_EXPORT int PLUGIN_CALL AmxUnload(AMX *amx) {
    return AMX_ERR_NONE;
}

PLUGIN_EXPORT void PLUGIN_CALL ProcessTick() {
}
