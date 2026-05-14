#pragma once
#include "Command.h"
#include "world/RoadGraph.h"
#include <algorithm>
#include <string>

// Removes one edge and inserts a new node plus two replacement edges.
// Undo restores the original edge and removes the new node/edges.
class SplitEdgeCommand : public Command {
public:
    SplitEdgeCommand(RouteGraph& graph, std::string splitEdgeId,
                     MapNode newNode, RouteEdge edge1, RouteEdge edge2)
        : m_Graph(graph)
        , m_SplitEdgeId(std::move(splitEdgeId))
        , m_NewNode(std::move(newNode))
        , m_Edge1(std::move(edge1))
        , m_Edge2(std::move(edge2))
    {}

    void execute() override {
        auto it = std::find_if(m_Graph.edges.begin(), m_Graph.edges.end(),
            [&](const RouteEdge& e){ return e.id == m_SplitEdgeId; });
        if (it != m_Graph.edges.end()) {
            m_SavedEdge = *it;
            m_Graph.edges.erase(it);
        }
        m_Graph.nodes.push_back(m_NewNode);
        m_Graph.edges.push_back(m_Edge1);
        m_Graph.edges.push_back(m_Edge2);
    }

    void undo() override {
        auto rmEdge = [&](const std::string& id) {
            m_Graph.edges.erase(
                std::remove_if(m_Graph.edges.begin(), m_Graph.edges.end(),
                    [&](const RouteEdge& e){ return e.id == id; }),
                m_Graph.edges.end());
        };
        rmEdge(m_Edge1.id);
        rmEdge(m_Edge2.id);
        m_Graph.nodes.erase(
            std::remove_if(m_Graph.nodes.begin(), m_Graph.nodes.end(),
                [&](const MapNode& n){ return n.id == m_NewNode.id; }),
            m_Graph.nodes.end());
        m_Graph.edges.push_back(m_SavedEdge);
    }

private:
    RouteGraph& m_Graph;
    std::string m_SplitEdgeId;
    MapNode     m_NewNode;
    RouteEdge   m_Edge1, m_Edge2;
    RouteEdge   m_SavedEdge;
};
