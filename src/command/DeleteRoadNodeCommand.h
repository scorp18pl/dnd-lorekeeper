#pragma once
#include "Command.h"
#include "world/RoadGraph.h"
#include <algorithm>
#include <vector>

class DeleteRoadNodeCommand : public Command {
public:
    DeleteRoadNodeCommand(RoadGraph& graph, std::string nodeId)
        : m_Graph(graph), m_NodeId(std::move(nodeId)) {}

    void execute() override {
        auto nit = std::find_if(m_Graph.nodes.begin(), m_Graph.nodes.end(),
            [&](const RoadNode& n) { return n.id == m_NodeId; });
        if (nit == m_Graph.nodes.end()) return;
        m_SavedNode = *nit;

        m_SavedEdges.clear();
        for (const auto& e : m_Graph.edges)
            if (e.from_id == m_NodeId || e.to_id == m_NodeId)
                m_SavedEdges.push_back(e);

        m_Graph.edges.erase(
            std::remove_if(m_Graph.edges.begin(), m_Graph.edges.end(),
                [&](const RoadEdge& e){ return e.from_id == m_NodeId || e.to_id == m_NodeId; }),
            m_Graph.edges.end());
        m_Graph.nodes.erase(nit);
    }

    void undo() override {
        m_Graph.nodes.push_back(m_SavedNode);
        for (const auto& e : m_SavedEdges)
            m_Graph.edges.push_back(e);
    }

private:
    RoadGraph&            m_Graph;
    std::string           m_NodeId;
    RoadNode              m_SavedNode;
    std::vector<RoadEdge> m_SavedEdges;
};
