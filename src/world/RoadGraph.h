#pragma once
#include <string>
#include <vector>

// Haversine great-circle distance between two lat/lon points.
float greatCircleKm(float lat1Deg, float lon1Deg,
                    float lat2Deg, float lon2Deg,
                    float radiusKm);

struct RoadNode {
    std::string id;
    std::string entity_ref;  // attached WorldEntity id (empty = standalone)
    float       lat_deg = 0.f;
    float       lon_deg = 0.f;
};

struct RoadEdge {
    std::string id;
    std::string from_id;
    std::string to_id;
    float       distance_km = 0.f;  // auto-computed, great-circle
};

struct RoadGraph {
    std::vector<RoadNode> nodes;
    std::vector<RoadEdge> edges;

    RoadNode*       findNode(const std::string& id);
    const RoadNode* findNode(const std::string& id) const;

    // Dijkstra shortest path. Returns total km, or -1 if unreachable.
    // Edges are treated as bidirectional.
    float shortestPath(const std::string& fromId, const std::string& toId) const;
};
