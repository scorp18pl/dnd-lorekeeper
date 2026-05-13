#pragma once
#include "Command.h"
#include "world/RoadGraph.h"
#include <algorithm>

class AddRoadEdgeCommand : public Command {
public:
    AddRoadEdgeCommand(std::vector<RoadEdge>& edges, RoadEdge edge)
        : m_Edges(edges), m_Edge(std::move(edge)) {}

    void execute() override { m_Edges.push_back(m_Edge); }

    void undo() override {
        auto it = std::find_if(m_Edges.begin(), m_Edges.end(),
            [&](const RoadEdge& e) { return e.id == m_Edge.id; });
        if (it != m_Edges.end()) m_Edges.erase(it);
    }

private:
    std::vector<RoadEdge>& m_Edges;
    RoadEdge               m_Edge;
};
