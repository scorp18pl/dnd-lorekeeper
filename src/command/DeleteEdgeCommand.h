#pragma once
#include "Command.h"
#include "world/RoadGraph.h"
#include <algorithm>
#include <string>

class DeleteEdgeCommand : public Command {
public:
    DeleteEdgeCommand(std::vector<RouteEdge>& edges, std::string edgeId)
        : m_Edges(edges), m_EdgeId(std::move(edgeId)) {}

    void execute() override {
        auto it = std::find_if(m_Edges.begin(), m_Edges.end(),
            [&](const RouteEdge& e){ return e.id == m_EdgeId; });
        if (it != m_Edges.end()) { m_Saved = *it; m_Edges.erase(it); }
    }

    void undo() override { m_Edges.push_back(m_Saved); }

private:
    std::vector<RouteEdge>& m_Edges;
    std::string             m_EdgeId;
    RouteEdge               m_Saved;
};
