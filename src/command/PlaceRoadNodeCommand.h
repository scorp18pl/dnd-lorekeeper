#pragma once
#include "Command.h"
#include "world/RoadGraph.h"
#include <algorithm>

class PlaceRoadNodeCommand : public Command {
public:
    PlaceRoadNodeCommand(std::vector<RoadNode>& nodes, RoadNode node)
        : m_Nodes(nodes), m_Node(std::move(node)) {}

    void execute() override { m_Nodes.push_back(m_Node); }

    void undo() override {
        auto it = std::find_if(m_Nodes.begin(), m_Nodes.end(),
            [&](const RoadNode& n) { return n.id == m_Node.id; });
        if (it != m_Nodes.end()) m_Nodes.erase(it);
    }

private:
    std::vector<RoadNode>& m_Nodes;
    RoadNode               m_Node;
};
