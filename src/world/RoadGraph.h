#pragma once
#include "WorldEntity.h"
#include <optional>
#include <string>
#include <vector>

float greatCircleKm(float lat1Deg, float lon1Deg,
                    float lat2Deg, float lon2Deg,
                    float radiusKm);

enum class RouteType { Road, Sea };

struct MapNode {
    std::string id;
    float lat_deg = 0.f, lon_deg = 0.f;
    std::string name;               // empty = unnamed waypoint
    EntityType  entity_type = EntityType::POI;
    std::string media_ref;
    std::optional<int> born_day;
    std::optional<int> died_day;
};

struct RouteEdge {
    std::string id;
    std::string from_id, to_id;
    float       distance_km = 0.f;
    RouteType   type = RouteType::Road;
};

struct RouteGraph {
    std::vector<MapNode>   nodes;
    std::vector<RouteEdge> edges;

    MapNode*       findNode(const std::string& id);
    const MapNode* findNode(const std::string& id) const;

    // Dijkstra shortest path. Returns total km, or -1 if unreachable.
    // typeFilter: if set, only traverse edges of that type.
    float shortestPath(const std::string& fromId, const std::string& toId,
                       std::optional<RouteType> typeFilter = std::nullopt) const;
};
