#pragma once
#include "Command.h"
#include "world/RoadGraph.h"
#include <algorithm>

class AddRouteEdgeCommand : public Command {
public:
    AddRouteEdgeCommand(std::vector<RouteEdge>& edges, RouteEdge edge)
        : m_Edges(edges), m_Edge(std::move(edge)) {}

    void execute() override { m_Edges.push_back(m_Edge); }

    void undo() override {
        auto it = std::find_if(m_Edges.begin(), m_Edges.end(),
            [&](const RouteEdge& e) { return e.id == m_Edge.id; });
        if (it != m_Edges.end()) m_Edges.erase(it);
    }

private:
    std::vector<RouteEdge>& m_Edges;
    RouteEdge               m_Edge;
};
