#include "RoadGraph.h"
#include <cmath>
#include <queue>
#include <unordered_map>

static constexpr float kPi = 3.14159265358979f;

float greatCircleKm(float lat1Deg, float lon1Deg,
                    float lat2Deg, float lon2Deg,
                    float radiusKm) {
    auto rad = [](float d) { return d * kPi / 180.f; };
    float la1 = rad(lat1Deg), lo1 = rad(lon1Deg);
    float la2 = rad(lat2Deg), lo2 = rad(lon2Deg);
    float dlat = la2 - la1, dlon = lo2 - lo1;
    float a = std::sin(dlat * 0.5f) * std::sin(dlat * 0.5f)
            + std::cos(la1) * std::cos(la2)
            * std::sin(dlon * 0.5f) * std::sin(dlon * 0.5f);
    return radiusKm * 2.f * std::atan2(std::sqrt(a), std::sqrt(1.f - a));
}

MapNode* RouteGraph::findNode(const std::string& id) {
    for (auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

const MapNode* RouteGraph::findNode(const std::string& id) const {
    for (const auto& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

float RouteGraph::shortestPath(const std::string& fromId, const std::string& toId,
                               std::optional<RouteType> typeFilter) const {
    using P = std::pair<float, std::string>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    std::unordered_map<std::string, float> dist;

    dist[fromId] = 0.f;
    pq.push({0.f, fromId});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (u == toId) return d;

        auto it = dist.find(u);
        if (it != dist.end() && d > it->second) continue;

        for (const auto& e : edges) {
            if (typeFilter && e.type != *typeFilter) continue;
            std::string v;
            if      (e.from_id == u) v = e.to_id;
            else if (e.to_id   == u) v = e.from_id;
            else continue;

            float nd = d + e.distance_km;
            auto jt = dist.find(v);
            if (jt == dist.end() || nd < jt->second) {
                dist[v] = nd;
                pq.push({nd, v});
            }
        }
    }
    return -1.f;
}
