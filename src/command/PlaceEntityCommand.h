#pragma once
#include "Command.h"
#include "world/RoadGraph.h"
#include <algorithm>

class PlaceNodeCommand : public Command {
public:
    PlaceNodeCommand(std::vector<MapNode>& nodes, MapNode node)
        : m_Nodes(nodes), m_Node(std::move(node)) {}

    void execute() override { m_Nodes.push_back(m_Node); }

    void undo() override {
        auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(),
            [&](const MapNode& n) { return n.id == m_Node.id; });
        if (it != m_Nodes.end()) m_Nodes.erase(it);
    }

private:
    std::vector<MapNode>& m_Nodes;
    MapNode               m_Node;
};
