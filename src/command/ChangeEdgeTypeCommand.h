#pragma once
#include "Command.h"
#include "../world/RoadGraph.h"
#include <string>
#include <vector>

class ChangeEdgeTypeCommand : public Command {
public:
    ChangeEdgeTypeCommand(std::vector<RouteEdge>& edges, std::string edgeId, RouteType newType)
        : m_Edges(edges), m_EdgeId(std::move(edgeId)), m_NewType(newType) {}

    void execute() override {
        for (auto& e : m_Edges) {
            if (e.id == m_EdgeId) { m_OldType = e.type; e.type = m_NewType; return; }
        }
    }

    void undo() override {
        for (auto& e : m_Edges) {
            if (e.id == m_EdgeId) { e.type = m_OldType; return; }
        }
    }

private:
    std::vector<RouteEdge>& m_Edges;
    std::string             m_EdgeId;
    RouteType               m_NewType;
    RouteType               m_OldType = RouteType::Road;
};
