#pragma once
#include "Command.h"
#include "world/RoadGraph.h"
#include <algorithm>
#include <string>

class MoveNodeCommand : public Command {
public:
    MoveNodeCommand(std::vector<MapNode>& nodes, std::string id,
                    float newLat, float newLon, float oldLat, float oldLon)
        : m_Nodes(nodes), m_Id(std::move(id)),
          m_NewLat(newLat), m_NewLon(newLon), m_OldLat(oldLat), m_OldLon(oldLon) {}

    void execute() override { apply(m_NewLat, m_NewLon); }
    void undo()    override { apply(m_OldLat, m_OldLon); }

private:
    void apply(float lat, float lon) {
        auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(),
            [&](const MapNode& n) { return n.id == m_Id; });
        if (it != m_Nodes.end()) { it->lat_deg = lat; it->lon_deg = lon; }
    }

    std::vector<MapNode>& m_Nodes;
    std::string           m_Id;
    float m_NewLat, m_NewLon;
    float m_OldLat, m_OldLon;
};
