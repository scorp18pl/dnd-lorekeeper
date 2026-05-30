#pragma once
#include "Command.h"
#include "world/RoadGraph.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

class MoveNodeCommand : public Command {
public:
    MoveNodeCommand(std::vector<MapNode>& nodes, std::vector<RouteEdge>& edges,
                    std::string id, float newLat, float newLon,
                    float oldLat, float oldLon, float radiusKm)
        : m_Nodes(nodes), m_Edges(edges), m_Id(std::move(id)),
          m_NewLat(newLat), m_NewLon(newLon), m_OldLat(oldLat), m_OldLon(oldLon),
          m_RadiusKm(radiusKm)
    {
        for (const auto& e : m_Edges)
            if (e.from_id == m_Id || e.to_id == m_Id)
                m_OldEdgeDists.emplace_back(e.id, e.distance_km);
    }

    void execute() override { applyPos(m_NewLat, m_NewLon); recalcEdges(); }
    void undo()    override { applyPos(m_OldLat, m_OldLon); restoreEdges(); }

private:
    void applyPos(float lat, float lon) {
        for (auto& n : m_Nodes)
            if (n.id == m_Id) { n.lat_deg = lat; n.lon_deg = lon; return; }
    }

    void recalcEdges() {
        const MapNode* moved = nullptr;
        for (const auto& n : m_Nodes)
            if (n.id == m_Id) { moved = &n; break; }
        if (!moved) return;
        for (auto& e : m_Edges) {
            if (e.from_id != m_Id && e.to_id != m_Id) continue;
            const std::string& otherId = (e.from_id == m_Id) ? e.to_id : e.from_id;
            for (const auto& n : m_Nodes) {
                if (n.id == otherId) {
                    e.distance_km = greatCircleKm(moved->lat_deg, moved->lon_deg,
                                                  n.lat_deg, n.lon_deg, m_RadiusKm);
                    break;
                }
            }
        }
    }

    void restoreEdges() {
        for (auto& e : m_Edges)
            for (const auto& [id, dist] : m_OldEdgeDists)
                if (e.id == id) { e.distance_km = dist; break; }
    }

    std::vector<MapNode>&  m_Nodes;
    std::vector<RouteEdge>& m_Edges;
    std::string             m_Id;
    float m_NewLat, m_NewLon;
    float m_OldLat, m_OldLon;
    float m_RadiusKm;
    std::vector<std::pair<std::string, float>> m_OldEdgeDists;
};
